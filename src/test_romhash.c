/*
 * test_romhash.c — checks the fingerprint code two ways.
 *
 * First against the published test vectors for MD5 (RFC 1321) and CRC-32,
 * then against real files so the results can be compared with the system's
 * own md5 and crc32 tools.
 *
 *   gcc -std=c99 -Wall -Wextra -o test_romhash \
 *       test_romhash.c romhash.c md5.c romscan.c strlist.c
 *   ./test_romhash                       # vectors only
 *   ./test_romhash /path/to/teste_roms   # also hash every ROM found
 */

#include <stdio.h>
#include <string.h>

#include "md5.h"
#include "romhash.h"
#include "romscan.h"
#include "strlist.h"

static int failures = 0;

static void check_md5(const char *input, const char *expected)
{
    Md5Context    ctx;
    unsigned char digest[MD5_DIGEST_BYTES];
    char          hex[MD5_HEX_LEN];

    md5_init(&ctx);
    md5_update(&ctx, (const unsigned char *)input, strlen(input));
    md5_final(&ctx, digest);
    md5_to_hex(digest, hex);

    if (strcmp(hex, expected) == 0) {
        printf("  ok    md5(\"%s\")\n", input);
    } else {
        printf("  FAIL  md5(\"%s\") = %s, expected %s\n", input, hex, expected);
        failures++;
    }
}

static void check_crc(const char *input, const char *expected)
{
    unsigned int crc = crc32_update(CRC32_INIT, (const unsigned char *)input, strlen(input));
    char         hex[CRC32_HEX_LEN];

    sprintf(hex, "%08x", crc);

    if (strcmp(hex, expected) == 0) {
        printf("  ok    crc32(\"%s\")\n", input);
    } else {
        printf("  FAIL  crc32(\"%s\") = %s, expected %s\n", input, hex, expected);
        failures++;
    }
}

static void vectors(void)
{
    puts("published test vectors");
    check_md5("", "d41d8cd98f00b204e9800998ecf8427e");
    check_md5("a", "0cc175b9c0f1b6a831c399e269772661");
    check_md5("abc", "900150983cd24fb0d6963f7d28e17f72");
    check_md5("message digest", "f96b697d7cb7938d525a2f31aaf161d0");
    check_md5("abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b");
    check_md5("12345678901234567890123456789012345678901234567890"
              "123456789012345678901234567890",
              "57edf4a22be3c955ac49da2e2107b67a");

    check_crc("", "00000000");
    check_crc("a", "e8b7be43");
    check_crc("abc", "352441c2");
    check_crc("123456789", "cbf43926");
    putchar('\n');
}

/* Hash a message split across several update calls. If the chunking logic is
 * wrong this diverges from the single-call result, which is exactly the bug
 * that would only show up on large files. */
static void chunking(void)
{
    const char   *part1 = "The quick brown fox ";
    const char   *part2 = "jumps over the lazy dog";
    Md5Context    ctx;
    unsigned char digest[MD5_DIGEST_BYTES];
    char          split[MD5_HEX_LEN], whole[MD5_HEX_LEN];
    char          joined[64];

    md5_init(&ctx);
    md5_update(&ctx, (const unsigned char *)part1, strlen(part1));
    md5_update(&ctx, (const unsigned char *)part2, strlen(part2));
    md5_final(&ctx, digest);
    md5_to_hex(digest, split);

    sprintf(joined, "%s%s", part1, part2);
    md5_init(&ctx);
    md5_update(&ctx, (const unsigned char *)joined, strlen(joined));
    md5_final(&ctx, digest);
    md5_to_hex(digest, whole);

    puts("chunked vs single-shot");
    if (strcmp(split, whole) == 0) {
        printf("  ok    %s\n\n", whole);
    } else {
        printf("  FAIL  %s vs %s\n\n", split, whole);
        failures++;
    }
}

static void hash_collection(const char *roms_dir)
{
    StrList folders;
    int     i;

    if (list_subdirs(roms_dir, &folders) != 0) {
        printf("could not open %s\n", roms_dir);
        return;
    }

    printf("hashing every ROM in %s\n", roms_dir);
    puts("------------------------------------------------------------");

    for (i = 0; i < folders.count; i++) {
        char    path[4096];
        StrList roms;
        int     j;

        snprintf(path, sizeof(path), "%s/%s", roms_dir, folders.items[i]);
        if (list_roms(path, NULL, &roms) != 0) {
            continue;
        }

        printf("\n%s\n", folders.items[i]);
        for (j = 0; j < roms.count; j++) {
            char      full[8192];
            RomHashes h;

            snprintf(full, sizeof(full), "%s/%s", path, roms.items[j]);
            if (rom_hash_file(full, &h) != 0) {
                printf("  ERROR reading %s\n", roms.items[j]);
                failures++;
                continue;
            }
            printf("  crc %s  md5 %s  %7ld B  %s\n", h.crc, h.md5, h.size, roms.items[j]);
        }
        strlist_free(&roms);
    }
    strlist_free(&folders);
    putchar('\n');
}

int main(int argc, char **argv)
{
    putchar('\n');
    vectors();
    chunking();

    if (argc > 1) {
        hash_collection(argv[1]);
    }

    printf("%s\n\n", failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
