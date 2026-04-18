#pragma once

#include <message.pb.h>

class AuthStore;
class MessageStreamRouter;

/// Where a validated stream `SignedMessage` should go after `MessageData` is parsed.
enum class RecipientDispatchAction {
    /// Empty `recipient_id` or `AuthStore::is_self(recipient_id)`: deliver on this stream.
    DeliverLocal,
    /// Routed to another peer's stream.
    Forwarded,
    /// Non-self recipient could not be routed (logged here).
    NoRouteContinue,
};

RecipientDispatchAction dispatch_signed_message_recipient(
    AuthStore* auth,
    MessageStreamRouter* router,
    const message::SignedMessage& msg,
    const message::MessageData& data
);
