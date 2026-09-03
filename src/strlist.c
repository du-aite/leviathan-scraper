/*
 * strlist.c — growable array of owned strings.
 */

#include <stdlib.h>
#include <string.h>

#include "strlist.h"

#define STRLIST_FIRST_CAPACITY 8

void strlist_init(StrList *l)
{
    if (l == NULL) {
        return;
    }
    l->items    = NULL;
    l->count    = 0;
    l->capacity = 0;
}

int strlist_add(StrList *l, const char *s)
{
    char *copy;

    if (l == NULL || s == NULL) {
        return -1;
    }

    /* Grow by doubling: the array is reallocated log2(n) times instead of
     * once per item, so adding N strings stays cheap. */
    if (l->count == l->capacity) {
        int    new_cap = (l->capacity == 0) ? STRLIST_FIRST_CAPACITY : l->capacity * 2;
        char **grown   = realloc(l->items, (size_t)new_cap * sizeof(char *));
        if (grown == NULL) {
            return -1; /* the old array is still valid and still ours */
        }
        l->items    = grown;
        l->capacity = new_cap;
    }

    copy = malloc(strlen(s) + 1);
    if (copy == NULL) {
        return -1;
    }
    strcpy(copy, s);

    l->items[l->count] = copy;
    l->count++;
    return 0;
}

static int compare_strings(const void *a, const void *b)
{
    const char *const *pa = (const char *const *)a;
    const char *const *pb = (const char *const *)b;
    return strcmp(*pa, *pb);
}

void strlist_sort(StrList *l)
{
    if (l == NULL || l->count < 2) {
        return;
    }
    qsort(l->items, (size_t)l->count, sizeof(char *), compare_strings);
}

int strlist_contains(const StrList *l, const char *s)
{
    int i;

    if (l == NULL || s == NULL) {
        return 0;
    }
    for (i = 0; i < l->count; i++) {
        if (strcmp(l->items[i], s) == 0) {
            return 1;
        }
    }
    return 0;
}

void strlist_free(StrList *l)
{
    int i;

    if (l == NULL) {
        return;
    }
    for (i = 0; i < l->count; i++) {
        free(l->items[i]);
    }
    free(l->items);
    strlist_init(l);
}
