/*
 *      lo_lines.c - Whole-line operations over an array of lines.
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

#include "lo_lines.h"

#include <string.h>


/*
 * Every operation that drops lines consumes the array: survivors are moved
 * down to the front, and each dropped line is freed as it is recognised.
 * Freeing at the point of removal rather than sweeping the tail afterwards
 * matters, because a survivor is often moved down past the slot it came
 * from, and a trailing sweep would either free a live string or miss one
 * that had been overwritten.
 */


/* --------------------------------------------------------------------- */
/* Predicates                                                             */
/* --------------------------------------------------------------------- */

gboolean
lo_lines_equal(const gchar *a, const gchar *b)
{
	/* strcmp, not g_strcmp0: both lines are known non-NULL. */
	return strcmp(a, b) == 0;
}


gboolean
lo_lines_is_empty(const gchar *line)
{
	return *line == '\0';
}


/* Length in bytes of the character starting at 'p'. */
static gint
char_len(const gchar *p)
{
	const guchar c0 = (guchar)p[0];

	if (c0 < 0xC0)
		return 1;
	if (c0 < 0xE0)
		return 2;
	if (c0 < 0xF0)
		return 3;
	return 4;
}


/* TRUE when the character at 'p' is whitespace. Malformed input is not. */
static gboolean
char_is_space(const gchar *p)
{
	gint need = char_len(p);

	if ((guchar)p[0] >= 0xC0 && !g_utf8_validate(p, need, NULL))
		return FALSE;

	return g_unichar_isspace((gunichar)g_utf8_get_char(p));
}


gboolean
lo_lines_is_whitespace(const gchar *line)
{
	const gchar *p = line;

	while (*p != '\0')
	{
		if (!char_is_space(p))
			return FALSE;

		p += char_len(p);
	}

	return TRUE;
}


/* --------------------------------------------------------------------- */
/* Counting helper                                                        */
/* --------------------------------------------------------------------- */

/*
 * Tally how many times each distinct line occurs.
 *
 * The upstream plugin compared every line against every later line to find
 * duplicates, which is quadratic: on a 100k-line log that is five billion
 * comparisons. A hash table makes it linear in the number of lines.
 *
 * The table borrows the line pointers as keys and never frees them. It must
 * be destroyed before any of those lines are freed.
 */
static void
lo_count_occurrences(gchar **lines, gint n, GHashTable *counts)
{
	gint i;

	for (i = 0; i < n; i++)
	{
		gpointer val = NULL;

		if (g_hash_table_lookup_extended(counts, lines[i], NULL, &val))
			g_hash_table_insert(counts, lines[i],
					GINT_TO_POINTER(GPOINTER_TO_INT(val) + 1));
		else
			g_hash_table_insert(counts, lines[i], GINT_TO_POINTER(1));
	}
}


static gint
lo_count_of(const GHashTable *counts, const gchar *line)
{
	gpointer val = g_hash_table_lookup((GHashTable *)counts, line);

	return val ? GPOINTER_TO_INT(val) : 0;
}


/*
 * Compact the array according to 'keep', freeing everything not kept.
 * Takes ownership: the caller must not free any of the lines afterwards.
 */
static gint
lo_apply_flags(gchar **lines, gint n, const gboolean *keep)
{
	gint kept = 0;
	gint i;

	for (i = 0; i < n; i++)
	{
		if (keep[i])
			lines[kept++] = lines[i];
		else
			g_free(lines[i]);
	}

	return n - kept;
}


/* --------------------------------------------------------------------- */
/* Duplicate removal                                                      */
/* --------------------------------------------------------------------- */

gint
lo_lines_dedupe_sorted(gchar **lines, gint n, lo_sort_mode mode,
		gboolean use_collation)
{
	gint kept = 0;
	gint i;

	if (n < 2)
		return 0;

	lo_sort_sort(lines, n, mode, use_collation);

	/*
	 * Walk the sorted run and keep the first of each group of equals.
	 * Because the sort is stable and the comparator is a total order,
	 * the survivor of each group is well defined.
	 */
	lines[kept++] = lines[0];

	for (i = 1; i < n; i++)
	{
		if (lo_sort_equivalent(mode, lines[kept - 1], lines[i],
					use_collation))
			g_free(lines[i]);
		else
			lines[kept++] = lines[i];
	}

	return n - kept;
}


gint
lo_lines_dedupe_ordered(gchar **lines, gint n)
{
	GHashTable *seen;
	gint kept = 0;
	gint i;

	if (n < 2)
		return 0;

	seen = g_hash_table_new(g_str_hash, g_str_equal);

	for (i = 0; i < n; i++)
	{
		if (g_hash_table_contains(seen, lines[i]))
		{
			/* Never added to the table, so freeing it is safe. */
			g_free(lines[i]);
			continue;
		}

		g_hash_table_add(seen, lines[i]);
		lines[kept++] = lines[i];
	}

	g_hash_table_destroy(seen);

	return n - kept;
}


gint
lo_lines_dedupe_consecutive(gchar **lines, gint n)
{
	gint kept = 0;
	gint i;

	if (n < 2)
		return 0;

	lines[kept++] = lines[0];

	for (i = 1; i < n; i++)
	{
		if (lo_lines_equal(lines[kept - 1], lines[i]))
			g_free(lines[i]);
		else
			lines[kept++] = lines[i];
	}

	return n - kept;
}


/* --------------------------------------------------------------------- */
/* Unique-line variants                                                   */
/* --------------------------------------------------------------------- */

static gint
lo_unique_variant(gchar **lines, gint n, gboolean keep_singletons)
{
	GHashTable *counts;
	gboolean *keep;
	gint removed;
	gint i;

	if (n < 2)
		return keep_singletons ? 0 : n;

	counts = g_hash_table_new(g_str_hash, g_str_equal);
	lo_count_occurrences(lines, n, counts);

	keep = g_new(gboolean, (size_t)n);
	for (i = 0; i < n; i++)
		keep[i] = (lo_count_of(counts, lines[i]) == 1) == keep_singletons;

	/* The table keys are the lines themselves, so it has to go first. */
	g_hash_table_destroy(counts);

	removed = lo_apply_flags(lines, n, keep);
	g_free(keep);

	return removed;
}


gint
lo_lines_remove_unique(gchar **lines, gint n)
{
	/* Drops the singletons, keeps whatever repeats. */
	return lo_unique_variant(lines, n, FALSE);
}


gint
lo_lines_keep_unique(gchar **lines, gint n)
{
	return lo_unique_variant(lines, n, TRUE);
}


/* --------------------------------------------------------------------- */
/* Removal and trimming                                                   */
/* --------------------------------------------------------------------- */

static gint
lo_remove_matching(gchar **lines, gint n, gboolean (*pred)(const gchar *))
{
	gboolean *keep = g_new(gboolean, (size_t)n);
	gint removed;
	gint i;

	for (i = 0; i < n; i++)
		keep[i] = !pred(lines[i]);

	removed = lo_apply_flags(lines, n, keep);
	g_free(keep);

	return removed;
}


gint
lo_lines_remove_empty(gchar **lines, gint n)
{
	return lo_remove_matching(lines, n, lo_lines_is_empty);
}


gint
lo_lines_remove_whitespace(gchar **lines, gint n)
{
	return lo_remove_matching(lines, n, lo_lines_is_whitespace);
}


gint
lo_lines_remove_every_nth(gchar **lines, gint n, gint every)
{
	gboolean *keep;
	gint removed;
	gint i;

	if (every < 1)
		return 0;

	keep = g_new(gboolean, (size_t)n);

	for (i = 0; i < n; i++)
		/* Positions every, 2*every, ... counting from 1. */
		keep[i] = ((i + 1) % every) != 0;

	removed = lo_apply_flags(lines, n, keep);
	g_free(keep);

	return removed;
}


gint
lo_lines_trim_trailing(gchar **lines, gint n)
{
	gint changed = 0;
	gint i;

	for (i = 0; i < n; i++)
	{
		gchar *line = lines[i];
		const gchar *p = line;
		gint keep = 0;        /* bytes up to the last non-space character */

		/*
		 * Walking forwards and remembering the last non-space position is
		 * simpler than walking backwards, where each step has to rescan
		 * the bytes of the character it just stepped over.
		 */
		while (*p != '\0')
		{
			gint need = char_len(p);

			if (!char_is_space(p))
				keep = (gint)(p - line) + need;

			p += need;
		}

		if (keep != (gint)strlen(line))
		{
			line[keep] = '\0';
			changed++;
		}
	}

	return changed;
}


gint
lo_lines_trim_leading(gchar **lines, gint n)
{
	gint changed = 0;
	gint i;

	for (i = 0; i < n; i++)
	{
		gchar *line = lines[i];
		const gchar *p = line;

		while (*p != '\0' && char_is_space(p))
			p += char_len(p);

		if (p != line)
		{
			/* Slide the remainder down over the whitespace consumed. */
			memmove(line, p, strlen(p) + 1);
			changed++;
		}
	}

	return changed;
}


/* --------------------------------------------------------------------- */
/* Join and split                                                         */
/* --------------------------------------------------------------------- */

gint
lo_lines_join(gchar **lines, gint n, const gchar *sep)
{
	GString *out;
	gint i;

	if (n < 2)
		return 0;

	if (sep == NULL)
		sep = " ";

	out = g_string_sized_new(1024);

	for (i = 0; i < n; i++)
	{
		if (i > 0)
			g_string_append(out, sep);

		g_string_append(out, lines[i]);
		g_free(lines[i]);
	}

	lines[0] = g_string_free(out, FALSE);

	return -(n - 1);
}


/*
 * Append the pieces of one line to 'pieces'.
 *
 * With a separator, this is a plain literal split. Without one the line is
 * broken on runs of whitespace, which is what a caller who cleared the
 * separator box in the preferences is asking for.
 */
static void
split_one_line(GPtrArray *pieces, const gchar *line, const gchar *sep)
{
	if (sep != NULL && *sep != '\0')
	{
		gchar **parts = g_strsplit(line, sep, -1);
		gint i;

		for (i = 0; parts[i] != NULL; i++)
			g_ptr_array_add(pieces, parts[i]);

		g_free(parts);
		return;
	}

	while (*line != '\0')
	{
		const gchar *start;

		while (*line != '\0' && g_ascii_isspace(*line))
			line++;

		start = line;
		while (*line != '\0' && !g_ascii_isspace(*line))
			line++;

		if (line > start)
			g_ptr_array_add(pieces, g_strndup(start, (size_t)(line - start)));
	}
}


gint
lo_lines_split(gchar ***plines, gint *pn, const gchar *sep)
{
	gchar **old = *plines;
	gint n = *pn;
	gchar **out;
	GPtrArray *pieces;
	gint out_n;
	gint i;

	if (n < 1)
		return 0;

	pieces = g_ptr_array_new();

	for (i = 0; i < n; i++)
	{
		split_one_line(pieces, old[i], sep);
		g_free(old[i]);
	}

	g_free(old);

	/*
	 * Take the count from the array's own length. g_ptr_array_free()
	 * makes no promise about a trailing NULL, so measuring the result
	 * with g_strv_length() would read past the end of the allocation.
	 */
	out_n = (gint)pieces->len;

	out = g_new(gchar *, (size_t)out_n + 1);
	for (i = 0; i < out_n; i++)
		out[i] = g_ptr_array_index(pieces, i);
	out[out_n] = NULL;

	/* No element free func was set, so this releases only the container. */
	g_ptr_array_free(pieces, TRUE);

	*plines = out;
	*pn = out_n;

	return out_n - n;
}
