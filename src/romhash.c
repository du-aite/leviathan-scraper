/*
 * romhash.c — CRC-32 and the single-pass file hashing loop.
 */

#include <stdio.h>
#include <string.h>

#include "romhash.h"

/* 64 KB: large enough that syscall overhead disappears, small enough to sit
 * comfortably on the Brick's stack budget when allocated statically below. */
#define HASH_CHUNK 65536

/* ------------------------------------------------------------------
 * CRC-32, IEEE 802.3 polynomial (0xEDB88320 reflected).
 *
 * The lookup table is built on first use instead of being written out as
 * 256 constants: it costs microseconds once and keeps the source honest.
 * ------------------------------------------------------------------ */

static unsigned int crc_table[256];
static int          crc_table_ready = 0;

static void build_crc_table(void)
{
    unsigned int i, j, value;

    for (i = 0; i < 256; i++) {
        value = i;
        for (j = 0; j < 8; j++) {
            value = (value & 1U) ? (0xEDB88320U ^ (value >> 1)) : (value >> 1);
        }
        crc_table[i] = value;
    }
    crc_table_ready = 1;
}

unsigned int crc32_update(unsigned int crc, const unsigned char *data, size_t len)
{
    size_t i;

    if (!crc_table_ready) {
        build_crc_table();
    }
    if (data == NULL) {
        return crc;
    }

    crc = crc ^ 0xFFFFFFFFU;
    for (i = 0; i < len; i++) {
        crc = crc_table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

/* ------------------------------------------------------------------
 * Hashing a file
 * ------------------------------------------------------------------ */

int rom_hash_file(const char *path, RomHashes *out)
{
    static unsigned char chunk[HASH_CHUNK]; /* static: too big for the stack */

    FILE         *f;
    Md5Context    md5ctx;
    unsigned char digest[MD5_DIGEST_BYTES];
    unsigned int  crc = CRC32_INIT;
    size_t        got;
    long          total = 0;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    if (path == NULL) {
        return -1;
    }

    f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }

    md5_init(&md5ctx);

    /* One pass, both hashes fed from the same chunk — the C equivalent of
     * io.MultiWriter(md5h, crch). */
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        md5_update(&md5ctx, chunk, got);
        crc = crc32_update(crc, chunk, got);
        total += (long)got;
    }

    if (ferror(f)) {
        fclose(f);
        memset(out, 0, sizeof(*out));
        return -1;
    }
    fclose(f);

    md5_final(&md5ctx, digest);
    md5_to_hex(digest, out->md5);

    /* %08x: ScreenScraper expects eight lowercase digits, zero padded. */
    sprintf(out->crc, "%08x", crc);
    out->size = total;

    return 0;
}
