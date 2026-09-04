/*
 * ssparse.c — walking a jeuInfos response.
 *
 * The reply nests as response -> jeu -> { noms[], medias[] }. jsmn hands
 * back a flat token array, so the code below descends by name and skips
 * whole subtrees it does not care about.
 */

#include <stdlib.h>
#include <string.h>

#define JSMN_HEADER
#include "jsmn.h"
#include "ssparse.h"

/* ------------------------------------------------------------------
 * Token helpers (same shape as the ones in systems.c)
 * ------------------------------------------------------------------ */

static int subtree_size(const jsmntok_t *tokens, int i)
{
    int n = 1;
    int k;

    if (tokens[i].type == JSMN_OBJECT || tokens[i].type == JSMN_ARRAY) {
        for (k = 0; k < tokens[i].size; k++) {
            n += subtree_size(tokens, i + n);
        }
    } else if (tokens[i].type == JSMN_STRING && tokens[i].size == 1) {
        n += subtree_size(tokens, i + n);
    }
    return n;
}

static int token_is(const char *json, const jsmntok_t *tok, const char *name)
{
    size_t len = (size_t)(tok->end - tok->start);

    return (tok->type == JSMN_STRING && strlen(name) == len &&
            strncmp(json + tok->start, name, len) == 0);
}

/* Encode a Unicode code point as UTF-8. Returns bytes written. */
static int utf8_encode(unsigned int cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

static unsigned int hex4(const char *s)
{
    unsigned int v = 0;
    int          i;

    for (i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= (unsigned int)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v |= (unsigned int)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            v |= (unsigned int)(c - 'A' + 10);
        }
    }
    return v;
}

/*
 * Copy a JSON string token, undoing the escapes. This matters twice over:
 * cover URLs arrive with \/ for every slash, and game names arrive with
 * \u00e9 and friends — "Pok\u00e9mon" has to come out as "Pokémon" or the
 * name is wrong and the URL does not resolve.
 */
static char *token_to_string(const char *json, const jsmntok_t *tok)
{
    size_t len = (size_t)(tok->end - tok->start);
    /* Escapes only ever shrink or keep length, except \uXXXX which can
     * become 3 bytes from 6 — still shorter. len+1 is always enough. */
    char  *out = malloc(len + 1);
    size_t i   = 0;
    size_t o   = 0;
    const char *src = json + tok->start;

    if (out == NULL) {
        return NULL;
    }

    while (i < len) {
        if (src[i] != '\\' || i + 1 >= len) {
            out[o++] = src[i++];
            continue;
        }
        i++; /* skip the backslash */
        switch (src[i]) {
        case 'n':  out[o++] = '\n'; i++; break;
        case 't':  out[o++] = '\t'; i++; break;
        case 'r':  out[o++] = '\r'; i++; break;
        case 'b':  out[o++] = '\b'; i++; break;
        case 'f':  out[o++] = '\f'; i++; break;
        case '/':  out[o++] = '/';  i++; break;
        case '"':  out[o++] = '"';  i++; break;
        case '\\': out[o++] = '\\'; i++; break;
        case 'u':
            if (i + 4 < len) {
                unsigned int cp = hex4(src + i + 1);
                o += (size_t)utf8_encode(cp, out + o);
                i += 5;
            } else {
                out[o++] = src[i++];
            }
            break;
        default:
            out[o++] = src[i++];
            break;
        }
    }

    out[o] = '\0';
    return out;
}

/* ------------------------------------------------------------------
 * Media list
 * ------------------------------------------------------------------ */

static int media_reserve(SsGame *g)
{
    SsMedia *grown;
    int      new_cap;

    if (g->media_count < g->media_capacity) {
        return 0;
    }
    new_cap = (g->media_capacity == 0) ? 16 : g->media_capacity * 2;
    grown   = realloc(g->medias, (size_t)new_cap * sizeof(SsMedia));
    if (grown == NULL) {
        return -1;
    }
    g->medias         = grown;
    g->media_capacity = new_cap;
    return 0;
}

/* Copy a short field into a fixed buffer, unescaping first. */
static void copy_field(const char *json, const jsmntok_t *tok, char *dest, size_t dest_size)
{
    char *value = token_to_string(json, tok);

    dest[0] = '\0';
    if (value == NULL) {
        return;
    }
    strncpy(dest, value, dest_size - 1);
    dest[dest_size - 1] = '\0';
    free(value);
}

/* ------------------------------------------------------------------
 * Parsing
 * ------------------------------------------------------------------ */

static void parse_medias(const char *json, const jsmntok_t *tokens, int array_index, SsGame *g)
{
    int count = tokens[array_index].size;
    int at    = array_index + 1;
    int e;

    for (e = 0; e < count; e++) {
        int object = at;

        if (tokens[object].type == JSMN_OBJECT) {
            SsMedia media;
            int     keys = tokens[object].size;
            int     k;
            int     i = object + 1;

            media.type[0]   = '\0';
            media.region[0] = '\0';
            media.url       = NULL;

            for (k = 0; k < keys; k++) {
                int key   = i;
                int value = i + 1;

                if (token_is(json, &tokens[key], "type")) {
                    copy_field(json, &tokens[value], media.type, sizeof(media.type));
                } else if (token_is(json, &tokens[key], "region")) {
                    copy_field(json, &tokens[value], media.region, sizeof(media.region));
                } else if (token_is(json, &tokens[key], "url")) {
                    media.url = token_to_string(json, &tokens[value]);
                }
                i += subtree_size(tokens, key);
            }

            if (media.url != NULL && media_reserve(g) == 0) {
                g->medias[g->media_count] = media;
                g->media_count++;
            } else {
                free(media.url);
            }
        }
        at += subtree_size(tokens, at);
    }
}

/* The Go reference took the first entry of noms[]. Kept identical here so
 * the two implementations report the same title. */
static void parse_names(const char *json, const jsmntok_t *tokens, int array_index, SsGame *g)
{
    int count = tokens[array_index].size;
    int at    = array_index + 1;

    if (count <= 0) {
        return;
    }
    if (tokens[at].type == JSMN_OBJECT) {
        int keys = tokens[at].size;
        int k;
        int i = at + 1;

        for (k = 0; k < keys; k++) {
            int key   = i;
            int value = i + 1;

            if (token_is(json, &tokens[key], "text")) {
                copy_field(json, &tokens[value], g->name, sizeof(g->name));
                return;
            }
            i += subtree_size(tokens, key);
        }
    }
}

static void parse_jeu(const char *json, const jsmntok_t *tokens, int object_index, SsGame *g)
{
    int keys = tokens[object_index].size;
    int k;
    int i = object_index + 1;

    for (k = 0; k < keys; k++) {
        int key   = i;
        int value = i + 1;

        if (token_is(json, &tokens[key], "noms") && tokens[value].type == JSMN_ARRAY) {
            parse_names(json, tokens, value, g);
        } else if (token_is(json, &tokens[key], "medias") && tokens[value].type == JSMN_ARRAY) {
            parse_medias(json, tokens, value, g);
        }
        i += subtree_size(tokens, key);
    }
}

SsParseResult ss_parse_game(const char *json, size_t len, SsGame *out)
{
    jsmn_parser parser;
    jsmntok_t  *tokens;
    int         total;
    int         keys, k, i;
    int         found_jeu = 0;

    if (out == NULL) {
        return SS_PARSE_MALFORMED;
    }
    memset(out, 0, sizeof(*out));

    if (json == NULL || len == 0) {
        return SS_PARSE_MALFORMED;
    }

    jsmn_init(&parser);
    total = jsmn_parse(&parser, json, len, NULL, 0);
    if (total < 1) {
        return SS_PARSE_MALFORMED;
    }

    tokens = malloc((size_t)total * sizeof(jsmntok_t));
    if (tokens == NULL) {
        return SS_PARSE_MALFORMED;
    }

    jsmn_init(&parser);
    if (jsmn_parse(&parser, json, len, tokens, (unsigned int)total) < 1 ||
        tokens[0].type != JSMN_OBJECT) {
        free(tokens);
        return SS_PARSE_MALFORMED;
    }

    /* Top level: find "response", then "jeu" inside it. */
    keys = tokens[0].size;
    i    = 1;
    for (k = 0; k < keys; k++) {
        int key   = i;
        int value = i + 1;

        if (token_is(json, &tokens[key], "response") && tokens[value].type == JSMN_OBJECT) {
            int rkeys = tokens[value].size;
            int r;
            int ri = value + 1;

            for (r = 0; r < rkeys; r++) {
                int rkey   = ri;
                int rvalue = ri + 1;

                if (token_is(json, &tokens[rkey], "jeu") &&
                    tokens[rvalue].type == JSMN_OBJECT) {
                    parse_jeu(json, tokens, rvalue, out);
                    found_jeu = 1;
                    break;
                }
                ri += subtree_size(tokens, rkey);
            }
            break;
        }
        i += subtree_size(tokens, key);
    }

    free(tokens);

    if (!found_jeu) {
        ss_game_free(out);
        return SS_PARSE_NO_GAME;
    }
    return SS_PARSE_OK;
}

void ss_game_free(SsGame *g)
{
    int i;

    if (g == NULL) {
        return;
    }
    for (i = 0; i < g->media_count; i++) {
        free(g->medias[i].url);
    }
    free(g->medias);
    memset(g, 0, sizeof(*g));
}

/* ------------------------------------------------------------------
 * Cover selection
 * ------------------------------------------------------------------ */

const SsMedia *ss_choose_cover(const SsGame *g, const char *type, const char *preferred_region)
{
    const char *targets[2];
    int         t, i;
    const SsMedia *first_of_type = NULL;

    if (g == NULL || type == NULL) {
        return NULL;
    }

    targets[0] = (preferred_region != NULL) ? preferred_region : "wor";
    targets[1] = "wor";

    for (t = 0; t < 2; t++) {
        for (i = 0; i < g->media_count; i++) {
            if (strcmp(g->medias[i].type, type) != 0) {
                continue;
            }
            if (first_of_type == NULL) {
                first_of_type = &g->medias[i];
            }
            if (strcmp(g->medias[i].region, targets[t]) == 0) {
                return &g->medias[i];
            }
        }
    }

    /* Nothing in the preferred regions: take the first of the right type. */
    return first_of_type;
}
