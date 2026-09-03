/*
 * test_systems.c — the C equivalent of "go run . --listar".
 *
 * Walks the ROM folder, resolves every tag, and counts the ROMs each system
 * has using its real extension list. Compare the output against the Go
 * reference implementation: the tags, ids and counts should match.
 *
 *   gcc -std=c99 -Wall -Wextra -o test_systems \
 *       test_systems.c systems.c romscan.c strlist.c
 *   ./test_systems sistemas.json "/path/to/teste_roms"
 */

#include <stdio.h>
#include <string.h>

#include "romscan.h"
#include "strlist.h"
#include "systems.h"

static void sanity_checks(const SystemList *systems)
{
    static const char *tags[] = { "FC", "SFC", "MD", "GB", "GBA", "PSP", "XYZ" };
    int i;

    puts("tag resolution");
    for (i = 0; i < (int)(sizeof(tags) / sizeof(tags[0])); i++) {
        int  id   = 0;
        char name[LEV_SYSTEM_NAME_LEN];

        if (systems_resolve_tag(systems, tags[i], &id, name, sizeof(name))) {
            printf("  %-5s -> id %-4d %s\n", tags[i], id, name);
        } else {
            printf("  %-5s -> not recognised\n", tags[i]);
        }
    }
    putchar('\n');
}

static void listar(const SystemList *systems, const char *roms_dir)
{
    StrList folders;
    int     i;
    int     recognised = 0;

    if (list_subdirs(roms_dir, &folders) != 0) {
        printf("could not open %s\n", roms_dir);
        return;
    }

    printf("available systems in %s\n", roms_dir);
    puts("------------------------------------------------------------");

    for (i = 0; i < folders.count; i++) {
        char    tag[LEV_TAG_MAX];
        char    name[LEV_SYSTEM_NAME_LEN];
        char    path[4096];
        int     id = 0;
        StrList exts, roms;

        extract_tag(folders.items[i], tag, sizeof(tag));

        if (!systems_resolve_tag(systems, tag, &id, name, sizeof(name))) {
            printf("  skip  %-24s tag '%s' not recognised\n", folders.items[i], tag);
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", roms_dir, folders.items[i]);
        systems_extensions(systems, id, &exts);

        if (list_roms(path, &exts, &roms) != 0) {
            printf("  %-6s -> id %-4d %-24s (unreadable)\n", tag, id, name);
            strlist_free(&exts);
            continue;
        }

        printf("  %-6s -> id %-4d %-24s %d ROMs", tag, id, name, roms.count);
        if (exts.count > 0) {
            int e;
            printf("   [");
            for (e = 0; e < exts.count; e++) {
                printf("%s%s", (e > 0) ? " " : "", exts.items[e]);
            }
            printf("]");
        } else {
            printf("   [no extension filter]");
        }
        putchar('\n');

        recognised++;
        strlist_free(&exts);
        strlist_free(&roms);
    }

    printf("\n  %d recognised system(s)\n\n", recognised);
    strlist_free(&folders);
}

int main(int argc, char **argv)
{
    SystemList systems;
    int        rc;

    if (argc < 2) {
        puts("usage: test_systems <sistemas.json> [roms_dir]");
        return 1;
    }

    putchar('\n');
    rc = systems_load(argv[1], &systems);
    if (rc == -1) {
        printf("could not read %s — run the cache generator first\n\n", argv[1]);
        return 1;
    }
    if (rc == -2) {
        printf("%s is present but malformed\n\n", argv[1]);
        return 1;
    }
    printf("loaded %d systems from %s\n\n", systems.count, argv[1]);

    sanity_checks(&systems);

    if (argc > 2) {
        listar(&systems, argv[2]);
    }

    systems_free(&systems);
    return 0;
}
