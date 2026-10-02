#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/ssl_endpoint.h>

#include <crypto/auth.h>
#include <crypto/sgn.h>
#include <crypto/ssl.h>
#include <utils/poll_engine.h>
#include <utils/unique_fd.h>

namespace {

EVP_PKEY_ptr MakeTestKey() {
    EVP_PKEY_CTX_ptr ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
    EXPECT_TRUE(ctx);
    EXPECT_EQ(EVP_PKEY_keygen_init(ctx), 1);
    EXPECT_EQ(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048), 1);
    EVP_PKEY* raw = nullptr;
    EXPECT_EQ(EVP_PKEY_keygen(ctx, &raw), 1);
    return EVP_PKEY_ptr(raw);
}

X509_ptr MakeTestCert(EVP_PKEY* pkey) {
    X509_ptr cert(X509_new());
    EXPECT_TRUE(cert);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 60 * 60);
    X509_set_pubkey(cert, pkey);
    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(
        name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0);
    X509_set_issuer_name(cert, name);
    EXPECT_GT(X509_sign(cert, pkey, EVP_sha256()), 0);
    return cert;
}

SSL_CTX_ptr MakeServerCtx(EVP_PKEY* pkey, X509* cert) {
    SSL_CTX_ptr ctx(SSL_CTX_new(TLS_server_method()));
    EXPECT_TRUE(ctx);
    EXPECT_EQ(SSL_CTX_use_certificate(ctx, cert), 1);
    EXPECT_EQ(SSL_CTX_use_PrivateKey(ctx, pkey), 1);
    return ctx;
}

SSL_CTX_ptr MakeClientCtx() {
    SSL_CTX_ptr ctx(SSL_CTX_new(TLS_client_method()));
    EXPECT_TRUE(ctx);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    return ctx;
}

void SetNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    ASSERT_GE(flags, 0);
    ASSERT_EQ(fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0);
}

}  // namespace

TEST(SslEndpointTest, ReadWriteRoundTrip) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    unique_fd server_fd(fds[0]);
    unique_fd client_fd(fds[1]);

    EVP_PKEY_ptr pkey = MakeTestKey();
    X509_ptr cert = MakeTestCert(pkey);
    SSL_CTX_ptr server_ctx = MakeServerCtx(pkey, cert);
    SSL_CTX_ptr client_ctx = MakeClientCtx();

    SSL_ptr server_ssl(SSL_new(server_ctx));
    SSL_ptr client_ssl(SSL_new(client_ctx));
    ASSERT_TRUE(server_ssl);
    ASSERT_TRUE(client_ssl);

    BIO_ptr server_bio(BIO_new_socket(server_fd, BIO_NOCLOSE));
    BIO_ptr client_bio(BIO_new_socket(client_fd, BIO_NOCLOSE));
    BIO* server_bio_raw = server_bio.handle();
    BIO* client_bio_raw = client_bio.handle();
    SSL_set_bio(server_ssl, server_bio_raw, server_bio_raw);
    SSL_set_bio(client_ssl, client_bio_raw, client_bio_raw);

    SetNonBlocking(server_fd);
    SetNonBlocking(client_fd);

    SSL_set_accept_state(server_ssl);
    SSL_set_connect_state(client_ssl);

    bool server_done = false;
    bool client_done = false;
    for (int i = 0; i < 200 && !(server_done && client_done); ++i) {
        if (!server_done) {
            int r = SSL_accept(server_ssl);
            if (r == 1) {
                server_done = true;
            } else {
                int err = SSL_get_error(server_ssl, r);
                ASSERT_TRUE(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                    << "SSL_accept err=" << err;
            }
        }
        if (!client_done) {
            int r = SSL_connect(client_ssl);
            if (r == 1) {
                client_done = true;
            } else {
                int err = SSL_get_error(client_ssl, r);
                ASSERT_TRUE(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                    << "SSL_connect err=" << err;
            }
        }
    }
    ASSERT_TRUE(server_done);
    ASSERT_TRUE(client_done);

    auto poller = std::make_shared<PollEngine>();
    auto server_ep = std::make_unique<SSLEndpoint>(
        std::move(server_fd), std::move(server_ssl), poller);
    auto client_ep = std::make_unique<SSLEndpoint>(
        std::move(client_fd), std::move(client_ssl), poller);

    const std::string payload = "hello-ssl-endpoint";
    grpc_exp::SliceBuffer write_buf;
    write_buf.Append(grpc_exp::Slice::FromCopiedString(payload));

    std::atomic<bool> write_done{false};
    absl::Status write_status = absl::UnknownError("unset");
    bool write_sync = client_ep->Write(
        [&](absl::Status st) {
            write_status = st;
            write_done.store(true);
        },
        &write_buf,
        grpc_exp::EventEngine::Endpoint::WriteArgs());

    grpc_exp::SliceBuffer read_buf;
    std::atomic<bool> read_done{false};
    absl::Status read_status = absl::UnknownError("unset");
    bool read_sync = server_ep->Read(
        [&](absl::Status st) {
            read_status = st;
            read_done.store(true);
        },
        &read_buf,
        grpc_exp::EventEngine::Endpoint::ReadArgs());

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while ((!write_done.load() || !read_done.load()) &&
           std::chrono::steady_clock::now() < deadline) {
        poller->poll(50);
    }

    ASSERT_TRUE(write_done.load());
    ASSERT_TRUE(read_done.load());
    EXPECT_TRUE(write_status.ok());
    EXPECT_TRUE(read_status.ok());
    if (write_sync) {
        EXPECT_TRUE(write_status.ok());
    }
    if (read_sync) {
        EXPECT_TRUE(read_status.ok());
    }

    std::string got;
    while (read_buf.Count() > 0) {
        grpc_exp::Slice s = read_buf.TakeFirst();
        got.append(reinterpret_cast<const char*>(s.begin()), s.size());
    }
    EXPECT_EQ(got, payload);
}
