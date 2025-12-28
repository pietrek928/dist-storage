#pragma once

#include <mutex>
#include <openssl/types.h>
#include <string>
#include <map>

#include <utils/sys/time.h>
#include <crypto/auth.h>
#include <crypto/sgn.h>


class PeerStore {
    std::mutex lock;
    typedef struct PeerInfo {
        timespec last_update;
        SSLVerifier verifier;
        std::string cert;
    } PeerInfo;

    std::map<std::string, PeerInfo> peers;

    public:

    bool has(const std::string &id) {
        std::lock_guard<std::mutex> guard(lock);
        auto it = peers.find(id);
        if (it == peers.end()) {
            return false;
        }
        return true;
    }

    void push(const std::string &id, const std::string &cert) {
        std::lock_guard<std::mutex> guard(lock);
        peers[id].cert = cert;
        peers[id].last_update = timespec_timestamp();

        X509_ptr x509 = string_to_x509(cert);
        EVP_PKEY_ptr pubkey = X509_get_pubkey(x509);

        peers[id].verifier = SSLVerifier(
            EVP_PKEY_type(pubkey), pubkey, "sha512"
        );
    }
};