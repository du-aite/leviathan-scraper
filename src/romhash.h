/*
 * romhash.h — the fingerprint ScreenScraper looks a ROM up by.
 *
 * Both hashes are computed in a single pass over the file, in 64 KB chunks,
 * so a 1 GB PSP image never lands in RAM. This mirrors what the Go version
 * did with io.MultiWriter.
 */

#ifndef LEVIATHAN_ROMHASH_H
#define LEVIATHAN_ROMHASH_H

#include <stddef.h>

#include "md5.h"

#define CRC32_HEX_LEN 9 /* 8 characters plus the terminator */

typedef struct {
    char crc[CRC32_HEX_LEN]; /* lowercase, always 8 digits, zero padded */
    char md5[MD5_HEX_LEN];   /* lowercase, 32 digits                    */
    long size;               /* file size in bytes                      */
} RomHashes;

/* Running CRC-32 (IEEE 802.3, the variant zip and Go's crc32.NewIEEE use). */
unsigned int crc32_update(unsigned int crc, const unsigned char *data, size_t len);

/* Start value for a fresh CRC-32 run. */
#define CRC32_INIT 0U

/*
 * Hash a whole file. Returns 0 on success, -1 if the file cannot be read.
 * On failure the struct is zeroed but safe to inspect.
 */
int rom_hash_file(const char *path, RomHashes *out);

#endif /* LEVIATHAN_ROMHASH_H */
