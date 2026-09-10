/*
 * scrape_ui.c — the Option A glue: run the blocking core without freezing.
 *
 * Metadata & artwork by ScreenScraper.fr — https://www.screenscraper.fr
 * ============================================================================
 *
 * The heart of Phase 2. scrape_collection() blocks until the whole run ends;
 * the trick that keeps the screen alive is that its on_progress callback, once
 * per ROM, does a mini game-loop turn: drain SDL events (to catch an abort)
 * and redraw the progress screen. Between ROMs the core pauses ~1.2s for rate
 * limiting, so "once per ROM" is a perfectly smooth refresh rate here.
 *
 * Single-threaded throughout: the callbacks run on the same (only) thread, so
 * they touch App fields with no locking. That is the whole reason we picked
 * Option A over a worker thread.
 */

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "scrape.h"
#include "scrape_ui.h"
#include "status.h"

/* Colours used only by the progress screen. Kept local; the main palette is
 * defined in main.c and not shared. */
static const SDL_Color PROG_BLUE = {  90, 170, 255, 255 };
static const SDL_Color PROG_GRAY = { 120, 120, 120, 255 };
static const SDL_Color PROG_WHITE= { 230, 230, 230, 255 };

/* ------------------------------------------------------------------
 * Drawing the progress screen from inside the callback.
 *
 * We can't call main.c's progress_draw (it's static and stubbed), so the live
 * progress screen is drawn here, where the live fields live. This is the one
 * screen whose drawing belongs next to the scrape, because only the scrape
 * knows what to put on it.
 * ------------------------------------------------------------------ */
/* A short, human line for each per-ROM outcome. The core returns codes; the
 * screen decides the words (and the language) \u2014 which is exactly why status.h
 * insists the core never carry display strings. */
static const char *result_line(ScrapeStatus status)
{
    switch (status) {
    case SCRAPE_OK:         return "Found!";
    case SCRAPE_SKIPPED:    return "Already had it";
    case SCRAPE_NO_COVER:   return "No cover available";
    case SCRAPE_NOT_FOUND:  return "Not in the database";
    case SCRAPE_NET_ERROR:  return "Network error";
    case SCRAPE_IO_ERROR:   return "Disk error";
    case SCRAPE_AUTH_ERROR: return "Credentials rejected";
    case SCRAPE_QUOTA:      return "Daily quota reached";
    default:                return "";
    }
}

static void draw_progress(App *app)
{
    char line[256];

    draw_background(app);

    draw_text_centered(app->ren, app->font_title, "SCRAPING", 90, PROG_BLUE);

    /* Which system, and how far into it. */
    if (app->cur_system[0] != '\0') {
        snprintf(line, sizeof(line), "%s   [%d / %d]",
                 app->cur_system, app->cur_index, app->cur_total);
        draw_text_centered(app->ren, app->font_body, line, 220, PROG_WHITE);
    }

    /* The ROM currently being handled. */
    if (app->cur_rom[0] != '\0') {
        char shown[64];
        strncpy(shown, app->cur_rom, sizeof(shown) - 1);
        shown[sizeof(shown) - 1] = '\0';
        draw_text_centered(app->ren, app->font_body, shown, 300, PROG_WHITE);
    }

    /* The live status line. Between asking and answering we show "Searching...";
     * once the core replies we show the outcome. This split is what gives the
     * screen its sense of motion — and it falls out of Option A for free,
     * because on_progress (before) and on_result (after) each repaint. */
    if (app->searching) {
        draw_text_centered(app->ren, app->font_body, "Searching...", 380, PROG_GRAY);
    } else if (app->last_status == SCRAPE_OK) {
        /* Success: just "Found!". The ROM's own filename already shows on the
         * line above, so the game name and the byte count would be noise. */
        draw_text_centered(app->ren, app->font_body, "Found!", 380, PROG_BLUE);
    } else {
        const char *msg = result_line(app->last_status);
        if (msg[0] != '\0') {
            draw_text_centered(app->ren, app->font_body, msg, 380, PROG_GRAY);
        }
    }

    /* A rough running count. */
    snprintf(line, sizeof(line), "done: %d", app->done_count);
    draw_text_centered(app->ren, app->font_body, line, 470, PROG_GRAY);

    /* Abort hint, or an acknowledgement once abort was asked for. */
    if (app->abort_requested) {
        draw_text_centered(app->ren, app->font_body,
                           "stopping after this game...", HEIGHT - 110, PROG_WHITE);
    } else {
        draw_text_centered(app->ren, app->font_body,
                           "back: abort (progress is kept)", HEIGHT - 110, PROG_GRAY);
    }

    draw_text_centered(app->ren, app->font_body,
                       "Metadata & artwork by ScreenScraper.fr",
                       HEIGHT - 50, PROG_GRAY);

    SDL_RenderPresent(app->ren);
}

/* Pump pending SDL events so the app stays responsive and an abort can be
 * caught. This is the "borrow the loop" half of Option A. Only Back/Escape
 * matters here — everything else is ignored during a scrape. */
static void pump_events(App *app)
{
    SDL_Event ev;

    while (SDL_PollEvent(&ev)) {
        /* Closing the window always stops cleanly, regardless of intent. */
        if (ev.type == SDL_QUIT) {
            app->abort_requested = true;
            continue;
        }

        /* Everything else goes through the SAME translator the rest of the app
         * uses. During a scrape only BACK matters — it asks for an abort. We do
         * not name a key or a button here: whatever the one mapping in
         * translate_event calls BACK (Esc on the Mac, the back button on the
         * Brick) aborts, and the A/B swap, once set there, is inherited for
         * free. This is the whole point of routing input through one place. */
        if (translate_event(&ev) == ACT_BACK) {
            app->abort_requested = true;
        }
    }
}

/* ------------------------------------------------------------------
 * The three callbacks.
 * user is the App*, passed straight through by the core.
 * ------------------------------------------------------------------ */

/* Before each ROM: record where we are, then lend the loop to the screen. */
static void cb_progress(const char *system_name, int index, int total,
                        const char *rom_name, void *user)
{
    App *app = (App *)user;

    strncpy(app->cur_system, system_name, sizeof(app->cur_system) - 1);
    app->cur_system[sizeof(app->cur_system) - 1] = '\0';
    app->cur_index = index;
    app->cur_total = total;
    strncpy(app->cur_rom, rom_name, sizeof(app->cur_rom) - 1);
    app->cur_rom[sizeof(app->cur_rom) - 1] = '\0';

    /* We are about to search this ROM: the status line shows "Searching...". */
    app->searching = true;

    /* Option A in one place: handle input and repaint, once per ROM. */
    pump_events(app);
    draw_progress(app);
}

/* After each ROM: remember the outcome so the next repaint can show it. */
static void cb_result(ScrapeStatus status, const char *rom_name,
                      const char *detail, void *user)
{
    App *app = (App *)user;

    (void)rom_name;
    app->last_status = status;
    app->searching   = false;  /* the core replied: show the outcome now */
    app->done_count++;

    (void)detail; /* the status alone drives the line now; detail is unused */

    /* Repaint here too, so the just-finished result shows immediately instead
     * of only appearing when the next ROM starts. */
    draw_progress(app);
}

/* Between ROMs: has the user asked to stop? */
static int cb_should_abort(void *user)
{
    App *app = (App *)user;
    return app->abort_requested ? 1 : 0;
}

/* ------------------------------------------------------------------
 * The public entry point.
 * ------------------------------------------------------------------ */

void run_scrape_for_selection(void *app_ptr, const SystemList *systems,
                              const char *roms_dir, const char *output_dir)
{
    App            *app = (App *)app_ptr;
    ScrapeOptions   options;
    ScrapeCallbacks callbacks;
    const char     *tags[SCAN_MAX_SYSTEMS];
    int             tag_count = 0;
    int             i;

    /* 1. Build the tag filter from the ticked systems. This is the single
     *    point where the UI's selection becomes the core's input. */
    for (i = 0; i < app->detected.count; i++) {
        if (app->detected.items[i].selected) {
            tags[tag_count++] = app->detected.items[i].tag;
        }
    }

    /* Reset the live fields for a fresh run. */
    app->cur_system[0]   = '\0';
    app->cur_rom[0]      = '\0';
    app->cur_index       = 0;
    app->cur_total       = 0;
    app->done_count      = 0;
    app->abort_requested = false;
    app->searching       = false;

    /* Paint an initial frame so the screen isn't blank before the first ROM. */
    draw_progress(app);

    /* 2. Options and callbacks. */
    options.roms_dir     = roms_dir;
    options.output_dir   = output_dir;
    options.filter_tags  = (tag_count > 0) ? tags : NULL;
    options.filter_count = tag_count;

    callbacks.on_progress  = cb_progress;
    callbacks.on_result    = cb_result;
    callbacks.should_abort = cb_should_abort;
    callbacks.user         = app;

    /* 3. Run it. Blocks, but Option A keeps the screen breathing throughout. */
    app->run_result = scrape_collection(&options, systems, &callbacks, &app->totals);

    /* 4. Done — the caller switches to the summary, which reads app->totals. */
}
