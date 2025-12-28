#pragma once

#include <crypto/sgn.h>
#include <message.pb.h>


void fill_message(
    message::SignedMessage *m,
    const std::string sender_id, const SSLSigner &signer, const message::MessageData &data,
    int ttl = 4, const std::string *sender_cert = NULL
);
