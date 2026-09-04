/*
 * sshttp.c — the libcurl transport.
 */

/* Asks for the default set of declarations — POSIX plus the usual
 * extensions — so both nanosleep and snprintf are visible. Pinning a
 * specific POSIX revision here hides snprintf on macOS.
 * Must come before any system header. */
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <curl/curl.h>

#include "config.h"
#include "sshttp.h"

#define SS_ENDPOINT "https://api.screenscraper.fr/api2/jeuInfos.php"

/* ------------------------------------------------------------------
 * Growing buffer for a response held in memory
 * ------------------------------------------------------------------ */

typedef struct {
    char  *data;
    size_t len;
    size_t capacity;
} MemoryBuffer;

static size_t write_to_memory(void *chunk, size_t size, size_t nmemb, void *user)
{
    size_t        incoming = size * nmemb;
    MemoryBuffer *buf      = (MemoryBuffer *)user;

    if (buf->len + incoming + 1 > buf->capacity) {
        size_t new_cap = (buf->capacity == 0) ? 16384 : buf->capacity;
        char  *grown;

        while (new_cap < buf->len + incoming + 1) {
            new_cap *= 2;
        }
        grown = realloc(buf->data, new_cap);
        if (grown == NULL) {
            return 0; /* tells libcurl to abort the transfer */
        }
        buf->data     = grown;
        buf->capacity = new_cap;
    }

    memcpy(buf->data + buf->len, chunk, incoming);
    buf->len += incoming;
    buf->data[buf->len] = '\0';
    return incoming;
}

/* ------------------------------------------------------------------
 * Setup
 * ------------------------------------------------------------------ */

int ss_http_init(void)
{
    return (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) ? 0 : -1;
}

void ss_http_cleanup(void)
{
    curl_global_cleanup();
}

/* Point libcurl at the bundle we ship, but only when it is actually there.
 * On a desktop the system store works fine and the shipped bundle may not
 * be next to the binary; on the device there is no system store at all. */
static void apply_ca_bundle(CURL *curl)
{
    FILE *f = fopen(LEV_CA_BUNDLE, "rb");

    if (f != NULL) {
        fclose(f);
        curl_easy_setopt(curl, CURLOPT_CAINFO, LEV_CA_BUNDLE);
    }
}

static void set_common_options(CURL *curl)
{
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)LEV_HTTP_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, SS_SOFT_NAME);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    apply_ca_bundle(curl);
}

static void set_error(char *err, size_t err_size, const char *message)
{
    if (err != NULL && err_size > 0) {
        strncpy(err, message, err_size - 1);
        err[err_size - 1] = '\0';
    }
}

/* ------------------------------------------------------------------
 * Looking a game up
 * ------------------------------------------------------------------ */

ScrapeStatus ss_fetch_game(const RomHashes *hashes, int system_id, SsGame *out,
                           char *err, size_t err_size)
{
    CURL         *curl;
    CURLcode      code;
    MemoryBuffer  buf;
    char          url[2048];
    char         *escaped_soft;
    long          http_code = 0;
    ScrapeStatus  status;

    if (hashes == NULL || out == NULL) {
        set_error(err, err_size, "bad arguments");
        return SCRAPE_IO_ERROR;
    }
    memset(out, 0, sizeof(*out));

    curl = curl_easy_init();
    if (curl == NULL) {
        set_error(err, err_size, "could not initialise libcurl");
        return SCRAPE_NET_ERROR;
    }

    /* softname carries a space, so it has to be escaped. The credentials are
     * escaped too: a password may legitimately contain & or =. */
    escaped_soft = curl_easy_escape(curl, SS_SOFT_NAME, 0);

    snprintf(url, sizeof(url),
             "%s?devid=%s&devpassword=%s&ssid=%s&sspassword=%s"
             "&softname=%s&output=json&systemeid=%d&crc=%s&md5=%s",
             SS_ENDPOINT, SS_DEV_ID, SS_DEV_PASSWORD, SS_USER_ID, SS_USER_PASSWORD,
             (escaped_soft != NULL) ? escaped_soft : "Leviathan",
             system_id, hashes->crc, hashes->md5);

    if (escaped_soft != NULL) {
        curl_free(escaped_soft);
    }

    buf.data     = NULL;
    buf.len      = 0;
    buf.capacity = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);
    set_common_options(curl);

    code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        set_error(err, err_size, curl_easy_strerror(code));
        free(buf.data);
        return SCRAPE_NET_ERROR;
    }

    /* ScreenScraper signals trouble through the status code, so read it
     * before bothering to parse the body. */
    switch (http_code) {
    case 200:
        break;
    case 401:
    case 403:
        set_error(err, err_size, "credentials rejected");
        free(buf.data);
        return SCRAPE_AUTH_ERROR;
    case 404:
        set_error(err, err_size, "hash not in the database");
        free(buf.data);
        return SCRAPE_NOT_FOUND;
    case 429:
    case 430:
    case 431:
        set_error(err, err_size, "daily quota reached");
        free(buf.data);
        return SCRAPE_QUOTA;
    default: {
        char detail[64];
        snprintf(detail, sizeof(detail), "unexpected HTTP %ld", http_code);
        set_error(err, err_size, detail);
        free(buf.data);
        return SCRAPE_NET_ERROR;
    }
    }

    switch (ss_parse_game(buf.data, buf.len, out)) {
    case SS_PARSE_OK:
        status = SCRAPE_OK;
        break;
    case SS_PARSE_NO_GAME:
        set_error(err, err_size, "reply carried no game");
        status = SCRAPE_NOT_FOUND;
        break;
    default:
        set_error(err, err_size, "could not read the reply");
        status = SCRAPE_NET_ERROR;
        break;
    }

    free(buf.data);
    return status;
}

/* ------------------------------------------------------------------
 * Downloading a cover
 * ------------------------------------------------------------------ */

/* mkdir -p. Walks the path creating each level, ignoring "already there". */
static int ensure_parent_directories(const char *file_path)
{
    char   work[4096];
    size_t i;

    strncpy(work, file_path, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    for (i = 1; work[i] != '\0'; i++) {
        if (work[i] == '/') {
            work[i] = '\0';
            if (mkdir(work, 0755) != 0) {
                struct stat st;
                if (stat(work, &st) != 0 || !S_ISDIR(st.st_mode)) {
                    return -1;
                }
            }
            work[i] = '/';
        }
    }
    return 0;
}

ScrapeStatus ss_download_cover(const SsMedia *media, const char *dest_path,
                               long *out_size, char *err, size_t err_size)
{
    CURL    *curl;
    CURLcode code;
    FILE    *file;
    long     http_code = 0;
    long     written;

    if (out_size != NULL) {
        *out_size = 0;
    }
    if (media == NULL || media->url == NULL || dest_path == NULL) {
        set_error(err, err_size, "bad arguments");
        return SCRAPE_IO_ERROR;
    }

    if (ensure_parent_directories(dest_path) != 0) {
        set_error(err, err_size, "could not create the output folder");
        return SCRAPE_IO_ERROR;
    }

    /* Write to a temporary name and rename on success, so an interrupted
     * download never leaves a half-written PNG that later runs would skip
     * as "already done". */
    {
        char temp_path[4096];
        snprintf(temp_path, sizeof(temp_path), "%s.part", dest_path);

        file = fopen(temp_path, "wb");
        if (file == NULL) {
            set_error(err, err_size, "could not open the output file");
            return SCRAPE_IO_ERROR;
        }

        curl = curl_easy_init();
        if (curl == NULL) {
            fclose(file);
            remove(temp_path);
            set_error(err, err_size, "could not initialise libcurl");
            return SCRAPE_NET_ERROR;
        }

        curl_easy_setopt(curl, CURLOPT_URL, media->url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)file);
        set_common_options(curl);

        code = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);

        written = ftell(file);
        fclose(file);

        if (code != CURLE_OK) {
            set_error(err, err_size, curl_easy_strerror(code));
            remove(temp_path);
            return SCRAPE_NET_ERROR;
        }
        if (http_code != 200 || written <= 0) {
            char detail[64];
            snprintf(detail, sizeof(detail), "cover unavailable (HTTP %ld)", http_code);
            set_error(err, err_size, detail);
            remove(temp_path);
            return SCRAPE_NET_ERROR;
        }

        remove(dest_path); /* rename fails on some systems if the target exists */
        if (rename(temp_path, dest_path) != 0) {
            set_error(err, err_size, "could not move the finished file");
            remove(temp_path);
            return SCRAPE_IO_ERROR;
        }
    }

    if (out_size != NULL) {
        *out_size = written;
    }
    return SCRAPE_OK;
}

/* ------------------------------------------------------------------
 * Hiding credentials
 * ------------------------------------------------------------------ */

/* Replace the value of one query parameter with asterisks, in place. */
static void mask_parameter(char *url, const char *key)
{
    char  needle[64];
    char *found;

    snprintf(needle, sizeof(needle), "%s=", key);
    found = strstr(url, needle);
    if (found == NULL) {
        return;
    }

    found += strlen(needle);
    while (*found != '\0' && *found != '&') {
        *found = '*';
        found++;
    }
}

void ss_mask_credentials(const char *url, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (url == NULL) {
        return;
    }

    strncpy(out, url, out_size - 1);
    out[out_size - 1] = '\0';

    mask_parameter(out, "devpassword");
    mask_parameter(out, "sspassword");
    mask_parameter(out, "devid");
    mask_parameter(out, "ssid");
}

/* ------------------------------------------------------------------
 * Pacing
 * ------------------------------------------------------------------ */

void ss_pause_between_requests(void)
{
#if LEV_REQUEST_PAUSE_MS > 0
    struct timespec pause;

    pause.tv_sec  = LEV_REQUEST_PAUSE_MS / 1000;
    pause.tv_nsec = (long)(LEV_REQUEST_PAUSE_MS % 1000) * 1000000L;
    nanosleep(&pause, NULL);
#endif
}
