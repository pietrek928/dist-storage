#include "sgn.h"

#include "call_check.h"
#include <cstddef>


void SSLFreeData(unsigned char *data_ptr) {
    OPENSSL_free(data_ptr);
}

SSLSigner::SSLSigner(int type, const byte_t *priv_key, size_t priv_key_len, const char *hash_name) {
    const EVP_MD *md;
    ssl_call("looking for hash",
        md = EVP_get_digestbyname(hash_name)
    );

    EVP_PKEY_ptr pkey;
    ssl_call("EVP_PKEY_new_raw_private_key",
        pkey = EVP_PKEY_new_raw_private_key(
            type, NULL, priv_key, priv_key_len
        )
    );

    ssl_call("EVP_MD_CTX_create", ctx = EVP_MD_CTX_create());

    ssl_call(
        "signer - inituializing signing context",
        EVP_DigestSignInit(ctx, NULL, md, NULL, pkey)
    );

    ssl_call(
        "Getting signature size",
        EVP_DigestSignFinal(ctx, NULL, &sig_len)
    );
}

size_t SSLSigner::signature_size() const {
    return sig_len;
}

void SSLSigner::sign(
    const byte_t *data, size_t data_len, byte_t *out_buf
) const {
    size_t out_len = 0;
    ssl_call("signer - signing digest", EVP_DigestSign(
        ctx, out_buf, &out_len, data, data_len
    ));
    if (unlikely(out_len != sig_len)) {
        throw SSLError("EVP_DigestSign: returned invalid signature size", -1);
    }
}


SSLVerifier::SSLVerifier(
    int type, EVP_PKEY *pkey, const char *hash_name
) {
    const EVP_MD *md;
    ssl_call("looking for hash",
        md = EVP_get_digestbyname(hash_name)
    );

    ctx = EVP_MD_CTX_create();
    ssl_call("EVP_MD_CTX_create", ctx);

    ssl_call(
        "verifier - initializing verification context",
        EVP_DigestVerifyInit(ctx, NULL, md, NULL, pkey)
    );
}

bool SSLVerifier::verify(
    const byte_t *dgst, size_t dgst_len, const byte_t *sgn, size_t sgn_len
) const {
    auto result = EVP_DigestVerify(
        ctx, sgn, sgn_len, dgst, dgst_len
    );
    switch(result) {
        case 1:
            return true;
        case 0:
            return false;
        default:
            throw SSLError("EVP_DigestVerify");
    }
}


SSLHasher::SSLHasher(const char *hash_name) {
    const EVP_MD *md;
    ssl_call("looking for hash",
        md = EVP_get_digestbyname(hash_name)
    );

    ssl_call("init hasher ctx", ctx = EVP_MD_CTX_new());
}

int SSLHasher::digest_size() const {
    return EVP_MD_size(md);
}

void SSLHasher::start() {
    ssl_call("hashing - init", EVP_DigestInit_ex2(ctx, md, NULL));
}
void SSLHasher::put(const byte_t *data, size_t data_len) {
    ssl_call("hashing - append data", EVP_DigestUpdate(ctx, data, data_len));
}
size_t SSLHasher::finish(byte_t *out_data) {
    unsigned int s;
    ssl_call("hashing - retrieving digest", EVP_DigestFinal_ex(ctx, (unsigned char*)out_data, &s));
    return s;
}
void SSLHasher::reset() {
    ssl_call("hashing - resetting context", EVP_MD_CTX_reset(ctx));
}

size_t SSLHasher::compute_hash(const byte_t *data, size_t data_len, byte_t *out_data) {
    try {
        start();
        put(data, data_len);
        return finish(out_data);
    } catch(...) {
        reset();
        throw;
    }
};


void generate_rsa_key(
    size_t bits, std::vector<byte_t> &pubkey, std::vector<byte_t> &privkey
) {
    EVP_PKEY_CTX_ptr pkey = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    ssl_call("EVP_PKEY_CTX_new_id", pkey);

    ssl_call("rsa keygen - init", EVP_PKEY_keygen_init(pkey));

    ssl_call("rsa keygen - set bits", EVP_PKEY_CTX_set_rsa_keygen_bits(pkey, bits));

    EVP_PKEY_ptr key;
    ssl_call("rsa keygen", EVP_PKEY_keygen(pkey, &key.ptr));

    size_t pubkey_len = 0;
    ssl_call(
        "rsa keygen - get pubkey len",
        EVP_PKEY_get_raw_public_key(key, NULL, &pubkey_len)
    );
    pubkey.resize(pubkey_len);
    ssl_call(
        "rsa keygen - get pubkey",
        EVP_PKEY_get_raw_public_key(key, pubkey.data(), &pubkey_len)
    );

    size_t privkey_len = 0;
    ssl_call(
        "rsa keygen - get privkey len",
        EVP_PKEY_get_raw_private_key(key, NULL, &privkey_len)
    );
    privkey.resize(privkey_len);
    ssl_call(
        "rsa keygen - get privkey",
        EVP_PKEY_get_raw_private_key(key, privkey.data(), &privkey_len)
    );
}

void generate_ecdsa_key(
    int curve_nid, std::vector<byte_t> &pubkey, std::vector<byte_t> &privkey
) {
    EVP_PKEY_CTX_ptr pkey;
    ssl_call("EVP_PKEY_CTX_new_id", pkey = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL));

    ssl_call(
        "ecdsa keygen - init",
        EVP_PKEY_keygen_init(pkey)
    );

    ssl_call(
        "ecdsa keygen - set curve",
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pkey, NID_X9_62_prime256v1)
    );

    EVP_PKEY_ptr key;
    ssl_call(
        "ecdca keygen",
        EVP_PKEY_keygen(pkey, &key.ptr)
    );

    size_t pubkey_len = 0;
    ssl_call(
        "ecdsa keygen - get pubkey len",
        EVP_PKEY_get_raw_public_key(key, NULL, &pubkey_len)
    );
    pubkey.resize(pubkey_len);
    ssl_call(
        "ecdsa keygen - get pubkey",
        EVP_PKEY_get_raw_public_key(key, pubkey.data(), &pubkey_len)
    );

    size_t privkey_len = 0;
    ssl_call(
        "ecdsa keygen - get privkey len",
        EVP_PKEY_get_raw_private_key(key, NULL, &privkey_len)
    );
    privkey.resize(privkey_len);
    ssl_call(
        "ecdsa keygen - get privkey",
        EVP_PKEY_get_raw_private_key(key, privkey.data(), &privkey_len)
    );
}