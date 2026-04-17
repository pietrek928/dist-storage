#include "message.h"

#include <stdexcept>

#include <absl/log/log.h>
#include <peer/store.h>


SignedPayloadAuthResult authenticate_signed_message_data(
    AuthStore* store,
    const message::SignedMessage& msg,
    message::MessageData* data_out,
    const char* log_prefix
) {
    if (!store) {
        return SignedPayloadAuthResult::UnknownSender;
    }
    if (!store->has(msg.sender_id())) {
        LOG(WARNING) << log_prefix << ": unknown sender_id";
        return SignedPayloadAuthResult::UnknownSender;
    }
    if (!store->validate_message(msg)) {
        LOG(WARNING) << log_prefix << ": signature validation failed";
        return SignedPayloadAuthResult::BadSignature;
    }
    if (!data_out->ParseFromString(msg.data())) {
        LOG(WARNING) << log_prefix << ": failed to parse MessageData payload";
        return SignedPayloadAuthResult::BadPayload;
    }
    return SignedPayloadAuthResult::Ok;
}

void fillMessageData(
    message::SignedMessage* msg,
    const message::MessageData &data,
    const SSLSigner &signer
) {
    if (!data.SerializeToString(msg->mutable_data())) {
        throw std::invalid_argument("Serializing MessageData failed");
    }
    msg->mutable_signature()->resize(signer.signature_size());
    signer.sign(
        (const byte_t*)msg->data().data(), msg->data().size(),
        (byte_t*)msg->mutable_signature()->data()
    );
}

void getMessageData(
    const message::SignedMessage &msg,
    message::MessageData* data,
    const SSLVerifier &verifier
) {
    if (!verifier.verify(
        (const byte_t*)msg.data().data(), msg.data().size(),
        (const byte_t*)msg.signature().data(), msg.signature().size()
    )) {
        throw std::invalid_argument("Invalid signature");
    }
    if (!data->ParseFromString(msg.data())) {
        throw std::invalid_argument("Parsing MessageData failed");
    }
}