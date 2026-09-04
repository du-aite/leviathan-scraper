/*
 * scrape.c — walking a collection, one ROM at a time.
 *
 * Metadata & artwork by ScreenScraper.fr — https://www.screenscraper.fr
 */

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "romhash.h"
#include "romscan.h"
#include "scrape.h"
#include "sshttp.h"
#include "ssparse.h"
#include "strlist.h"

/* ------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------ */

const char *scrape_status_label(ScrapeStatus status)
{
    switch (status) {
    case SCRAPE_OK:         return "downloaded";
    case SCRAPE_SKIPPED:    return "already had a cover";
    case SCRAPE_NO_COVER:   return "no cover available";
    case SCRAPE_NOT_FOUND:  return "not in the database";
    case SCRAPE_NET_ERROR:  return "network error";
    case SCRAPE_IO_ERROR:   return "disk error";
    case SCRAPE_AUTH_ERROR: return "credentials rejected";
    case SCRAPE_QUOTA:      return "daily quota reached";
    default:                return "unknown";
    }
}

/* File name without its directory or its extension.
 * "/roms/FC/Contra (USA).nes" -> "Contra (USA)" */
static void rom_base_name(const char *rom_path, char *out, size_t out_size)
{
    const char *slash = strrchr(rom_path, '/');
    const char *name  = (slash != NULL) ? slash + 1 : rom_path;
    char       *dot;

    strncpy(out, name, out_size - 1);
    out[out_size - 1] = '\0';

    dot = strrchr(out, '.');
    if (dot != NULL && dot != out) {
        *dot = '\0';
    }
}

/* Where a cover belongs: <output>/<TAG>/<rom name>.png — the exact layout
 * the Trimui stock OS looks for. */
static void cover_path(const char *output_dir, const char *tag, const char *rom_path,
                       char *out, size_t out_size)
{
    char base[512];

    rom_base_name(rom_path, base, sizeof(base));
    snprintf(out, out_size, "%s/%s/%s.png", output_dir, tag, base);
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

static void set_detail(char *detail, size_t size, const char *text)
{
    if (detail != NULL && size > 0) {
        strncpy(detail, text, size - 1);
        detail[size - 1] = '\0';
    }
}

/* ------------------------------------------------------------------
 * One ROM
 * ------------------------------------------------------------------ */

ScrapeStatus scrape_one_game(const char *rom_path, int system_id, const char *tag,
                             const char *output_dir, char *detail, size_t detail_size)
{
    RomHashes      hashes;
    SsGame         game;
    const SsMedia *cover;
    ScrapeStatus   status;
    char           destination[4096];
    char           err[256] = "";
    long           written  = 0;

    set_detail(detail, detail_size, "");

    if (rom_path == NULL || tag == NULL || output_dir == NULL) {
        return SCRAPE_IO_ERROR;
    }

    cover_path(output_dir, tag, rom_path, destination, sizeof(destination));

    /* Cheapest possible outcome first: if the cover is already on disk we
     * spend neither a hash nor an API call. */
#if LEV_SKIP_EXISTING
    if (file_exists(destination)) {
        set_detail(detail, detail_size, "cover already on disk");
        return SCRAPE_SKIPPED;
    }
#endif

    if (rom_hash_file(rom_path, &hashes) != 0) {
        set_detail(detail, detail_size, "could not read the ROM");
        return SCRAPE_IO_ERROR;
    }

    status = ss_fetch_game(&hashes, system_id, &game, err, sizeof(err));
    if (status != SCRAPE_OK) {
        set_detail(detail, detail_size, err[0] ? err : scrape_status_label(status));
        return status;
    }

    cover = ss_choose_cover(&game, LEV_MEDIA_TYPE, LEV_PREFERRED_REGION);
    if (cover == NULL) {
        char line[LEV_DETAIL_MAX + 128];
        snprintf(line, sizeof(line), "%s (no %s)", game.name, LEV_MEDIA_TYPE);
        set_detail(detail, detail_size, line);
        ss_game_free(&game);
        return SCRAPE_NO_COVER;
    }

    status = ss_download_cover(cover, destination, &written, err, sizeof(err));
    if (status != SCRAPE_OK) {
        set_detail(detail, detail_size, err[0] ? err : scrape_status_label(status));
        ss_game_free(&game);
        return status;
    }

    {
        char line[LEV_DETAIL_MAX + 128];
        snprintf(line, sizeof(line), "%s -> %.0f KB [%s]",
                 game.name, (double)written / 1024.0, cover->region);
        set_detail(detail, detail_size, line);
    }

    ss_game_free(&game);
    return SCRAPE_OK;
}

/* ------------------------------------------------------------------
 * The whole collection
 * ------------------------------------------------------------------ */

/* Is this tag one the caller asked for? An empty filter means "all". */
static int tag_wanted(const ScrapeOptions *options, const char *tag)
{
    int i;

    if (options->filter_tags == NULL || options->filter_count <= 0) {
        return 1;
    }
    for (i = 0; i < options->filter_count; i++) {
        const char *wanted = options->filter_tags[i];
        size_t      k      = 0;
        int         same   = 1;

        if (wanted == NULL) {
            continue;
        }
        /* Compare uppercase, since tags are normalised but the command line
         * may not be. */
        while (wanted[k] != '\0' && tag[k] != '\0') {
            char a = wanted[k];
            char b = tag[k];
            if (a >= 'a' && a <= 'z') { a = (char)(a - 'a' + 'A'); }
            if (b >= 'a' && b <= 'z') { b = (char)(b - 'a' + 'A'); }
            if (a != b) {
                same = 0;
                break;
            }
            k++;
        }
        if (same && wanted[k] == '\0' && tag[k] == '\0') {
            return 1;
        }
    }
    return 0;
}

static void remember_position(ScrapeTotals *totals, const char *system_name,
                              int index, int total)
{
    strncpy(totals->last_system, system_name, sizeof(totals->last_system) - 1);
    totals->last_system[sizeof(totals->last_system) - 1] = '\0';
    totals->last_index = index;
    totals->last_total = total;
}

RunStatus scrape_collection(const ScrapeOptions *options, const SystemList *systems,
                            const ScrapeCallbacks *callbacks, ScrapeTotals *out_totals)
{
    ScrapeTotals  local;
    ScrapeTotals *totals = (out_totals != NULL) ? out_totals : &local;
    StrList       folders;
    int           f;

    /* Unpacked once so the loop below stays readable. */
    ProgressFn    on_progress  = (callbacks != NULL) ? callbacks->on_progress  : NULL;
    ResultFn      on_result    = (callbacks != NULL) ? callbacks->on_result    : NULL;
    ShouldAbortFn should_abort = (callbacks != NULL) ? callbacks->should_abort : NULL;
    void         *user         = (callbacks != NULL) ? callbacks->user         : NULL;

    totals_reset(totals);

    if (options == NULL || options->roms_dir == NULL) {
        return RUN_ROMS_DIR_NOT_FOUND;
    }

    if (list_subdirs(options->roms_dir, &folders) != 0) {
        return RUN_ROMS_DIR_NOT_FOUND;
    }

    for (f = 0; f < folders.count; f++) {
        char     tag[LEV_TAG_MAX];
        char     system_name[LEV_SYSTEM_NAME_LEN];
        char     folder_path[4096];
        int      system_id = 0;
        StrList  extensions;
        StrList  roms;
        int      r;

        extract_tag(folders.items[f], tag, sizeof(tag));

        if (!tag_wanted(options, tag)) {
            continue;
        }
        if (!systems_resolve_tag(systems, tag, &system_id, system_name, sizeof(system_name))) {
            continue; /* unknown tag: skipped by design, not an error */
        }

        snprintf(folder_path, sizeof(folder_path), "%s/%s",
                 options->roms_dir, folders.items[f]);

        systems_extensions(systems, system_id, &extensions);
        if (list_roms(folder_path, &extensions, &roms) != 0) {
            strlist_free(&extensions);
            continue;
        }
        strlist_free(&extensions);

        for (r = 0; r < roms.count; r++) {
            char         rom_path[8192];
            char         detail[LEV_DETAIL_MAX];
            ScrapeStatus status;

            /* Asked between ROMs, never in the middle of one, so a stop
             * never leaves a half-written file behind. */
            if (should_abort != NULL && should_abort(user)) {
                remember_position(totals, system_name, r, roms.count);
                strlist_free(&roms);
                strlist_free(&folders);
                return RUN_ABORTED;
            }

            snprintf(rom_path, sizeof(rom_path), "%s/%s", folder_path, roms.items[r]);
            remember_position(totals, system_name, r + 1, roms.count);

            if (on_progress != NULL) {
                on_progress(system_name, r + 1, roms.count, roms.items[r], user);
            }

            status = scrape_one_game(rom_path, system_id, tag,
                                     options->output_dir, detail, sizeof(detail));

            if (status >= 0 && status < SCRAPE_STATUS_COUNT) {
                totals->count[status]++;
            }

            if (on_result != NULL) {
                on_result(status, roms.items[r], detail, user);
            }

            /* Credentials being wrong or quota being spent will not fix
             * itself on the next ROM. Stop rather than burn the collection
             * generating the same failure hundreds of times. */
            if (status == SCRAPE_AUTH_ERROR || status == SCRAPE_QUOTA) {
                strlist_free(&roms);
                strlist_free(&folders);
                return RUN_ABORTED;
            }

            /* Only pace ourselves when a request actually went out. */
            if (status != SCRAPE_SKIPPED) {
                ss_pause_between_requests();
            }
        }

        strlist_free(&roms);
    }

    strlist_free(&folders);
    return RUN_COMPLETED;
}
