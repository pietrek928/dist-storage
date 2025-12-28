#pragma once

#include <mutex>
#include <openssl/types.h>
#include <string>
#include <map>

#include <utils/sys/time.h>
#include <crypto/auth.h>
#include <crypto/sgn.h>
#include <crypto/call_check.h>


class PeerStore {
    std::mutex lock;
    typedef struct PeerInfo {
        timespec last_update;
        std::string cert;
        SSLVerifier verifier;
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

    void push(const std::string &cert) {
        X509_ptr x509;
        EVP_PKEY_ptr pubkey;

        ssl_call("converting 509 to string", x509 = string_to_x509(cert));
        ssl_call("getting public key", pubkey = X509_get_pubkey(x509));

        std::string id;
        id.resize(512 / 8);
        peer_id_from_pubkey(pubkey, "sha512", (byte_t *)id.data());

        {
            std::lock_guard<std::mutex> guard(lock);
            peers.emplace(id, PeerInfo{
                .last_update = timespec_timestamp(),
                .cert = cert,
                .verifier =  SSLVerifier(
                    EVP_PKEY_type(pubkey), pubkey, "sha512"
                )
            });
        }
    }
};