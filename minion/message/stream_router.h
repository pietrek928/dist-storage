#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <message.pb.h>

class AuthStore;

// Inbound delivery target for stream-routed SignedMessages. Implementations must be
// thread-safe; the message service server typically invokes from the gRPC CQ thread.
class MessageStreamChannel {
public:
    virtual ~MessageStreamChannel() = default;
    virtual bool try_enqueue(message::SignedMessage msg) = 0;
    virtual bool is_valid() const = 0;
};

// Registry of open stream sessions by peer id. Forwards only if AuthStore can validate
// the message the same way as unary Send (known peer + valid signature).
class MessageStreamRouter : public std::enable_shared_from_this<MessageStreamRouter> {
    std::mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<MessageStreamChannel>> by_peer_;
    std::shared_ptr<AuthStore> auth_;

public:
    explicit MessageStreamRouter(std::shared_ptr<AuthStore> auth);

    void register_session(const std::string& peer_id, const std::shared_ptr<MessageStreamChannel>& channel);
    void unregister_session(const std::string& peer_id);

    std::shared_ptr<MessageStreamChannel> get_channel(const std::string& peer_id);

    // Returns false if sender is not verifiable, recipient is unknown, or delivery failed.
    bool route_to_recipient(const std::string& recipient_id, const message::SignedMessage& msg);
};
