#include "create.h"


void fill_message(
    message::SignedMessage *m,
    const std::string sender_id, const SSLSigner &signer, const message::MessageData &data,
    int ttl, const std::string *sender_cert
) {
    m->set_sender_id(sender_id);
    if (sender_cert) {
        m->set_sender_cert(*sender_cert);
    }
    m->set_data(data.SerializeAsString());
    m->set_ttl(ttl);

    m->mutable_signature()->resize(signer.signature_size());
    signer.sign((const byte_t*)m->data().data(), m->data().size(), (byte_t*)m->mutable_signature()->data());
}
