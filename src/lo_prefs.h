/*
 *      lo_prefs.h - Preferences for the line operations plugin.
 *
 *      Ported from geany-plugins' lineoperations plugin
 *      Copyright 2015 Sylvan Mostert <smostert.dev@gmail.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#ifndef LO_PREFS_H
#define LO_PREFS_H

#include <geanyplugin.h>

#define LO_CONFIG_DIR   "lineoperations-plus"

typedef struct
{
	gchar *config_file;

	/*
	 * When enabled, characters are compared with g_utf8_collate() so that
	 * accented letters sort beside their base letters. When disabled they
	 * compare by code point. Applies to the natural and lexicographic sort
	 * modes only; the numeric modes never compare characters.
	 */
	gboolean use_collation_compare;

	/* Character used to join lines when joining a range. */
	gchar *join_separator;

	/* Substring that lines are split on. */
	gchar *split_separator;

	/* Remembered so the Nth-line dialog reopens on the last value. */
	gint remove_every_nth;
} LineOpsInfo;

extern LineOpsInfo *lo_info;

GtkWidget *lo_configure(GeanyPlugin *plugin, GtkDialog *dialog,
		gpointer pdata);

void lo_init_prefs(GeanyPlugin *plugin);
void lo_free_info(void);

/* TRUE when lines should be compared with locale collation. */
gboolean lo_collation_enabled(void);

#endif
