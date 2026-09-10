/*
 * scan_ui.h — the SCREEN's view of the ROM collection.
 * Metadata & artwork by ScreenScraper.fr — https://www.screenscraper.fr
 * ============================================================================
 *
 * The core (scrape.c) walks the folders DURING a scrape and throws the walk
 * away as it goes. The screen needs something different: it scans ONCE, keeps
 * the result, and lets the user browse and tick boxes for as long as they
 * like before deciding to scrape. So the list has to SURVIVE between the scan
 * and the scrape — that is what this structure is for.
 *
 * This is a UI-side type. The one field the core neither has nor wants is
 * `selected`: it is pure screen state. When the user hits Scrape, the screen
 * collects the tags of the selected rows and hands them to the core as the
 * scrape filter. That single hand-off is the only place the two worlds meet.
 */

#ifndef LEVIATHAN_SCAN_UI_H
#define LEVIATHAN_SCAN_UI_H

#include <stdbool.h>

#include "romscan.h"   /* LEV_TAG_MAX          */
#include "systems.h"   /* LEV_SYSTEM_NAME_LEN, SystemList */

/* One row on the "Choose systems" screen.
 *
 * As of the filtered scan, every row that reaches this screen is recognised
 * AND holds at least one ROM — empty and unrecognised folders are dropped by
 * scan_collection(). The `recognised` field therefore always reads true here;
 * it stays in the struct as honest documentation and as defence should the
 * filter ever loosen (e.g. a future "show all" toggle). */
typedef struct {
    char folder[256];                 /* the raw subfolder name on the card  */
    char tag[LEV_TAG_MAX];            /* "GBA", extracted from the folder    */
    char name[LEV_SYSTEM_NAME_LEN];   /* "Game Boy Advance", for display     */
    int  system_id;                   /* ScreenScraper id                    */
    int  rom_count;                   /* how many ROMs the folder holds (>0) */
    bool recognised;                  /* always true past the current filter */
    bool selected;                    /* [X] or [ ] — UI only, core ignores  */
} DetectedSystem;

/* The whole detected collection: a fixed array is plenty — nobody has 128
 * distinct systems on one card, and a fixed array means no allocation to get
 * wrong. count says how many slots are actually filled. */
#define SCAN_MAX_SYSTEMS 128

typedef struct {
    DetectedSystem items[SCAN_MAX_SYSTEMS];
    int            count;
} DetectedList;

/*
 * Scan roms_dir and fill `out` with one row per SCRAPABLE subfolder.
 *
 * A folder makes the list only if its tag resolves to a ScreenScraper system
 * AND it contains at least one ROM. Empty folders (a stock Brick has one per
 * supported system) and unrecognised tags are dropped, so the chooser shows
 * only valid choices. The full picture — including what was skipped — belongs
 * to the "Scan everything" path and the final summary, not to this screen.
 *
 * Every row starts UNSELECTED: "Choose systems" means the user picks what to
 * scrape; "Scan everything" on the menu is the path for scraping all.
 *
 * `systems` must already be loaded (systems_load). Returns 0 on success, -1
 * if roms_dir cannot be opened. Rows past SCAN_MAX_SYSTEMS are ignored.
 */
int scan_collection(const char *roms_dir, const SystemList *systems,
                    DetectedList *out);

/* How many rows are currently ticked. The Scrape row shows this count and is
 * disabled when it is zero. */
int scan_selected_count(const DetectedList *list);

#endif /* LEVIATHAN_SCAN_UI_H */
