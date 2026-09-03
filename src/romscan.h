/*
 * romscan.h — walking the ROM folder and reading system tags from it.
 *
 * Port of the directory-walking half of the Go core. No network, no JSON:
 * everything here works against the filesystem alone.
 */

#ifndef LEVIATHAN_ROMSCAN_H
#define LEVIATHAN_ROMSCAN_H

#include <stddef.h>

#include "strlist.h"

/* Longest system tag we accept. Real tags are 2-5 chars ("FC", "PCE"). */
#define LEV_TAG_MAX 32

/*
 * Read the system tag out of a ROM subfolder name.
 *
 *   "Game Boy Advance (GBA)" -> "GBA"   (parenthesised suffix wins)
 *   "  GBA  "                -> "GBA"   (the name itself is the tag)
 *
 * Always uppercase, always trimmed. Writes at most out_size-1 chars plus a
 * terminator, so out is always a valid string even if the name is absurd.
 */
void extract_tag(const char *folder_name, char *out, size_t out_size);

/*
 * List the subdirectories of a folder, sorted. Files are ignored, and so
 * are entries whose name starts with a dot.
 *
 * Returns 0 on success, -1 if the folder cannot be opened. On failure the
 * list is left empty but valid.
 */
int list_subdirs(const char *path, StrList *out);

/*
 * List the ROM files of a system folder, sorted.
 *
 * exts filters by extension and may be NULL, which means "we do not know
 * this system's extensions, accept every file" — the same fallback the Go
 * version used. When given, entries must be lowercase and dotted (".gba").
 * Once the sistemas.json parser lands it will fill exactly this list, and
 * nothing here changes.
 *
 * Subdirectories and dotfiles are skipped. Returns 0 on success, -1 if the
 * folder cannot be opened.
 */
int list_roms(const char *path, const StrList *exts, StrList *out);

/*
 * Split a comma-separated extension string ("gba,gb,gbc") into a list of
 * lowercase dotted extensions (".gba", ".gb", ".gbc"). Blank pieces are
 * dropped. This is the shape sistemas.json stores extensions in.
 */
int parse_extensions(const char *csv, StrList *out);

#endif /* LEVIATHAN_ROMSCAN_H */
