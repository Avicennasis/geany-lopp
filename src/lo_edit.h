/*
 *      lo_edit.h - Glue between Scintilla and the line operations.
 *
 *      Ported from geany-plugins' lineoperations plugin
 *      Copyright 2015 Sylvan Mostert <smostert.dev@gmail.com>
 *      and extended for the Notepad++ command set.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#ifndef LO_EDIT_H
#define LO_EDIT_H

#include <geanyplugin.h>
#include "Scintilla.h"

/* The range of lines an operation applies to. */
typedef struct
{
	gint start_line;
	gint end_line;
	gboolean is_selection;
} lo_range;

/* Work out which lines the operation applies to, from the selection. */
void lo_edit_get_range(ScintillaObject *sci, lo_range *range);

/*
 * A whole-line operation. Receives the lines of the range with their line
 * terminators already stripped. It must leave '*n' equal to the number of
 * live lines afterwards - anything that drops lines reduces it - and may
 * replace '*lines' outright (split does). Only the first '*n' entries are
 * read or freed afterwards.
 *
 * Returns the number of lines removed, or the negative of the number added.
 */
typedef gint (*lo_line_op_fn)(gchar ***lines, gint *n, gpointer user_data);

/*
 * An operation Scintilla performs itself, because it is about the caret
 * rather than about the text. Returns the number of lines affected.
 */
typedef gint (*lo_sci_op_fn)(GeanyEditor *editor, gpointer user_data);

/*
 * Run a whole-line operation over the current document's range.
 *
 * The range's lines are extracted, handed to 'fn', and written back joined
 * by the document's own line terminator. Whether the last line of the range
 * ended with a terminator beforehand is preserved, so operating on a file
 * that does not end in a newline does not quietly add one.
 *
 * Returns whatever 'fn' returned.
 */
gint lo_edit_run_line_op(GeanyEditor *editor, lo_line_op_fn fn,
		gpointer user_data);

/* Run a Scintilla-level operation over the current document's range. */
gint lo_edit_run_sci_op(GeanyEditor *editor, lo_sci_op_fn fn,
		gpointer user_data);

/* Duplicate the current line, or every line of the selection. */
gint lo_edit_duplicate_lines(GeanyEditor *editor, gpointer user_data);

/*
 * Move the current line, or selected block, one line up or down.
 * 'user_data' is a gboolean: TRUE moves up.
 */
gint lo_edit_move_lines(GeanyEditor *editor, gpointer user_data);

/*
 * Remove every Nth line. 'user_data' is the interval cast to a gpointer.
 */
gint lo_edit_remove_every_nth(GeanyEditor *editor, gpointer user_data);

#endif
