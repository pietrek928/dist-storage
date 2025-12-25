#include <openssl/ssl.h>

#include <utils/guard_ptr.h>


typedef guard_ptr<SSL, SSL_free> SSL_ptr;

void prepare_ssl_for_grpc(SSL_CTX* ctx);
