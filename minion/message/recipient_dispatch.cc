#include "recipient_dispatch.h"

#include <absl/log/log.h>

#include <peer/store.h>

#include "stream_router.h"

RecipientDispatchAction dispatch_signed_message_recipient(
    AuthStore* auth,
    MessageStreamRouter* router,
    const message::SignedMessage& msg,
    const message::MessageData& data
) {
    if (!auth || !router) {
        return RecipientDispatchAction::NoRouteContinue;
    }

    const std::string& dest = data.recipient_id();
    if (dest.empty()) {
        return RecipientDispatchAction::DeliverLocal;
    }
    if (auth->is_self(dest)) {
        return RecipientDispatchAction::DeliverLocal;
    }
    if (router->route_to_recipient(dest, msg)) {
        return RecipientDispatchAction::Forwarded;
    }
    LOG(WARNING) << "Stream: routing failed (unknown recipient or validation)";
    return RecipientDispatchAction::NoRouteContinue;
}
