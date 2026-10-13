/*
 *      lo_prefs.c - Preferences for the line operations plugin.
 *
 *      Ported from geany-plugins' lineoperations plugin
 *      Copyright 2015 Sylvan Mostert <smostert.dev@gmail.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#include "lo_prefs.h"


static struct
{
	GtkWidget *collation_cb;
	GtkWidget *join_entry;
	GtkWidget *split_entry;
	GtkWidget *nth_spin;
} config_widgets;


LineOpsInfo *lo_info = NULL;


gboolean
lo_collation_enabled(void)
{
	return lo_info != NULL && lo_info->use_collation_compare;
}


/*
 * Copy the widget values into lo_info and write them back to disk.
 *
 * Called on OK and on Apply, so an already-open dialog keeps applying
 * further changes rather than silently discarding them.
 */
static void
lo_save_prefs(void)
{
	GKeyFile *config = g_key_file_new();
	gchar *config_dir = g_path_get_dirname(lo_info->config_file);
	gchar *data;

	lo_info->use_collation_compare =
			gtk_toggle_button_get_active(
					GTK_TOGGLE_BUTTON(config_widgets.collation_cb));

	g_free(lo_info->join_separator);
	lo_info->join_separator = g_strdup(
			gtk_entry_get_text(GTK_ENTRY(config_widgets.join_entry)));

	g_free(lo_info->split_separator);
	lo_info->split_separator = g_strdup(
			gtk_entry_get_text(GTK_ENTRY(config_widgets.split_entry)));

	lo_info->remove_every_nth = gtk_spin_button_get_value_as_int(
			GTK_SPIN_BUTTON(config_widgets.nth_spin));

	g_key_file_load_from_file(config, lo_info->config_file, G_KEY_FILE_NONE,
			NULL);

	g_key_file_set_boolean(config, "general", "use_collation_compare",
			lo_info->use_collation_compare);
	g_key_file_set_string(config, "general", "join_separator",
			lo_info->join_separator);
	g_key_file_set_string(config, "general", "split_separator",
			lo_info->split_separator);
	g_key_file_set_integer(config, "general", "remove_every_nth",
			lo_info->remove_every_nth);

	if (!g_file_test(config_dir, G_FILE_TEST_IS_DIR) &&
			utils_mkdir(config_dir, TRUE) != 0)
	{
		dialogs_show_msgbox(GTK_MESSAGE_ERROR,
				_("Plugin configuration directory could not be created."));
	}
	else
	{
		data = g_key_file_to_data(config, NULL, NULL);
		utils_write_file(lo_info->config_file, data);
		g_free(data);
	}

	g_free(config_dir);
	g_key_file_free(config);
}


static void
lo_configure_response_cb(GtkDialog *dialog, gint response, gpointer user_data)
{
	(void) dialog;
	(void) user_data;

	if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_APPLY)
		lo_save_prefs();
}


static GtkWidget *
add_labelled(GtkWidget *box, const gchar *label, GtkWidget *widget)
{
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

	gtk_box_pack_start(GTK_BOX(row), gtk_label_new(label), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), widget, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 2);

	return row;
}


GtkWidget *
lo_configure(G_GNUC_UNUSED GeanyPlugin *plugin, GtkDialog *dialog,
		G_GNUC_UNUSED gpointer pdata)
{
	GtkWidget *vbox, *frame, *sep;

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

	frame = gtk_frame_new(_("Sorting"));
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 2);

	config_widgets.collation_cb = gtk_check_button_new_with_label(
			_("Use collation based string compare"));

	gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(config_widgets.collation_cb),
			lo_info->use_collation_compare);

	gtk_box_pack_start(GTK_BOX(gtk_bin_get_child(GTK_BIN(frame))),
			config_widgets.collation_cb, FALSE, FALSE, 2);

	sep = gtk_label_new(
			_("Affects the natural and lexicographic sorts only.\n"
			  "The numeric sorts always read the line as a number."));
	gtk_label_set_line_wrap(GTK_LABEL(sep), TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 2);

	frame = gtk_frame_new(_("Separators"));
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 2);

	config_widgets.join_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(config_widgets.join_entry),
			lo_info->join_separator);
	gtk_entry_set_width_chars(GTK_ENTRY(config_widgets.join_entry), 16);
	add_labelled(gtk_bin_get_child(GTK_BIN(frame)),
			_("Join lines with:"), config_widgets.join_entry);

	config_widgets.split_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(config_widgets.split_entry),
			lo_info->split_separator);
	gtk_entry_set_width_chars(GTK_ENTRY(config_widgets.split_entry), 16);
	add_labelled(gtk_bin_get_child(GTK_BIN(frame)),
			_("Split lines on:"), config_widgets.split_entry);

	sep = gtk_label_new(_("Leave the split separator empty to split on spaces."));
	gtk_label_set_line_wrap(GTK_LABEL(sep), TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 2);

	frame = gtk_frame_new(_("Remove every Nth line"));
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 2);

	config_widgets.nth_spin = gtk_spin_button_new_with_range(2, 1000, 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(config_widgets.nth_spin),
			lo_info->remove_every_nth);
	add_labelled(gtk_bin_get_child(GTK_BIN(frame)), _("Every:"),
			config_widgets.nth_spin);

	gtk_widget_show_all(vbox);
	g_signal_connect(dialog, "response",
			G_CALLBACK(lo_configure_response_cb), NULL);

	return vbox;
}


void
lo_init_prefs(GeanyPlugin *plugin)
{
	GeanyData *geany_data = plugin->geany_data;
	GKeyFile *config = g_key_file_new();

	lo_info = g_new0(LineOpsInfo, 1);
	lo_info->config_file = g_strconcat(geany->app->configdir,
			G_DIR_SEPARATOR_S, "plugins", G_DIR_SEPARATOR_S,
			LO_CONFIG_DIR, G_DIR_SEPARATOR_S, "general.conf", NULL);

	g_key_file_load_from_file(config, lo_info->config_file, G_KEY_FILE_NONE,
			NULL);

	lo_info->use_collation_compare = utils_get_setting_boolean(config,
			"general", "use_collation_compare", FALSE);

	lo_info->join_separator = g_strdup(
			utils_get_setting_string(config, "general",
				"join_separator", " "));

	lo_info->split_separator = g_strdup(
			utils_get_setting_string(config, "general",
				"split_separator", ", "));

	lo_info->remove_every_nth = utils_get_setting_integer(config,
			"general", "remove_every_nth", 2);

	g_key_file_free(config);
}


void
lo_free_info(void)
{
	if (lo_info == NULL)
		return;

	g_free(lo_info->config_file);
	g_free(lo_info->join_separator);
	g_free(lo_info->split_separator);
	g_free(lo_info);
	lo_info = NULL;
}
