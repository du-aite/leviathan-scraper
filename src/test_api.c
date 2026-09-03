/*
 * test_api.c — one real call to ScreenScraper, end to end.
 *
 * Hashes a single ROM, looks it up, prints what came back and optionally
 * saves the cover. Deliberately one ROM at a time: this is the step where
 * mistakes cost API quota.
 *
 *   cp config.h.example config.h   (then fill in the credentials)
 *   gcc -std=c99 -Wall -Wextra -o test_api \
 *       test_api.c sshttp.c ssparse.c romhash.c md5.c romscan.c strlist.c \
 *       systems.c -lcurl
 *
 *   ./test_api <sistemas.json> <rom file> <TAG> [--save]
 *
 * Example:
 *   ./test_api ../assets/sistemas.json "/roms/FC/Contra (USA).nes" FC --save
 */

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "romhash.h"
#include "romscan.h"
#include "sshttp.h"
#include "ssparse.h"
#include "status.h"
#include "systems.h"

static const char *status_text(ScrapeStatus s)
{
    switch (s) {
    case SCRAPE_OK:         return "OK";
    case SCRAPE_SKIPPED:    return "skipped";
    case SCRAPE_NO_COVER:   return "no cover of that type";
    case SCRAPE_NOT_FOUND:  return "not in the database";
    case SCRAPE_NET_ERROR:  return "network error";
    case SCRAPE_IO_ERROR:   return "disk error";
    case SCRAPE_AUTH_ERROR: return "credentials rejected";
    case SCRAPE_QUOTA:      return "daily quota reached";
    default:                return "?";
    }
}

int main(int argc, char **argv)
{
    SystemList   systems;
    RomHashes    hashes;
    SsGame       game;
    ScrapeStatus status;
    char         err[256] = "";
    char         name[LEV_SYSTEM_NAME_LEN];
    int          system_id = 0;
    int          save = 0;
    int          i;

    if (argc < 4) {
        puts("\nusage: test_api <sistemas.json> <rom file> <TAG> [--save]\n");
        return 1;
    }
    for (i = 4; i < argc; i++) {
        if (strcmp(argv[i], "--save") == 0) {
            save = 1;
        }
    }

    if (SS_DEV_ID[0] == '\0' || SS_USER_ID[0] == '\0') {
        puts("\nconfig.h has no credentials yet."
             "\ncopy config.h.example to config.h and fill them in.\n");
        return 1;
    }

    putchar('\n');

    if (systems_load(argv[1], &systems) < 0) {
        printf("could not load %s\n\n", argv[1]);
        return 1;
    }

    if (!systems_resolve_tag(&systems, argv[3], &system_id, name, sizeof(name))) {
        printf("tag '%s' not recognised\n\n", argv[3]);
        systems_free(&systems);
        return 1;
    }
    printf("system   %s -> id %d (%s)\n", argv[3], system_id, name);

    if (rom_hash_file(argv[2], &hashes) != 0) {
        printf("could not read %s\n\n", argv[2]);
        systems_free(&systems);
        return 1;
    }
    printf("rom      %s\n", argv[2]);
    printf("size     %ld bytes\n", hashes.size);
    printf("crc      %s\n", hashes.crc);
    printf("md5      %s\n\n", hashes.md5);

    if (ss_http_init() != 0) {
        puts("could not start libcurl\n");
        systems_free(&systems);
        return 1;
    }

    puts("asking ScreenScraper...");
    status = ss_fetch_game(&hashes, system_id, &game, err, sizeof(err));
    printf("result   %s%s%s\n\n", status_text(status),
           err[0] ? " — " : "", err);

    if (status == SCRAPE_OK) {
        const SsMedia *cover;

        printf("game     %s\n", game.name);
        printf("medias   %d\n", game.media_count);

        for (i = 0; i < game.media_count; i++) {
            if (strcmp(game.medias[i].type, LEV_MEDIA_TYPE) == 0) {
                printf("           %-8s %s\n", game.medias[i].region, game.medias[i].url);
            }
        }

        cover = ss_choose_cover(&game, LEV_MEDIA_TYPE, LEV_PREFERRED_REGION);
        if (cover == NULL) {
            printf("\nno %s cover for this game\n", LEV_MEDIA_TYPE);
        } else {
            printf("\nchosen   region %s\n", cover->region);

            if (save) {
                char base[512];
                char dest[1024];
                char *dot;

                /* Imgs/<TAG>/<rom name without extension>.png */
                strncpy(base, strrchr(argv[2], '/') ? strrchr(argv[2], '/') + 1 : argv[2],
                        sizeof(base) - 1);
                base[sizeof(base) - 1] = '\0';
                dot = strrchr(base, '.');
                if (dot != NULL) {
                    *dot = '\0';
                }
                snprintf(dest, sizeof(dest), "%s/%s/%s.png", LEV_OUTPUT_DIR, argv[3], base);

                {
                    long size = 0;
                    ScrapeStatus d = ss_download_cover(cover, dest, &size, err, sizeof(err));
                    if (d == SCRAPE_OK) {
                        printf("saved    %s (%.1f KB)\n", dest, (double)size / 1024.0);
                    } else {
                        printf("download failed: %s — %s\n", status_text(d), err);
                    }
                }
            } else {
                puts("(pass --save to download it)");
            }
        }
        ss_game_free(&game);
    }

    putchar('\n');
    ss_http_cleanup();
    systems_free(&systems);
    return 0;
}
