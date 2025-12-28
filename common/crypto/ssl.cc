#include "ssl.h"


int alpn_select_h2_cb(
    SSL* ssl, const unsigned char** out, unsigned char* outlen,
    const unsigned char* in, unsigned int inlen, void* arg
) {
    // We strictly look for "h2" (the protocol for HTTP/2)
    // The 'in' buffer contains the protocols offered by the client
    // For a simple 'force h2' approach, we just set 'out' to "h2"
    constexpr unsigned char h2_proto[] = {'h', '2'};

    *out = h2_proto;
    *outlen = 2;

    return SSL_TLSEXT_ERR_OK;
}

void prepare_ssl_for_grpc(SSL_CTX* ctx) {
    // Register the function pointer. The 'nullptr' at the end
    // is for optional user data (the 'arg' in the callback).
    SSL_CTX_set_alpn_select_cb(ctx, alpn_select_h2_cb, nullptr);

    // SSL_OP_NO_RENEGOTIATION ???
    // Optional: Disable SSLv3/TLS1.0/1.1 for security, as h2 requires modern TLS
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
}
