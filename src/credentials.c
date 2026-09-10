/*
 * credentials.c — parsing the user's login out of a key=value file.
 *
 * The whole job: open a small text file, walk it line by line, and pull the
 * values of the ssid and sspassword keys out of it. Everything else — blank
 * lines, comments, keys we do not recognise — is quietly ignored.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "credentials.h"

/* Longest single line we bother reading. A key=value pair here is tens of
 * characters; this leaves generous room and anything past it is truncated,
 * never overflowed. */
#define CRED_LINE_MAX 512

/* Strip leading and trailing whitespace in place. Same shape as the trim in
 * romscan.c — kept local rather than shared so this file stays self-contained
 * and testable on its own. Casting to unsigned char matters: isspace() on a
 * negative char is undefined, and bytes above 127 are negative where char is
 * signed. */
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

/* Copy src into a fixed field, always leaving it terminated. */
static void store(char *dest, const char *src)
{
    strncpy(dest, src, LEV_CRED_MAX - 1);
    dest[LEV_CRED_MAX - 1] = '\0';
}

int credentials_load(const char *path, UserCredentials *out)
{
    FILE *f;
    char  line[CRED_LINE_MAX];

    /* Start from a clean, valid state no matter what happens next. Even the
     * "file missing" return below leaves out fully usable. */
    if (out != NULL) {
        out->ssid[0]       = '\0';
        out->sspassword[0] = '\0';
        out->complete      = 0;
    }
    if (path == NULL || out == NULL) {
        return -1;
    }

    f = fopen(path, "rb");
    if (f == NULL) {
        return -1; /* missing file: complete stays 0, which is what matters */
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        char *equals;
        char *key;
        char *value;

        /* Drop a trailing newline so it does not ride along into the value.
         * fgets keeps it; we do not want it. */
        line[strcspn(line, "\r\n")] = '\0';

        trim(line);

        /* Skip blank lines and comments. A comment is any line whose first
         * non-space character is '#', which is why we trim first. */
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        /* No '=' means this is not a key/value line; ignore it rather than
         * guess at its meaning. */
        equals = strchr(line, '=');
        if (equals == NULL) {
            continue;
        }

        /* Split the line at the '=' into key and value, and trim each so
         * "ssid = bob" reads the same as "ssid=bob". */
        *equals = '\0';
        key     = line;
        value   = equals + 1;
        trim(key);
        trim(value);

        if (value[0] == '\0') {
            continue; /* key present but no value: treat as not set */
        }

        /* A value still equal to the shipped placeholder means the user never
         * edited that line. Leave the field empty so it reads as not set,
         * rather than sending "YOUR_SCREENSCRAPER_USERNAME" to the API and
         * spending an HTTP round trip to be told it is wrong. */
        if (strcmp(key, "ssid") == 0) {
            if (strcmp(value, LEV_CRED_PLACEHOLDER_SSID) != 0) {
                store(out->ssid, value);
            }
        } else if (strcmp(key, "sspassword") == 0) {
            if (strcmp(value, LEV_CRED_PLACEHOLDER_PASS) != 0) {
                store(out->sspassword, value);
            }
        }
        /* Any other key is deliberately ignored, so the file can carry extra
         * settings later without this parser needing to know about them. */
    }

    fclose(f);

    /* The single verdict the app acts on: usable only when BOTH are present.
     * One field alone is treated exactly like none — barring the scrape early
     * with a clear message beats spending an HTTP request to discover the
     * password was blank. */
    out->complete = (out->ssid[0] != '\0' && out->sspassword[0] != '\0') ? 1 : 0;

    return 0;
}
