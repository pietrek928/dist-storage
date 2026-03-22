#pragma once

#include <openssl/ssl.h>

#include <utils/guard_ptr.h>


typedef guard_ptr<SSL, SSL_free> SSL_ptr;
typedef guard_ptr<SSL_CTX, SSL_CTX_free> SSL_CTX_ptr;

void prepare_ssl_for_grpc(SSL_CTX* ctx);
