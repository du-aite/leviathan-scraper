/*
 * romscan.c — filesystem walking for the Leviathan Scraper core.
 */

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "romscan.h"

/* ------------------------------------------------------------------
 * Small local helpers
 * ------------------------------------------------------------------ */

/* Uppercase in place, ASCII only. Casting to unsigned char first is not
 * pedantry: toupper() on a negative char is undefined behaviour, and any
 * byte above 127 (an accented letter in a folder name) is negative on
 * platforms where char is signed. */
static void to_upper_ascii(char *s)
{
    for (; *s != '\0'; s++) {
        *s = (char)toupper((unsigned char)*s);
    }
}

static void to_lower_ascii(char *s)
{
    for (; *s != '\0'; s++) {
        *s = (char)tolower((unsigned char)*s);
    }
}

/* Strip leading and trailing whitespace in place. */
static void trim(char *s)
{
    char  *start = s;
    size_t len;

    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }

    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

/* Pointer to the extension (including the dot), or NULL if there is none.
 * A leading dot does not count as an extension separator here because we
 * never look at dotfiles anyway. */
static const char *file_extension(const char *name)
{
    const char *dot = strrchr(name, '.');

    if (dot == NULL || dot == name) {
        return NULL;
    }
    return dot;
}

/* Join a directory and an entry name into out. Returns 0 on success, -1 if
 * the result would not fit — checked rather than truncated, because a
 * silently shortened path would open the wrong file. */
static int join_path(char *out, size_t out_size, const char *dir, const char *name)
{
    size_t need = strlen(dir) + 1 + strlen(name) + 1;

    if (need > out_size) {
        return -1;
    }
    sprintf(out, "%s/%s", dir, name);
    return 0;
}

/* 1 if path is a directory. Uses stat instead of dirent's d_type because
 * d_type is not filled in on every filesystem — notably it can come back
 * as DT_UNKNOWN on the SD card formats the Brick uses. */
static int is_directory(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

/* ------------------------------------------------------------------
 * Tag extraction
 * ------------------------------------------------------------------ */

void extract_tag(const char *folder_name, char *out, size_t out_size)
{
    char   work[512];
    char  *open_paren;
    size_t len;

    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (folder_name == NULL) {
        return;
    }

    /* Work on a bounded copy so a monstrous folder name cannot overrun us. */
    strncpy(work, folder_name, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';
    trim(work);

    len = strlen(work);

    /* Parenthesised suffix wins: "Game Boy Advance (GBA)" -> "GBA".
     * strrchr finds the LAST '(' so a name like "Sega CD (Mega-CD) (SCD)"
     * still resolves to the trailing tag. */
    if (len >= 2 && work[len - 1] == ')' && (open_paren = strrchr(work, '(')) != NULL) {
        size_t inner_len = (size_t)(&work[len - 1] - open_paren - 1);
        char   inner[512];

        memcpy(inner, open_paren + 1, inner_len);
        inner[inner_len] = '\0';
        trim(inner);

        /* An empty "()" is not a tag — fall through to using the whole name. */
        if (inner[0] != '\0') {
            strncpy(out, inner, out_size - 1);
            out[out_size - 1] = '\0';
            to_upper_ascii(out);
            return;
        }
    }

    strncpy(out, work, out_size - 1);
    out[out_size - 1] = '\0';
    to_upper_ascii(out);
}

/* ------------------------------------------------------------------
 * Directory listing
 * ------------------------------------------------------------------ */

int list_subdirs(const char *path, StrList *out)
{
    DIR           *dir;
    struct dirent *entry;
    char           full[4096];

    if (path == NULL || out == NULL) {
        return -1;
    }
    strlist_init(out);

    dir = opendir(path);
    if (dir == NULL) {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        /* Skips ".", ".." and hidden entries in one stroke — which also
         * keeps macOS metadata like .DS_Store out of the results. */
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (join_path(full, sizeof(full), path, entry->d_name) != 0) {
            continue;
        }
        if (!is_directory(full)) {
            continue;
        }
        if (strlist_add(out, entry->d_name) != 0) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);
    strlist_sort(out);
    return 0;
}

int list_roms(const char *path, const StrList *exts, StrList *out)
{
    DIR           *dir;
    struct dirent *entry;
    char           full[4096];

    if (path == NULL || out == NULL) {
        return -1;
    }
    strlist_init(out);

    dir = opendir(path);
    if (dir == NULL) {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        const char *ext;
        char        lowered[64];

        if (entry->d_name[0] == '.') {
            continue;
        }
        if (join_path(full, sizeof(full), path, entry->d_name) != 0) {
            continue;
        }
        if (is_directory(full)) {
            continue;
        }

        /* No extension list means we do not know this system: take everything,
         * exactly as the Go version did. */
        if (exts != NULL && exts->count > 0) {
            ext = file_extension(entry->d_name);
            if (ext == NULL) {
                continue;
            }
            if (strlen(ext) >= sizeof(lowered)) {
                continue; /* absurd extension, not a ROM */
            }
            strcpy(lowered, ext);
            to_lower_ascii(lowered);
            if (!strlist_contains(exts, lowered)) {
                continue;
            }
        }

        if (strlist_add(out, entry->d_name) != 0) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);
    strlist_sort(out);
    return 0;
}

/* ------------------------------------------------------------------
 * Extension strings
 * ------------------------------------------------------------------ */

int parse_extensions(const char *csv, StrList *out)
{
    const char *p;
    char        piece[64];
    char        dotted[66];

    if (out == NULL) {
        return -1;
    }
    strlist_init(out);
    if (csv == NULL) {
        return 0;
    }

    p = csv;
    while (*p != '\0') {
        const char *comma = strchr(p, ',');
        size_t      len   = (comma != NULL) ? (size_t)(comma - p) : strlen(p);

        if (len < sizeof(piece)) {
            memcpy(piece, p, len);
            piece[len] = '\0';
            trim(piece);
            to_lower_ascii(piece);

            if (piece[0] != '\0') {
                /* Tolerate values that already carry a dot. */
                if (piece[0] == '.') {
                    strcpy(dotted, piece);
                } else {
                    sprintf(dotted, ".%s", piece);
                }
                if (strlist_add(out, dotted) != 0) {
                    return -1;
                }
            }
        }

        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }

    return 0;
}
