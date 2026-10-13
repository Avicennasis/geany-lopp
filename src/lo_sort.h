/*
 *      lo_sort.h - Comparison and ordering of lines.
 *
 *      Ported from geany-plugins' lineoperations plugin
 *      Copyright 2015 Sylvan Mostert <smostert.dev@gmail.com>
 *      and extended with the full Notepad++ line-sort matrix.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#ifndef LO_SORT_H
#define LO_SORT_H

#include <glib.h>

/*
 * The comparison modes offered under Edit > Line Operations.
 *
 * These mirror Notepad++'s Line Operations submenu. Every mode is a total
 * order: two lines that compare equal under the numeric modes, or under
 * a case-folded mode, are still broken by a secondary comparison so that
 * sorting is reproducible run to run.
 */
typedef enum
{
	LO_SORT_NATURAL_ASC,
	LO_SORT_NATURAL_DESC,

	LO_SORT_LEXICOGRAPHIC_ASC,
	LO_SORT_LEXICOGRAPHIC_DESC,

	LO_SORT_NATURAL_NOCASE_ASC,
	LO_SORT_NATURAL_NOCASE_DESC,

	LO_SORT_NUM_DEC_ASC,
	LO_SORT_NUM_DEC_DESC,

	LO_SORT_NUM_HEX_ASC,
	LO_SORT_NUM_HEX_DESC,

	LO_SORT_NUM_OCT_ASC,
	LO_SORT_NUM_OCT_DESC,

	LO_SORT_MODE_COUNT
} lo_sort_mode;


/* Non-zero when 'mode' orders descending. Zero for ascending modes. */
gboolean lo_sort_mode_is_desc(lo_sort_mode mode);

/* Non-zero when 'mode' compares case-insensitively. */
gboolean lo_sort_mode_is_nocase(lo_sort_mode mode);

/* Non-zero when 'mode' interprets each line as a number. */
gboolean lo_sort_mode_is_numeric(lo_sort_mode mode);

/* Short label for the mode, used in the status bar and in tests. */
const gchar *lo_sort_mode_label(lo_sort_mode mode);

/*
 * Compare two lines under 'mode'.
 *
 * 'use_collation' selects how individual characters are compared: FALSE
 * compares them by code point, TRUE uses g_utf8_collate() so that accented
 * and case-varied letters interleave the way the current locale expects.
 * It only affects the character-comparison step; the numeric modes ignore
 * it entirely because they never reach that step.
 *
 * Returns -1, 0 or 1.
 */
gint lo_sort_compare(lo_sort_mode mode, const gchar *a, const gchar *b,
		gboolean use_collation);

/*
 * Whether 'a' and 'b' are duplicates of each other under 'mode'.
 *
 * Distinct from lo_sort_compare(), which adds a tie-break to make the sort
 * a total order. Here the question is whether the two lines are the same
 * line, so the ignoring-case modes treat "Apple" and "apple" as one and the
 * numeric modes treat "16" and "0x10" as one.
 */
gboolean lo_sort_equivalent(lo_sort_mode mode, const gchar *a, const gchar *b,
		gboolean use_collation);

/*
 * Sort 'lines[0..n)' in place using a stable merge sort.
 *
 * Stability matters here: 'Remove Duplicate Lines, Sorted' keeps one
 * representative per run of equals, and with an unstable sort which
 * representative survives would vary between runs.
 */
void lo_sort_sort(gchar **lines, gint n, lo_sort_mode mode,
		gboolean use_collation);

#endif
