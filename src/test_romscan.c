/*
 * test_romscan.c — terminal check for the directory-walking step.
 *
 * Build and run on the Mac; no network and no device needed.
 *   gcc -std=c99 -Wall -Wextra -o test_romscan \
 *       test_romscan.c romscan.c strlist.c
 *   ./test_romscan "/path/to/teste_roms"
 */

#include <stdio.h>
#include <string.h>

#include "romscan.h"
#include "strlist.h"

static int failures = 0;

static void check_tag(const char *folder, const char *expected)
{
    char tag[LEV_TAG_MAX];

    extract_tag(folder, tag, sizeof(tag));
    if (strcmp(tag, expected) == 0) {
        printf("  ok    \"%s\" -> %s\n", folder, tag);
    } else {
        printf("  FAIL  \"%s\" -> %s (expected %s)\n", folder, tag, expected);
        failures++;
    }
}

static void tag_tests(void)
{
    puts("extract_tag");
    check_tag("Game Boy Advance (GBA)", "GBA");
    check_tag("FC", "FC");
    check_tag("  Sega Mega Drive (MD)  ", "MD");
    check_tag("gba", "GBA");
    check_tag("Sega CD (Mega-CD) (SCD)", "SCD");
    check_tag("Pasta Estranha ()", "PASTA ESTRANHA ()");
    check_tag("PC Engine ( PCE )", "PCE");
    check_tag("", "");
    putchar('\n');
}

static void extension_tests(void)
{
    StrList exts;
    int     i;

    puts("parse_extensions(\"gba, GB ,gbc,,zip\")");
    parse_extensions("gba, GB ,gbc,,zip", &exts);
    printf("  %d entries:", exts.count);
    for (i = 0; i < exts.count; i++) {
        printf(" %s", exts.items[i]);
    }
    printf("\n  %s\n\n", exts.count == 4 ? "ok" : "FAIL expected 4");
    if (exts.count != 4) {
        failures++;
    }
    strlist_free(&exts);
}

static void walk(const char *roms_dir)
{
    StrList folders;
    int     i;

    printf("walking %s\n", roms_dir);

    if (list_subdirs(roms_dir, &folders) != 0) {
        printf("  FAIL  could not open the folder\n");
        failures++;
        return;
    }
    printf("  %d subfolder(s)\n\n", folders.count);

    for (i = 0; i < folders.count; i++) {
        char    path[4096];
        char    tag[LEV_TAG_MAX];
        StrList roms;
        int     j;

        extract_tag(folders.items[i], tag, sizeof(tag));
        snprintf(path, sizeof(path), "%s/%s", roms_dir, folders.items[i]);

        printf("  %-28s tag %-6s", folders.items[i], tag);

        /* NULL extension list = we do not know this system yet, take all. */
        if (list_roms(path, NULL, &roms) != 0) {
            printf("  (unreadable)\n");
            continue;
        }
        printf("%d rom(s)\n", roms.count);
        for (j = 0; j < roms.count; j++) {
            printf("        %s\n", roms.items[j]);
        }
        strlist_free(&roms);
    }
    strlist_free(&folders);
    putchar('\n');
}

static void filtered_walk(const char *roms_dir)
{
    StrList exts, roms;
    char    path[4096];
    int     i;

    parse_extensions("gba", &exts);
    snprintf(path, sizeof(path), "%s/Game Boy Advance (GBA)", roms_dir);

    printf("filtered by .gba in %s\n", path);
    if (list_roms(path, &exts, &roms) == 0) {
        printf("  %d rom(s)\n", roms.count);
        for (i = 0; i < roms.count; i++) {
            printf("        %s\n", roms.items[i]);
        }
        strlist_free(&roms);
    } else {
        puts("  (folder not present, skipping)");
    }
    strlist_free(&exts);
    putchar('\n');
}

int main(int argc, char **argv)
{
    putchar('\n');
    tag_tests();
    extension_tests();

    if (argc > 1) {
        walk(argv[1]);
        filtered_walk(argv[1]);
    } else {
        puts("(no folder given; pass one to walk it)\n");
    }

    printf("%s\n\n", failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
