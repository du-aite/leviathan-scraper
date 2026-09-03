/*
 * systems.h — the ScreenScraper system cache and layered tag resolution.
 *
 * Reads the distilled sistemas.json produced by gera_cache_sistemas, and
 * answers the one question the scraper keeps asking: given a folder tag
 * like "FC", which ScreenScraper system is that?
 *
 * Resolution happens in two layers, in this order:
 *   1. a manual map, for Trimui tags that no alias list would ever match
 *   2. the alias lists in sistemas.json
 *
 * The manual layer wins on purpose: it is where we override, and it lets
 * unknown future folders still resolve through layer 2 with no code change.
 */

#ifndef LEVIATHAN_SYSTEMS_H
#define LEVIATHAN_SYSTEMS_H

#include <stddef.h>

#include "strlist.h"

/* Comfortably above the longest real values in sistemas.json. */
#define LEV_SYSTEM_NAME_LEN 64

typedef struct {
    int     id;
    char   *name;       /* "Super Nintendo"        */
    StrList aliases;    /* every known spelling    */
    char   *extensions; /* raw csv: "sfc,smc,fig"  */
} System;

typedef struct {
    System *items;
    int     count;
    int     capacity;
} SystemList;

/*
 * Load and parse sistemas.json.
 *
 * Returns 0 on success. Returns -1 if the file is missing or unreadable and
 * -2 if it is present but malformed — the caller can tell "run the cache
 * generator" apart from "the cache is corrupt". On any failure the list is
 * left empty but valid, and resolution still works through the manual map
 * alone, which is what the Go version degraded to.
 */
int systems_load(const char *path, SystemList *out);

/* Release everything the list owns. */
void systems_free(SystemList *l);

/* The entry with this id, or NULL. */
const System *systems_find_by_id(const SystemList *l, int id);

/*
 * Resolve a folder tag to a ScreenScraper system id.
 *
 * out_name receives a display name and is always a valid string. Returns 1
 * when the tag was resolved, 0 when it was not — an unresolved tag is a
 * folder we skip, not an error.
 */
int systems_resolve_tag(const SystemList *l, const char *tag, int *out_id,
                        char *out_name, size_t out_name_size);

/*
 * Valid file extensions for a system, as a list of lowercase dotted strings
 * ready for list_roms(). Returns 0 on success. An empty list means "we do
 * not know", and list_roms treats that as accept-everything.
 */
int systems_extensions(const SystemList *l, int id, StrList *out);

#endif /* LEVIATHAN_SYSTEMS_H */
