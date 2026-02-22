#include "message.h"

#include <stdexcept>


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