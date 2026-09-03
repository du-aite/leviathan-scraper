/*
 * md5.h — MD5 message digest.
 *
 * Implemented here rather than linked against OpenSSL. The device does ship
 * libcrypto, but MD5 is small and self-contained, and keeping it in-tree
 * means the hashing path is byte-identical on the Mac and on the Brick with
 * nothing to configure.
 *
 * MD5 is broken for security purposes. That is irrelevant here: ScreenScraper
 * indexes its database by MD5, so this is a lookup key, not a guarantee.
 */

#ifndef LEVIATHAN_MD5_H
#define LEVIATHAN_MD5_H

#include <stddef.h>

#define MD5_DIGEST_BYTES 16
#define MD5_HEX_LEN      33 /* 32 characters plus the terminator */

typedef struct {
    unsigned int   state[4];
    unsigned int   count[2]; /* message length in bits, low word first */
    unsigned char  buffer[64];
} Md5Context;

void md5_init(Md5Context *ctx);
void md5_update(Md5Context *ctx, const unsigned char *data, size_t len);
void md5_final(Md5Context *ctx, unsigned char digest[MD5_DIGEST_BYTES]);

/* Write the digest as lowercase hex. out must hold MD5_HEX_LEN bytes. */
void md5_to_hex(const unsigned char digest[MD5_DIGEST_BYTES], char *out);

#endif /* LEVIATHAN_MD5_H */
