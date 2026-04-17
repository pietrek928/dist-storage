#include "store.h"

#include <openssl/x509v3.h>
#include <stdexcept>

#include "message.h"


bool AuthStore::is_self(const std::string& peer_id) const {
    std::lock_guard<std::mutex> guard(lock);
    return !self_id.empty() && peer_id == self_id;
}

bool AuthStore::has(const std::string &id) {
    std::lock_guard<std::mutex> guard(lock);
    auto it = peers.find(id);
    if (it == peers.end()) {
        return false;
    }
    return true;
}

void AuthStore::push_authority(const std::string &authority_id, const std::string &cert) {
    X509_ptr x509;
    EVP_PKEY_ptr pubkey;

    ssl_call("converting 509 to string", x509 = string_to_x509(cert));
    ssl_call("getting public key", pubkey = X509_get_pubkey(x509));

    std::lock_guard<std::mutex> guard(lock);
    root_authorities.insert_or_assign(authority_id, PeerInfo{
        .last_update = timespec_timestamp(),
        .cert = cert,
        .verifier = SSLVerifier(
            EVP_PKEY_type(pubkey), pubkey, "sha512"
        )
    });
}

void AuthStore::push(const std::string &cert) {
    X509_ptr x509;
    EVP_PKEY_ptr pubkey;
    EVP_PKEY_ptr authority_pubkey;

    ssl_call("converting 509 to string", x509 = string_to_x509(cert));
    ssl_call("getting public key", pubkey = X509_get_pubkey(x509));

    AUTHORITY_KEYID* authority_key_id = reinterpret_cast<AUTHORITY_KEYID*>(
        X509_get_ext_d2i(x509, NID_authority_key_identifier, nullptr, nullptr)
    );
    if (!authority_key_id) {
        throw std::runtime_error("peer cert missing authority key identifier");
    }
    guard_ptr<AUTHORITY_KEYID, AUTHORITY_KEYID_free> authority_key_id_guard(authority_key_id);
    if (!authority_key_id->keyid || !authority_key_id->keyid->data || authority_key_id->keyid->length <= 0) {
        throw std::runtime_error("peer cert authority key identifier has empty keyid");
    }
    std::string authority_id(
        reinterpret_cast<const char*>(authority_key_id->keyid->data),
        authority_key_id->keyid->length
    );

    std::string authority_cert;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto it = root_authorities.find(authority_id);
        if (it == root_authorities.end()) {
            throw std::runtime_error("unknown authority for peer cert");
        }
        authority_cert = it->second.cert;
    }

    X509_ptr authority_x509;
    ssl_call("converting authority cert", authority_x509 = string_to_x509(authority_cert));
    ssl_call("getting authority public key", authority_pubkey = X509_get_pubkey(authority_x509));

    int verify_res = X509_verify(x509, authority_pubkey);
    if (verify_res != 1) {
        throw std::runtime_error("peer cert signature verification failed");
    }

    std::string id;
    id.resize(512 / 8);
    const size_t peer_id_len = peer_id_from_pubkey(
        pubkey, "sha512", reinterpret_cast<byte_t*>(id.data()), id.size()
    );
    id.resize(peer_id_len);

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

bool AuthStore::validate_message(const message::SignedMessage &msg) {
    std::lock_guard<std::mutex> guard(lock);
    auto it = peers.find(msg.sender_id());
    if (it == peers.end()) {
        return false;
    }
    const auto &verifier = it->second.verifier;
    return verifier.verify(
        (const byte_t*)msg.data().data(), msg.data().size(),
        (const byte_t*)msg.signature().data(), msg.signature().size()
    );
}

message::SignedMessage AuthStore::sign_message(
    const message::MessageData &message_data, const std::string recipient_id, bool add_cert
) {
    message::MessageData send_data = message_data;
    send_data.set_recipient_id(recipient_id);
    send_data.set_has_recipient_cert(has(recipient_id));

    message::SignedMessage msg;
    msg.set_sender_id(self_id);
    msg.set_ttl(4);
    if (add_cert) {
        msg.set_sender_cert(self_cert);  // TODO: not always send cert - its big
    }

    if (!signer) {
        throw std::runtime_error("AuthStore: no signing key configured");
    }
    fillMessageData(&msg, send_data, *signer);
    return msg;
}
