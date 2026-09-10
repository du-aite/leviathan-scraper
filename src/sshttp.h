/*
 * sshttp.h — talking to ScreenScraper over HTTPS.
 *
 * Metadata & artwork by ScreenScraper.fr — https://www.screenscraper.fr
 *
 * This is the only file that knows libcurl exists. Everything above it deals
 * in games and covers; everything below is transport.
 *
 * The device has no system certificate store, so the CA bundle we ship is
 * handed to libcurl explicitly. Without that, every request fails
 * certificate verification on the Brick while working fine on a Mac.
 */

#ifndef LEVIATHAN_SSHTTP_H
#define LEVIATHAN_SSHTTP_H

#include <stddef.h>

#include "romhash.h"
#include "ssparse.h"
#include "status.h"

/* Call once at startup and once at shutdown. libcurl needs global setup
 * that is not thread-safe, so it cannot be done lazily per request. */
int  ss_http_init(void);
void ss_http_cleanup(void);

/*
 * Hand in the user's ScreenScraper login, read from the runtime credentials
 * file. Call once at startup, after loading the file and before the first
 * fetch. The dev credentials are compiled in; only this user pair is set here.
 * Passing NULL for either argument leaves that value unchanged.
 */
void ss_set_user_credentials(const char *ssid, const char *sspassword);

/*
 * Look a ROM up by its hashes.
 *
 * Returns SCRAPE_OK with out filled in, or one of:
 *   SCRAPE_NOT_FOUND  the hash is not in the database
 *   SCRAPE_AUTH_ERROR credentials rejected — a config problem, not a network one
 *   SCRAPE_QUOTA      the account's daily allowance is spent
 *   SCRAPE_NET_ERROR  connection, TLS, timeout, or an unreadable reply
 *
 * err receives a short human-readable detail and may be NULL.
 */
ScrapeStatus ss_fetch_game(const RomHashes *hashes, int system_id, SsGame *out,
                           char *err, size_t err_size);

/*
 * Download a cover to dest_path, creating parent directories as needed.
 * Returns SCRAPE_OK, SCRAPE_NET_ERROR or SCRAPE_IO_ERROR.
 */
ScrapeStatus ss_download_cover(const SsMedia *media, const char *dest_path,
                               long *out_size, char *err, size_t err_size);

/* Sleep between requests. ScreenScraper asks callers to pace themselves. */
void ss_pause_between_requests(void);

/*
 * Copy a URL with the credential values replaced by asterisks.
 *
 * ScreenScraper's media URLs carry devpassword and sspassword as query
 * parameters, so printing one raw puts the account on screen. Anything that
 * may end up in a log, on the device's display, or in a screenshot goes
 * through here first.
 */
void ss_mask_credentials(const char *url, char *out, size_t out_size);

#endif /* LEVIATHAN_SSHTTP_H */
