/*
 * status.c — helpers for the result tally.
 */

#include <string.h>

#include "status.h"

void totals_reset(ScrapeTotals *t)
{
    if (t == NULL) {
        return;
    }
    memset(t, 0, sizeof(*t));
}

int totals_processed(const ScrapeTotals *t)
{
    int i, n = 0;

    if (t == NULL) {
        return 0;
    }
    for (i = 0; i < SCRAPE_STATUS_COUNT; i++) {
        if (i != SCRAPE_SKIPPED) {
            n += t->count[i];
        }
    }
    return n;
}
