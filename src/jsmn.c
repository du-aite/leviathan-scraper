/*
 * jsmn.c — the single home of the jsmn implementation.
 *
 * jsmn.h carries both the declarations and the code. Including it plainly
 * from more than one .c file therefore compiles the functions twice and the
 * linker rejects the duplicates. The convention the library expects: every
 * consumer defines JSMN_HEADER first to get declarations only, and exactly
 * one file — this one — includes it without the define to emit the code.
 */

#include "jsmn.h"
