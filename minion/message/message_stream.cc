#include "message_stream.h"

#include <grpc/callback.h>

#include <absl/log/log.h>
#include <grpcpp/grpcpp.h>

#include <deque>
#include <memory>
#include <mutex>
#include <utility>

#include <message.grpc.pb.h>
#include <message.pb.h>
#include <peer/message.h>
#include <peer/store.h>

#include "recipient_dispatch.h"
#include "stream_router.h"

// gRPC CQ `ok` flag is passed as `running` in existing handlers; we use `running` the same way.
class MessageStreamHandler final : public std::enable_shared_from_this<MessageStreamHandler>,
                                   public GRPCStreamHandler<
                                       MessageStreamHandler,
                                       message::Message::AsyncService,
                                       message::SignedMessage,
                                       message::SignedMessage>,
                                   public MessageStreamChannel {
    enum class PendingOp { kRead, kWrite, kFinish };

    PendingOp pending_ = PendingOp::kRead;

    std::shared_ptr<AuthStore> auth_store_;
    std::shared_ptr<MessageStreamRouter> router_;

    /// Keeps the handler alive until an explicit teardown (registry + CQ completion paths).
    std::shared_ptr<MessageStreamHandler> self_ref_;

    std::mutex outbound_mu_;
    std::deque<message::SignedMessage> outbound_;
    bool write_pending_ = false;
    bool closing_after_writes_ = false;

    bool session_active_ = false;

    std::string registered_peer_id_;

public:
    MessageStreamHandler(
        message::Message::AsyncService* async_service,
        std::shared_ptr<AuthStore> auth_store,
        std::shared_ptr<MessageStreamRouter> router
    )
        : GRPCStreamHandler(async_service),
          auth_store_(std::move(auth_store)),
          router_(std::move(router)) {}

    MessageStreamHandler(const MessageStreamHandler& other)
        : GRPCStreamHandler(other.service),
          auth_store_(other.auth_store_),
          router_(other.router_) {}

    void attach_shared(std::shared_ptr<MessageStreamHandler> self) { self_ref_ = std::move(self); }

    void bind(grpc::ServerCompletionQueue* cq) override {
        service->RequestStream(&ctx, &stream, cq, cq, this);
    }

    void on_stream_rpc_connected(grpc::ServerCompletionQueue* cq) override {
        (void)cq;
        session_active_ = true;
        pending_ = PendingOp::kRead;
        read();
    }

    void on_stream_connect_failed() override {
        session_active_ = false;
        unregister_if_registered();
        self_ref_.reset();
    }

    void clone_acceptor_for_next_request(grpc::ServerCompletionQueue* cq, bool running) override {
        if (running) {
            auto sp = std::make_shared<MessageStreamHandler>(static_cast<const MessageStreamHandler&>(*this));
            sp->attach_shared(sp);
            sp->process(cq, running);
            grpc_run_deferred_handler_destroys();
        }
    }

    bool try_enqueue(message::SignedMessage msg) override { return enqueue_outgoing(std::move(msg)); }

    bool is_valid() const override { return session_active_; }

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
        if (stream_connect_phase_ != StreamConnectPhase::kStreaming) {
            handle_stream_connect(cq, running);
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
                complete_after_stream_finish();
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

    void complete_after_stream_finish() {
        session_active_ = false;
        unregister_if_registered();
        self_ref_.reset();
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

        router_->register_session(
            req.sender_id(),
            std::static_pointer_cast<MessageStreamChannel>(shared_from_this())
        );
        registered_peer_id_ = req.sender_id();

        switch (dispatch_signed_message_recipient(auth_store_.get(), router_.get(), req, data)) {
            case RecipientDispatchAction::DeliverLocal:
                enqueue_outgoing(req);
                break;
            case RecipientDispatchAction::Forwarded:
                break;
            case RecipientDispatchAction::NoRouteContinue:
                break;
        }
        return true;
    }

    void begin_shutdown_after_read_closed() {
        session_active_ = false;
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
        session_active_ = false;
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

void start_message_stream_acceptor(
    message::Message::AsyncService* async_service,
    grpc::ServerCompletionQueue* cq,
    std::shared_ptr<AuthStore> auth_store,
    std::shared_ptr<MessageStreamRouter> router
) {
    auto sp = std::make_shared<MessageStreamHandler>(async_service, std::move(auth_store), std::move(router));
    sp->attach_shared(sp);
    sp->process(cq, true);
    grpc_run_deferred_handler_destroys();
}
