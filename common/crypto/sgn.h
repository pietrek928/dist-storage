#pragma once

#include <vector>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/ec.h>
#include <openssl/objects.h>
#include <openssl/err.h>

#include <utils/defs.h>
#include <utils/guard_ptr.h>


void SSLFreeData(unsigned char *data_ptr);

template <auto DeleteFn>
struct DeleterFromFn {
    template <typename T>
    void operator()(T* ptr) const {
        DeleteFn(ptr);
    }
};

typedef guard_ptr<EVP_MD_CTX, EVP_MD_CTX_free> EVP_MD_CTX_ptr;
typedef guard_ptr<EVP_PKEY, EVP_PKEY_free> EVP_PKEY_ptr;
typedef guard_ptr<EVP_PKEY_CTX, EVP_PKEY_CTX_free> EVP_PKEY_CTX_ptr;
typedef guard_ptr<unsigned char, SSLFreeData> SSL_DATA_ptr;

class SSLSigner {
    EVP_MD_CTX_ptr ctx;

    public:
    SSLSigner(int type, const byte_t *priv_key, size_t priv_key_len);
    size_t sign(const byte_t *data, size_t data_len, byte_t *out_buf, size_t out_len);
};

class SSLVerifier {
    EVP_MD_CTX_ptr ctx;

    public:
    SSLVerifier(int type, const byte_t *pub_key, size_t pub_key_len);
    bool verify(const byte_t *dgst, size_t dgst_len, const byte_t *sgn, size_t sgn_len);
};

class SSLHasher {
    EVP_MD_CTX_ptr ctx;
    const EVP_MD *md;

    public:
    SSLHasher(const char *hash_name);
    int digest_size();

    void start();
    void put(const byte_t *data, size_t data_len);
    size_t finish(byte_t *out_data);
    void reset();

    size_t compute_hash(const byte_t *data, size_t data_len, byte_t *out_data);
};

void generate_rsa_key(size_t bits, std::vector<byte_t> &pubkey, std::vector<byte_t> &privkey);
void generate_ecdsa_key(int curve_nid, std::vector<byte_t> &pubkey, std::vector<byte_t> &privkey);
