#pragma once

#include <cstdlib>
#include <string>
#include <openssl/x509.h>

#include <utils/defs.h>
#include <utils/guard_ptr.h>
#include <utils/exception.h>


typedef guard_ptr<X509, X509_free> X509_ptr;

void peer_id_from_hash(const byte_t *data, size_t data_len, const char *hash_algo, byte_t *out_buf);
void peer_id_from_pubkey(EVP_PKEY *pubkey, const char *hash_algo, byte_t *out_buf);
std::string x509_to_string(X509* cert);
X509* string_to_x509(const std::string &der_data);
