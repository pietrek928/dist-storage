#include "stream_router.h"

#include <peer/store.h>

#include <utility>

MessageStreamRouter::MessageStreamRouter(std::shared_ptr<AuthStore> auth)
    : auth_(std::move(auth)) {}

void MessageStreamRouter::register_session(
    const std::string& peer_id, const std::shared_ptr<MessageStreamChannel>& channel
) {
    std::lock_guard<std::mutex> lock(mu_);
    by_peer_.insert_or_assign(peer_id, channel);
}

void MessageStreamRouter::unregister_session(const std::string& peer_id) {
    std::lock_guard<std::mutex> lock(mu_);
    by_peer_.erase(peer_id);
}

std::shared_ptr<MessageStreamChannel> MessageStreamRouter::get_channel(const std::string& peer_id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = by_peer_.find(peer_id);
    if (it == by_peer_.end()) {
        return nullptr;
    }
    std::shared_ptr<MessageStreamChannel> ch = it->second;
    if (!ch->is_valid()) {
        by_peer_.erase(it);
        return nullptr;
    }
    return ch;
}

bool MessageStreamRouter::route_to_recipient(
    const std::string& recipient_id, const message::SignedMessage& msg
) {
    std::shared_ptr<AuthStore> auth = auth_;
    if (!auth) {
        return false;
    }
    if (!auth->has(msg.sender_id()) || !auth->validate_message(msg)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mu_);
    auto it = by_peer_.find(recipient_id);
    if (it == by_peer_.end()) {
        return false;
    }
    std::shared_ptr<MessageStreamChannel> ch = it->second;
    if (!ch->is_valid()) {
        by_peer_.erase(it);
        return false;
    }
    return ch->try_enqueue(msg);
}
