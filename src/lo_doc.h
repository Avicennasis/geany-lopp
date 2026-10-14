/*
 *      lo_doc.h - Document text handling for the line operations.
 *
 *      Everything here works on a whole document as a single string and
 *      knows nothing about Scintilla, so the parts that are easy to get
 *      wrong - line terminator preservation, reordering a block of lines -
 *      can be tested directly.
 *
 *      Ported from geany-plugins' lineoperations plugin
 *      Copyright 2015 Sylvan Mostert <smostert.dev@gmail.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#ifndef LO_DOC_H
#define LO_DOC_H

#include <glib.h>

/* The line terminator in use, taken from a document's first line. */
const gchar *lo_doc_eol(const gchar *text);

/*
 * How many lines 'text' holds. A trailing terminator does not add an empty
 * final line, so "a\nb\n" is two lines and "a\nb" is two lines.
 */
gint lo_doc_line_count(const gchar *text);

/*
 * Pull lines [start..end] out of 'text', without their terminators.
 *
 * '*was_terminated' is set when the last line of the range ended with a
 * terminator, which is what decides whether an operation that rewrites the
 * range should put one back. Returns a NULL-terminated array the caller
 * frees with g_strfreev(); '*n' receives the line count.
 */
gchar **lo_doc_extract(const gchar *text, gint start, gint end, gint *n,
		gboolean *was_terminated);

/*
 * Free the first 'n' lines and the array that holds them.
 *
 * The line operations compact the array in place and it is not
 * NULL-terminated, so g_strfreev() would walk into stale slots past the
 * live lines. Everything that frees a line array uses this instead.
 */
void lo_doc_free_lines(gchar **lines, gint n);

/*
 * Join 'n' lines with 'eol', appending a trailing terminator only when
 * '*was_terminated' is TRUE.
 *
 * This is what keeps a file that does not end in a newline from quietly
 * gaining one the first time a line operation is run on it.
 */
gchar *lo_doc_reassemble(gchar **lines, gint n, const gchar *eol,
		gboolean was_terminated);

/*
 * Move the block of lines [start..end] one line up (TRUE) or down (FALSE),
 * swapping it with its neighbour.
 *
 * Returns FALSE, leaving 'lines' untouched, when the block is already at
 * the top or bottom of the document. On success 'lines' has been
 * reordered in place and TRUE is returned.
 */
gboolean lo_doc_move_block(gchar **lines, gint n, gint start, gint end,
		gboolean up);

/*
 * Concatenate lines [start..end] of 'text', keeping their terminators,
 * including the one that ends the last line if it has one.
 */
gchar *lo_doc_slice(const gchar *text, gint start, gint end);

#endif
