/*
 * test_credentials.c — proving the credential parser on its own.
 *
 * Stands where test_ssparse stands: no network, no SDL, no device. It writes
 * small credential files to a temp directory, reads them back, and checks that
 * each one yields the ssid, sspassword and — above all — the complete verdict
 * the rest of the app will branch on.
 *
 *   cc -std=c99 -Wall -Wextra -o test_credentials \
 *       test_credentials.c credentials.c
 *   ./test_credentials
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "credentials.h"

static int failures = 0;

/* Write text to a path, aborting the whole test run if even that fails —
 * a test that cannot set up its own fixture proves nothing. */
static void write_file(const char *path, const char *contents)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "could not create fixture %s\n", path);
        exit(2);
    }
    fputs(contents, f);
    fclose(f);
}

/* One case: write the file, load it, and check all three outcomes against
 * what we expected. A NULL expected string means "we do not care about this
 * field" (used when complete is 0 and the values are irrelevant). */
static void check(const char *label, const char *file_contents,
                  int expect_complete, const char *expect_ssid,
                  const char *expect_pass)
{
    const char     *path = "test_credentials.tmp";
    UserCredentials cred;
    int             ok = 1;

    write_file(path, file_contents);
    credentials_load(path, &cred);
    remove(path);

    if (cred.complete != expect_complete) {
        ok = 0;
    }
    if (expect_ssid != NULL && strcmp(cred.ssid, expect_ssid) != 0) {
        ok = 0;
    }
    if (expect_pass != NULL && strcmp(cred.sspassword, expect_pass) != 0) {
        ok = 0;
    }

    if (ok) {
        printf("  ok    %s\n", label);
    } else {
        failures++;
        printf("  FAIL  %s\n", label);
        printf("        complete: got %d, wanted %d\n", cred.complete, expect_complete);
        printf("        ssid:     got '%s'\n", cred.ssid);
        printf("        password: got '%s'\n", cred.sspassword);
    }
}

int main(void)
{
    UserCredentials cred;

    puts("\ncredential parser");
    puts("--------------------------------------------------------------");

    /* The happy path: both fields present. */
    check("both fields present",
          "ssid=bob\nsspassword=secret\n",
          1, "bob", "secret");

    /* Comments and blank lines are ignored; the real values still come through.
     * This is the shape of the file that ships with the app. */
    check("comments and blank lines ignored",
          "# Leviathan Scraper credentials\n"
          "# ssid       = your username\n"
          "# sspassword = your password\n"
          "\n"
          "ssid=alice\n"
          "sspassword=hunter2\n",
          1, "alice", "hunter2");

    /* Whitespace around key and value is trimmed. */
    check("whitespace around = is trimmed",
          "  ssid  =  bob  \n  sspassword = secret \n",
          1, "bob", "secret");

    /* Only the username filled in: not usable, must read as incomplete. This is
     * the case the "bar early" decision is really about. */
    check("password missing -> incomplete",
          "ssid=bob\n",
          0, "bob", "");

    /* Only the password filled in: equally unusable. */
    check("ssid missing -> incomplete",
          "sspassword=secret\n",
          0, "", "secret");

    /* A key present but its value blank counts as not set. */
    check("blank value -> incomplete",
          "ssid=bob\nsspassword=\n",
          0, "bob", "");

    /* Nothing but comments: the untouched, unedited template. Must be
     * incomplete, and is the most common first-run state. */
    check("comments only -> incomplete",
          "# edit the two lines below\n# ssid=\n# sspassword=\n",
          0, "", "");

    /* Unknown keys are ignored without disturbing the ones we want. */
    check("unknown keys ignored",
          "region=us\nssid=bob\nfavourite=zelda\nsspassword=secret\n",
          1, "bob", "secret");

    /* The unedited template: both values still the shipped placeholders. Must
     * read as incomplete, so an unedited file never reaches the API. */
    check("untouched placeholders -> incomplete",
          "ssid=" LEV_CRED_PLACEHOLDER_SSID "\n"
          "sspassword=" LEV_CRED_PLACEHOLDER_PASS "\n",
          0, "", "");

    /* One line edited, the other still the placeholder: still incomplete,
     * because the placeholder half is treated as not set. */
    check("one real, one placeholder -> incomplete",
          "ssid=bob\nsspassword=" LEV_CRED_PLACEHOLDER_PASS "\n",
          0, "bob", "");

    /* A completely missing file: load returns -1, but the verdict we act on is
     * still just complete == 0. Checked directly here since check() writes a
     * file every time. */
    {
        int rc = credentials_load("this_file_does_not_exist.tmp", &cred);
        if (rc == -1 && cred.complete == 0) {
            puts("  ok    missing file -> incomplete");
        } else {
            failures++;
            printf("  FAIL  missing file: rc=%d complete=%d\n", rc, cred.complete);
        }
    }

    puts("--------------------------------------------------------------");
    if (failures == 0) {
        puts("all passed\n");
        return 0;
    }
    printf("%d FAILED\n\n", failures);
    return 1;
}
