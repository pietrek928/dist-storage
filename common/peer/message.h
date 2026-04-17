#pragma once

#include <crypto/sgn.h>
#include <message.pb.h>

class AuthStore;

enum class SignedPayloadAuthResult {
    Ok,
    UnknownSender,
    BadSignature,
    BadPayload,
};

// Shared validation for unary and stream paths: known sender, valid signature, parse MessageData.
// On failure, logs with `log_prefix` (e.g. "Send", "Stream").
SignedPayloadAuthResult authenticate_signed_message_data(
    AuthStore* store,
    const message::SignedMessage& msg,
    message::MessageData* data_out,
    const char* log_prefix
);

void fillMessageData(
    message::SignedMessage* msg,
    const message::MessageData &data,
    const SSLSigner &signer
);

void getMessageData(
    const message::SignedMessage &msg,
    message::MessageData* data,
    const SSLVerifier &verifier
);
