/*
 *      lo_lines.h - Whole-line operations over an array of lines.
 *
 *      Ported from geany-plugins' lineoperations plugin
 *      Copyright 2015 Sylvan Mostert <smostert.dev@gmail.com>
 *      and extended with the operations Notepad++ offers.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#ifndef LO_LINES_H
#define LO_LINES_H

#include <glib.h>
#include "lo_sort.h"

/*
 * Every operation here works on an array of 'n' NUL-terminated lines that
 * do NOT include their line terminators, and compacts the array in place,
 * leaving the surviving lines at the front.
 *
 * Each returns the number of lines REMOVED, or a negative count of lines
 * ADDED, so the caller can report the change without recomputing it.
 * Functions returning a count of *kept* lines instead are documented as
 * such. On success the new line count is n - removed.
 */

/* Exact byte equality between two lines. */
gboolean lo_lines_equal(const gchar *a, const gchar *b);

/* TRUE when 'line' holds no characters at all. */
gboolean lo_lines_is_empty(const gchar *line);

/* TRUE when 'line' holds only whitespace (Unicode-aware). */
gboolean lo_lines_is_whitespace(const gchar *line);

/* --------------------------------------------------------------- */
/* Duplicate removal                                               */
/* --------------------------------------------------------------- */

/*
 * Sort the lines into the given order and drop every run of equal lines,
 * keeping the first of each run. Returns the number removed.
 */
gint lo_lines_dedupe_sorted(gchar **lines, gint n, lo_sort_mode mode,
		gboolean use_collation);

/*
 * Drop every line that has an earlier equal line, preserving the order the
 * remaining lines were already in. Returns the number removed.
 */
gint lo_lines_dedupe_ordered(gchar **lines, gint n);

/*
 * Drop every line equal to the line immediately before it. Lines that are
 * equal but not adjacent are both kept. Returns the number removed.
 */
gint lo_lines_dedupe_consecutive(gchar **lines, gint n);

/* --------------------------------------------------------------- */
/* Unique-line variants                                            */
/* --------------------------------------------------------------- */

/*
 * Remove the lines that occur exactly once, keeping those that occur more
 * than once. Returns the number removed.
 */
gint lo_lines_remove_unique(gchar **lines, gint n);

/*
 * Keep only the lines that occur exactly once, removing those that repeat.
 * Returns the number removed.
 */
gint lo_lines_keep_unique(gchar **lines, gint n);

/* --------------------------------------------------------------- */
/* Removal and trimming                                            */
/* --------------------------------------------------------------- */

/* Remove lines with no characters. Returns the number removed. */
gint lo_lines_remove_empty(gchar **lines, gint n);

/* Remove lines holding only whitespace. Returns the number removed. */
gint lo_lines_remove_whitespace(gchar **lines, gint n);

/* Remove lines at positions n, 2n, 3n... counting from 1. Returns the count. */
gint lo_lines_remove_every_nth(gchar **lines, gint n, gint every);

/*
 * Strip trailing whitespace from each line. The lines are rewritten in
 * place, reusing each line's own allocation where possible so the array's
 * pointers stay valid. Returns the number of lines changed.
 */
gint lo_lines_trim_trailing(gchar **lines, gint n);

/* Strip leading whitespace from each line. Returns the number changed. */
gint lo_lines_trim_leading(gchar **lines, gint n);

/*
 * Collapse every line into a single line, joining the pieces with "sep".
 * Returns the negative of the number of lines removed.
 */
gint lo_lines_join(gchar **lines, gint n, const gchar *sep);

/*
 * Split each line on the literal substring "sep" into separate lines. A
 * NULL or empty separator splits into individual characters.
 *
 * This is the one operation that can grow the array, so it takes the array
 * by reference and reallocates it; the caller must stop using the old
 * pointer. Returns the positive number of lines added.
 */
gint lo_lines_split(gchar ***lines, gint *n, const gchar *sep);

#endif
