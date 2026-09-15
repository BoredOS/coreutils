// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef LIBCRYPT_SHA512_H
#define LIBCRYPT_SHA512_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define SHA512_CRYPT_ROUNDS_DEFAULT 5000
#define SHA512_CRYPT_ROUNDS_MIN     1000
#define SHA512_CRYPT_ROUNDS_MAX     999999999
#define SHA512_CRYPT_SALT_LEN_MAX   16

char *sha512_crypt(const char *key, const char *setting, char *out, size_t out_len);
bool sha512_crypt_gensalt(char *out, size_t out_len);
bool sha512_crypt_verify(const char *key, const char *hash);

#endif // LIBCRYPT_SHA512_H
