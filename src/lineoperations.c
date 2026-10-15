/*
 *      lineoperations.c - Notepad++ line operations for Geany.
 *
 *      Based on geany-plugins' lineoperations plugin
 *      Copyright 2015 Sylvan Mostert <smostert.dev@gmail.com>
 *      and extended with the full Notepad++ line-operation set.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#ifdef HAVE_CONFIG_H
	#include "config.h" /* for the gettext domain */
#endif

#include <geanyplugin.h>

#include "Scintilla.h"
#include "lo_edit.h"
#include "lo_lines.h"
#include "lo_prefs.h"
#include "lo_sort.h"


/*
 * The name as it appears in the plugin manager, the menu bar and the
 * keybinding dialog.
 *
 * Deliberately unprefixed: Geany's plugin manager and keybinding dialog
 * list nothing but Geany plugins, so a "Geany - " prefix there is noise.
 * The prefixed name is the project's name on GitHub, where it earns its
 * keep as something to search for.
 */
#define GEANY_LOPP_NAME "Line Operations++"

static GtkWidget *main_menu_item = NULL;


/* --------------------------------------------------------------------- */
/* Adapters: wrap the pure operations in the shape the runners expect     */
/* --------------------------------------------------------------------- */

/*
 * The core functions report how many lines they dropped; the runner needs
 * the array's new length. Every adapter that can drop lines turns one into
 * the other, or the runner reads and frees lines that are already gone.
 */

static gint
removed(gint *n, gint count)
{
	*n -= count;

	return count;
}


static gint
adapt_dedupe_sorted(gchar ***lines, gint *n, gpointer data)
{
	return removed(n, lo_lines_dedupe_sorted(*lines, *n,
			(lo_sort_mode)GPOINTER_TO_INT(data), lo_collation_enabled()));
}

static gint
adapt_sort(gchar ***lines, gint *n, gpointer data)
{
	lo_sort_sort(*lines, *n, (lo_sort_mode)GPOINTER_TO_INT(data),
			lo_collation_enabled());

	return 0;       /* nothing was removed, only reordered */
}

static gint
adapt_dedupe_ordered(gchar ***lines, gint *n, G_GNUC_UNUSED gpointer data)
{
	return removed(n, lo_lines_dedupe_ordered(*lines, *n));
}

static gint
adapt_dedupe_consecutive(gchar ***lines, gint *n, G_GNUC_UNUSED gpointer data)
{
	return removed(n, lo_lines_dedupe_consecutive(*lines, *n));
}

static gint
adapt_remove_unique(gchar ***lines, gint *n, G_GNUC_UNUSED gpointer data)
{
	return removed(n, lo_lines_remove_unique(*lines, *n));
}

static gint
adapt_keep_unique(gchar ***lines, gint *n, G_GNUC_UNUSED gpointer data)
{
	return removed(n, lo_lines_keep_unique(*lines, *n));
}

static gint
adapt_remove_empty(gchar ***lines, gint *n, G_GNUC_UNUSED gpointer data)
{
	return removed(n, lo_lines_remove_empty(*lines, *n));
}

static gint
adapt_remove_whitespace(gchar ***lines, gint *n, G_GNUC_UNUSED gpointer data)
{
	return removed(n, lo_lines_remove_whitespace(*lines, *n));
}

static gint
adapt_trim_trailing(gchar ***lines, gint *n, G_GNUC_UNUSED gpointer data)
{
	return lo_lines_trim_trailing(*lines, *n);
}

static gint
adapt_trim_leading(gchar ***lines, gint *n, G_GNUC_UNUSED gpointer data)
{
	return lo_lines_trim_leading(*lines, *n);
}

static gint
adapt_join(gchar ***lines, gint *n, G_GNUC_UNUSED gpointer data)
{
	/* lo_lines_join() returns the negative of the lines it removed. */
	return removed(n, -lo_lines_join(*lines, *n, lo_info->join_separator));
}

static gint
adapt_split(gchar ***lines, gint *n, G_GNUC_UNUSED gpointer data)
{
	/* lo_lines_split() replaces the array and its count itself, and
	 * returns the positive number of lines it added. */
	return -lo_lines_split(lines, n, lo_info->split_separator);
}


/*
 * "Remove Every Nth Line" asks for its interval each time, because a
 * remembered value is a poor default when the previous run had nothing to
 * do with the current one.
 */
static gint
sci_remove_every_nth(GeanyEditor *editor, G_GNUC_UNUSED gpointer data)
{
	gdouble n = lo_info->remove_every_nth;
	gboolean ok;

	ok = dialogs_show_input_numeric(_("Remove every Nth line"),
			_("Value of N"), &n, 2, 1000, 1);

	if (!ok)
		return 0;

	lo_info->remove_every_nth = (gint)n;

	return lo_edit_remove_every_nth(editor, GINT_TO_POINTER((gint)n));
}


static gint
sci_move_up(GeanyEditor *editor, G_GNUC_UNUSED gpointer data)
{
	return lo_edit_move_lines(editor, GINT_TO_POINTER(TRUE));
}


static gint
sci_move_down(GeanyEditor *editor, G_GNUC_UNUSED gpointer data)
{
	return lo_edit_move_lines(editor, GINT_TO_POINTER(FALSE));
}


/* --------------------------------------------------------------------- */
/* Menu model                                                             */
/* --------------------------------------------------------------------- */

typedef enum
{
	LO_MENU_LINE_OP,      /* whole-line operation over the range */
	LO_MENU_SORT,         /* sort the range under 'mode'         */
	LO_MENU_SCI           /* Scintilla-level operation           */
} lo_menu_kind;

typedef struct
{
	const gchar *label;
	const gchar *kb_section_name;
	lo_menu_kind kind;
	lo_line_op_fn line_op;
	lo_sci_op_fn sci_op;
	lo_sort_mode mode;
} lo_menu_item;


/* A NULL label is a separator. */
static lo_menu_item menu_items[] =
{
	{ N_("_Duplicate Current Line"), "duplicate_current_line",
		LO_MENU_SCI, NULL, lo_edit_duplicate_lines, 0 },
	{ N_("_Move Line Up"), "move_line_up",
		LO_MENU_SCI, NULL, sci_move_up, 0 },
	{ N_("_Move Line Down"), "move_line_down",
		LO_MENU_SCI, NULL, sci_move_down, 0 },
	{ NULL, NULL, 0, NULL, NULL, 0 },

	{ N_("Remove _Duplicate Lines, _Sorted"), "remove_duplicate_lines_sorted",
		LO_MENU_LINE_OP, adapt_dedupe_sorted, NULL, 0 },
	{ N_("Remove Duplicate Lines, _Ordered"), "remove_duplicate_lines_ordered",
		LO_MENU_LINE_OP, adapt_dedupe_ordered, NULL, 0 },
	{ N_("Remove _Consecutive Duplicate Lines"),
		"remove_consecutive_duplicate_lines",
		LO_MENU_LINE_OP, adapt_dedupe_consecutive, NULL, 0 },
	{ N_("Remove _Unique Lines"), "remove_unique_lines",
		LO_MENU_LINE_OP, adapt_remove_unique, NULL, 0 },
	{ N_("Keep _Unique Lines"), "keep_unique_lines",
		LO_MENU_LINE_OP, adapt_keep_unique, NULL, 0 },
	{ NULL, NULL, 0, NULL, NULL, 0 },

	{ N_("Remove _Empty Lines"), "remove_empty_lines",
		LO_MENU_LINE_OP, adapt_remove_empty, NULL, 0 },
	{ N_("Remove _Whitespace Lines"), "remove_whitespace_lines",
		LO_MENU_LINE_OP, adapt_remove_whitespace, NULL, 0 },
	{ N_("Remove Every _Nth Line"), "remove_every_nth_line",
		LO_MENU_SCI, NULL, sci_remove_every_nth, 0 },
	{ NULL, NULL, 0, NULL, NULL, 0 },

	{ N_("Trim _Leading Whitespace"), "trim_leading_whitespace",
		LO_MENU_LINE_OP, adapt_trim_leading, NULL, 0 },
	{ N_("Trim _Trailing Whitespace"), "trim_trailing_whitespace",
		LO_MENU_LINE_OP, adapt_trim_trailing, NULL, 0 },
	{ N_("_Join Lines"), "join_lines",
		LO_MENU_LINE_OP, adapt_join, NULL, 0 },
	{ N_("S_plit Lines"), "split_lines",
		LO_MENU_LINE_OP, adapt_split, NULL, 0 },
	{ NULL, NULL, 0, NULL, NULL, 0 },

	/* ---- Notepad++ sort matrix ---- */
	{ N_("Sort _Ascending"), "sort_ascending",
		LO_MENU_SORT, adapt_sort, NULL, LO_SORT_NATURAL_ASC },
	{ N_("Sort _Descending"), "sort_descending",
		LO_MENU_SORT, adapt_sort, NULL, LO_SORT_NATURAL_DESC },
	{ NULL, NULL, 0, NULL, NULL, 0 },
	{ N_("Sort _Lexicographically Ascending"), "sort_lexicographically_ascending",
		LO_MENU_SORT, adapt_sort, NULL, LO_SORT_LEXICOGRAPHIC_ASC },
	{ N_("Sort _Lexicographically Descending"), "sort_lexicographically_descending",
		LO_MENU_SORT, adapt_sort, NULL, LO_SORT_LEXICOGRAPHIC_DESC },
	{ NULL, NULL, 0, NULL, NULL, 0 },
	{ N_("Sort _Ignoring Case Ascending"), "sort_ignoring_case_ascending",
		LO_MENU_SORT, adapt_sort, NULL, LO_SORT_NATURAL_NOCASE_ASC },
	{ N_("Sort _Ignoring Case Descending"), "sort_ignoring_case_descending",
		LO_MENU_SORT, adapt_sort, NULL, LO_SORT_NATURAL_NOCASE_DESC },
	{ NULL, NULL, 0, NULL, NULL, 0 },
	{ N_("Sort _Numerally (Decimal) Ascending"), "sort_numeral_decimal_ascending",
		LO_MENU_SORT, adapt_sort, NULL, LO_SORT_NUM_DEC_ASC },
	{ N_("Sort Numerally (Decimal) _Descending"), "sort_numeral_decimal_descending",
		LO_MENU_SORT, adapt_sort, NULL, LO_SORT_NUM_DEC_DESC },
	{ NULL, NULL, 0, NULL, NULL, 0 },
	{ N_("Sort Numerically (_Hexadecimal) Ascending"), "sort_numeral_hex_ascending",
		LO_MENU_SORT, adapt_sort, NULL, LO_SORT_NUM_HEX_ASC },
	{ N_("Sort Numerically (Hexadecimal) _Descending"), "sort_numeral_hex_descending",
		LO_MENU_SORT, adapt_sort, NULL, LO_SORT_NUM_HEX_DESC },
	{ NULL, NULL, 0, NULL, NULL, 0 },
	{ N_("Sort Numerically (_Octal) Ascending"), "sort_numeral_oct_ascending",
		LO_MENU_SORT, adapt_sort, NULL, LO_SORT_NUM_OCT_ASC },
	{ N_("Sort Numerically (Octal) _Descending"), "sort_numeral_oct_descending",
		LO_MENU_SORT, adapt_sort, NULL, LO_SORT_NUM_OCT_DESC },
};


/*
 * Menu entries in keybinding order.
 *
 * The keybinding API addresses items by a plain index, so separators cannot
 * occupy an index without also occupying a shortcut slot. This array holds
 * only the bindable entries, in the order they appear.
 */
static lo_menu_item *key_map[64];
static guint key_map_len = 0;


/* --------------------------------------------------------------------- */
/* Dispatch                                                               */
/* --------------------------------------------------------------------- */

static void
user_indicate(GeanyEditor *editor, gint delta, const gchar *what)
{
	if (delta < 0)
		ui_set_statusbar(FALSE,
				_("%s: %d line(s) added, %d removed."), what,
				-delta, 0);
	else
		ui_set_statusbar(FALSE,
				_("%s: %d line(s) affected."), what, delta);
}


/*
 * Run one menu entry. 'data' is the lo_menu_item the callback was bound to,
 * which is also how the keybinding path finds its target.
 */
static void
lo_activate(GtkMenuItem *menuitem, gpointer data)
{
	const lo_menu_item *item = data;
	GeanyDocument *doc = document_get_current();
	GeanyEditor *editor;
	gint delta;

	(void) menuitem;

	g_return_if_fail(item != NULL);

	if (doc == NULL)
		return;

	editor = doc->editor;

	switch (item->kind)
	{
		case LO_MENU_SORT:
			delta = lo_edit_run_line_op(editor, item->line_op,
					GINT_TO_POINTER(item->mode));
			user_indicate(editor, delta, lo_sort_mode_label(item->mode));
			break;

		case LO_MENU_LINE_OP:
			delta = lo_edit_run_line_op(editor, item->line_op, NULL);
			user_indicate(editor, delta, _(item->label));
			break;

		case LO_MENU_SCI:
			delta = lo_edit_run_sci_op(editor, item->sci_op, NULL);
			user_indicate(editor, delta, _(item->label));
			break;

		default:
			break;
	}
}


static void
lo_keybinding_callback(guint key_id)
{
	g_return_if_fail(key_id < key_map_len);

	lo_activate(NULL, key_map[key_id]);
}


/* --------------------------------------------------------------------- */
/* Plugin entry points                                                    */
/* --------------------------------------------------------------------- */

static gboolean
lo_init(GeanyPlugin *plugin, G_GNUC_UNUSED gpointer gdata)
{
	GeanyData *geany_data = plugin->geany_data;
	GeanyKeyGroup *key_group;
	GtkWidget *submenu;
	guint i;

	lo_init_prefs(plugin);

	main_menu_item = gtk_menu_item_new_with_mnemonic(
			_("_Line Operations++"));
	gtk_widget_show(main_menu_item);

	submenu = gtk_menu_new();
	gtk_widget_show(submenu);

	for (i = 0; i < G_N_ELEMENTS(menu_items); i++)
	{
		GtkWidget *item;

		if (menu_items[i].label == NULL)
		{
			item = gtk_separator_menu_item_new();
		}
		else
		{
			item = gtk_menu_item_new_with_mnemonic(_(menu_items[i].label));
			g_signal_connect(item, "activate", G_CALLBACK(lo_activate),
					&menu_items[i]);
			ui_add_document_sensitive(item);

			if (key_map_len < G_N_ELEMENTS(key_map))
				key_map[key_map_len++] = &menu_items[i];
		}

		gtk_widget_show(item);
		gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
	}

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(main_menu_item), submenu);
	gtk_container_add(GTK_CONTAINER(geany->main_widgets->tools_menu),
			main_menu_item);

	key_group = plugin_set_key_group(plugin, GEANY_LOPP_NAME,
			key_map_len, NULL);

	for (i = 0; i < key_map_len; i++)
		keybindings_set_item(key_group, i, lo_keybinding_callback, 0, 0,
				key_map[i]->kb_section_name,
				_(key_map[i]->label), NULL);

	return TRUE;
}


static void
lo_help(G_GNUC_UNUSED GeanyPlugin *plugin, G_GNUC_UNUSED gpointer pdata)
{
	utils_open_browser("https://plugins.geany.org/lineoperations.html");
}


static void
lo_cleanup(GeanyPlugin *plugin, gpointer pdata)
{
	(void) plugin;
	(void) pdata;

	gtk_widget_destroy(main_menu_item);
	main_menu_item = NULL;
	key_map_len = 0;

	lo_free_info();
}


G_MODULE_EXPORT
void
geany_load_module(GeanyPlugin *plugin)
{
	main_locale_init(LOCALEDIR, GETTEXT_PACKAGE);

	plugin->info->name        = _(GEANY_LOPP_NAME);
	plugin->info->description = _(
		"Notepad++'s line operations for Geany. Duplicate, move, join and "
		"split lines; remove duplicate, consecutive-duplicate, unique, "
		"empty and whitespace-only lines; trim whitespace; and sort with "
		"the full Notepad++ comparison matrix - natural, lexicographic, "
		"ignoring case, decimal, hexadecimal and octal, each ascending and "
		"descending. Every operation applies to the whole document, or to "
		"just the selection when there is one.");
	plugin->info->version     = "1.0";
	plugin->info->author      = "Sylvan Mostert <smostert.dev@gmail.com>";

	plugin->funcs->init       = lo_init;
	plugin->funcs->cleanup    = lo_cleanup;
	plugin->funcs->configure  = lo_configure;
	plugin->funcs->help       = lo_help;

	GEANY_PLUGIN_REGISTER(plugin, 225);
}
