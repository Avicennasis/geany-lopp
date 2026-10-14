/*
 *      lo_edit.c - Glue between Scintilla and the line operations.
 *
 *      Ported from geany-plugins' lineoperations plugin
 *      Copyright 2015 Sylvan Mostert <smostert.dev@gmail.com>
 *      and extended for the Notepad++ command set.
 *
 *      The text handling itself lives in lo_doc.c, which has no Scintilla
 *      dependency and is covered by its own tests; this file only moves
 *      text between Scintilla and those functions.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#include "lo_edit.h"
#include "lo_doc.h"

#include <string.h>


/* --------------------------------------------------------------------- */
/* Range detection                                                        */
/* --------------------------------------------------------------------- */

void
lo_edit_get_range(ScintillaObject *sci, lo_range *range)
{
	gint last = sci_get_line_count(sci) - 1;

	/*
	 * Scintilla counts the empty "line" after a final terminator. It is
	 * not a line of the file: sorting would float it to the top, and
	 * removing empty lines would strip the file's last newline.
	 */
	if (last > 0 && sci_get_line_length(sci, last) == 0)
		last--;

	if (last < 0)
		last = 0;

	if (sci_has_selection(sci))
	{
		gint start = sci_get_selection_start(sci);
		gint end = sci_get_selection_end(sci);

		range->start_line = sci_get_line_from_position(sci, start);
		range->end_line = sci_get_line_from_position(sci, end);

		/*
		 * A selection that stops at the very start of a line does not
		 * really include that line, even though the position maps onto
		 * it.
		 */
		if (range->end_line > range->start_line &&
				end == sci_get_position_from_line(sci, range->end_line))
			range->end_line--;

		range->is_selection = TRUE;
	}
	else
	{
		range->start_line = 0;
		range->end_line = last;
		range->is_selection = FALSE;
	}

	if (range->start_line < 0)
		range->start_line = 0;
	if (range->end_line > last)
		range->end_line = last;
	if (range->end_line < range->start_line)
		range->end_line = range->start_line;
}


/* The selected lines, or just the caret's line when nothing is selected. */
static void
get_caret_range(ScintillaObject *sci, lo_range *range)
{
	lo_edit_get_range(sci, range);

	if (!range->is_selection)
	{
		gint line = sci_get_current_line(sci);

		range->start_line = line;
		range->end_line = line;
	}
}


/* --------------------------------------------------------------------- */
/* Scintilla helpers                                                      */
/* --------------------------------------------------------------------- */

/*
 * Copy one line's text without its line terminator into a fresh string.
 *
 * sci_get_line() hands back the terminator too, and its length differs from
 * the text length by exactly the terminator's width, so the text length is
 * the line-end position minus the line-start position.
 */
static gchar *
extract_line(ScintillaObject *sci, gint line)
{
	gchar *raw = sci_get_line(sci, line);
	gint len = sci_get_line_end_position(sci, line) -
			sci_get_position_from_line(sci, line);
	gchar *out;

	if (len < 0)
		len = 0;

	out = g_strndup(raw, (size_t)len);
	g_free(raw);

	return out;
}


/*
 * True when 'line' ends with a line terminator.
 *
 * Only the final line of a document can lack one, so anything else is
 * terminated by definition.
 */
static gboolean
line_is_terminated(ScintillaObject *sci, gint line)
{
	gint text_len = sci_get_line_end_position(sci, line) -
			sci_get_position_from_line(sci, line);

	if (line < sci_get_line_count(sci) - 1)
		return TRUE;

	return sci_get_line_length(sci, line) > text_len;
}


/* Pull lines [first..last] out of the document, terminators stripped. */
static gchar **
extract_range(ScintillaObject *sci, gint first, gint last, gint *n,
		gboolean *terminated)
{
	gchar **lines = g_new(gchar *, (size_t)(last - first + 1));
	gint i;

	for (i = first; i <= last; i++)
		lines[i - first] = extract_line(sci, i);

	*n = last - first + 1;

	if (terminated != NULL)
		*terminated = line_is_terminated(sci, last);

	return lines;
}


/* --------------------------------------------------------------------- */
/* Running operations                                                     */
/* --------------------------------------------------------------------- */

gint
lo_edit_run_line_op(GeanyEditor *editor, lo_line_op_fn fn, gpointer user_data)
{
	ScintillaObject *sci = editor->sci;
	lo_range range;
	gchar **lines;
	gchar *text;
	gboolean was_terminated;
	gint mode = 0, sel_anchor = 0, sel_caret = 0;
	gint n, delta;

	g_return_val_if_fail(fn != NULL, 0);

	lo_edit_get_range(sci, &range);

	lines = extract_range(sci, range.start_line, range.end_line, &n,
			&was_terminated);

	if (range.is_selection)
	{
		mode = sci_get_selection_mode(sci);
		sel_anchor = sci_get_selection_start(sci);
		sel_caret = sci_get_selection_end(sci);
	}

	/*
	 * Select exactly the lines that were extracted, terminator included
	 * when it was, so the replacement overwrites them rather than landing
	 * beside them.
	 */
	sci_set_selection_mode(sci, SC_SEL_STREAM);
	sci_set_selection_start(sci,
			sci_get_position_from_line(sci, range.start_line));
	sci_set_selection_end(sci, was_terminated
			? sci_get_position_from_line(sci, range.end_line + 1)
			: sci_get_line_end_position(sci, range.end_line));

	sci_start_undo_action(sci);
	delta = fn(&lines, &n, user_data);

	/*
	 * lo_doc_reassemble() puts the trailing terminator back only if the
	 * range's last line had one, so running an operation over a file with
	 * no final newline does not quietly add one.
	 */
	text = lo_doc_reassemble(lines, n, editor_get_eol_char(editor),
			was_terminated);
	lo_doc_free_lines(lines, n);

	sci_replace_sel(sci, text);
	sci_end_undo_action(sci);
	g_free(text);

	if (range.is_selection)
	{
		/* Reselect the rewritten range, clamped to its new end. */
		gint first = sci_get_position_from_line(sci, range.start_line);
		gint last_line = range.start_line + (n > 0 ? n - 1 : 0);
		gint last = sci_get_line_end_position(sci, last_line);

		if (sel_anchor > last)
			sel_anchor = last;
		if (sel_caret > last)
			sel_caret = last;

		sci_set_selection_start(sci, first);
		sci_set_selection_end(sci, sel_caret);

		sci_set_selection_mode(sci, mode);
	}

	return delta;
}


gint
lo_edit_run_sci_op(GeanyEditor *editor, lo_sci_op_fn fn, gpointer user_data)
{
	g_return_val_if_fail(fn != NULL, 0);

	sci_start_undo_action(editor->sci);
	gint delta = fn(editor, user_data);
	sci_end_undo_action(editor->sci);

	return delta;
}


/* --------------------------------------------------------------------- */
/* Caret operations                                                       */
/* --------------------------------------------------------------------- */

/*
 * Duplicate the line holding the caret, or every selected line.
 *
 * The copy lands directly below the original, which is what "Duplicate
 * Current Line" means in Notepad++ and what Ctrl+D does elsewhere. The
 * caret follows onto the copy so that repeating the command stacks copies.
 */
gint
lo_edit_duplicate_lines(GeanyEditor *editor, gpointer user_data)
{
	ScintillaObject *sci = editor->sci;
	lo_range range;
	gchar **lines, *block;
	gint caret_pos, caret_line, caret_offset;
	gint count, n;

	(void) user_data;

	get_caret_range(sci, &range);

	caret_pos = sci_get_current_position(sci);
	caret_line = sci_get_line_from_position(sci, caret_pos);
	caret_offset = caret_pos - sci_get_position_from_line(sci, caret_line);

	lines = extract_range(sci, range.start_line, range.end_line, &n, NULL);

	/*
	 * The copy goes in at the end of the last line's text, so it leads
	 * with a terminator and carries none of its own: that works the same
	 * whether or not the original line was terminated.
	 */
	{
		gchar *body = lo_doc_reassemble(lines, n,
				editor_get_eol_char(editor), FALSE);

		block = g_strconcat(editor_get_eol_char(editor), body, NULL);
		g_free(body);
	}
	lo_doc_free_lines(lines, n);

	sci_insert_text(sci, sci_get_line_end_position(sci, range.end_line),
			block);
	g_free(block);

	count = n;

	caret_line += count;
	{
		gint posn = sci_get_position_from_line(sci, caret_line) + caret_offset;
		gint end = sci_get_line_end_position(sci, caret_line);

		if (posn > end)
			posn = end;

		sci_set_current_position(sci, posn, TRUE);
	}

	return count;
}


/*
 * Move a line, or a selected block, one line up or down.
 *
 * The moved block and the neighbour it swaps with are read out together
 * into one window, reordered there, and written back as a single
 * replacement. Splicing two separate insert points instead drifts as soon
 * as the two interact.
 */
gint
lo_edit_move_lines(GeanyEditor *editor, gpointer user_data)
{
	gboolean up = GPOINTER_TO_INT(user_data);
	ScintillaObject *sci = editor->sci;
	lo_range range;
	gchar **lines, *text;
	gboolean terminated;
	gint first, last, n, region_start, region_end;
	gint anchor_offset, caret_offset;

	get_caret_range(sci, &range);

	if (up && range.start_line == 0)
		return 0;
	if (!up && range.end_line >= sci_get_line_count(sci) - 1)
		return 0;

	/* The window is the block plus the one line it swaps with. */
	first = up ? range.start_line - 1 : range.start_line;
	last = up ? range.end_line : range.end_line + 1;

	lines = extract_range(sci, first, last, &n, &terminated);

	/*
	 * Within that window the block occupies every position except the
	 * neighbour's: [neighbour, block...] going up, [block..., neighbour]
	 * going down. Moving it the requested way swaps the two.
	 */
	if (!lo_doc_move_block(lines, n, up ? 1 : 0, up ? n - 1 : n - 2, up))
	{
		lo_doc_free_lines(lines, n);
		return 0;
	}

	text = lo_doc_reassemble(lines, n, editor_get_eol_char(editor),
			terminated);
	lo_doc_free_lines(lines, n);

	/* Remember where the caret sat inside its own line. */
	{
		gint apos = sci_get_selection_start(sci);
		gint cpos = sci_get_selection_end(sci);
		gint apos_line = sci_get_line_from_position(sci, apos);
		gint cpos_line = sci_get_line_from_position(sci, cpos);

		anchor_offset = apos - sci_get_position_from_line(sci, apos_line);
		caret_offset = cpos - sci_get_position_from_line(sci, cpos_line);
	}

	region_start = sci_get_position_from_line(sci, first);
	region_end = sci_get_line_end_position(sci, last) +
			(terminated ? editor_get_eol_char_len(editor) : 0);

	sci_set_selection_start(sci, region_start);
	sci_set_selection_end(sci, region_end);
	sci_replace_sel(sci, text);
	g_free(text);

	/* The block now begins at the old neighbour's position. */
	{
		gint new_start = sci_get_position_from_line(sci,
				up ? range.start_line - 1 : range.start_line + 1);
		gint new_end = sci_get_line_end_position(sci,
				up ? range.end_line - 1 : range.end_line + 1);

		sci_set_selection_start(sci, new_start + anchor_offset);
		sci_set_selection_end(sci, new_start + caret_offset);

		if (new_start + caret_offset < new_end)
			sci_set_current_position(sci, new_start + caret_offset, TRUE);
	}

	return range.end_line - range.start_line + 1;
}


/* --------------------------------------------------------------------- */
/* Scintilla-level line removal                                           */
/* --------------------------------------------------------------------- */

/*
 * Remove every Nth line of the range, counting from the first line of the
 * range as 1. 'user_data' is the interval cast to a gpointer.
 */
gint
lo_edit_remove_every_nth(GeanyEditor *editor, gpointer user_data)
{
	ScintillaObject *sci = editor->sci;
	lo_range range;
	gint every = GPOINTER_TO_INT(user_data);
	gint count, removed = 0;
	gint i;

	if (every < 1)
		return 0;

	lo_edit_get_range(sci, &range);
	count = range.end_line - range.start_line + 1;

	/*
	 * Delete from the bottom up: removing a line shifts every position
	 * below it, so working upwards would examine the wrong lines.
	 */
	for (i = count - 1; i >= 0; i--)
	{
		gint line = range.start_line + i;

		if ((i + 1) % every != 0)
			continue;

		scintilla_send_message(sci, SCI_DELETERANGE,
				sci_get_position_from_line(sci, line),
				sci_get_line_length(sci, line));
		removed++;
	}

	return removed;
}
