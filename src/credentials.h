/*
 * credentials.h — reading the user's ScreenScraper login at runtime.
 *
 * Why this file exists at all: the developer credentials (devid, devpassword)
 * identify THIS program to the API and are the same for everyone, so they stay
 * compiled into the binary. But the user credentials (ssid, sspassword) belong
 * to each person's own ScreenScraper account. Compiling those in would mean a
 * recompile to change a password — impossible for anyone without the toolchain.
 *
 * So the user pair moves OUT of config.h and into a plain text file that sits
 * next to the binary, read once when the app starts. The user opens it in a
 * text editor, types their login, saves, and reopens the app. No compiler.
 *
 * This file knows nothing about curl or SDL: it opens a file, reads two values,
 * and hands them back. That keeps it testable on its own, the same way ssparse
 * is testable offline against saved replies.
 */

#ifndef LEVIATHAN_CREDENTIALS_H
#define LEVIATHAN_CREDENTIALS_H

/* Longest login we store. ScreenScraper usernames and passwords are short;
 * this is comfortably above anything real, and any value longer than this is
 * truncated rather than allowed to overflow. */
#define LEV_CRED_MAX 128

/* The placeholder values the shipped credentials.txt carries out of the box.
 * The most common first-run mistake is opening the app before editing the
 * file, leaving these untouched — so a value equal to its placeholder is
 * treated as not filled in, exactly like a blank one. Defining them here,
 * where both the parser and the template that ships with the app read from,
 * keeps the two from ever drifting apart: change the wording once and the
 * check that guards it changes with it.
 *
 * We deliberately do NOT enforce any minimum length. Without knowing
 * ScreenScraper's own rules for how short a real username or password may be,
 * any minimum we invented could bar a legitimate short login — worse than
 * letting a bad one through, since the API rejects a bad one with a 401 we
 * already handle, while a wrongly barred user is just stuck. */
#define LEV_CRED_PLACEHOLDER_SSID "YOUR_SCREENSCRAPER_USERNAME"
#define LEV_CRED_PLACEHOLDER_PASS "YOUR_SCREENSCRAPER_PASSWORD"

/* Where the app looks for the login file: alongside the binary, not under
 * assets/, so a user can find and edit it without digging. launch.sh makes the
 * app folder the working directory, so a bare relative name resolves there on
 * the device, and in the project folder on the Mac — the same way LEV_CA_BUNDLE
 * and LEV_SYSTEMS_FILE already resolve, needing no per-platform #ifdef.
 *
 * This lives here, not in config.h, on purpose: config.h is per-person and
 * gitignored, but this path is a constant of the app, identical for everyone.
 * It is app logic, not personal preference — the same line MELZI and MUDO run. */
#define LEV_CREDENTIALS_FILE "credentials.txt"

typedef struct {
    char ssid[LEV_CRED_MAX];
    char sspassword[LEV_CRED_MAX];
    int  complete; /* 1 only when BOTH fields are present and non-empty */
} UserCredentials;

/*
 * Load ssid and sspassword from a "key=value" file.
 *
 * Format, one entry per line:
 *
 *   # lines starting with a hash are comments and are ignored
 *   ssid=myusername
 *   sspassword=mypassword
 *
 * Blank lines and comment lines are skipped. Leading and trailing whitespace
 * around both the key and the value is trimmed, so "ssid = bob" works. Keys
 * other than ssid and sspassword are ignored, which leaves room to grow the
 * file later without this parser choking on new lines.
 *
 * out is always left in a valid, fully-initialised state. out->complete is the
 * only field the caller needs to branch on: it is 1 only when BOTH values came
 * back non-empty. A missing file, a file with only comments, or one field left
 * blank all yield complete == 0 — a single "no usable credentials" outcome, so
 * the app has just one failure path to show the user.
 *
 * Returns 0 if the file was opened and read, -1 if it could not be opened.
 * Note that -1 is NOT the "ask the user to fix it" signal — complete == 0 is.
 * A caller that only cares whether it can scrape should look at complete alone.
 */
int credentials_load(const char *path, UserCredentials *out);

#endif /* LEVIATHAN_CREDENTIALS_H */
