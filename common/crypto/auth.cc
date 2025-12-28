#include "auth.h"

#include "call_check.h"
#include "sgn.h"


constexpr static int PEER_ID_HASH_ROUNDS = 1<<14;

constexpr static char PEER_ID_SEED_1[] = "834h804n82y387ynx2307ny8x70236n4786yn38923";
constexpr static char PEER_ID_SEED_2[] = "83n1-9348yc5n780345mn83485mt483mtg7tm58c48";
constexpr static char PEER_ID_SEED_3[] = "c45e654wv653w6537v6brftrxwqt56bf79n8t7f6f6";
constexpr static char PEER_ID_SEED_4[] = "7b5es75nm698g098ym76e534xcw76v8r67bt9tbt6t";
void peer_id_from_hash(const byte_t *data, size_t data_len, const char *hash_algo, byte_t *out_buf) {
    SSLHasher hasher(hash_algo);
    auto hash_size = hasher.digest_size();

    hasher.start();
    hasher.put((const byte_t *)PEER_ID_SEED_1, sizeof(PEER_ID_SEED_1));
    hasher.put(data, data_len);
    hasher.put((const byte_t *)PEER_ID_SEED_2, sizeof(PEER_ID_SEED_2));
    hasher.finish(out_buf);

    for (int i=0; i<PEER_ID_HASH_ROUNDS; i++) {
        hasher.start();
        hasher.put(out_buf, hash_size);
        hasher.put((const byte_t *)PEER_ID_SEED_3, sizeof(PEER_ID_SEED_3));
        hasher.put(out_buf, hash_size);
        hasher.put((const byte_t *)PEER_ID_SEED_4, sizeof(PEER_ID_SEED_4));
        hasher.finish(out_buf);
    }
}

void peer_id_from_cert(X509 *cert, const char *hash_algo, byte_t *out_buf) {
    EVP_PKEY_ptr pubkey;
    scall("cert - getting public key", pubkey = X509_get_pubkey(cert));

    SSL_DATA_ptr pubkey_subject_info;
    auto subject_len = i2d_PUBKEY(pubkey, (unsigned char **)&pubkey_subject_info.ptr);
    if (subject_len <= 0 || !pubkey_subject_info) {
        throw SSLError("cert - transforming to subject form failed");
    }

    peer_id_from_hash(pubkey_subject_info.ptr, subject_len, hash_algo, out_buf);
}

std::string x509_to_string(X509* cert) {
    if (!cert) return "";

    // Determine the length of the DER encoding
    int len = i2d_X509(cert, nullptr);
    if (len <= 0) {
        throw SSLError("i2d_X509 failed");
    }

    std::string der_out;
    der_out.resize(len);

    // i2d_X509 increments the pointer, so use a temporary one
    unsigned char* p = reinterpret_cast<unsigned char*>(&der_out[0]);
    i2d_X509(cert, &p);

    return der_out;
}

X509* string_to_x509(const std::string &der_data) {
    if (der_data.empty()) return nullptr;

    // Use a temporary pointer because d2i_X509 increments it
    const unsigned char* p = reinterpret_cast<const unsigned char*>(der_data.data());

    X509* cert = d2i_X509(nullptr, &p, der_data.size());

    if (!cert) {
        throw SSLError("d2i_X509 failed");
    }

    return cert;
}
