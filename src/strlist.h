/*
 * strlist.h — a growable array of strings.
 *
 * C has no []string, so this is the building block that replaces every
 * Go slice of strings in the port. It owns its strings: strlist_add()
 * copies what you give it, and strlist_free() releases everything.
 */

#ifndef LEVIATHAN_STRLIST_H
#define LEVIATHAN_STRLIST_H

#include <stddef.h>

typedef struct {
    char **items;
    int    count;    /* how many strings are in use  */
    int    capacity; /* how many slots are allocated */
} StrList;

/* Prepare an empty list. Always call this first; a stack struct is junk. */
void strlist_init(StrList *l);

/* Append a COPY of s. Returns 0 on success, -1 if memory ran out. */
int strlist_add(StrList *l, const char *s);

/* Byte-wise ascending sort, same ordering Go's sort.Strings produces. */
void strlist_sort(StrList *l);

/* 1 if s is present (exact, case-sensitive match), 0 otherwise. */
int strlist_contains(const StrList *l, const char *s);

/* Release every string and the array itself, leaving an empty list. */
void strlist_free(StrList *l);

#endif /* LEVIATHAN_STRLIST_H */
