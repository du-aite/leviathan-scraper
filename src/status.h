/*
 * status.h — result codes for the Leviathan Scraper core.
 *
 * Design rule: the core returns CODES, never display strings. Whoever shows
 * the result (terminal, on-screen UI) decides the wording and the language.
 * Nothing in the core may branch on a human-readable message.
 */

#ifndef LEVIATHAN_STATUS_H
#define LEVIATHAN_STATUS_H

/* ------------------------------------------------------------------
 * Per-ROM result: what happened to ONE file.
 * ------------------------------------------------------------------ */
typedef enum {
    SCRAPE_OK = 0,    /* cover found and written to disk                  */
    SCRAPE_SKIPPED,   /* cover already existed; no API call was made      */
    SCRAPE_NO_COVER,  /* game identified, but no cover of the wanted type */
    SCRAPE_NOT_FOUND, /* hash unknown to ScreenScraper (v1.1 hook: this
                       * is the condition that will trigger the
                       * libretro-thumbnails name-based fallback)         */
    SCRAPE_NET_ERROR, /* network, TLS or HTTP failure                     */
    SCRAPE_IO_ERROR,  /* could not read the ROM or write the image        */
    SCRAPE_AUTH_ERROR,/* credentials rejected: a settings problem, and no
                       * amount of retrying will fix it                   */
    SCRAPE_QUOTA,     /* daily allowance spent: try again tomorrow        */

    SCRAPE_STATUS_COUNT /* keep last: array size, never a real status     */
} ScrapeStatus;

/* ------------------------------------------------------------------
 * Whole-run result: how the SWEEP ended. A different level entirely
 * from ScrapeStatus — do not mix the two.
 * ------------------------------------------------------------------ */
typedef enum {
    RUN_COMPLETED = 0,     /* reached the end of the collection */
    RUN_ABORTED,           /* the user asked it to stop         */
    RUN_ROMS_DIR_NOT_FOUND /* ROM folder missing or unreadable  */
} RunStatus;

/* ------------------------------------------------------------------
 * Tally of a run. Filled in by the core and returned to the caller
 * REGARDLESS of how the run ended, so an aborted run still reports
 * everything it managed to download before stopping.
 * ------------------------------------------------------------------ */
#define LEV_SYSTEM_NAME_MAX 64

typedef struct {
    int count[SCRAPE_STATUS_COUNT]; /* indexed by ScrapeStatus */

    /* Where the run stopped. Meaningful mainly for RUN_ABORTED, but always
     * filled so a UI can show "last seen" without extra bookkeeping. */
    char last_system[LEV_SYSTEM_NAME_MAX];
    int  last_index; /* 1-based position of the last ROM touched */
    int  last_total; /* how many ROMs that system had            */
} ScrapeTotals;

/* Zero a tally before a run. Always call this; a stack struct starts as junk. */
void totals_reset(ScrapeTotals *t);

/* Convenience: number of ROMs actually processed (everything but skips). */
int totals_processed(const ScrapeTotals *t);

/* ------------------------------------------------------------------
 * Callbacks: how the core talks to whoever is driving it.
 * The core does not know whether that is a terminal or a screen.
 * Both may be NULL — the core then runs silently and never aborts.
 * ------------------------------------------------------------------ */

/* Called once per ROM, before work starts, so the caller can show progress. */
typedef void (*ProgressFn)(const char *system_name, int index, int total,
                           const char *rom_name, void *user);

/* Polled between ROMs. Return non-zero to stop the run cleanly. */
typedef int (*ShouldAbortFn)(void *user);

#endif /* LEVIATHAN_STATUS_H */
