/*
 *      lo_doc.c - Document text handling for the line operations.
 *
 *      Ported from geany-plugins' lineoperations plugin
 *      Copyright 2015 Sylvan Mostert <smostert.dev@gmail.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#include "lo_doc.h"

#include <string.h>


/* Length of the terminator at the start of 'text', or 0 if there is none. */
static gsize
eol_len_at(const gchar *text)
{
	if (text[0] == '\r' && text[1] == '\n')
		return 2;
	if (text[0] == '\n' || text[0] == '\r')
		return 1;
	return 0;
}


/*
 * The terminator the document uses.
 *
 * This has to scan for the first terminator anywhere in the text, not look
 * at the first character: the first character is ordinary line content
 * unless the document happens to begin with an empty line.
 */
const gchar *
lo_doc_eol(const gchar *text)
{
	for ( ; *text != '\0'; text++)
	{
		if (text[0] == '\r' && text[1] == '\n')
			return "\r\n";
		if (text[0] == '\r')
			return "\r";
		if (text[0] == '\n')
			return "\n";
	}

	/* A single line with no terminator at all: assume the common case. */
	return "\n";
}


gint
lo_doc_line_count(const gchar *text)
{
	gint count = 0;
	gboolean partial = FALSE;   /* characters seen since the last terminator */

	/* An empty document still holds one empty line. */
	if (*text == '\0')
		return 1;

	while (*text != '\0')
	{
		gsize len = eol_len_at(text);

		if (len == 0)
		{
			partial = TRUE;
			text++;
		}
		else
		{
			count++;
			partial = FALSE;
			text += len;
		}
	}

	/*
	 * A terminator ends the line it follows and does not begin another,
	 * so only text left over after the last terminator adds a line.
	 */
	return partial ? count + 1 : count;
}


/* Length of line 'line' in 'text', terminator excluded. */
static gsize
line_text_len(const gchar *p)
{
	const gchar *q = p;
	gsize len;

	while (*q != '\0')
	{
		len = eol_len_at(q);

		if (len != 0)
			break;

		q++;
	}

	return (gsize)(q - p);
}

/*
 * Walk to the start of line 'want'.
 *
 * Each step has to skip the whole line, not just the terminator: advancing
 * only past a terminator would land in the middle of the following line.
 *
 * Returns NULL if the document has fewer lines than that.
 */
static const gchar *
goto_line(const gchar *text, gint want)
{
	const gchar *p = text;
	gint i = 0;

	if (want < 0)
		return NULL;

	while (i < want)
	{
		gsize tlen = line_text_len(p);
		gsize eol = eol_len_at(p + tlen);

		if (tlen == 0 && eol == 0)
			return NULL;     /* ran off the end of the document */

		p += tlen + eol;
		i++;
	}

	return p;
}




gchar **
lo_doc_extract(const gchar *text, gint start, gint end, gint *n,
		gboolean *was_terminated)
{
	GPtrArray *arr;
	const gchar *p;
	gchar **out;
	gint count = 0;
	gint i;

	if (was_terminated != NULL)
		*was_terminated = FALSE;

	p = goto_line(text, start);
	arr = g_ptr_array_new();

	for (i = start; i <= end; i++)
	{
		gsize len, eol;

		/* The document may hold fewer lines than the range asks for. */
		if (p == NULL || *p == '\0')
			break;

		len = line_text_len(p);
		eol = eol_len_at(p + len);

		g_ptr_array_add(arr, g_strndup(p, len));
		count++;

		if (i == end)
		{
			/*
			 * Whether this line carried a terminator is the fact the
			 * caller needs in order to put it back afterwards.
			 */
			if (was_terminated != NULL)
				*was_terminated = (eol != 0);

			break;
		}

		p += len + eol;
	}

	g_ptr_array_add(arr, NULL);
	out = (gchar **)g_ptr_array_free(arr, FALSE);

	*n = count;
	return out;
}


void
lo_doc_free_lines(gchar **lines, gint n)
{
	gint i;

	if (lines == NULL)
		return;

	for (i = 0; i < n; i++)
		g_free(lines[i]);

	g_free(lines);
}


gchar *
lo_doc_reassemble(gchar **lines, gint n, const gchar *eol,
		gboolean was_terminated)
{
	GString *out = g_string_sized_new(1024);
	gint i;

	for (i = 0; i < n; i++)
	{
		if (i > 0)
			g_string_append(out, eol);

		g_string_append(out, lines[i]);
	}

	if (n > 0 && was_terminated)
		g_string_append(out, eol);

	return g_string_free(out, FALSE);
}


/*
 * Reorder a block by swapping it with the line above or below.
 *
 * Only three pointers move in each direction, so this is written out
 * explicitly rather than done with a general rotate: it keeps the ordering
 * obvious, which matters because an off-by-one here silently scrambles a
 * selection.
 */
gboolean
lo_doc_move_block(gchar **lines, gint n, gint start, gint end, gboolean up)
{
	gint i;

	if (start < 0 || end < start || end >= n)
		return FALSE;

	if (up)
	{
		if (start == 0)
			return FALSE;

		/* [a, block..., z] -> [block..., a, z] */
		gchar *moved = lines[start - 1];

		for (i = start - 1; i < end; i++)
			lines[i] = lines[i + 1];

		lines[end] = moved;
	}
	else
	{
		if (end == n - 1)
			return FALSE;

		/* [a, block..., z] -> [a, z, block...] */
		gchar *moved = lines[end + 1];

		for (i = end + 1; i > start; i--)
			lines[i] = lines[i - 1];

		lines[start] = moved;
	}

	return TRUE;
}


gchar *
lo_doc_slice(const gchar *text, gint start, gint end)
{
	const gchar *p, *stop;
	const gchar *eol_end;
	GString *out;

	p = goto_line(text, start);
	if (p == NULL)
		return g_strdup("");

	stop = goto_line(text, end);
	if (stop == NULL)
		stop = text + strlen(text);

	eol_end = stop + line_text_len(stop);
	if (*eol_end != '\0')
		eol_end += eol_len_at(eol_end);

	out = g_string_new_len(p, (gssize)(eol_end - p));

	return g_string_free(out, FALSE);
}
