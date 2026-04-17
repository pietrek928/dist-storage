#include "message_stream.h"

#include <grpc/callback.h>

#include <absl/log/log.h>
#include <grpcpp/grpcpp.h>

#include <deque>
#include <mutex>
#include <utility>

#include <message.grpc.pb.h>
#include <message.pb.h>
#include <peer/message.h>
#include <peer/store.h>

#include "stream_router.h"

class MessageStreamHandler;

class MessageStreamChannelImpl final : public MessageStreamChannel {
    MessageStreamHandler* owner_;

public:
    explicit MessageStreamChannelImpl(MessageStreamHandler* owner) : owner_(owner) {}

    bool try_enqueue(message::SignedMessage msg) override;
};

// gRPC CQ `ok` flag is passed as `running` in existing handlers; we use `running` the same way.
class MessageStreamHandler final : public GRPCStreamHandler<
                                     message::Message::AsyncService,
                                     message::SignedMessage,
                                     message::SignedMessage
                                 > {
    enum class ConnectPhase { kInitial, kAccepted, kStreaming };

    enum class PendingOp { kRead, kWrite, kFinish };

    ConnectPhase connect_phase_ = ConnectPhase::kInitial;
    PendingOp pending_ = PendingOp::kRead;

    std::shared_ptr<AuthStore> auth_store_;
    std::shared_ptr<MessageStreamRouter> router_;
    std::shared_ptr<MessageStreamChannelImpl> channel_;

    std::mutex outbound_mu_;
    std::deque<message::SignedMessage> outbound_;
    bool write_pending_ = false;
    bool closing_after_writes_ = false;

    std::string registered_peer_id_;

public:
    MessageStreamHandler(
        message::Message::AsyncService* async_service,
        std::shared_ptr<AuthStore> auth_store,
        std::shared_ptr<MessageStreamRouter> router
    )
        : GRPCStreamHandler(async_service),
          auth_store_(std::move(auth_store)),
          router_(std::move(router)),
          channel_(std::make_shared<MessageStreamChannelImpl>(this)) {}

    MessageStreamHandler(const MessageStreamHandler& other)
        : GRPCStreamHandler(other.service),
          auth_store_(other.auth_store_),
          router_(other.router_),
          channel_(std::make_shared<MessageStreamChannelImpl>(this)) {}

    void bind(grpc::ServerCompletionQueue* cq) {
        service->RequestStream(&ctx, &stream, cq, cq, this);
    }

    bool enqueue_outgoing(message::SignedMessage msg) {
        std::unique_lock<std::mutex> lock(outbound_mu_);
        outbound_.push_back(std::move(msg));
        if (!write_pending_) {
            response.CopyFrom(outbound_.front());
            write_pending_ = true;
            pending_ = PendingOp::kWrite;
            lock.unlock();
            write();
        }
        return true;
    }

    void process(grpc::ServerCompletionQueue* cq, bool running) override {
        if (connect_phase_ != ConnectPhase::kStreaming) {
            handle_connect(cq, running);
            return;
        }

        switch (pending_) {
            case PendingOp::kRead:
                handle_read_completion(running);
                break;
            case PendingOp::kWrite:
                handle_write_completion(running);
                break;
            case PendingOp::kFinish:
                dispose();
                break;
        }
    }

private:
    void unregister_if_registered() {
        if (!registered_peer_id_.empty() && router_) {
            router_->unregister_session(registered_peer_id_);
            registered_peer_id_.clear();
        }
    }

    void handle_connect(grpc::ServerCompletionQueue* cq, bool running) {
        if (connect_phase_ == ConnectPhase::kInitial) {
            connect_phase_ = ConnectPhase::kAccepted;
            bind(cq);
            return;
        }
        if (connect_phase_ == ConnectPhase::kAccepted) {
            if (!running) {
                dispose();
                return;
            }
            connect_phase_ = ConnectPhase::kStreaming;
            (new MessageStreamHandler(*this))->process(cq, true);
            pending_ = PendingOp::kRead;
            read();
            return;
        }
    }

    void handle_read_completion(bool ok) {
        if (!ok) {
            begin_shutdown_after_read_closed();
            return;
        }

        if (!handle_incoming_message()) {
            shutdown_with_status(grpc::Status(
                grpc::UNAUTHENTICATED,
                "stream message rejected (unknown sender or invalid signature)"
            ));
            return;
        }

        pending_ = PendingOp::kRead;
        read();
    }

    bool handle_incoming_message() {
        const message::SignedMessage& req = request;

        message::MessageData data;
        switch (authenticate_signed_message_data(auth_store_.get(), req, &data, "Stream")) {
            case SignedPayloadAuthResult::Ok:
                break;
            default:
                return false;
        }

        if (!router_) {
            return false;
        }

        router_->register_session(req.sender_id(), channel_);
        registered_peer_id_ = req.sender_id();

        const std::string& dest = data.recipient_id();
        if (!dest.empty()) {
            if (auth_store_->is_self(dest)) {
                enqueue_outgoing(req);
                return true;
            }
            if (router_->route_to_recipient(dest, req)) {
                return true;
            }
            LOG(WARNING) << "Stream: routing failed (unknown recipient or validation)";
            return true;
        }

        enqueue_outgoing(req);
        return true;
    }

    void begin_shutdown_after_read_closed() {
        unregister_if_registered();
        std::unique_lock<std::mutex> lock(outbound_mu_);
        outbound_.clear();
        bool wp = write_pending_;
        closing_after_writes_ = wp;
        lock.unlock();

        if (!wp) {
            pending_ = PendingOp::kFinish;
            finish(grpc::Status::OK);
        }
    }

    void shutdown_with_status(grpc::Status st) {
        unregister_if_registered();
        {
            std::lock_guard<std::mutex> lock(outbound_mu_);
            outbound_.clear();
            write_pending_ = false;
            closing_after_writes_ = false;
        }
        pending_ = PendingOp::kFinish;
        finish(st);
    }

    void handle_write_completion(bool ok) {
        std::unique_lock<std::mutex> lock(outbound_mu_);
        if (!ok) {
            outbound_.clear();
            write_pending_ = false;
            bool need_finish = closing_after_writes_;
            closing_after_writes_ = false;
            lock.unlock();
            if (need_finish) {
                pending_ = PendingOp::kFinish;
                finish(grpc::Status::OK);
            }
            return;
        }

        outbound_.pop_front();
        if (!outbound_.empty()) {
            response.CopyFrom(outbound_.front());
            pending_ = PendingOp::kWrite;
            lock.unlock();
            write();
            return;
        }

        write_pending_ = false;
        bool need_finish = closing_after_writes_;
        closing_after_writes_ = false;
        lock.unlock();

        if (need_finish) {
            pending_ = PendingOp::kFinish;
            finish(grpc::Status::OK);
        }
    }
};

bool MessageStreamChannelImpl::try_enqueue(message::SignedMessage msg) {
    return owner_->enqueue_outgoing(std::move(msg));
}

void start_message_stream_acceptor(
    message::Message::AsyncService* async_service,
    grpc::ServerCompletionQueue* cq,
    std::shared_ptr<AuthStore> auth_store,
    std::shared_ptr<MessageStreamRouter> router
) {
    auto* initial = new MessageStreamHandler(async_service, std::move(auth_store), std::move(router));
    initial->process(cq, true);
}
