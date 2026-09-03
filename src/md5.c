/*
 * md5.c — MD5 as described in RFC 1321.
 *
 * All word packing and unpacking is done byte by byte, so the result does
 * not depend on the machine's endianness: x86 Mac and aarch64 Brick produce
 * the same digest.
 */

#include <string.h>

#include "md5.h"

#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | ~(z)))

#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define STEP(f, a, b, c, d, x, s, ac)                  \
    do {                                               \
        (a) += f((b), (c), (d)) + (x) + (unsigned int)(ac); \
        (a) = ROTATE_LEFT((a), (s));                   \
        (a) += (b);                                    \
    } while (0)

static void md5_transform(unsigned int state[4], const unsigned char block[64]);

/* Padding: a single 1 bit followed by zeros. */
static const unsigned char PADDING[64] = { 0x80 };

void md5_init(Md5Context *ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->count[0] = 0;
    ctx->count[1] = 0;
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xefcdab89U;
    ctx->state[2] = 0x98badcfeU;
    ctx->state[3] = 0x10325476U;
}

void md5_update(Md5Context *ctx, const unsigned char *data, size_t len)
{
    size_t i;
    unsigned int index, part_len;

    if (ctx == NULL || data == NULL) {
        return;
    }

    /* Byte offset inside the 64-byte working block. */
    index = (ctx->count[0] >> 3) & 0x3F;

    /* Advance the 64-bit bit counter, carrying into the high word. */
    ctx->count[0] += (unsigned int)(len << 3);
    if (ctx->count[0] < (unsigned int)(len << 3)) {
        ctx->count[1]++;
    }
    ctx->count[1] += (unsigned int)(len >> 29);

    part_len = 64 - index;

    if (len >= part_len) {
        memcpy(&ctx->buffer[index], data, part_len);
        md5_transform(ctx->state, ctx->buffer);

        for (i = part_len; i + 63 < len; i += 64) {
            md5_transform(ctx->state, &data[i]);
        }
        index = 0;
    } else {
        i = 0;
    }

    memcpy(&ctx->buffer[index], &data[i], len - i);
}

void md5_final(Md5Context *ctx, unsigned char digest[MD5_DIGEST_BYTES])
{
    unsigned char bits[8];
    unsigned int  index, pad_len;
    int           i;

    if (ctx == NULL || digest == NULL) {
        return;
    }

    /* Save the length before padding changes it. */
    for (i = 0; i < 4; i++) {
        bits[i]     = (unsigned char)((ctx->count[0] >> (8 * i)) & 0xFF);
        bits[i + 4] = (unsigned char)((ctx->count[1] >> (8 * i)) & 0xFF);
    }

    index   = (ctx->count[0] >> 3) & 0x3F;
    pad_len = (index < 56) ? (56 - index) : (120 - index);
    md5_update(ctx, PADDING, pad_len);
    md5_update(ctx, bits, 8);

    for (i = 0; i < 4; i++) {
        digest[i * 4]     = (unsigned char)((ctx->state[i]) & 0xFF);
        digest[i * 4 + 1] = (unsigned char)((ctx->state[i] >> 8) & 0xFF);
        digest[i * 4 + 2] = (unsigned char)((ctx->state[i] >> 16) & 0xFF);
        digest[i * 4 + 3] = (unsigned char)((ctx->state[i] >> 24) & 0xFF);
    }

    memset(ctx, 0, sizeof(*ctx));
}

static void md5_transform(unsigned int state[4], const unsigned char block[64])
{
    unsigned int a = state[0], b = state[1], c = state[2], d = state[3];
    unsigned int x[16];
    int          i;

    /* Little-endian unpack, done explicitly so endianness cannot matter. */
    for (i = 0; i < 16; i++) {
        x[i] = (unsigned int)block[i * 4] |
               ((unsigned int)block[i * 4 + 1] << 8) |
               ((unsigned int)block[i * 4 + 2] << 16) |
               ((unsigned int)block[i * 4 + 3] << 24);
    }

    /* Round 1 */
    STEP(F, a, b, c, d, x[0],  7,  0xd76aa478);
    STEP(F, d, a, b, c, x[1],  12, 0xe8c7b756);
    STEP(F, c, d, a, b, x[2],  17, 0x242070db);
    STEP(F, b, c, d, a, x[3],  22, 0xc1bdceee);
    STEP(F, a, b, c, d, x[4],  7,  0xf57c0faf);
    STEP(F, d, a, b, c, x[5],  12, 0x4787c62a);
    STEP(F, c, d, a, b, x[6],  17, 0xa8304613);
    STEP(F, b, c, d, a, x[7],  22, 0xfd469501);
    STEP(F, a, b, c, d, x[8],  7,  0x698098d8);
    STEP(F, d, a, b, c, x[9],  12, 0x8b44f7af);
    STEP(F, c, d, a, b, x[10], 17, 0xffff5bb1);
    STEP(F, b, c, d, a, x[11], 22, 0x895cd7be);
    STEP(F, a, b, c, d, x[12], 7,  0x6b901122);
    STEP(F, d, a, b, c, x[13], 12, 0xfd987193);
    STEP(F, c, d, a, b, x[14], 17, 0xa679438e);
    STEP(F, b, c, d, a, x[15], 22, 0x49b40821);

    /* Round 2 */
    STEP(G, a, b, c, d, x[1],  5,  0xf61e2562);
    STEP(G, d, a, b, c, x[6],  9,  0xc040b340);
    STEP(G, c, d, a, b, x[11], 14, 0x265e5a51);
    STEP(G, b, c, d, a, x[0],  20, 0xe9b6c7aa);
    STEP(G, a, b, c, d, x[5],  5,  0xd62f105d);
    STEP(G, d, a, b, c, x[10], 9,  0x02441453);
    STEP(G, c, d, a, b, x[15], 14, 0xd8a1e681);
    STEP(G, b, c, d, a, x[4],  20, 0xe7d3fbc8);
    STEP(G, a, b, c, d, x[9],  5,  0x21e1cde6);
    STEP(G, d, a, b, c, x[14], 9,  0xc33707d6);
    STEP(G, c, d, a, b, x[3],  14, 0xf4d50d87);
    STEP(G, b, c, d, a, x[8],  20, 0x455a14ed);
    STEP(G, a, b, c, d, x[13], 5,  0xa9e3e905);
    STEP(G, d, a, b, c, x[2],  9,  0xfcefa3f8);
    STEP(G, c, d, a, b, x[7],  14, 0x676f02d9);
    STEP(G, b, c, d, a, x[12], 20, 0x8d2a4c8a);

    /* Round 3 */
    STEP(H, a, b, c, d, x[5],  4,  0xfffa3942);
    STEP(H, d, a, b, c, x[8],  11, 0x8771f681);
    STEP(H, c, d, a, b, x[11], 16, 0x6d9d6122);
    STEP(H, b, c, d, a, x[14], 23, 0xfde5380c);
    STEP(H, a, b, c, d, x[1],  4,  0xa4beea44);
    STEP(H, d, a, b, c, x[4],  11, 0x4bdecfa9);
    STEP(H, c, d, a, b, x[7],  16, 0xf6bb4b60);
    STEP(H, b, c, d, a, x[10], 23, 0xbebfbc70);
    STEP(H, a, b, c, d, x[13], 4,  0x289b7ec6);
    STEP(H, d, a, b, c, x[0],  11, 0xeaa127fa);
    STEP(H, c, d, a, b, x[3],  16, 0xd4ef3085);
    STEP(H, b, c, d, a, x[6],  23, 0x04881d05);
    STEP(H, a, b, c, d, x[9],  4,  0xd9d4d039);
    STEP(H, d, a, b, c, x[12], 11, 0xe6db99e5);
    STEP(H, c, d, a, b, x[15], 16, 0x1fa27cf8);
    STEP(H, b, c, d, a, x[2],  23, 0xc4ac5665);

    /* Round 4 */
    STEP(I, a, b, c, d, x[0],  6,  0xf4292244);
    STEP(I, d, a, b, c, x[7],  10, 0x432aff97);
    STEP(I, c, d, a, b, x[14], 15, 0xab9423a7);
    STEP(I, b, c, d, a, x[5],  21, 0xfc93a039);
    STEP(I, a, b, c, d, x[12], 6,  0x655b59c3);
    STEP(I, d, a, b, c, x[3],  10, 0x8f0ccc92);
    STEP(I, c, d, a, b, x[10], 15, 0xffeff47d);
    STEP(I, b, c, d, a, x[1],  21, 0x85845dd1);
    STEP(I, a, b, c, d, x[8],  6,  0x6fa87e4f);
    STEP(I, d, a, b, c, x[15], 10, 0xfe2ce6e0);
    STEP(I, c, d, a, b, x[6],  15, 0xa3014314);
    STEP(I, b, c, d, a, x[13], 21, 0x4e0811a1);
    STEP(I, a, b, c, d, x[4],  6,  0xf7537e82);
    STEP(I, d, a, b, c, x[11], 10, 0xbd3af235);
    STEP(I, c, d, a, b, x[2],  15, 0x2ad7d2bb);
    STEP(I, b, c, d, a, x[9],  21, 0xeb86d391);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;

    memset(x, 0, sizeof(x));
}

void md5_to_hex(const unsigned char digest[MD5_DIGEST_BYTES], char *out)
{
    static const char hex[] = "0123456789abcdef";
    int i;

    if (digest == NULL || out == NULL) {
        return;
    }
    for (i = 0; i < MD5_DIGEST_BYTES; i++) {
        out[i * 2]     = hex[(digest[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[digest[i] & 0x0F];
    }
    out[MD5_DIGEST_BYTES * 2] = '\0';
}
