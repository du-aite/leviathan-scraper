/*
 * scrape_ui.h — driving the core from the screen (the Option A glue).
 * Metadata & artwork by ScreenScraper.fr — https://www.screenscraper.fr
 * ============================================================================
 *
 * scrape_collection() is BLOCKING: it walks the whole collection and only
 * returns at the end. Called naively from an SDL app it would freeze the
 * screen for minutes. Option A (chosen at the start of Phase 2) fixes this
 * WITHOUT threads: the on_progress callback pumps SDL events and redraws once
 * per ROM. Because the core already pauses ~1.2s between requests, the screen
 * refreshing once per ROM is smooth enough, and the whole thing stays
 * single-threaded — no mutex, no race conditions.
 *
 * This one function is the only place the screen and the core meet at scrape
 * time. It:
 *   1. turns the ticked systems into the core's tag filter,
 *   2. installs the three callbacks (progress, result, abort),
 *   3. runs scrape_collection(), and
 *   4. leaves the tally and run status in the App for the summary screen.
 *
 * It takes a void* rather than App* so main.c's App type need not leak into
 * this header; scrape_ui.c casts it back. The callbacks live in scrape_ui.c
 * and reach the renderer through that same pointer.
 */

#ifndef LEVIATHAN_SCRAPE_UI_H
#define LEVIATHAN_SCRAPE_UI_H

#include "systems.h"

/*
 * Run a full scrape for whatever the user ticked, driving the progress screen
 * as it goes. Blocks until the scrape finishes or the user aborts — but keeps
 * the screen alive throughout via Option A. On return, the App's totals and
 * run_result are filled in and the caller should switch to the summary screen.
 *
 * app_ptr is the App*; systems is the loaded cache; roms_dir/output_dir are
 * the paths the core needs.
 */
void run_scrape_for_selection(void *app_ptr, const SystemList *systems,
                              const char *roms_dir, const char *output_dir);

#endif /* LEVIATHAN_SCRAPE_UI_H */
