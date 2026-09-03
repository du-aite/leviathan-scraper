/*
 * ssparse.h — reading a ScreenScraper jeuInfos response.
 *
 * Deliberately separate from the HTTP code. Parsing is pure: give it a
 * buffer of JSON and it hands back a game, with no network involved. That
 * makes it testable against a saved response, which is how the region
 * fallback below gets exercised without spending API quota.
 */

#ifndef LEVIATHAN_SSPARSE_H
#define LEVIATHAN_SSPARSE_H

#include <stddef.h>

#define SS_GAME_NAME_MAX 256
#define SS_TYPE_MAX      32
#define SS_REGION_MAX    16

typedef struct {
    char  type[SS_TYPE_MAX];     /* "box-2D", "ss", "wheel", ... */
    char  region[SS_REGION_MAX]; /* "us", "eu", "jp", "wor", ... */
    char *url;                   /* owned */
} SsMedia;

typedef struct {
    char      name[SS_GAME_NAME_MAX];
    SsMedia  *medias;
    int       media_count;
    int       media_capacity;
} SsGame;

typedef enum {
    SS_PARSE_OK = 0,
    SS_PARSE_NO_GAME,   /* well-formed reply, but it carries no game */
    SS_PARSE_MALFORMED  /* not JSON, or not the shape we expect      */
} SsParseResult;

/* Parse a jeuInfos response. On SS_PARSE_OK the caller must ss_game_free(). */
SsParseResult ss_parse_game(const char *json, size_t len, SsGame *out);

void ss_game_free(SsGame *g);

/*
 * Pick a cover: the requested media type, preferring the given region, then
 * "wor", then whatever came first. Returns NULL when the game has no media
 * of that type at all.
 *
 * The fallback chain is not about internationalisation — plenty of games
 * simply have no cover in any one particular region, and without the chain
 * they would all come back empty.
 */
const SsMedia *ss_choose_cover(const SsGame *g, const char *type, const char *preferred_region);

#endif /* LEVIATHAN_SSPARSE_H */
