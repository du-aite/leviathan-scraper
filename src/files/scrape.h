/*
 * scrape.h — the conductor: walk a collection and fetch every cover.
 *
 * This is the piece the user interface talks to. It knows nothing about
 * terminals or screens: progress goes out through a callback, stopping
 * comes in through another, and the tally comes back through a pointer.
 * Phase 2 will hand it SDL callbacks and change nothing here.
 */

#ifndef LEVIATHAN_SCRAPE_H
#define LEVIATHAN_SCRAPE_H

#include <stddef.h>

#include "status.h"
#include "systems.h"

#define LEV_DETAIL_MAX 256

typedef struct {
    const char  *roms_dir;    /* folder holding one subfolder per system  */
    const char  *output_dir;  /* covers land in <output_dir>/<TAG>/       */
    const char **filter_tags; /* NULL or empty means every system         */
    int          filter_count;
} ScrapeOptions;

/*
 * Handle a single ROM: skip it if the cover is already there, otherwise
 * hash it, look it up, pick a cover and write it out.
 *
 * detail receives a short human-readable line (the game's title, or why
 * nothing was downloaded) and may be NULL.
 */
ScrapeStatus scrape_one_game(const char *rom_path, int system_id, const char *tag,
                             const char *output_dir, char *detail, size_t detail_size);

/*
 * Walk the whole collection.
 *
 * callbacks may be NULL, and so may any field inside it — with no abort
 * callback the run simply goes to the end, which is what the terminal
 * tests want.
 *
 * out_totals is filled in whether the run completed or was aborted. That is
 * the point: stopping halfway must not throw away the count of what was
 * already downloaded.
 */
RunStatus scrape_collection(const ScrapeOptions *options, const SystemList *systems,
                            const ScrapeCallbacks *callbacks, ScrapeTotals *out_totals);

/* Short label for a status, in English, for logs and the terminal. */
const char *scrape_status_label(ScrapeStatus status);

#endif /* LEVIATHAN_SCRAPE_H */
