#pragma once

#include "defs.h"
#include "exception.h"

#include <cstdlib>


void peer_id_from_hash(const byte_t *data, size_t data_len, const char *hash_algo, byte_t *out_buf);
void peer_id_from_cert(X509 *cert, const char *hash_algo, byte_t *out_buf);
