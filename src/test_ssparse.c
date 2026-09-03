/*
 * test_ssparse.c — offline checks for the response parser.
 *
 * Runs against saved API replies, so it costs no quota and needs no
 * network. This is where the region fallback is proven.
 *
 *   gcc -std=c99 -Wall -Wextra -o test_ssparse test_ssparse.c ssparse.c
 *   ./test_ssparse samples/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ssparse.h"

static int failures = 0;

static char *slurp(const char *path, size_t *out_len)
{
    FILE  *f = fopen(path, "rb");
    char  *buf;
    long   size;
    size_t got;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);

    buf = malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len != NULL) {
        *out_len = got;
    }
    return buf;
}

static void expect(const char *label, int condition, const char *detail)
{
    if (condition) {
        printf("  ok    %-34s %s\n", label, detail);
    } else {
        printf("  FAIL  %-34s %s\n", label, detail);
        failures++;
    }
}

static void run_case(const char *dir, const char *file, const char *label,
                     SsParseResult want_result, const char *want_name,
                     const char *want_region)
{
    char           path[1024];
    char          *json;
    size_t         len;
    SsGame         game;
    SsParseResult  result;
    const SsMedia *cover;

    snprintf(path, sizeof(path), "%s/%s", dir, file);
    json = slurp(path, &len);
    if (json == NULL) {
        printf("  FAIL  %-34s could not read %s\n", label, path);
        failures++;
        return;
    }

    result = ss_parse_game(json, len, &game);

    if (result != want_result) {
        printf("  FAIL  %-34s parse result %d, wanted %d\n", label, result, want_result);
        failures++;
        free(json);
        ss_game_free(&game);
        return;
    }

    if (result != SS_PARSE_OK) {
        printf("  ok    %-34s correctly reported no game\n", label);
        free(json);
        return;
    }

    if (want_name != NULL) {
        char detail[512];
        snprintf(detail, sizeof(detail), "name = %s", game.name);
        expect(label, strcmp(game.name, want_name) == 0, detail);
    }

    cover = ss_choose_cover(&game, "box-2D", "us");
    if (want_region == NULL) {
        expect("  no cover expected", cover == NULL, cover ? "got one anyway" : "none, correct");
    } else {
        char detail[512];
        snprintf(detail, sizeof(detail), "region %s -> %s",
                 cover ? cover->region : "(none)", cover ? cover->url : "(none)");
        expect("  cover choice", cover != NULL && strcmp(cover->region, want_region) == 0, detail);
    }

    ss_game_free(&game);
    free(json);
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";

    putchar('\n');
    puts("parsing saved ScreenScraper replies");

    /* Accented name arrives as \u00e9 and must come out as UTF-8. */
    run_case(dir, "sample_response.json", "full reply, us cover present",
             SS_PARSE_OK, "Pokémon Blue Version", "us");

    /* No us cover: the chain must fall through to wor. */
    run_case(dir, "sample_worldonly.json", "no us cover, wor available",
             SS_PARSE_OK, "Tetris", "wor");

    /* Neither us nor wor: take whatever exists rather than giving up. */
    run_case(dir, "sample_jponly.json", "only a jp cover",
             SS_PARSE_OK, "Idol Game", "jp");

    /* Game found, but it has no cover of the requested type. */
    run_case(dir, "sample_nocover.json", "game with no box-2D at all",
             SS_PARSE_OK, "Obscure Hack", NULL);

    /* ScreenScraper does not know this hash. */
    run_case(dir, "sample_notfound.json", "hash not in the database",
             SS_PARSE_NO_GAME, NULL, NULL);

    /* Garbage in must not crash. */
    {
        SsGame game;
        const char *junk = "this is not json at all";
        expect("garbage input rejected",
               ss_parse_game(junk, strlen(junk), &game) == SS_PARSE_MALFORMED, "");
        ss_game_free(&game);
    }

    /* A truncated reply is the realistic network failure. */
    {
        SsGame game;
        const char *cut = "{\"response\":{\"jeu\":{\"noms\":[{\"text\":\"Half";
        SsParseResult r = ss_parse_game(cut, strlen(cut), &game);
        expect("truncated reply rejected", r == SS_PARSE_MALFORMED, "");
        ss_game_free(&game);
    }

    printf("\n%s\n\n", failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
