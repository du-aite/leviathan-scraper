/*
 * test_scrape.c — the terminal front end for the Phase 1 core.
 *
 * Stands where the Go testeTerminal() stood: it only decides and calls.
 * Every piece it uses has no idea a terminal exists, which is what lets
 * Phase 2 hang a screen on the same functions without touching them.
 *
 *   gcc -std=c99 -Wall -Wextra -o test_scrape \
 *       test_scrape.c scrape.c sshttp.c ssparse.c romhash.c md5.c \
 *       romscan.c strlist.c systems.c status.c jsmn.c -lcurl
 *
 *   ./test_scrape --list <roms dir>            list systems, download nothing
 *   ./test_scrape <roms dir>                   scrape everything
 *   ./test_scrape <roms dir> FC GB             scrape only those tags
 *   ./test_scrape --stop-after 3 <roms dir>    stop early, to prove the tally survives
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "credentials.h"
#include "romscan.h"
#include "scrape.h"
#include "sshttp.h"
#include "status.h"
#include "strlist.h"
#include "systems.h"

/* What the callbacks share with main. In Phase 2 this becomes the screen. */
typedef struct {
    int   processed;
    int   stop_after; /* 0 = run to the end */
    char  last_system[LEV_SYSTEM_NAME_MAX];
} TerminalState;

/* Where this terminal test writes covers. LEV_OUTPUT_DIR used to live in
 * config.h, but moved into main.c as platform logic in Phase 3, so this test
 * defines its own — the same "./Imgs" the app uses on the Mac side. */
#define TEST_OUTPUT_DIR "./Imgs"

static void on_progress(const char *system_name, int index, int total,
                        const char *rom_name, void *user)
{
    TerminalState *state = (TerminalState *)user;
    char           shown[40];

    /* A header, printed only when the system changes. */
    if (strcmp(state->last_system, system_name) != 0) {
        printf("\n%s\n", system_name);
        strncpy(state->last_system, system_name, sizeof(state->last_system) - 1);
        state->last_system[sizeof(state->last_system) - 1] = '\0';
    }

    strncpy(shown, rom_name, sizeof(shown) - 1);
    shown[sizeof(shown) - 1] = '\0';

    printf("  [%3d/%3d] %-39s ", index, total, shown);
    fflush(stdout); /* the line only finishes once the ROM is done */
}

static void on_result(ScrapeStatus status, const char *rom_name,
                      const char *detail, void *user)
{
    TerminalState *state = (TerminalState *)user;
    const char    *mark;

    (void)rom_name;
    state->processed++;

    switch (status) {
    case SCRAPE_OK:      mark = "ok  "; break;
    case SCRAPE_SKIPPED: mark = "skip"; break;
    default:             mark = "--  "; break;
    }

    printf("%s %s\n", mark,
           (detail != NULL && detail[0] != '\0') ? detail : scrape_status_label(status));
}

static int should_abort(void *user)
{
    TerminalState *state = (TerminalState *)user;

    if (state->stop_after <= 0) {
        return 0;
    }
    return (state->processed >= state->stop_after) ? 1 : 0;
}

/* ------------------------------------------------------------------
 * Listing only
 * ------------------------------------------------------------------ */

static void list_systems(const SystemList *systems, const char *roms_dir)
{
    StrList folders;
    int     i, recognised = 0, total_roms = 0;

    if (list_subdirs(roms_dir, &folders) != 0) {
        printf("could not open %s\n\n", roms_dir);
        return;
    }

    printf("\navailable systems in %s\n", roms_dir);
    puts("--------------------------------------------------------------");

    for (i = 0; i < folders.count; i++) {
        char    tag[LEV_TAG_MAX];
        char    name[LEV_SYSTEM_NAME_LEN];
        char    path[4096];
        int     id = 0;
        StrList exts, roms;

        extract_tag(folders.items[i], tag, sizeof(tag));
        if (!systems_resolve_tag(systems, tag, &id, name, sizeof(name))) {
            printf("  skip   %-24s tag '%s' not recognised\n", folders.items[i], tag);
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", roms_dir, folders.items[i]);
        systems_extensions(systems, id, &exts);
        if (list_roms(path, &exts, &roms) == 0) {
            printf("  %-6s -> id %-4d %-24s %d ROMs\n", tag, id, name, roms.count);
            total_roms += roms.count;
            recognised++;
            strlist_free(&roms);
        }
        strlist_free(&exts);
    }

    printf("\n  %d systems, %d ROMs\n\n", recognised, total_roms);
    strlist_free(&folders);
}

/* ------------------------------------------------------------------
 * Summary
 * ------------------------------------------------------------------ */

static void print_summary(const ScrapeTotals *totals, RunStatus run)
{
    puts("\n==============================================================");
    puts("SUMMARY");
    puts("==============================================================");
    printf("  downloaded    %d\n", totals->count[SCRAPE_OK]);
    printf("  skipped       %d\n", totals->count[SCRAPE_SKIPPED]);
    printf("  no cover      %d\n", totals->count[SCRAPE_NO_COVER]);
    printf("  not found     %d\n", totals->count[SCRAPE_NOT_FOUND]);
    printf("  network error %d\n", totals->count[SCRAPE_NET_ERROR]);
    printf("  disk error    %d\n", totals->count[SCRAPE_IO_ERROR]);

    if (totals->count[SCRAPE_AUTH_ERROR] > 0) {
        puts("  credentials rejected — check config.h");
    }
    if (totals->count[SCRAPE_QUOTA] > 0) {
        puts("  daily quota reached — try again tomorrow");
    }

    puts("==============================================================");

    switch (run) {
    case RUN_COMPLETED:
        puts("Done.");
        break;
    case RUN_ABORTED:
        printf("Stopped during %s (%d of %d). The counts above still hold.\n",
               totals->last_system, totals->last_index, totals->last_total);
        break;
    case RUN_ROMS_DIR_NOT_FOUND:
        puts("ROM folder not found.");
        break;
    }

    puts("\nMetadata & artwork by ScreenScraper.fr\n");
}

/* ------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    SystemList      systems;
    ScrapeOptions   options;
    ScrapeCallbacks callbacks;
    ScrapeTotals    totals;
    TerminalState   state;
    RunStatus       run;
    const char     *roms_dir = NULL;
    const char     *tags[32];
    int             tag_count = 0;
    int             list_only = 0;
    int             i;

    state.processed      = 0;
    state.stop_after     = 0;
    state.last_system[0] = '\0';

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0) {
            list_only = 1;
        } else if (strcmp(argv[i], "--stop-after") == 0 && i + 1 < argc) {
            state.stop_after = atoi(argv[++i]);
        } else if (roms_dir == NULL) {
            roms_dir = argv[i];
        } else if (tag_count < 32) {
            tags[tag_count++] = argv[i];
        }
    }

    if (roms_dir == NULL) {
        puts("\nusage: test_scrape [--list] [--stop-after N] <roms dir> [TAG ...]\n");
        return 1;
    }

    if (SS_DEV_ID[0] == '\0') {
        puts("\nconfig.h has no developer credentials yet."
             "\ncopy config.h.example to config.h and fill them in.\n");
        return 1;
    }

    /* User login comes from credentials.txt now, the same source the real app
     * reads — so this test exercises the actual parser and setter rather than a
     * parallel path of its own. */
    {
        UserCredentials cred;
        credentials_load(LEV_CREDENTIALS_FILE, &cred);
        if (!cred.complete) {
            printf("\n%s has no usable login yet."
                   "\nedit it with your ScreenScraper username and password.\n\n",
                   LEV_CREDENTIALS_FILE);
            return 1;
        }
        ss_set_user_credentials(cred.ssid, cred.sspassword);
    }

    if (systems_load(LEV_SYSTEMS_FILE, &systems) < 0) {
        printf("\ncould not load %s\n\n", LEV_SYSTEMS_FILE);
        return 1;
    }

    if (list_only) {
        list_systems(&systems, roms_dir);
        systems_free(&systems);
        return 0;
    }

    if (ss_http_init() != 0) {
        puts("\ncould not start libcurl\n");
        systems_free(&systems);
        return 1;
    }

    options.roms_dir     = roms_dir;
    options.output_dir   = TEST_OUTPUT_DIR;
    options.filter_tags  = (tag_count > 0) ? tags : NULL;
    options.filter_count = tag_count;

    callbacks.on_progress  = on_progress;
    callbacks.on_result    = on_result;
    callbacks.should_abort = should_abort;
    callbacks.user         = &state;

    puts("\nLEVIATHAN SCRAPER");
    printf("roms     %s\n", roms_dir);
    printf("output   %s/<TAG>/\n", TEST_OUTPUT_DIR);
    if (tag_count > 0) {
        printf("only     ");
        for (i = 0; i < tag_count; i++) {
            printf("%s ", tags[i]);
        }
        putchar('\n');
    }
    if (state.stop_after > 0) {
        printf("stopping after %d ROMs, to check the tally survives\n", state.stop_after);
    }

    run = scrape_collection(&options, &systems, &callbacks, &totals);

    print_summary(&totals, run);

    ss_http_cleanup();
    systems_free(&systems);
    return 0;
}
