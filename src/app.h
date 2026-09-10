/*
 * app.h — the shared shape of the running app.
 * ============================================================================
 *
 * The App struct and the enums live here, not in main.c, for one reason: the
 * scrape glue (scrape_ui.c) needs to see them too. Its callbacks read the
 * renderer and fonts and write the live-progress fields, and they redraw the
 * progress screen. Putting the definition here lets main.c and scrape_ui.c
 * both include it, with neither depending on the other — that is what breaks
 * the otherwise circular include.
 *
 * The screen drawing helpers the callbacks use are declared here as well; they
 * are defined in main.c.
 */

#ifndef LEVIATHAN_APP_H
#define LEVIATHAN_APP_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <stdbool.h>

#include "scan_ui.h"
#include "scrape.h"   /* LEV_DETAIL_MAX */
#include "status.h"
#include "systems.h"

#define MENU_ITEM_COUNT 2

typedef enum {
    SCREEN_SPLASH,
    SCREEN_MENU,
    SCREEN_SYSTEMS,
    SCREEN_PROGRESS,
    SCREEN_SUMMARY,
    SCREEN_QUIT
} Screen;

typedef enum {
    ACT_NONE, ACT_UP, ACT_DOWN, ACT_CONFIRM, ACT_BACK, ACT_START
} Action;

typedef struct {
    Screen screen;

    SDL_Renderer *ren;
    TTF_Font     *font_title;
    TTF_Font     *font_body;

    /* Whether a usable ScreenScraper login was found at startup. Set once in
     * main() from the runtime credentials file, then read by the menu: when
     * false, the menu shows an instruction to edit credentials.txt instead of
     * its normal entries, and refuses to enter the scrape flow. One field, set
     * in one place, so the "do we have credentials" question has a single
     * answer the whole app agrees on. */
    bool have_credentials;

    /* Background art, loaded once at startup and reused every frame. Sits
     * behind every screen. May be NULL if the file failed to load — the draw
     * code falls back to the plain dark fill, so a missing image degrades
     * gracefully instead of crashing. */
    SDL_Texture  *bg_tex;

    int menu_index;

    /* SYSTEMS screen ------------------------------------------------------ */
    SystemList   *systems;      /* the loaded sistemas.json cache            */
    const char   *roms_dir;     /* where the ROM subfolders live             */
    const char   *output_dir;   /* where covers are written: <dir>/<TAG>/    */
    DetectedList  detected;     /* the result of the scan (already filtered) */
    int           sys_cursor;   /* highlighted row; == detected.count means
                                 * the cursor is on the SCRAPE line          */
    int           scroll_top;   /* index of the first system row drawn       */
    bool          popup_active; /* confirm overlay up, over the systems list */
    bool          popup_scan_all; /* popup was raised by 'Scan everything'   */

    /* Live scrape state (written by the callbacks, read by progress_draw) */
    char          cur_system[LEV_SYSTEM_NAME_MAX]; /* system being scraped   */
    int           cur_index;    /* 1-based ROM position within the system    */
    int           cur_total;    /* how many ROMs that system has             */
    char          cur_rom[128]; /* the ROM file currently being handled      */
    ScrapeStatus  last_status;  /* status of the last finished ROM           */
    bool          searching;    /* true between progress and result: "..."    */
    bool          abort_requested; /* set when the user asks to stop         */
    int           done_count;   /* ROMs finished so far, for a rough tally   */

    /* Result of the finished run, handed to the summary screen. */
    ScrapeTotals  totals;
    RunStatus     run_result;

    /* Phase 2d will add: live progress fields, abort flag, ScrapeTotals,
     * RunStatus, popup flag. Left out until the scrape is wired. */
} App;

/* Drawing helpers, defined in main.c, used by the scrape callbacks too. */
void draw_text(SDL_Renderer *ren, TTF_Font *font,
               const char *text, int x, int y, SDL_Color color);
void draw_text_centered(SDL_Renderer *ren, TTF_Font *font,
                        const char *text, int y, SDL_Color color);

/* Paint the dim background behind a screen (or the plain dark fill if the image
 * didn't load). Defined in main.c; used by the progress screen in scrape_ui.c
 * too, so every screen shares one background. Pass the App so it can reach the
 * renderer and the loaded texture. */
void draw_background(App *app);

/* The single translator of hardware to intent. Defined in main.c. Shared here
 * so the scrape glue (scrape_ui.c) reads input through the SAME mapping the
 * rest of the app does, instead of keeping its own copy of the button codes.
 * This is what makes the A/B swap a one-place change: only this function knows
 * which physical button means which Action. */
Action translate_event(const SDL_Event *ev);

/* Screen dimensions, needed by both files for centring and layout. */
#define WIDTH  1024
#define HEIGHT 768

#endif /* LEVIATHAN_APP_H */
