#pragma once

#include <mutex>
#include <openssl/types.h>
#include <string>
#include <map>

#include <utils/sys/time.h>
#include <crypto/auth.h>
#include <crypto/sgn.h>
#include <crypto/call_check.h>

#include <message.pb.h>


class AuthStoreStore {
    std::mutex lock;
    typedef struct PeerInfo {
        timespec last_update;
        std::string cert;
        SSLVerifier verifier;
    } PeerInfo;

    std::string self_id;
    std::string self_cert;
    SSLSigner signer;
    std::map<std::string, PeerInfo> peers;

    public:

    bool has(const std::string &id);
    void push(const std::string &cert);
    bool validate_message(const message::SignedMessage &msg); // omit certificate - just based on cached pub key
    message::SignedMessage sign_message(
        const message::MessageData &message_data, const std::string recipient_id, bool add_cert = false
    );
};
