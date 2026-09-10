/*
 * scan_ui.c — building the screen's list of detected systems.
 *
 * The scanning logic here is a faithful port of what test_scrape.c's
 * list_systems() already does and proved in Phase 1: walk the subfolders,
 * read each tag, resolve it, and count the ROMs. The difference is the
 * destination — instead of printf'ing each line, we fill a DetectedSystem —
 * and one deliberate FILTER: only systems that are recognised AND actually
 * hold ROMs make it onto the list.
 *
 * Why filter: a stock Brick ships with an empty ROM folder for every system
 * it supports, so an unfiltered "Choose systems" screen would bury the two or
 * three systems that matter under thirty empty ones. The screen where you
 * CHOOSE should only show valid choices. Everything the scan saw but hid
 * (empty folders, unrecognised tags) still surfaces on the "Scan everything"
 * path and in the final summary — the report keeps the full picture; the
 * chooser stays clean.
 *
 * Metadata & artwork by ScreenScraper.fr — https://www.screenscraper.fr
 */

#include <stdio.h>
#include <string.h>

#include "romscan.h"
#include "scan_ui.h"
#include "strlist.h"
#include "systems.h"

int scan_collection(const char *roms_dir, const SystemList *systems,
                    DetectedList *out)
{
    StrList folders;
    int     i;

    out->count = 0;

    /* Same first step as list_systems(): the sorted subfolders of roms_dir.
     * If the folder can't be opened, leave the list empty and report it. */
    if (list_subdirs(roms_dir, &folders) != 0) {
        return -1;
    }

    for (i = 0; i < folders.count && out->count < SCAN_MAX_SYSTEMS; i++) {
        char tag[LEV_TAG_MAX];
        char name[LEV_SYSTEM_NAME_LEN];
        char path[4096];
        int  id = 0;
        int  rom_count = 0;

        /* Read the tag out of the folder name: "Game Boy Advance (GBA)" -> "GBA". */
        extract_tag(folders.items[i], tag, sizeof(tag));

        /* An unrecognised tag can never be scraped, so it never belongs on the
         * chooser. Skip it here; the "Scan everything" path is where it still
         * gets a look. */
        if (!systems_resolve_tag(systems, tag, &id, name, sizeof(name))) {
            continue;
        }

        /* Count the ROMs, exactly as list_systems() does: ask the system for
         * its extensions, then list the matching files. We must count BEFORE
         * deciding whether to keep the row, because an empty system is hidden. */
        {
            StrList exts, roms;

            snprintf(path, sizeof(path), "%s/%s", roms_dir, folders.items[i]);
            systems_extensions(systems, id, &exts);

            if (list_roms(path, &exts, &roms) == 0) {
                rom_count = roms.count;
                strlist_free(&roms);
            }
            strlist_free(&exts);
        }

        /* The filter: a recognised system with zero ROMs is an empty stock
         * folder. Hide it from the chooser. */
        if (rom_count == 0) {
            continue;
        }

        /* Survived the filter — record the row. Every row that lands here is,
         * by construction, recognised and non-empty, hence always tickable. */
        {
            DetectedSystem *row = &out->items[out->count];

            strncpy(row->folder, folders.items[i], sizeof(row->folder) - 1);
            row->folder[sizeof(row->folder) - 1] = '\0';
            strncpy(row->tag, tag, sizeof(row->tag) - 1);
            row->tag[sizeof(row->tag) - 1] = '\0';
            strncpy(row->name, name, sizeof(row->name) - 1);
            row->name[sizeof(row->name) - 1] = '\0';

            row->system_id  = id;
            row->rom_count  = rom_count;
            row->recognised = true;   /* always true past the filter */
            row->selected   = false;  /* every row starts unticked   */

            out->count++;
        }
    }

    strlist_free(&folders);
    return 0;
}

int scan_selected_count(const DetectedList *list)
{
    int i, n = 0;

    for (i = 0; i < list->count; i++) {
        if (list->items[i].selected) {
            n++;
        }
    }
    return n;
}
