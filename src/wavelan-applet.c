/* 
 * Copyright (C) 2001 Free Software Foundation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Authors : Eskil Heyn Olsen <eskil@eskil.dk>
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <dirent.h>

#include <gnome.h>
#include <libgnomeui/gnome-window-icon.h>
#include <applet-widget.h>
#include <glade/glade.h>
#include <eel/eel-image.h>
#include <eel/eel-label.h>
#include <eel/eel-gdk-pixbuf-extensions.h>

#define CFG_DEVICE "eth0"
#define CFG_SHOW_PERCENT "FALSE"
#define CFG_SHOW_DIALOGS "FALSE"
#define CFG_UPDATE_INTERVAL "2"
#define CFG_TEXT_AA "TRUE"
#define CFG_THEME "default"

typedef struct {
	gchar *device;
	gchar *theme;
	gboolean show_percent, show_dialogs;
	gint update_interval;
	gboolean text_aa;

	gint text_smaller;
	gint smooth_font_size;
	GList *devices;
	GList *themes;

	/* contains a glist of char*, pointing to available images for the current theme */
	GList *images;
	/* contains a list of char*, pointing to the themes no-link-XX images (if any) */
	GList *no_link_images;
	/* contains a list of char*, pointing to the themes broken-XX images (if any) */
	GList *broken_images;
	/* contains pointers into the images GList. 0-100 are for battery, 101-201 are for AC */
	char *pixmaps[101];
	/* pointer to the current used file name */
	char *current_pixmap;
	/* set to true when the applet is display animated connection loss */
	gboolean flashing;
} Properties;

char *pixmap_extensions[] = 
{
	"png",
	"xpm",
	NULL
};

typedef enum {
	BUSTED_LINK,
	NO_LINK,	
	NONE
} AnimationState;

static GtkWidget *global_property_box = NULL;
static GtkWidget *global_applet = NULL;
static GtkWidget *glade_property_box = NULL;
static GladeXML *xml = NULL;
static gchar* glade_file=NULL;

static void show_error_dialog (gchar*,...);
static void show_warning_dialog (gchar*,...);
static void show_message_dialog (gchar*,...);
static int wavelan_applet_timeout_handler (GtkWidget *applet);

/******************************************************************/

static void
do_draw (GtkWidget *applet, gpointer data) {	
	guchar *rgb;
	int w, h, rowstride;
	GdkPixbuf *pixbuf;
	GdkColor colour;
	EelImage *pixmap;
	EelLabel *label;

	pixmap = EEL_IMAGE (gtk_object_get_data (GTK_OBJECT (applet), "pixmap"));
	label = EEL_LABEL (gtk_object_get_data (GTK_OBJECT (applet), "percent-label"));
	applet_widget_get_rgb_bg (APPLET_WIDGET (applet), 
				  &rgb, &w, &h,
				  &rowstride);

	pixbuf = gdk_pixbuf_new_from_data (rgb, GDK_COLORSPACE_RGB, FALSE, 8,
					   w, h, rowstride, 
					   (GdkPixbufDestroyNotify)g_free, NULL);
	
	eel_gdk_pixbuf_average_value (pixbuf, &colour);

	/*
	if (colour.red+colour.green+colour.blue < 32768*3) {
		eel_label_set_text_color (label, 0xFFFFFF);
	} else {
		eel_label_set_text_color (label, 0x000000);
	}
	*/

  	eel_image_set_tile_pixbuf (pixmap, pixbuf);
	eel_label_set_tile_pixbuf (label, pixbuf);

	gdk_pixbuf_unref (pixbuf);
}

static void 
wavelan_applet_draw (GtkWidget *applet, 
		     int percent)
{
	GtkWidget *pixmap;
	GtkWidget *pct_label;
	Properties *props;

	props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	pct_label = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "percent-label"));

	pixmap = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "pixmap"));

	if (!GTK_WIDGET_REALIZED (pixmap)) {
		show_warning_dialog ("WaveLan applet widget not realised");
		g_warning ("wavelan_applet_draw ! realised");
		return;
	}

	/* Update the percentage */
	/* if (props->show_percent) */ {
		char *tmp;
		if (percent >= 0) {
			tmp = g_strdup_printf ("%2.0d%%", percent);
		} else {
			tmp = g_strdup_printf ("N/A");
		}
		if (g_strcasecmp (tmp, eel_label_get_text (EEL_LABEL (pct_label))) != 0) {
			applet_widget_set_tooltip (APPLET_WIDGET (applet),
						   tmp);
			eel_label_set_text (EEL_LABEL (pct_label), tmp);
		}
		g_free (tmp);
	}

	/* Update the image */
	if (percent >= 0 && percent <= 100) {
		g_message ("%p != %p = %s", 
			   props->pixmaps[percent], props->current_pixmap, 
			   props->pixmaps[percent] != props->current_pixmap ? "TRUE" : "FALSE");

		if (props->pixmaps[percent] != props->current_pixmap) {
			props->current_pixmap = props->pixmaps[percent];
			if ( !props->flashing) {
				eel_image_set_pixbuf_from_file_name (EEL_IMAGE (pixmap), props->current_pixmap);
			}
			g_message ("loading image %d = %s",
				   percent,
				   props->current_pixmap);
		}
	} 
}


static gboolean
wavelan_applet_animate_timeout (GtkWidget *applet) 
{	
	static int num = 0;
	Properties *properties;
	GtkWidget *pixmap;
	GList *image;
	GList *animation_list = NULL;
	AnimationState state = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (applet), "animation_state"));
	
	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	switch (state) {
	case NO_LINK:
		animation_list = properties->no_link_images;
		break;
	case BUSTED_LINK:
		animation_list = properties->broken_images;
		break;
	default:
		g_assert_not_reached ();
		break;
	};

	pixmap = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "pixmap"));
	if (num >= g_list_length (animation_list)) {
		num = 0;
	}

	image = g_list_nth (animation_list, num);
	g_message ("animating %d of %d to %s", num, g_list_length (animation_list), (char*)image->data);
	eel_image_set_pixbuf_from_file_name (EEL_IMAGE (pixmap), (char*)image->data);	
	num++;

	return TRUE;
}

static void
wavelan_applet_start_animation (GtkWidget *applet) 
{
	GtkWidget *pixmap;
	guint timeout_handler_id;

	timeout_handler_id = gtk_timeout_add (500, 
					      (GtkFunction)wavelan_applet_animate_timeout,
					      applet);
	pixmap = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "pixmap"));
	gtk_object_set_data (GTK_OBJECT (applet), 
			     "animate_timer", 
			     GINT_TO_POINTER (timeout_handler_id));
}

static void
wavelan_applet_stop_animation (GtkWidget *applet) 
{
	GtkWidget *pixmap;
	guint timeout_handler_id = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (applet),
									 "animate_timer"));
	Properties *properties;

	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	gtk_timeout_remove (timeout_handler_id);
	pixmap = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "pixmap"));
	eel_image_set_pixbuf_from_file_name (EEL_IMAGE (pixmap), properties->current_pixmap);	
}

static void
wavelan_applet_animation_state (GtkWidget *applet) 
{
	Properties *properties;

	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	if (properties->flashing == FALSE) {
		wavelan_applet_start_animation (applet);
		properties->flashing = TRUE;
	}
}

static void
wavelan_applet_update_state (GtkWidget *applet,
			     char *device,
			     double link,
			     long int level,
			     long int noise)
{
	Properties *properties;
	int percent;

	/* Calculate the percentage based on the link quality */
	if (level < 0) {
		percent = -1;
	} else {
		if (link<1) {
			percent = 0;
		} else {
			percent = (int)rint ((log (link) / log (92)) * 100.0);
		}
	}
	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	if (percent < 0) {
		gtk_object_set_data (GTK_OBJECT (applet), "animate_state", GINT_TO_POINTER (BUSTED_LINK));
		wavelan_applet_animation_state (applet);
	} else if (percent == 0) {
		gtk_object_set_data (GTK_OBJECT (applet), "animate_state", GINT_TO_POINTER (NO_LINK));
		wavelan_applet_animation_state (applet);
	} else if (properties->flashing) {
		gtk_object_set_data (GTK_OBJECT (applet), "animate_state", GINT_TO_POINTER (NONE));
		wavelan_applet_stop_animation (applet);
	}

	wavelan_applet_draw (applet, percent);

/*
	if (properties->show_dialogs) {
		if (new_state == BUSTED_LINK && org_state != BUSTED_LINK) {
			show_warning_dialog (_("Network device %s has disappeared."), device);
		}
		if (new_state != BUSTED_LINK && org_state == BUSTED_LINK) {
			show_message_dialog (_("Network device %s has reappeared."), device);
		}
		if (new_state == NO_LINK && org_state != NO_LINK) {
			show_warning_dialog (_("Network device %s has lost the connection."), device);
		}
		if (new_state != NO_LINK && org_state == NO_LINK) {
			show_message_dialog (_("Network device %s has connection."), device);
		}
	}
*/
	
}

static void
wavelan_applet_load_theme_image (GtkWidget *applet, 
				 Properties *properties,
				 const char *themedir,
				 const char *filename) 
{
	int j;
	char *dot_pos = strrchr (filename, '.') + 1; /* only called if a previous strrchr worked */

	/* Check the allowed extensions */
	for (j = 0; pixmap_extensions[j]; j++) {
		if (strcasecmp (dot_pos, pixmap_extensions[j])==0) { 
			int i;
			int pixmap_offset_begin = 0;
			int pixmap_offset_end = 0;
			char *dupe;
			gboolean check_range = FALSE;

			/* g_message ("Located theme image %s", filename); */
			if (strncmp (filename, "signal-", 7) == 0) {
				sscanf (filename, "signal-%d-%d.",
					&pixmap_offset_begin, &pixmap_offset_end);
				check_range = TRUE;
			} else if (strncmp (filename, "no-link-", 8) == 0) {
				properties->no_link_images = g_list_prepend (properties->no_link_images,
									     g_strdup_printf ("%s/%s", 
											      themedir, 
											      filename));
			} else if (strncmp (filename, "broken-", 7) == 0) {
				properties->broken_images = g_list_prepend (properties->broken_images,
									    g_strdup_printf ("%s/%s", 
											     themedir, 
											     filename));
			} else {
				/* uhm, not charging or battery or low, but an 
				   image ? could be wrongly named */
				show_warning_dialog ("Theme %s has an odd image.\n"
						     "%s", 
						     properties->theme, filename);
			}

			if (check_range) {
				dupe = g_strdup_printf ("%s/%s", themedir, filename);
				properties->images = g_list_prepend (properties->images,
								     dupe);
				for (i = pixmap_offset_begin; i <= pixmap_offset_end; i++) {
					if (properties->pixmaps[i] != NULL) {
						show_warning_dialog ("Probable image overlap in\n"
								     "%s.", filename);
						
					} else {
						properties->pixmaps[i] = dupe;
					}
				}
			}

		}
	}	
}

static void
wavelan_applet_load_theme (GtkWidget *applet) {
	Properties *properties;
	DIR *dir;
	struct dirent *dirent;
	char *pixmapdir;
	char *themedir;

	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	pixmapdir = gnome_unconditional_pixmap_file (PACKAGE);
	themedir = g_strdup_printf ("%s/%s", pixmapdir, properties->theme);
	dir = opendir (themedir);

	/* blank out */
	if (properties->images) {
		int j;
		g_list_foreach (properties->no_link_images, (GFunc)g_free, NULL);
		g_list_free (properties->no_link_images);
		properties->no_link_images = NULL;

		g_list_foreach (properties->broken_images, (GFunc)g_free, NULL);
		g_list_free (properties->broken_images);
		properties->broken_images = NULL;

		g_list_foreach (properties->images, (GFunc)g_free, NULL);
		g_list_free (properties->images);
		properties->images = NULL;
		for (j=0; j < 101; j++) {
			properties->pixmaps[j] = NULL;
		}
	}

	if (!dir) {
		show_error_dialog (_("No themes installed"));
	} else 
		while ((dirent = readdir (dir)) != NULL) {
			if (*dirent->d_name != '.') {
				if (strrchr (dirent->d_name, '.')!=NULL) {
					wavelan_applet_load_theme_image (applet, 
									 properties,
									 themedir,
									 dirent->d_name);
				}
			}
		}

	if (properties->no_link_images && g_list_length (properties->no_link_images) > 1) {
		properties->no_link_images = g_list_sort (properties->no_link_images,
							  (GCompareFunc)g_strcasecmp);
	}
	
	if (properties->broken_images && g_list_length (properties->broken_images) > 1) {
		properties->broken_images = g_list_sort (properties->broken_images,
							 (GCompareFunc)g_strcasecmp);
	}

	g_free (pixmapdir);
	g_free (themedir);
}

static void
wavelan_applet_set_theme (GtkWidget *applet, gchar *theme) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	g_free (props->theme);
	props->theme = g_strdup (theme);
	/* load the new images and update the gtk widgets */
	wavelan_applet_load_theme (applet);
	wavelan_applet_draw (global_applet, 0);  
}

static void
wavelan_applet_set_device (GtkWidget *applet, gchar *device) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	g_free (props->device);
	props->device = g_strdup (device);
}

static void
wavelan_applet_set_update_interval (GtkWidget *applet, int interval) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	guint timeout_handler_id = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (applet), "timeout_handler_id"));

	props->update_interval = interval;

	gtk_timeout_remove (timeout_handler_id);

	timeout_handler_id = gtk_timeout_add (props->update_interval * 1000, 
					      (GtkFunction)wavelan_applet_timeout_handler, 
					      applet);
	gtk_object_set_data (GTK_OBJECT (applet), 
			     "timeout_handler_id", 
			     GINT_TO_POINTER (timeout_handler_id));

}

static void
wavelan_applet_set_text_smaller (GtkWidget *applet, gint how_much) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	GtkWidget *pct_label  = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "percent-label"));

	props->text_smaller = how_much;
	eel_label_make_smaller (EEL_LABEL (pct_label), props->text_smaller);
	//props->smooth_font_size = eel_label_get_smooth_font_size (EEL_LABEL (pct_label));
	do_draw (applet, NULL);
}

static void
wavelan_applet_set_text_aa (GtkWidget *applet, gboolean text_aa) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	GtkWidget *pct_label  = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "percent-label"));

	props->text_aa = text_aa;

	if (props->show_percent) {
		gtk_widget_hide_all (pct_label);
	}

	/* reeducate label */
	eel_label_set_smooth_font_size (EEL_LABEL (pct_label), props->smooth_font_size);
	eel_label_set_is_smooth (EEL_LABEL (pct_label), props->text_aa);

	if (props->show_percent) {
		gtk_widget_show_all (pct_label);
	}
}

static void
wavelan_applet_set_show_percent (GtkWidget *applet, gboolean show) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	GtkWidget *pct_label  = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (applet), "percent-label"));
	props->show_percent = show;
	if (props->show_percent) {
		/* reeducate label */
		eel_label_set_smooth_font_size (EEL_LABEL (pct_label), props->smooth_font_size);
		eel_label_set_is_smooth (EEL_LABEL (pct_label), props->text_aa);
		gtk_widget_show_all (pct_label);
	} else {
		//props->smooth_font_size = eel_label_get_smooth_font_size (EEL_LABEL (pct_label));
		gtk_widget_hide_all (pct_label);
	}
	do_draw (applet, NULL);
}

static void
wavelan_applet_set_show_dialogs (GtkWidget *applet, gboolean show) {
	Properties *props = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	props->show_dialogs = show;
}

/* check stats, modify the state attribute and return TRUE 
   if redraw is needed */
static void
wavelan_applet_read_device_state (GtkWidget *applet)
{
	Properties *properties;
	FILE *wireless;
	
	long int level, noise;
	double link;
	char device[256];
	char line[256];
	
	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	wireless = (FILE*) (gtk_object_get_data (GTK_OBJECT (applet), "file"));

	/* resest list of available wavelan devices */
	g_list_foreach (properties->devices, (GFunc)g_free, NULL);
	g_list_free (properties->devices);
	properties->devices = NULL;

	/* Here we begin to suck... */
	do {
		char *ptr;

		fgets (line, 256, wireless);

		if (feof (wireless)) {
			break;
		}

		if (line[6] == ':') {
			char *tptr = line;
			while (isspace (*tptr)) tptr++;
			strncpy (device, tptr, 6);
			(*strchr(device, ':')) = 0;
			ptr = line + 12;

			/* Add the devices encountered to the list of possible devices */
			properties->devices = g_list_prepend (properties->devices, g_strdup (device));

			/*
			g_message ("prop->dev = %s, dev = %s, cmp = %d", 
				   properties->device, device,
				   g_strcasecmp (properties->device, device));			
			*/

			/* is it the one we're supposed to monitor ? */
			if (g_strcasecmp (properties->device, device)==0) {
				link = strtod (ptr, &ptr);
				ptr++;
				
				level = strtol (ptr, &ptr, 10);
				ptr++;

				noise = strtol (ptr, &ptr, 10);
				ptr++;
				
				wavelan_applet_update_state (applet, device, link, level, noise);
			}
		}
	} while (1);

	if (g_list_length (properties->devices)==1) {
		wavelan_applet_set_device (applet, (char*)properties->devices->data);
	} else {
		wavelan_applet_update_state (applet, properties->device, -1, -1, -1);
	}

	/* rewind the /proc/net/wireless file */
	rewind (wireless);
}

static int
wavelan_applet_timeout_handler (GtkWidget *applet)
{
	FILE *wireless;

	wireless = (FILE*) (gtk_object_get_data (GTK_OBJECT (applet), "file"));

	if (wireless == NULL) {
		return FALSE;
	}

	wavelan_applet_read_device_state (applet);

	return TRUE;
}


static void 
show_error_dialog (gchar *mesg,...) 
{
	GtkWidget *dialogWindow;
	char *tmp;
	va_list ap;

	va_start (ap,mesg);
	tmp = g_strdup_vprintf (mesg,ap);
	dialogWindow = gnome_message_box_new (tmp,GNOME_MESSAGE_BOX_ERROR,
					     GNOME_STOCK_BUTTON_OK,NULL);
	gnome_dialog_run_and_close (GNOME_DIALOG (dialogWindow));
	g_free (tmp);
	va_end (ap);
}

static void 
show_warning_dialog (gchar *mesg,...) 
{
	GtkWidget *dialogWindow;
	char *tmp;
	va_list ap;

	va_start (ap,mesg);
	tmp = g_strdup_vprintf (mesg,ap);
	dialogWindow = gnome_message_box_new (tmp,GNOME_MESSAGE_BOX_WARNING,
					     GNOME_STOCK_BUTTON_OK,NULL);
	gnome_dialog_run_and_close (GNOME_DIALOG (dialogWindow));
	g_free (tmp);
	va_end (ap);
}


static void 
show_message_dialog (char *mesg,...)
{
	GtkWidget *dialogWindow;
	char *tmp;
	va_list ap;

	va_start (ap,mesg);
	tmp = g_strdup_vprintf (mesg,ap);
	dialogWindow = gnome_message_box_new (tmp,GNOME_MESSAGE_BOX_GENERIC,
					     GNOME_STOCK_BUTTON_OK,NULL);
	gnome_dialog_run_and_close (GNOME_DIALOG (dialogWindow));
	g_free (tmp);
	va_end (ap);
}

static void
start_file_read (GtkWidget *applet)
{
	FILE *file;

	file = fopen ("/proc/net/wireless", "rt");
	if (file == NULL) {
		applet_widget_set_tooltip (APPLET_WIDGET (applet),
					   _("No /proc/net/wireless"));
		show_error_dialog (_("Cannot read /proc/net/wireless"));
	} else {
		gtk_object_set_data (GTK_OBJECT (applet), "file", file);
	}	
}

static void
wavelan_applet_load_properties (GtkWidget *applet)
{
	Properties *properties;
	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	gnome_config_push_prefix (APPLET_WIDGET (applet)->privcfgpath);
	properties->device = gnome_config_get_string ("wavelan/device=" CFG_DEVICE);
	properties->show_percent = gnome_config_get_bool ("wavelan/show_percent=" CFG_SHOW_PERCENT);
	properties->show_dialogs = gnome_config_get_bool ("wavelan/show_dialogs=" CFG_SHOW_DIALOGS);
	properties->update_interval = gnome_config_get_int ("wavelan/update_interval=" CFG_UPDATE_INTERVAL);
	properties->text_aa = gnome_config_get_bool ("wavelan/text_aa=" CFG_TEXT_AA);
	properties->theme = gnome_config_get_string ("wavelan/theme=" CFG_THEME);

	properties->text_smaller = 5;
	gnome_config_pop_prefix ();
}

static void
wavelan_applet_save_properties (GtkWidget *applet, 
				const char *cfgpath, 
				const char *globalcfgpath) 
{
	Properties *properties;
	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");
	g_assert (properties);
	
	gnome_config_push_prefix (cfgpath);
	
	gnome_config_set_string ("wavelan/device", properties->device);
	gnome_config_set_bool ("wavelan/show_percent", properties->show_percent);
	gnome_config_set_bool ("wavelan/show_dialogs", properties->show_dialogs);
	gnome_config_set_int ("wavelan/update_interval", properties->update_interval);
	gnome_config_set_bool ("wavelan/text_aa", properties->text_aa);
	gnome_config_set_string ("wavelan/theme", properties->theme);
	
	gnome_config_pop_prefix ();
	gnome_config_sync ();
	gnome_config_drop_all ();
}

static void
wavelan_applet_apply_properties_cb (GtkWidget *pb, gpointer unused)
{
	GtkWidget *entry;
	GtkWidget *menu;
	char *str;

	/* Get all the properties and update the applet */
	entry = gtk_object_get_data (GTK_OBJECT (pb), "show-percent-button");
	wavelan_applet_set_show_percent (global_applet, 
					 gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (entry)));

	entry = gtk_object_get_data (GTK_OBJECT (pb), "show-dialog-button");
	wavelan_applet_set_show_dialogs (global_applet, 
					 gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (entry)));

	entry = gtk_object_get_data (GTK_OBJECT (pb), "update-interval-spin");
	wavelan_applet_set_update_interval (global_applet, 
					    gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (entry)));

	entry = gtk_object_get_data (GTK_OBJECT (pb), "text-smaller");
	wavelan_applet_set_text_smaller (global_applet, 
					 gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (entry)));

	entry = gtk_object_get_data (GTK_OBJECT (pb), "text-aa");
	wavelan_applet_set_text_aa (global_applet, 
				    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (entry)));

	entry = gtk_object_get_data (GTK_OBJECT (pb), "device-menu");
	menu = gtk_menu_get_active (GTK_MENU (gtk_option_menu_get_menu (GTK_OPTION_MENU (entry))));
	str = gtk_object_get_data (GTK_OBJECT (menu), "device-selected");
	wavelan_applet_set_device (global_applet, str);

	entry = gtk_object_get_data (GTK_OBJECT (pb), "theme-menu");
	menu = gtk_menu_get_active (GTK_MENU (gtk_option_menu_get_menu (GTK_OPTION_MENU (entry))));
	str = gtk_object_get_data (GTK_OBJECT (menu), "theme-selected");
	wavelan_applet_set_theme (global_applet, str);

	/* Save the properties */
	wavelan_applet_save_properties (global_applet, 
					APPLET_WIDGET (global_applet)->privcfgpath,
					APPLET_WIDGET (global_applet)->globcfgpath);
}

static void
wavelan_applet_option_change (GtkWidget *widget, gpointer user_data) {
	GnomePropertyBox *box = GNOME_PROPERTY_BOX (user_data);
	gnome_property_box_changed (box);
}

static void
wavelan_applet_add_theme_menu (GtkWidget *applet,
			       GtkWidget *property_box,
			       GtkWidget *menu,
			       const char *entry)
{
	GtkWidget *item;
	item = gtk_menu_item_new_with_label (entry);
	gtk_object_set_data_full (GTK_OBJECT (item), 
				  "theme-selected",
				  g_strdup (entry),
				  g_free);
	gtk_signal_connect (GTK_OBJECT (item),
			    "activate",
			    GTK_SIGNAL_FUNC (wavelan_applet_option_change),
			    property_box);
	gtk_menu_append (GTK_MENU (menu), item);
}

static void 
wavelan_applet_properties_dialog (GtkWidget *applet, 
				  gpointer data)
{
	GtkWidget *pb;
	GtkWidget *theme, *pct, *dialog, *device, *interval, *text_aa;
	/* GtkWidget , *text_interval; */
	Properties *properties;
	static GnomeHelpMenuEntry help_entry = {"wavelan-applet","properties"};

	properties = gtk_object_get_data (GTK_OBJECT (applet), "properties");

	if (global_property_box == NULL) {
		xml = glade_xml_new (glade_file,"propertybox1");
		glade_property_box = glade_xml_get_widget (xml,"propertybox1");		
	}

	pb = glade_property_box;

	gtk_window_set_title (GTK_WINDOW (pb), _("WaveLan applet properties"));
	theme = glade_xml_get_widget (xml, "theme_menu");
	pct = glade_xml_get_widget (xml, "pct_check_button");
	dialog = glade_xml_get_widget (xml, "dialog_check_button");
	device = glade_xml_get_widget (xml, "device_menu");
	interval = glade_xml_get_widget (xml, "interval_spin");
	/*text_interval = glade_xml_get_widget (xml, "text_interval_spin");*/
	text_aa = glade_xml_get_widget (xml, "text_aa");

	/* Set the show-percent thingy */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pct),
				      properties->show_percent);
	gtk_signal_connect (GTK_OBJECT (pct),
			    "toggled",
			    GTK_SIGNAL_FUNC (wavelan_applet_option_change),
			    pb);
	gtk_object_set_data (GTK_OBJECT (pb), "show-percent-button", pct);

	/* Set the show-dialog thingy */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog),
				      properties->show_dialogs);
	gtk_signal_connect (GTK_OBJECT (dialog),
			    "toggled",
			    GTK_SIGNAL_FUNC (wavelan_applet_option_change),
			    pb);
	gtk_object_set_data (GTK_OBJECT (pb), "show-dialog-button", dialog);

	/* Set the update interval thingy */
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (interval), properties->update_interval);
	gtk_signal_connect (GTK_OBJECT (interval),
			    "changed",
			    GTK_SIGNAL_FUNC (wavelan_applet_option_change),
			    pb);
	gtk_object_set_data (GTK_OBJECT (pb), "update-interval-spin", interval);

	/* Set the text resize thingy */
/*
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (text_interval), properties->text_smaller);
	gtk_signal_connect (GTK_OBJECT (text_interval),
			    "changed",
			    GTK_SIGNAL_FUNC (wavelan_applet_option_change),
			    pb);
	gtk_object_set_data (GTK_OBJECT (pb), "text-smaller", text_interval);
*/


	/* Set the show-percent thingy */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (text_aa),
				      properties->text_aa);
	gtk_signal_connect (GTK_OBJECT (text_aa),
			    "toggled",
			    GTK_SIGNAL_FUNC (wavelan_applet_option_change),
			    pb);
	gtk_object_set_data (GTK_OBJECT (pb), "text-aa", text_aa);

	/* Set the theme menu */
	gtk_option_menu_remove_menu (GTK_OPTION_MENU (theme));
	{
		char *pixmapdir;
		DIR *dir;
		struct dirent *dirent;
		GtkWidget *menu;
		int idx = 0, choice = 0;

		pixmapdir = gnome_unconditional_pixmap_file (PACKAGE);
		menu = gtk_menu_new ();

		dir = opendir (pixmapdir);
		/* scan the directory */
		if (!dir) {
			wavelan_applet_add_theme_menu (applet, pb, menu, "no themes");
		} else 
			while ((dirent = readdir (dir)) != NULL) {
				/* everything not a ./.. (I assume there's only themes dirs there 
				   gets added to the menu list */
				if (*dirent->d_name != '.') {
					wavelan_applet_add_theme_menu (applet, pb, menu,
								       (const char*)dirent->d_name);
					if (strcmp (properties->theme, dirent->d_name)==0) {
						choice = idx;
					}
					idx++;
				}
			}
		closedir (dir);
		gtk_option_menu_set_menu (GTK_OPTION_MENU (theme), menu);
		gtk_option_menu_set_history (GTK_OPTION_MENU (theme), choice);
	}
	gtk_object_set_data (GTK_OBJECT (pb), "theme-menu", theme);

        /* Set the device menu */
	gtk_option_menu_remove_menu (GTK_OPTION_MENU (device));
	{
		GtkWidget *menu;
		GtkWidget *item;
		GList *d;
		int idx = 0, choice = 0;

		menu = gtk_menu_new ();
		
		for (d = properties->devices; d; d = g_list_next (d)) {
			item = gtk_menu_item_new_with_label ((char*)d->data);
			gtk_menu_append (GTK_MENU (menu), item);
			gtk_object_set_data_full (GTK_OBJECT (item), 
						  "device-selected",
						  g_strdup (d->data),
						  g_free);
			gtk_signal_connect (GTK_OBJECT (item),
					    "activate",
					    GTK_SIGNAL_FUNC (wavelan_applet_option_change),
					    pb);
			if (strcmp (properties->device, d->data)==0) {
				choice = idx;
				
			}
			idx++;
		}
		gtk_option_menu_set_menu (GTK_OPTION_MENU (device), menu);
		gtk_option_menu_set_history (GTK_OPTION_MENU (device), choice);
	}
	gtk_object_set_data (GTK_OBJECT (pb), "device-menu", device);

	gtk_signal_connect (GTK_OBJECT (pb), 
			   "apply", 
			   GTK_SIGNAL_FUNC (wavelan_applet_apply_properties_cb),
			   NULL);
	gtk_signal_connect (GTK_OBJECT (pb),
			   "destroy",
			   GTK_SIGNAL_FUNC (gtk_widget_destroy),
			   pb);
	gtk_signal_connect (GTK_OBJECT (pb),
			   "help",
			   GTK_SIGNAL_FUNC (gnome_help_pbox_display),
			   &help_entry);

	gtk_widget_show_all (pb);
}

static gint 
wavelan_applet_clicked_cb (GtkWidget *applet, 
			   GdkEventButton *e, 
			   gpointer data)
{
	return TRUE; 
}

static void
wavelan_applet_about_cb (AppletWidget *widget, gpointer data)
{
	GtkWidget *about;
	const gchar *authors[] = {"Eskil Heyn Olsen <eskil@eskil.org>",NULL};
	gchar version[] = VERSION;

	about = gnome_about_new (_("WaveLan Applet"), 
				version,
				_("(C) 2001 Free Software Foundation "),
				(const gchar**)authors,
				_("Yet another applet that shows the\n"
				  "waterlevel in Sortedamssøen."),
				NULL);
	gtk_widget_show (about);

	return;
}

static gint
wavelan_applet_save_session (GtkWidget *applet,
			     const char *cfgpath,
			     const char *globcfgpath,
			     gpointer data)
{
	wavelan_applet_save_properties (applet, cfgpath, globcfgpath);
	return FALSE;
}

static void
wavelan_applet_destroy (GtkWidget *applet,gpointer horse)
{
}

static GtkWidget *
wavelan_applet_new (GtkWidget *applet)
{
	GtkWidget *event_box;
	GtkWidget *box;

	/* This is all the data that we associate with the applet instance */
	Properties *properties;
	GtkWidget *pixmap; 
	GtkWidget *pct_label;
	guint timeout_handler_id;

	properties = g_new0 (Properties, 1);
	gtk_object_set_data (GTK_OBJECT (applet), "properties", properties);

	/* this ensures that properties are loaded and 'properties' points to them */
	wavelan_applet_load_properties (applet);
	wavelan_applet_load_theme (applet);

	event_box = gtk_event_box_new ();

	/* construct pixmap widget */
	pixmap = eel_image_new (properties->pixmaps[0]);
	gtk_object_set_data (GTK_OBJECT (applet), "pixmap", pixmap);
	gtk_widget_show_all (pixmap);

	/* construct pct widget */
	pct_label = eel_label_new (NULL);
	eel_label_set_never_smooth (EEL_LABEL (pct_label), FALSE);
	eel_label_set_is_smooth (EEL_LABEL (pct_label), properties->text_aa);       
	eel_label_make_smaller (EEL_LABEL (pct_label), properties->text_smaller);
	gtk_object_set_data (GTK_OBJECT (applet), "percent-label", pct_label);
	properties->smooth_font_size = eel_label_get_smooth_font_size (EEL_LABEL (pct_label));

	if (properties->show_percent) {
		gtk_widget_show_all (pct_label);
	} else {
		gtk_widget_hide_all (pct_label);
	}
	
	/* pack */
	box = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), pixmap, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), pct_label, FALSE, FALSE, 0);
	/* note, I don't use show_all, because this way the percent label is only
	   realised if it's enabled */
	gtk_widget_show (box);

	gtk_widget_set_events (event_box, gtk_widget_get_events (applet) |
			      GDK_BUTTON_PRESS_MASK);

	gtk_signal_connect (GTK_OBJECT (event_box), "button-press-event",
			   GTK_SIGNAL_FUNC (wavelan_applet_clicked_cb), NULL);


	gtk_container_add (GTK_CONTAINER (event_box), box);
  
	gtk_signal_connect (GTK_OBJECT (applet),"save_session",
			   GTK_SIGNAL_FUNC (wavelan_applet_save_session),
			   NULL);
	gtk_signal_connect (GTK_OBJECT (applet),"destroy",
			   GTK_SIGNAL_FUNC (wavelan_applet_destroy),NULL);

	gtk_signal_connect (GTK_OBJECT (applet), "do_draw", 
			    GTK_SIGNAL_FUNC (do_draw), 
			    NULL);
	applet_widget_send_draw(APPLET_WIDGET(applet), TRUE);
	do_draw (applet, NULL);

	applet_widget_register_stock_callback (APPLET_WIDGET (applet),
					      "properties",
					      GNOME_STOCK_MENU_PROP,
					      _("Properties..."),
					      GTK_SIGNAL_FUNC (wavelan_applet_properties_dialog),
					      NULL);	
	applet_widget_register_stock_callback (APPLET_WIDGET (applet),
					      "about",
					      GNOME_STOCK_MENU_ABOUT,
					      _("About..."),
					      GTK_SIGNAL_FUNC (wavelan_applet_about_cb),
					      NULL);	

	timeout_handler_id = gtk_timeout_add (properties->update_interval * 1000, 
					      (GtkFunction)wavelan_applet_timeout_handler, 
					      applet);
	gtk_object_set_data (GTK_OBJECT (applet), 
			     "timeout_handler_id", 
			     GINT_TO_POINTER (timeout_handler_id));
      
	return event_box;
}

static GtkWidget * applet_start_new_applet (const gchar *goad_id, const char **params, int nparams) {
	GtkWidget *wavelan_applet;
  
	if (strcmp (goad_id, "wavelan-applet")) {
		show_warning_dialog ("goad_id = %s", goad_id);
		return NULL;
	}
     
	global_applet = applet_widget_new (goad_id);
	if (!global_applet) {
		g_error (_("Can't create applet!\n"));
	}
  
	gtk_widget_realize (global_applet);
	wavelan_applet = wavelan_applet_new (global_applet);
	gtk_widget_show (wavelan_applet);

	applet_widget_add (APPLET_WIDGET (global_applet), wavelan_applet);
	gtk_widget_show (global_applet);
  
	return global_applet;
}

static CORBA_Object
applet_activator (CORBA_Object poa,
		  const char *goad_id,
		  const char **params,
		  gpointer *impl_ptr,
		  CORBA_Environment *ev)
{
	GtkWidget *pilot;

	global_applet = applet_widget_new (goad_id);
	if (!global_applet) {
		g_error (_("Can't create applet!\n"));
	}

	gtk_widget_realize (global_applet);
	pilot = wavelan_applet_new (global_applet);
	start_file_read (global_applet);
	gtk_widget_show (pilot);

  
	applet_widget_add (APPLET_WIDGET (global_applet), pilot);
	gtk_widget_show (global_applet);

	/* make the applet draw a 0% strength */
	wavelan_applet_draw (global_applet, 0);  

	return applet_widget_corba_activate (global_applet, 
					     (PortableServer_POA)poa, 
					     goad_id, 
					     params,
					     impl_ptr, 
					     ev);
}

static void
applet_deactivator (CORBA_Object poa,
		    const char *goad_id,
		    gpointer impl_ptr,
		    CORBA_Environment *ev)
{
	applet_widget_corba_deactivate ((PortableServer_POA)poa, 
					goad_id, 
					impl_ptr, 
					ev);
}

#if defined (SHLIB_APPLETS)
static const char *repo_id[]={"IDL:GNOME/Applet:1.0", NULL};
static GnomePluginObject applets_list[] = {
	{repo_id, "wavelan-applet", NULL, "wavelan applet",
	 &wavelan_applet_activator, &wavelan_applet_deactivator},
	{NULL}
};

GnomePlugin GNOME_Plugin_info = {
	applets_list, NULL
};
#else
int
main (int argc, char *argv[])
{
	gpointer wavelan_applet_impl;

	/* initialize the i18n stuff */
	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	/* initialize applet stuff */
	gnomelib_init ("wavelan-applet", VERSION);
	applet_widget_init ("wavelan-applet", VERSION, 
			    argc, argv, NULL, 0,NULL);

	glade_gnome_init ();
	glade_file = gnome_unconditional_datadir_file (PACKAGE"/wavelan-applet.glade");

	gnome_window_icon_set_default_from_file (GNOME_ICONDIR"/wavelan-applet.png");

	applet_factory_new ("wavelan-applet_factory", NULL, applet_start_new_applet);

	APPLET_ACTIVATE (applet_activator, "wavelan-applet", &wavelan_applet_impl);

	applet_widget_gtk_main ();

	APPLET_DEACTIVATE (applet_deactivator, "wavelan-applet", wavelan_applet_impl);

	return 0;
}
#endif

