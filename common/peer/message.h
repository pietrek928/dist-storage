#pragma once

#include <crypto/sgn.h>
#include <message.pb.h>


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