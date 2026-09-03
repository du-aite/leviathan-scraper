/*
 * systems.c — parsing sistemas.json and resolving folder tags.
 *
 * JSON parsing uses jsmn (MIT, Serge Zaitsev), a single-header tokenizer.
 * jsmn does not build a tree: it hands back a flat array of tokens, and the
 * caller walks it. That keeps the dependency to one file with no allocator
 * of its own, at the cost of the walking code below.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jsmn.h"
#include "romscan.h"
#include "systems.h"

/* ------------------------------------------------------------------
 * Layer 1: the manual map.
 *
 * Trimui uses short tags that no ScreenScraper alias list contains, so
 * these have to be stated outright. Everything else resolves through the
 * alias lists in layer 2.
 * ------------------------------------------------------------------ */
typedef struct {
    const char *tag;
    int         system_id;
} ManualEntry;

static const ManualEntry manual_map[] = {
    { "FC",  3  }, /* NES / Famicom              */
    { "SFC", 4  }, /* Super Nintendo / Famicom   */
    { "MD",  1  }, /* Mega Drive / Genesis       */
    { "MS",  2  }, /* Master System              */
    { "PS",  57 }, /* PlayStation                */
    { "SS",  22 }, /* Saturn                     */
    { "PCE", 31 }, /* PC Engine / TurboGrafx-16  */
    { "WS",  45 }, /* WonderSwan                 */
    { "SCD", 20 }  /* Sega CD / Mega-CD          */
};

#define MANUAL_MAP_COUNT ((int)(sizeof(manual_map) / sizeof(manual_map[0])))

/* ------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------ */

/* Case-insensitive compare. Written out rather than using strcasecmp,
 * which is POSIX rather than standard C and is not always declared. */
static int equals_ignore_case(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

/* Read a whole file into a NUL-terminated buffer the caller must free. */
static char *read_whole_file(const char *path, long *out_size)
{
    FILE *f;
    char *buffer;
    long  size;
    size_t got;

    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    buffer = malloc((size_t)size + 1);
    if (buffer == NULL) {
        fclose(f);
        return NULL;
    }
    got = fread(buffer, 1, (size_t)size, f);
    fclose(f);

    buffer[got] = '\0';
    if (out_size != NULL) {
        *out_size = (long)got;
    }
    return buffer;
}

/* Copy the text a token covers into a fresh string. */
static char *token_to_string(const char *json, const jsmntok_t *tok)
{
    size_t len = (size_t)(tok->end - tok->start);
    char  *s   = malloc(len + 1);

    if (s == NULL) {
        return NULL;
    }
    memcpy(s, json + tok->start, len);
    s[len] = '\0';
    return s;
}

/*
 * How many tokens the value at index i occupies, including everything
 * nested inside it. This is what lets us skip over a key we do not care
 * about without losing our place in the flat token array.
 */
static int subtree_size(const jsmntok_t *tokens, int i)
{
    int n = 1;
    int k;

    if (tokens[i].type == JSMN_OBJECT || tokens[i].type == JSMN_ARRAY) {
        for (k = 0; k < tokens[i].size; k++) {
            n += subtree_size(tokens, i + n);
        }
    } else if (tokens[i].type == JSMN_STRING && tokens[i].size == 1) {
        /* An object key: its value follows immediately. */
        n += subtree_size(tokens, i + n);
    }
    return n;
}

/* Does a string token equal this literal? */
static int token_is(const char *json, const jsmntok_t *tok, const char *name)
{
    size_t len = (size_t)(tok->end - tok->start);

    return (tok->type == JSMN_STRING && strlen(name) == len &&
            strncmp(json + tok->start, name, len) == 0);
}

/* ------------------------------------------------------------------
 * Growing the system list
 * ------------------------------------------------------------------ */

static int systems_reserve(SystemList *l)
{
    System *grown;
    int     new_cap;

    if (l->count < l->capacity) {
        return 0;
    }
    new_cap = (l->capacity == 0) ? 32 : l->capacity * 2;
    grown   = realloc(l->items, (size_t)new_cap * sizeof(System));
    if (grown == NULL) {
        return -1;
    }
    l->items    = grown;
    l->capacity = new_cap;
    return 0;
}

/* ------------------------------------------------------------------
 * Loading
 * ------------------------------------------------------------------ */

int systems_load(const char *path, SystemList *out)
{
    char        *json;
    long         size;
    jsmn_parser  parser;
    jsmntok_t   *tokens;
    int          token_total;
    int          i;
    int          result = 0;

    if (out == NULL) {
        return -1;
    }
    out->items    = NULL;
    out->count    = 0;
    out->capacity = 0;

    if (path == NULL) {
        return -1;
    }

    json = read_whole_file(path, &size);
    if (json == NULL) {
        return -1; /* missing or unreadable — run gera_cache_sistemas */
    }

    /* First pass with a NULL token array only counts what is needed, so we
     * allocate exactly once instead of guessing. */
    jsmn_init(&parser);
    token_total = jsmn_parse(&parser, json, (size_t)size, NULL, 0);
    if (token_total < 1) {
        free(json);
        return -2; /* present but malformed */
    }

    tokens = malloc((size_t)token_total * sizeof(jsmntok_t));
    if (tokens == NULL) {
        free(json);
        return -2;
    }

    jsmn_init(&parser);
    if (jsmn_parse(&parser, json, (size_t)size, tokens, (unsigned int)token_total) < 1 ||
        tokens[0].type != JSMN_ARRAY) {
        free(tokens);
        free(json);
        return -2;
    }

    /* tokens[0] is the top-level array; its first element starts at 1. */
    i = 1;
    {
        int remaining = tokens[0].size;

        while (remaining > 0 && i < token_total) {
            int     object_index = i;
            int     keys;
            int     k;
            System  sys;

            if (tokens[object_index].type != JSMN_OBJECT) {
                i += subtree_size(tokens, i);
                remaining--;
                continue;
            }

            sys.id         = 0;
            sys.name       = NULL;
            sys.extensions = NULL;
            strlist_init(&sys.aliases);

            keys = tokens[object_index].size;
            i    = object_index + 1;

            for (k = 0; k < keys; k++) {
                int key_index   = i;
                int value_index = i + 1;

                if (token_is(json, &tokens[key_index], "id")) {
                    char *raw = token_to_string(json, &tokens[value_index]);
                    if (raw != NULL) {
                        /* atoi copes with the id arriving as 3 or as "3",
                         * which is the same reason the Go port used
                         * json.Number here. */
                        sys.id = atoi(raw);
                        free(raw);
                    }
                } else if (token_is(json, &tokens[key_index], "nome")) {
                    sys.name = token_to_string(json, &tokens[value_index]);
                } else if (token_is(json, &tokens[key_index], "extensions")) {
                    sys.extensions = token_to_string(json, &tokens[value_index]);
                } else if (token_is(json, &tokens[key_index], "apelidos") &&
                           tokens[value_index].type == JSMN_ARRAY) {
                    int alias_count = tokens[value_index].size;
                    int a;
                    int at = value_index + 1;

                    for (a = 0; a < alias_count; a++) {
                        char *alias = token_to_string(json, &tokens[at]);
                        if (alias != NULL) {
                            strlist_add(&sys.aliases, alias);
                            free(alias);
                        }
                        at += subtree_size(tokens, at);
                    }
                }

                i += subtree_size(tokens, key_index);
            }

            if (systems_reserve(out) != 0) {
                free(sys.name);
                free(sys.extensions);
                strlist_free(&sys.aliases);
                result = -2;
                break;
            }
            out->items[out->count] = sys;
            out->count++;

            remaining--;
        }
    }

    free(tokens);
    free(json);
    return result;
}

void systems_free(SystemList *l)
{
    int i;

    if (l == NULL) {
        return;
    }
    for (i = 0; i < l->count; i++) {
        free(l->items[i].name);
        free(l->items[i].extensions);
        strlist_free(&l->items[i].aliases);
    }
    free(l->items);
    l->items    = NULL;
    l->count    = 0;
    l->capacity = 0;
}

/* ------------------------------------------------------------------
 * Lookup and resolution
 * ------------------------------------------------------------------ */

const System *systems_find_by_id(const SystemList *l, int id)
{
    int i;

    if (l == NULL) {
        return NULL;
    }
    for (i = 0; i < l->count; i++) {
        if (l->items[i].id == id) {
            return &l->items[i];
        }
    }
    return NULL;
}

int systems_resolve_tag(const SystemList *l, const char *tag, int *out_id,
                        char *out_name, size_t out_name_size)
{
    int i, j;

    if (out_name != NULL && out_name_size > 0) {
        out_name[0] = '\0';
    }
    if (tag == NULL || tag[0] == '\0') {
        return 0;
    }

    /* Layer 1: the manual map wins. */
    for (i = 0; i < MANUAL_MAP_COUNT; i++) {
        if (equals_ignore_case(manual_map[i].tag, tag)) {
            const System *sys = systems_find_by_id(l, manual_map[i].system_id);

            if (out_id != NULL) {
                *out_id = manual_map[i].system_id;
            }
            if (out_name != NULL && out_name_size > 0) {
                /* Prefer the real name from the cache; fall back to the tag
                 * so the manual map still works with no sistemas.json. */
                const char *display = (sys != NULL && sys->name != NULL) ? sys->name : tag;
                strncpy(out_name, display, out_name_size - 1);
                out_name[out_name_size - 1] = '\0';
            }
            return 1;
        }
    }

    /* Layer 2: match the tag against every alias. */
    if (l == NULL) {
        return 0;
    }
    for (i = 0; i < l->count; i++) {
        for (j = 0; j < l->items[i].aliases.count; j++) {
            if (equals_ignore_case(l->items[i].aliases.items[j], tag)) {
                if (out_id != NULL) {
                    *out_id = l->items[i].id;
                }
                if (out_name != NULL && out_name_size > 0 && l->items[i].name != NULL) {
                    strncpy(out_name, l->items[i].name, out_name_size - 1);
                    out_name[out_name_size - 1] = '\0';
                }
                return 1;
            }
        }
    }

    return 0;
}

int systems_extensions(const SystemList *l, int id, StrList *out)
{
    const System *sys;

    if (out == NULL) {
        return -1;
    }
    strlist_init(out);

    sys = systems_find_by_id(l, id);
    if (sys == NULL || sys->extensions == NULL) {
        return 0; /* unknown: empty list means accept everything */
    }
    return parse_extensions(sys->extensions, out);
}
