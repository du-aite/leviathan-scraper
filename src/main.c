/*
 * main.c — Leviathan Scraper, the screen (Phase 2).
 * Metadata & artwork by ScreenScraper.fr — https://www.screenscraper.fr
 * ============================================================================
 *
 * STATE OF THIS FILE (Phase 2, third piece):
 *   The SYSTEMS screen is now polished: it shows only recognised, non-empty
 *   systems (a stock Brick has many empty ROM folders), scrolls when the list
 *   is longer than the screen, and its SCRAPE line explains itself when no
 *   system is ticked instead of just sitting there greyed out. Splash, Menu
 *   and the flow past SCRAPE are still skeleton — the actual scraping is next.
 *   Nothing in the core (scrape.c and friends) was touched.
 *
 * THE STATE MACHINE (unchanged):
 *   SPLASH --confirm--> MENU
 *                         |- "Scan everything"  -> (later) PROGRESS, all systems
 *                         `- "Choose systems"   -> SYSTEMS -> (later) PROGRESS
 *   PROGRESS -> SUMMARY --confirm/back--> MENU
 *
 * INPUT AS INTENTION:
 *   All input passes through translate_event() and becomes an Action. Only
 *   that one function knows physical buttons exist, so the known Brick A/B
 *   swap stays a one-line fix.
 *
 * BUILD & RUN ON THE MAC (one line, no backslashes to trip zsh):
 *   From the PROJECT ROOT (so assets/ and the sistemas.json are found):
 *
 *   cc -std=c99 -Wall -Wextra -o leviathan_ui src/main.c src/scan_ui.c src/romscan.c src/strlist.c src/systems.c src/status.c src/jsmn.c -I/opt/homebrew/include -L/opt/homebrew/lib -lSDL2 -lSDL2_ttf
 *   ./leviathan_ui "<path to your teste_roms>"
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "app.h"       /* App, Screen, Action, draw_* prototypes, WIDTH/HEIGHT */
#include "config.h"    /* LEV_SYSTEMS_FILE (output paths now live in main) */
#include "credentials.h" /* runtime user login, read from credentials.txt */
#include "scan_ui.h"
#include "scrape.h"    /* scrape_collection, ScrapeOptions, callbacks */
#include "scrape_ui.h" /* run_scrape_for_selection() \u2014 Option A glue     */
#include "sshttp.h"    /* ss_set_user_credentials() */
#include "status.h"
#include "systems.h"

/* ============================================================================
 * Constants
 * ========================================================================== */

#define FONT_PATH        "assets/silkscreen.ttf"
#define FONT_SIZE_TITLE  56
#define FONT_SIZE_BODY   28

/* Background art. 1024x768 — the exact screen — so it blits 1:1. */
#define BACKGROUND_PATH  "assets/background.png"

static const SDL_Color BLUE  = {  90, 170, 255, 255 };
static const SDL_Color GRAY  = { 120, 120, 120, 255 };
static const SDL_Color WHITE = { 230, 230, 230, 255 };
static const SDL_Color DIM   = {  70,  70,  80, 255 }; /* disabled / unrecognised */
static const SDL_Color BG    = {  12,  14,  20, 255 };
/* The ScreenScraper credit sits on every screen. On the splash it's readable
 * (GRAY); on the working screens it fades back so it doesn't pull the eye off
 * the content, while still being present as the attribution requires. */
static const SDL_Color CREDIT_DIM = { 55, 58, 68, 255 };

/* Layout of the systems list. */
#define LIST_X        80    /* left margin: the list is LEFT-aligned          */
#define LIST_TOP      230   /* y of the first visible row                     */
#define ROW_H         42    /* vertical step between rows (was 38 for body 24) */
#define COUNT_X       720   /* x where the "42 ROMs" column starts            */
#define VISIBLE_ROWS  7     /* system rows that fit ABOVE the fixed SCRAPE bar   */
#define SCRAPE_BAR_Y  (HEIGHT - 168) /* fixed y of the always-visible SCRAPE bar */
#define SCRAPE_SEP_Y  (SCRAPE_BAR_Y - 30) /* separator line just above the bar   */

/* ============================================================================
 * The state machine
 * ========================================================================== */

/* Screen, Action and App now live in app.h (shared with scrape_ui.c). */

/* ============================================================================
 * Input as intention
 * ========================================================================== */



/* The one place hardware becomes intent. NOT static: scrape_ui.c's pump_events
 * calls this too, so there is a single button-to-Action mapping in the whole
 * app. Declared in app.h. */
Action translate_event(const SDL_Event *ev)
{
    if (ev->type == SDL_KEYDOWN) {
        switch (ev->key.keysym.sym) {
        case SDLK_UP:     return ACT_UP;
        case SDLK_DOWN:   return ACT_DOWN;
        case SDLK_RETURN: return ACT_CONFIRM;
        case SDLK_ESCAPE: return ACT_BACK;
        case SDLK_SPACE:  return ACT_START;
        default:          return ACT_NONE;
        }
    }

    /* Brick gamepad. A/B may arrive swapped (X360, per Phase 0). If confirm
     * and back come out inverted on the device, swap ONLY the two marked
     * lines — nothing else in the app knows these buttons exist. */
    if (ev->type == SDL_CONTROLLERBUTTONDOWN) {
        switch (ev->cbutton.button) {
        /* A/B mapping. PROVEN on the device (Phase 3 log probe): the Brick's
         * firmware reports the PHYSICAL A button as SDL label B (=1) and the
         * physical B as SDL label A (=0) — the classic X360-over-Nintendo swap.
         * So on the Brick we bind by SDL label to get the physical intent:
         *   physical A (arrives as BUTTON_B) -> CONFIRM
         *   physical B (arrives as BUTTON_A) -> BACK
         * On the Mac (no TARGET_BRICK) a real/keyboard controller follows the
         * plain X360 convention, so we bind the labels straight.
         * This is the ONLY place the swap lives; pump_events inherits it via
         * translate_event. */
#ifdef TARGET_BRICK
        case SDL_CONTROLLER_BUTTON_B:         return ACT_CONFIRM; /* physical A */
        case SDL_CONTROLLER_BUTTON_A:         return ACT_BACK;    /* physical B */
#else
        case SDL_CONTROLLER_BUTTON_A:         return ACT_CONFIRM;
        case SDL_CONTROLLER_BUTTON_B:         return ACT_BACK;
#endif
        case SDL_CONTROLLER_BUTTON_DPAD_UP:   return ACT_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return ACT_DOWN;
        case SDL_CONTROLLER_BUTTON_START:     return ACT_START;
        default:                              return ACT_NONE;
        }
    }
    return ACT_NONE;
}

/* ============================================================================
 * The App
 * ========================================================================== */




/* The SCRAPE line sits one row past the last system, so it is index
 * detected.count in a virtual list of (count + 1) navigable rows. */
static int sys_row_count(const App *app)
{
    return app->detected.count + 1; /* +1 for the SCRAPE line */
}

/* ============================================================================
 * Drawing helpers
 * ========================================================================== */

void draw_text(SDL_Renderer *ren, TTF_Font *font,
                      const char *text, int x, int y, SDL_Color color)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, color);
    if (surf == NULL) {
        fprintf(stderr, "TTF_Render failed: %s\n", TTF_GetError());
        return;
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);
    if (tex == NULL) {
        fprintf(stderr, "CreateTexture failed: %s\n", SDL_GetError());
        SDL_FreeSurface(surf);
        return;
    }
    SDL_Rect dst = { x, y, surf->w, surf->h };
    SDL_RenderCopy(ren, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

void draw_text_centered(SDL_Renderer *ren, TTF_Font *font,
                               const char *text, int y, SDL_Color color)
{
    int w = 0, h = 0;
    if (TTF_SizeUTF8(font, text, &w, &h) != 0) {
        return;
    }
    draw_text(ren, font, text, (WIDTH - w) / 2, y, color);
}

/* Fill the screen with the dim background art. If the texture failed to load
 * (missing file, decode error), fall back to the plain dark colour so the app
 * still looks intentional instead of showing garbage. Covers the whole 1024x768
 * screen, so callers do not need to SDL_RenderClear first. */
void draw_background(App *app)
{
    if (app->bg_tex != NULL) {
        SDL_RenderCopy(app->ren, app->bg_tex, NULL, NULL);
    } else {
        SDL_SetRenderDrawColor(app->ren, BG.r, BG.g, BG.b, BG.a);
        SDL_RenderClear(app->ren);
    }
}

static void draw_credit(App *app, SDL_Color color)
{
    draw_text_centered(app->ren, app->font_body,
                       "Metadata & artwork by ScreenScraper.fr",
                       HEIGHT - 50, color);
}

/* ============================================================================
 * SCREEN: Splash
 * ========================================================================== */

static void splash_input(App *app, Action action)
{
    switch (action) {
    case ACT_CONFIRM:
        /* With no usable login there is nothing to do past this screen — every
         * path beyond leads to a request that would just fail. So the app holds
         * the user here, on the very first screen, where splash_draw is already
         * telling them to edit credentials.txt. Fix the file, reopen, and the
         * gate opens. Back still quits normally. */
        if (!app->have_credentials) {
            break;
        }
        app->screen = SCREEN_MENU;
        break;
    case ACT_BACK:    app->screen = SCREEN_QUIT; break;
    default: break;
    }
}

static void splash_draw(App *app)
{
    draw_text_centered(app->ren, app->font_title, "LEVIATHAN SCRAPER", 260, BLUE);
    draw_text_centered(app->ren, app->font_body,  "v1.0.0", 330, GRAY);

    /* The prompt below the title changes with the credential state. Normally it
     * invites the user in; with no usable login it tells them what to fix
     * instead, right here on the first screen, and splash_input keeps them from
     * advancing until it is sorted. Short lines: the Silkscreen face is wide. */
    if (app->have_credentials) {
        draw_text_centered(app->ren, app->font_body,
                           "press confirm to begin", 440, WHITE);
    } else {
        draw_text_centered(app->ren, app->font_body,
                           "No ScreenScraper login found.", 430, WHITE);
        draw_text_centered(app->ren, app->font_body,
                           "Edit  credentials.txt  next to", 480, GRAY);
        draw_text_centered(app->ren, app->font_body,
                           "the app, then reopen.", 520, GRAY);
    }

    draw_credit(app, GRAY);
}

/* ============================================================================
 * SCREEN: Menu
 * ========================================================================== */

static const char *MENU_ITEMS[MENU_ITEM_COUNT] = {
    "Scan everything",
    "Choose systems"
};

/* Run the scan and land on the systems screen.
 *
 * select_all is the ONLY thing that separates the two menu entries:
 *   "Choose systems" enters with nothing ticked (select_all = false);
 *   "Scan everything" enters with every system ticked (select_all = true),
 * so the user still SEES what is about to be scraped and can untick any of it
 * before confirming. Same screen, same popup, same everything downstream. */
static void enter_systems(App *app, bool select_all)
{
    int i;

    if (scan_collection(app->roms_dir, app->systems, &app->detected) != 0) {
        app->detected.count = 0; /* folder unreadable: empty-list message shows */
    }

    for (i = 0; i < app->detected.count; i++) {
        app->detected.items[i].selected = select_all;
    }

    app->sys_cursor    = 0;
    app->scroll_top    = 0;
    app->screen        = SCREEN_SYSTEMS;

    /* "Scan everything" lands on the list WITH the confirm popup already up, so
     * the user immediately sees both what got selected (everything) and the
     * question. "Choose systems" lands with no popup, ready to pick. */
    app->popup_active   = select_all;
    app->popup_scan_all = select_all;
}

static void menu_input(App *app, Action action)
{
    switch (action) {
    case ACT_UP:
        app->menu_index = (app->menu_index - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
        break;
    case ACT_DOWN:
        app->menu_index = (app->menu_index + 1) % MENU_ITEM_COUNT;
        break;
    case ACT_CONFIRM:
        if (app->menu_index == 0) {
            /* "Scan everything": same screen as Choose systems, but every
             * system pre-ticked. The user reviews and confirms via the popup. */
            enter_systems(app, true);
        } else {
            /* "Choose systems": nothing ticked; the user picks. */
            enter_systems(app, false);
        }
        break;
    case ACT_BACK:
        app->screen = SCREEN_SPLASH;
        break;
    default:
        break;
    }
}

static void menu_draw(App *app)
{
    int i;
    draw_text_centered(app->ren, app->font_title, "MENU", 120, BLUE);
    for (i = 0; i < MENU_ITEM_COUNT; i++) {
        bool selected = (i == app->menu_index);
        char line[128];
        snprintf(line, sizeof(line), "%s %s",
                 selected ? ">" : " ", MENU_ITEMS[i]);
        draw_text(app->ren, app->font_body, line,
                  380, 300 + i * 50, selected ? WHITE : GRAY);
    }
    draw_text_centered(app->ren, app->font_body,
                       "Confirm: A   Back: B",
                       HEIGHT - 110, GRAY);
    draw_credit(app, CREDIT_DIM);
}

/* ============================================================================
 * SCREEN: Systems
 * ----------------------------------------------------------------------------
 * Only recognised, non-empty systems reach this screen (scan_ui.c filters the
 * rest), so every system row is always tickable. The cursor runs from row 0
 * to detected.count inclusive; the last position IS the SCRAPE line. When the
 * list is longer than VISIBLE_ROWS, a window of rows scrolls with the cursor.
 * ========================================================================== */

/* The one place the SCRAPE action lives, so the cursor+confirm path and the
 * Start-button path do exactly the same thing. Only fires when at least one
 * system is ticked; the popup that will guard the heavy operation comes next
 * piece (for now it goes straight to the stubbed PROGRESS screen). */
static void try_start_scrape(App *app)
{
    /* Both the cursor+confirm path and the Start button land here. We don't
     * start the scrape directly: we raise the confirm popup. The popup is the
     * shared guard that makes an accidental press harmless \u2014 an unintended A
     * or Start just brings up a question the user can cancel. */
    if (scan_selected_count(&app->detected) > 0) {
        app->popup_active = true;
    }
}

/* After any cursor move, slide the scroll window so the cursor stays visible.
 * This is the whole logic of scrolling: keep the highlighted row inside the
 * [scroll_top, scroll_top + VISIBLE_ROWS) window, nudging the window by one
 * when the cursor steps past either edge. The SCRAPE line (index == count) is
 * treated as one more row for this purpose, so scrolling reaches it too. */
static void clamp_scroll(App *app)
{
    int on_scrape = (app->sys_cursor == app->detected.count);

    /* The scrollable list is systems only (0 .. count-1). The SCRAPE row is
     * fixed at the bottom and never scrolls, so when the cursor is on it we
     * leave the window showing the last block of systems. */
    if (!on_scrape) {
        /* Cursor above the window: pull the window up to it. */
        if (app->sys_cursor < app->scroll_top) {
            app->scroll_top = app->sys_cursor;
        }
        /* Cursor below the window: push the window down so the cursor is the
         * last visible row. */
        if (app->sys_cursor > app->scroll_top + VISIBLE_ROWS - 1) {
            app->scroll_top = app->sys_cursor - (VISIBLE_ROWS - 1);
        }
    }

    /* Never scroll past the last system. */
    if (app->detected.count > VISIBLE_ROWS) {
        int max_top = app->detected.count - VISIBLE_ROWS;
        if (app->scroll_top > max_top) {
            app->scroll_top = max_top;
        }
    } else {
        app->scroll_top = 0; /* everything fits; no scrolling */
    }

    if (app->scroll_top < 0) {
        app->scroll_top = 0;
    }
}

static void systems_input(App *app, Action action)
{
    int rows;
    int on_scrape;

    /* MODAL: while the confirm popup is up, all input belongs to it and the
     * list behind is frozen. This single early-return is what makes the
     * overlay modal \u2014 nothing below runs until the popup is dismissed. */
    if (app->popup_active) {
        switch (action) {
        case ACT_CONFIRM:
            /* Yes: dismiss the popup and run the real scrape. The call blocks
             * until the run finishes or is aborted, but Option A keeps the
             * progress screen alive throughout. When it returns, the tally is
             * in app->totals and we move to the summary. */
            app->popup_active   = false;
            app->popup_scan_all = false;
            app->screen = SCREEN_PROGRESS;
            run_scrape_for_selection(app, app->systems,
                                     app->roms_dir, app->output_dir);
            app->screen = SCREEN_SUMMARY;
            break;
        case ACT_BACK:
            /* No: dismiss the popup and stay on the list with the selection
             * intact. Clear scan_all so a later manual Start shows the plain
             * headline \u2014 the user may have unticked systems by then. */
            app->popup_active   = false;
            app->popup_scan_all = false;
            break;
        default:
            /* Up/down/start do nothing while the popup owns the input. */
            break;
        }
        return;
    }

    rows = sys_row_count(app);
    on_scrape = (app->sys_cursor == app->detected.count);

    switch (action) {
    case ACT_UP:
        app->sys_cursor = (app->sys_cursor - 1 + rows) % rows;
        clamp_scroll(app);
        break;
    case ACT_DOWN:
        app->sys_cursor = (app->sys_cursor + 1) % rows;
        clamp_scroll(app);
        break;
    case ACT_CONFIRM:
        if (on_scrape) {
            /* Cursor is on the fixed SCRAPE bar: confirm starts it (guarded by
             * the selection count inside try_start_scrape). */
            try_start_scrape(app);
        } else {
            /* Cursor is on a system: confirm toggles its box. Every visible
             * system is recognised and non-empty, so it is always tickable;
             * the recognised guard stays as defence if the filter loosens. */
            DetectedSystem *row = &app->detected.items[app->sys_cursor];
            if (row->recognised) {
                row->selected = !row->selected;
            }
        }
        break;
    case ACT_START:
        /* Start fires the scrape from ANY cursor position \u2014 the whole point of
         * the dedicated button: you never have to travel to the SCRAPE bar. */
        try_start_scrape(app);
        break;
    case ACT_BACK:
        app->screen = SCREEN_MENU;
        break;
    default:
        break;
    }
}

/* Draw the confirm overlay on top of whatever is already on screen. Called
 * last in systems_draw so it sits above the (frozen) list. Three layers:
 *   1. a translucent black sheet that dims the list, signalling "not now";
 *   2. a solid box, so the question reads cleanly over the dimmed list;
 *   3. the text, plus a tooltip spelling out which button does what.
 *
 * The translucent sheet needs alpha blending, which SDL has OFF by default;
 * main() turns it on once (SDL_SetRenderDrawBlendMode) so the alpha below
 * actually takes effect instead of drawing a solid rectangle. */
static void popup_draw(App *app)
{
    SDL_Rect sheet = { 0, 0, WIDTH, HEIGHT };
    SDL_Rect box;
    int bx = 160, by = 270, bw = WIDTH - 320, bh = 230;

    /* 1. dim sheet over the whole screen */
    SDL_SetRenderDrawColor(app->ren, 0, 0, 0, 180); /* alpha 180/255 */
    SDL_RenderFillRect(app->ren, &sheet);

    /* 2. solid box */
    box.x = bx; box.y = by; box.w = bw; box.h = bh;
    SDL_SetRenderDrawColor(app->ren, 20, 24, 34, 255);
    SDL_RenderFillRect(app->ren, &box);
    /* a thin blue frame: draw a slightly larger rect behind, cheap border */
    SDL_SetRenderDrawColor(app->ren, BLUE.r, BLUE.g, BLUE.b, 255);
    SDL_RenderDrawRect(app->ren, &box);

    /* 3. text \u2014 the headline depends on where the popup came from. */
    if (app->popup_scan_all) {
        draw_text_centered(app->ren, app->font_title, "ALL SYSTEMS SELECTED", by + 30, WHITE);
        draw_text_centered(app->ren, app->font_body,
                           "Scrape them all? This may take a while.",
                           by + 110, GRAY);
    } else {
        draw_text_centered(app->ren, app->font_title, "START SCRAPE?", by + 30, WHITE);
        draw_text_centered(app->ren, app->font_body,
                           "This may take a while. Abort saves progress.",
                           by + 110, GRAY);
    }
    /* tooltip: exactly which button does what */
    draw_text_centered(app->ren, app->font_body,
                       "A: yes      B: no",
                       by + 165, BLUE);
}

static void systems_draw(App *app)
{
    int i;
    int selected_count = scan_selected_count(&app->detected);
    int window_end;

    draw_text_centered(app->ren, app->font_title, "CHOOSE SYSTEMS", 60, BLUE);
    draw_text_centered(app->ren, app->font_body,
                       "showing only systems containing ROMs", 130, GRAY);

    if (app->detected.count == 0) {
        draw_text_centered(app->ren, app->font_body,
                           "no systems with ROMs found in that folder", 340, GRAY);
        draw_text_centered(app->ren, app->font_body,
                           "B: back", HEIGHT - 110, GRAY);
        draw_credit(app, CREDIT_DIM);
        return;
    }

    /* --- The scrollable list: systems only (0 .. count-1). --- */
    window_end = app->scroll_top + VISIBLE_ROWS;
    if (window_end > app->detected.count) {
        window_end = app->detected.count;
    }

    for (i = app->scroll_top; i < window_end; i++) {
        const DetectedSystem *row = &app->detected.items[i];
        bool  on_cursor = (i == app->sys_cursor);
        int   screen_row = i - app->scroll_top;
        int   y = LIST_TOP + screen_row * ROW_H;
        char  left[256];
        char  count[32];
        SDL_Color color = on_cursor ? WHITE : GRAY;

        snprintf(left, sizeof(left), "%s [%s] %s (%s)",
                 on_cursor ? ">" : " ",
                 row->selected ? "X" : " ",
                 row->name, row->tag);
        draw_text(app->ren, app->font_body, left, LIST_X, y, color);

        snprintf(count, sizeof(count), "%d ROMs", row->rom_count);
        draw_text(app->ren, app->font_body, count, COUNT_X, y, color);
    }

    /* Scroll hints when there is more list off-screen. */
    if (app->scroll_top > 0) {
        draw_text(app->ren, app->font_body, "^", WIDTH - 60, LIST_TOP, GRAY);
    }
    if (window_end < app->detected.count) {
        draw_text(app->ren, app->font_body, "v", WIDTH - 60,
                  LIST_TOP + (VISIBLE_ROWS - 1) * ROW_H, GRAY);
    }

    /* --- The fixed SCRAPE bar: always visible, anchored at the bottom, never
     * scrolls. The cursor reaches it as position detected.count; Start reaches
     * it from anywhere. When nothing is ticked it turns into an instruction
     * instead of a mute disabled button. --- */
    {
        bool on_cursor = (app->sys_cursor == app->detected.count);
        bool enabled   = (selected_count > 0);
        char line[80];
        SDL_Color color;

        /* A thin separator so the bar reads as its own zone, not another row. */
        draw_text_centered(app->ren, app->font_body,
                           "------------------------------------------------",
                           SCRAPE_SEP_Y, DIM);

        if (!enabled) {
            color = on_cursor ? WHITE : DIM;
            snprintf(line, sizeof(line), "%s SELECT AT LEAST ONE SYSTEM",
                     on_cursor ? ">" : " ");
        } else {
            color = on_cursor ? WHITE : BLUE;
            snprintf(line, sizeof(line), "%s SCRAPE  (%d selected)   [START]",
                     on_cursor ? ">" : " ", selected_count);
        }
        draw_text(app->ren, app->font_body, line, LIST_X, SCRAPE_BAR_Y, color);
    }

    draw_text_centered(app->ren, app->font_body,
                       "Confirm: A   Back: B   Scrape: START",
                       HEIGHT - 110, GRAY);
    draw_credit(app, CREDIT_DIM);

    /* The popup goes on top of everything, so it is the last thing drawn. */
    if (app->popup_active) {
        popup_draw(app);
    }
}

/* ============================================================================
 * SCREEN: Progress (still a stub)
 * ========================================================================== */

/* NOTE: during an actual scrape the progress screen is driven entirely by
 * scrape_ui.c (draw_progress), because scrape_collection() blocks the main
 * loop until it returns \u2014 at which point app->screen is already SUMMARY. So
 * these two functions are effectively unreachable in the normal flow; they
 * stay as a minimal, honest fallback in case the state is ever entered
 * directly (e.g. future changes). */
static void progress_input(App *app, Action action)
{
    if (action == ACT_BACK || action == ACT_CONFIRM) {
        app->screen = SCREEN_SUMMARY;
    }
}

static void progress_draw(App *app)
{
    draw_text_centered(app->ren, app->font_title, "SCRAPING", 120, BLUE);
    draw_credit(app, CREDIT_DIM);
}

/* ============================================================================
 * SCREEN: Summary (still a stub)
 * ========================================================================== */

static void summary_input(App *app, Action action)
{
    switch (action) {
    case ACT_CONFIRM:
    case ACT_BACK:
        app->menu_index = 0;
        app->screen = SCREEN_MENU;
        break;
    default:
        break;
    }
}

/* One "label ....... value" row of the tally, left-label / right-value, so the
 * numbers line up in a column. Mirrors what test_scrape's print_summary shows,
 * only drawn instead of printed. */
static void summary_row(App *app, const char *label, int value, int y, SDL_Color color)
{
    char num[16];
    draw_text(app->ren, app->font_body, label, 300, y, color);
    snprintf(num, sizeof(num), "%d", value);
    draw_text(app->ren, app->font_body, num, 640, y, color);
}

static void summary_draw(App *app)
{
    const ScrapeTotals *t = &app->totals;
    int y = 210;

    draw_text_centered(app->ren, app->font_title, "SUMMARY", 90, BLUE);

    summary_row(app, "downloaded",    t->count[SCRAPE_OK],        y,       BLUE);
    summary_row(app, "already had",   t->count[SCRAPE_SKIPPED],   y +  45, GRAY);
    summary_row(app, "no cover",      t->count[SCRAPE_NO_COVER],  y +  90, GRAY);
    summary_row(app, "not found",     t->count[SCRAPE_NOT_FOUND], y + 135, GRAY);
    summary_row(app, "network error", t->count[SCRAPE_NET_ERROR], y + 180, GRAY);
    summary_row(app, "disk error",    t->count[SCRAPE_IO_ERROR],  y + 225, GRAY);

    /* Special conditions worth a full-width note rather than a number. */
    if (t->count[SCRAPE_AUTH_ERROR] > 0) {
        draw_text_centered(app->ren, app->font_body,
                           "credentials rejected - check config.h", y + 285, WHITE);
    } else if (t->count[SCRAPE_QUOTA] > 0) {
        draw_text_centered(app->ren, app->font_body,
                           "daily quota reached - try again tomorrow", y + 285, WHITE);
    }

    /* How the run ended. */
    switch (app->run_result) {
    case RUN_COMPLETED:
        draw_text_centered(app->ren, app->font_body, "Done.", y + 330, WHITE);
        break;
    case RUN_ABORTED: {
        char line[160];
        snprintf(line, sizeof(line), "Stopped during %s. The counts above hold.",
                 t->last_system);
        draw_text_centered(app->ren, app->font_body, line, y + 330, WHITE);
        break;
    }
    case RUN_ROMS_DIR_NOT_FOUND:
        draw_text_centered(app->ren, app->font_body,
                           "ROM folder not found.", y + 330, WHITE);
        break;
    }

    draw_text_centered(app->ren, app->font_body,
                       "A or B: menu", HEIGHT - 110, GRAY);
    draw_credit(app, CREDIT_DIM);
}

/* ============================================================================
 * main
 * ========================================================================== */

int main(int argc, char *argv[])
{
    App app;
    memset(&app, 0, sizeof(app));
    app.screen = SCREEN_SPLASH;

    /* The two platform paths, decided together in one place. On the Brick the
     * stock OS fixes both: ROMs live under /mnt/SDCARD/Roms and covers must land
     * in /mnt/SDCARD/Imgs/<TAG>/ for the OS to show them. launch.sh starts us
     * with no argument, so we hard-wire them. On the Mac both are relative to
     * the working directory: the ROM folder comes from the command line (how
     * every test run drives it) and covers go to ./Imgs so a local test never
     * writes outside the project. */
#ifdef TARGET_BRICK
    app.roms_dir   = "/mnt/SDCARD/Roms";
    app.output_dir = "/mnt/SDCARD/Imgs";
    (void)argc; (void)argv;
#else
    app.roms_dir   = (argc > 1) ? argv[1] : ".";
    app.output_dir = "Imgs";
#endif

    static SystemList systems;
    if (systems_load(LEV_SYSTEMS_FILE, &systems) < 0) {
        fprintf(stderr, "warning: could not load %s; "
                        "tag resolution will be limited\n", LEV_SYSTEMS_FILE);
    }
    app.systems = &systems;

    /* ---- User credentials ----
     * Read the login from the file next to the binary and hand it to the
     * transport layer, which holds it for every request. have_credentials is
     * the verdict the menu reads: false when the file is missing, unedited, or
     * half-filled, in which case the menu shows how to fix it and refuses to
     * start a scrape. We set the pair even when incomplete (with empty strings)
     * so the transport's state matches what the app believes rather than
     * carrying stale values. */
    {
        UserCredentials cred;
        credentials_load(LEV_CREDENTIALS_FILE, &cred);
        ss_set_user_credentials(cred.ssid, cred.sspassword);
        app.have_credentials = (cred.complete != 0);
    }

    /* Initialise libcurl once, up front and on purpose. Until now the app
     * relied on curl_easy_init doing this implicitly on the first request,
     * which libcurl's own docs warn against — it is not thread-safe and is not
     * guaranteed. Doing it here makes startup deliberate. A failure is not
     * fatal: we log it and carry on, since the implicit path may still work,
     * and blocking the whole app over it would be worse than letting a scrape
     * try and report a network error. */
    if (ss_http_init() != 0) {
        fprintf(stderr, "warning: could not initialise libcurl up front; "
                        "scraping may still work but is on shakier ground\n");
    }

    /* ---- SDL bring-up ---- */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "Leviathan Scraper",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
    if (win == NULL) {
        fprintf(stderr, "CreateWindow failed: %s\n", SDL_GetError());
        TTF_Quit(); SDL_Quit();
        return 1;
    }

    app.ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (app.ren == NULL) {
        fprintf(stderr, "CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit();
        return 1;
    }

    /* Enable alpha blending so the popup's translucent dim sheet actually
     * blends over the list instead of painting a solid black rectangle. */
    SDL_SetRenderDrawBlendMode(app.ren, SDL_BLENDMODE_BLEND);

    app.font_title = TTF_OpenFont(FONT_PATH, FONT_SIZE_TITLE);
    app.font_body  = TTF_OpenFont(FONT_PATH, FONT_SIZE_BODY);
    if (app.font_title == NULL || app.font_body == NULL) {
        fprintf(stderr, "OpenFont failed (%s): %s\n", FONT_PATH, TTF_GetError());
        if (app.font_title) TTF_CloseFont(app.font_title);
        if (app.font_body)  TTF_CloseFont(app.font_body);
        SDL_DestroyRenderer(app.ren); SDL_DestroyWindow(win);
        TTF_Quit(); SDL_Quit();
        return 1;
    }

    /* Background art. IMG_Init asks SDL_image for PNG support; if it or the load
     * fails we carry on with a NULL texture — draw_background falls back to the
     * plain dark fill, so the app still runs. It loads once here and lives for
     * the whole session; loading a PNG every frame would be needless work. */
    IMG_Init(IMG_INIT_PNG);
    app.bg_tex = IMG_LoadTexture(app.ren, BACKGROUND_PATH);
    if (app.bg_tex == NULL) {
        /* Not fatal: log it and let draw_background handle the missing art. */
        fprintf(stderr, "warning: background art failed to load (%s); "
                        "using plain fill\n", IMG_GetError());
    }

    SDL_GameController *pad = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            if (pad != NULL) break;
        }
    }

    /* ---- The loop ---- */
    while (app.screen != SCREEN_QUIT) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                app.screen = SCREEN_QUIT;
                break;
            }
            Action action = translate_event(&ev);
            if (action == ACT_NONE) {
                continue;
            }
            switch (app.screen) {
            case SCREEN_SPLASH:   splash_input(&app, action);   break;
            case SCREEN_MENU:     menu_input(&app, action);     break;
            case SCREEN_SYSTEMS:  systems_input(&app, action);  break;
            case SCREEN_PROGRESS: progress_input(&app, action); break;
            case SCREEN_SUMMARY:  summary_input(&app, action);  break;
            case SCREEN_QUIT:     break;
            }
        }

        if (app.screen == SCREEN_QUIT) {
            break;
        }

        /* Lay down the backdrop for this frame — the same dim art behind every
         * screen. Covers the whole 1024x768, replacing the old solid clear. */
        draw_background(&app);

        switch (app.screen) {
        case SCREEN_SPLASH:   splash_draw(&app);   break;
        case SCREEN_MENU:     menu_draw(&app);     break;
        case SCREEN_SYSTEMS:  systems_draw(&app);  break;
        case SCREEN_PROGRESS: progress_draw(&app); break;
        case SCREEN_SUMMARY:  summary_draw(&app);  break;
        case SCREEN_QUIT:     break;
        }

        SDL_RenderPresent(app.ren);
    }

    /* ---- Cleanup ---- */
    ss_http_cleanup();
    if (pad != NULL) {
        SDL_GameControllerClose(pad);
    }
    systems_free(&systems);
    if (app.bg_tex) SDL_DestroyTexture(app.bg_tex);
    IMG_Quit();
    TTF_CloseFont(app.font_body);
    TTF_CloseFont(app.font_title);
    SDL_DestroyRenderer(app.ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
