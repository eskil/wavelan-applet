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
#include <panel-applet.h>
#include <panel-applet-gconf.h>
#include <glade/glade.h>
#include <littleskin/littleskin.h>
#include <eel/eel.h>
#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>

#include "wireless.h"

typedef struct {
  gchar *theme;
  gchar *device;
  gboolean show_percent;
  gboolean show_dialogs;
  gint update_interval;
  gint smooth_font_size;

} Properties;

typedef struct {
  /* The applet widget */
  GtkWidget *applet;

  /* Settings */
  Properties properties;
  Properties saved_properties;
  GladeXML *properties_dialog_xml;
  GtkWidget *properties_dialog;

  /* Themings */
  LittleSkin *skin;
	/* label */
	GtkWidget *label;

  /* Current state of the app */
  GtkTooltips *tooltips;

  gboolean message_already_showed;
  GtkWidget *msg_widget;

  Wireless *wireless;
  WirelessInfo oldinfo;
  WirelessInfo info;
  /* creepy */
  gint text_smaller;
} WaveLanApplet;

GdkPixbuf *alpha_buf = NULL;
#define WAVELAN_APPLET(s) ((WaveLanApplet*)(s))

/* Function prototypes */
static gboolean applet_factory (PanelApplet *applet, const gchar *iid, gpointer 
				data);
static WaveLanApplet* applet_fill_wavelan_applet (PanelApplet *applet);
static gboolean applet_fill (PanelApplet *applet);

static void applet_destroy (GtkWidget *widget, gpointer data);
static void applet_change_size (GtkWidget *widget, int size, gpointer data);
static void applet_change_orientation (GtkWidget *widget, PanelAppletOrient orient, gpointer data);
static void applet_ui_event (BonoboUIComponent *comp, const gchar *path, Bonobo_UIComponent_EventType type, const gchar *state_string, gpointer *data);
static void applet_properties_cb (GtkWidget *w, gpointer data);
static void applet_about_cb(GtkWidget *w, gpointer data);
static void applet_change_background (GtkWidget *w,
				      PanelAppletBackgroundType  type,
				      GdkColor *color,
				      GdkPixmap *pixmap,
				      WaveLanApplet *pa);
static void applet_set_background (WaveLanApplet *pa);

static GList* applet_get_skin_loads (WaveLanApplet *pa);
static gboolean applet_timeout (WaveLanApplet *pa);

static void applet_show_message (WaveLanApplet *pa, char *message,...);
static void applet_show_error (WaveLanApplet *pa, char *message,...);
static void applet_load_properties (WaveLanApplet *pa);
static void applet_save_properties (WaveLanApplet *pa);

static const BonoboUIVerb applet_menu_verbs [] = {
  BONOBO_UI_UNSAFE_VERB ("Preferences", applet_properties_cb),
  BONOBO_UI_UNSAFE_VERB ("About", applet_about_cb),
  BONOBO_UI_VERB_END
};

PANEL_APPLET_BONOBO_FACTORY ("OAFIID:GNOME_WaveLanApplet_Factory",
			     PANEL_TYPE_APPLET,
			     "wavelanappletfactory",
			     "0",
			     applet_factory,
			     NULL);

static void
fuck_off_and_die (char *arse, GdkPixbuf *pixbuf, GtkImage *img, GdkPixmap *pixmap) {
  GtkWidget *window, *hbox;
  
  hbox = gtk_hbox_new (FALSE, 0);
  if (pixbuf) {
    gtk_box_pack_start (GTK_BOX (hbox), 
			GTK_WIDGET (gtk_image_new_from_pixbuf (pixbuf)),
			FALSE, FALSE, 0);
  }
  if (img) {
    gtk_box_pack_start (GTK_BOX (hbox), 
			GTK_WIDGET (img),
			FALSE, FALSE, 0);
  }
  if (pixmap) {
    gtk_box_pack_start (GTK_BOX (hbox), 
			GTK_WIDGET (gtk_image_new_from_pixmap (pixmap, NULL)),
			FALSE, FALSE, 0);
  }

  gtk_box_pack_start (GTK_BOX (hbox), 
		      gtk_label_new (arse),
		      FALSE, FALSE, 0);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  if (pixbuf == NULL) {
    gtk_window_set_title (GTK_WINDOW (window), "ARSE");
  } else {
    gtk_window_set_title (GTK_WINDOW (window), "fuck off and die");
  }
  gtk_container_add (GTK_CONTAINER (window), hbox);
  gtk_widget_show_all (window);
}

static gboolean
applet_factory (PanelApplet *applet, 
		const char *iid,
		gpointer data) {
  gboolean result = FALSE;
  if (strcmp (iid, "OAFIID:GNOME_WaveLanApplet") == 0) {
    result = applet_fill (applet);
  }
  return result;
}

static WaveLanApplet*
applet_fill_wavelan_applet (PanelApplet *applet) {
  WaveLanApplet *pa;

  pa = g_new0 (WaveLanApplet, 1);
  pa->applet = GTK_WIDGET (applet);
  pa->message_already_showed = TRUE;
  g_object_set_data (G_OBJECT (applet), "wavelanapplet", pa);
  pa->tooltips = gtk_tooltips_new ();
  g_object_ref (pa->tooltips);
  gtk_object_sink (GTK_OBJECT (pa->tooltips));
  pa->properties.theme = strdup ("default");
  pa->properties.device = strdup ("eth0");
  pa->skin = little_skin_new (THEMEDIR,
			      pa->properties.theme, 
			      applet_get_skin_loads (pa));
  pa->label = eel_labeled_image_new (NULL, NULL);
  eel_labeled_image_set_label_position (EEL_LABELED_IMAGE (pa->label), GTK_POS_RIGHT);
  pa->wireless = wireless_new ();

  pa->oldinfo.link = -1;
  pa->oldinfo.level = -1;
  pa->oldinfo.noise = -1;
  pa->oldinfo.percent = -1;

  applet_load_properties (pa);
  applet_set_background (pa);

  return pa;
}

static gboolean
applet_fill (PanelApplet *applet) {
  WaveLanApplet *pa;
  /* bonobo is an arse name, who the hell comes
     up with this shite ? */
  BonoboUIComponent *ui;
  GtkWidget *hbox, *image;

  glade_gnome_init ();

  if (!(pa = applet_fill_wavelan_applet (applet))) return FALSE;
    
  /* hook all top structure into the applet, so in the callback,
     we can get access back to the WaveLanApplet structure */
  g_object_set_data (G_OBJECT (pa->applet), "top", pa);

  g_signal_connect (G_OBJECT (applet), "destroy",
		    G_CALLBACK (applet_destroy), pa);
  g_signal_connect (applet, "change_orient", 
		    G_CALLBACK (applet_change_orientation), pa);
  g_signal_connect (applet, "change_size", 
		    G_CALLBACK (applet_change_size), pa);
  g_signal_connect (applet, "change_background", 
		    G_CALLBACK (applet_change_background), pa);

  panel_applet_setup_menu_from_file (PANEL_APPLET (applet),
				     NULL,
				     "GNOME_WaveLanApplet.xml",
				     NULL,
				     applet_menu_verbs,
				     pa);

  ui = panel_applet_get_popup_component (PANEL_APPLET (applet));
  g_signal_connect (ui, "ui-event", G_CALLBACK (applet_ui_event), pa);
       	
  image = little_skin_get_image_widget (pa->skin);
  g_assert (GTK_IS_WIDGET (image));
  gtk_widget_show_all (image);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (hbox), 0);
  /*gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);*/
  gtk_box_pack_start (GTK_BOX (hbox), pa->label, FALSE, FALSE, 0);
  gtk_widget_show_all (hbox);

  gtk_container_set_border_width (GTK_CONTAINER (applet), 0);
  gtk_container_add (GTK_CONTAINER (applet), hbox);
  gtk_widget_realize (GTK_WIDGET (pa->applet));
  gtk_widget_show (GTK_WIDGET (pa->applet));

  applet_timeout (pa);

  if (wireless_ok (pa->wireless)) {
    gtk_timeout_add (pa->properties.update_interval>1 ? pa->properties.update_interval*100 : 1000, 
		     (GtkFunction)applet_timeout, pa);
    return TRUE;					   
  } else {
    applet_show_error (pa, _("No /proc/net/wireless."));
    return FALSE;
  }
}

/******************************************************************/

static void 
applet_destroy (GtkWidget *widget, 
		gpointer data) {
  little_skin_destroy (WAVELAN_APPLET (data)->skin);
}

static void 
applet_change_size (GtkWidget *widget, int size, gpointer data) {
}

static void 
applet_change_orientation (GtkWidget *widget, PanelAppletOrient orient, gpointer data) {
}

static void 
applet_ui_event (BonoboUIComponent *comp, 
		 const gchar *path, Bonobo_UIComponent_EventType type, 
		 const gchar *state_string, 
		 gpointer *data) {
}

static void
applet_properties_hookup (WaveLanApplet *pa,
			  const char *widget_name, 
			  const char *name, 
			  GCallback cb, 
			  gpointer data) 
{
  GtkWidget *widget = glade_xml_get_widget (pa->properties_dialog_xml, widget_name);
  g_assert (widget);
  g_signal_connect (G_OBJECT (widget), name, 
		    (GCallback)cb,
		    data);
}

static void
applet_properties_fill_menu (WaveLanApplet *pa, const char *widget_name, 
			     const GList *items, GCallback handler) {
  GtkWidget *menu = gtk_menu_new ();
  GtkOptionMenu *option_menu = GTK_OPTION_MENU (glade_xml_get_widget (pa->properties_dialog_xml, widget_name));
  GtkWidget *item;
  const GList *ptr;
  int x = 0;
  int select_item = 0;

  for (ptr = items; ptr; ptr = g_list_next (ptr)) {
	  item = gtk_menu_item_new_with_label ((char*)ptr->data);
	  g_object_set_data (G_OBJECT (item),
			     "applet", pa);
	  g_signal_connect (G_OBJECT (item), 
			    "activate",
			    handler,
			    (char*)ptr->data);
	  gtk_widget_show (item);
	  gtk_menu_shell_insert (GTK_MENU_SHELL (menu), item, x);    
	  if (strcmp (pa->properties.theme, ptr->data)==0) select_item = x;
	  x++;
	  g_object_unref (G_OBJECT (item));
  }
  gtk_option_menu_set_menu (option_menu, menu);
  gtk_option_menu_set_history (option_menu, select_item);
  gtk_widget_show (GTK_WIDGET (option_menu));
}

static Properties 
applet_copy_properties (Properties props) {
	Properties new_props;
	new_props = props;
	new_props.theme = g_strdup (props.theme);
	new_props.device = g_strdup (props.device);
	return new_props;
}

static void
applet_free_properties (Properties props) {
	g_free (props.theme);
	g_free (props.device);
}

static void
applet_properties_cancel (GtkButton *w, WaveLanApplet *pa) {
	applet_free_properties (pa->properties);
	pa->properties = applet_copy_properties (pa->saved_properties);
	applet_free_properties (pa->saved_properties);
	gtk_widget_hide (GTK_WIDGET (pa->properties_dialog));
	//g_object_unref (G_OBJECT (pa->properties_dialog));
}

static void
applet_properties_ok (GtkButton *w, WaveLanApplet *pa) {
	applet_free_properties (pa->saved_properties);
	gtk_widget_hide (GTK_WIDGET (pa->properties_dialog));
	//g_object_unref (G_OBJECT (pa->properties_dialog));
	applet_save_properties (pa);
}

static void
applet_properties_set_percent (GtkCheckButton *w, WaveLanApplet *pa) {	
	pa->properties.show_percent = gtk_toggle_button_get_mode (GTK_TOGGLE_BUTTON (w));
}

static void
applet_properties_set_update (GtkSpinButton *w, WaveLanApplet *pa) {
	pa->properties.update_interval = (int)gtk_spin_button_get_value (w);
}

static void
applet_properties_set_dialogs (GtkCheckButton *w, WaveLanApplet *pa) {     
	pa->properties.show_dialogs = gtk_toggle_button_get_mode (GTK_TOGGLE_BUTTON (w));
}

static void
applet_properties_set_theme (GtkMenuItem *w, const char *name) {
	WaveLanApplet *pa = g_object_get_data (G_OBJECT (w), "applet");
	little_skin_set_theme (pa->skin, name);	
	applet_timeout (pa);
}

static void
applet_properties_set_device (GtkMenuItem *w, const char *name) {
	WaveLanApplet *pa = g_object_get_data (G_OBJECT (w), "applet");
	g_free (pa->properties.device);
	pa->properties.device = g_strdup (name);
	applet_timeout (pa);
}

static void
applet_properties_setup_theme_menu (WaveLanApplet *pa) {
  const GList *themes = NULL;
  themes = little_skin_get_theme_names (pa->skin);
  applet_properties_fill_menu (pa, "theme_menu", themes, (GCallback)applet_properties_set_theme);
}

static void
applet_properties_setup_device_menu (WaveLanApplet *pa) {
	GList *devices = wireless_get_devices (pa->wireless);
	applet_properties_fill_menu (pa, "device_menu", devices, (GCallback)applet_properties_set_device);
	g_list_foreach (devices, (GFunc)g_free, NULL);
	g_list_free (devices);
}

static void 
applet_properties_cb (GtkWidget *w, gpointer data) {
  WaveLanApplet *pa = (WaveLanApplet*)data;

  pa->saved_properties = applet_copy_properties (pa->properties);

  if (!pa->properties_dialog_xml) {
    pa->properties_dialog_xml = glade_xml_new (DATADIR"/wavelan-applet.glade", 
					       "properties-dialog", NULL);
    pa->properties_dialog = glade_xml_get_widget (pa->properties_dialog_xml, 
						  "properties-dialog");

    applet_properties_hookup (pa, "cancel_button", "clicked", 
			      (GCallback)applet_properties_cancel, data);
    applet_properties_hookup (pa, "ok_button", "clicked", 
			      (GCallback)applet_properties_ok, data);
    applet_properties_hookup (pa, "update_spin", "value_change", 
			      (GCallback)applet_properties_set_update, data);
    applet_properties_hookup (pa, "percent_toggle", "toggled", 
			      (GCallback)applet_properties_set_percent, data);
    applet_properties_hookup (pa, "dialogs_toggle", "toggled", 
			      (GCallback)applet_properties_set_dialogs, data);
    applet_properties_setup_theme_menu (pa);
    g_object_set_data (G_OBJECT (glade_xml_get_widget (pa->properties_dialog_xml, 
						       "device_menu")),
		       "applet", pa);
    applet_properties_setup_device_menu (pa);
  }

  gtk_widget_show_all (GTK_WIDGET (pa->properties_dialog));
}

static void 
applet_about_cb(GtkWidget *w, gpointer data) {
  static GtkWidget *about = NULL;
  GdkPixbuf *pixbuf = NULL;
  gchar *file;
  
  static const gchar *authors[] =
    {
      "Eskil Olsen <eskil at eskil dot org>",
      NULL
    };
  /* Translator credits */
  const char *translator_credits = _("translator_credits");
  
  if (about != NULL)
    {
      gtk_window_present (GTK_WINDOW (about));
      return;
    }
  pixbuf = NULL;
  
  file = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_PIXMAP,
				    "wavelan-applet.png", TRUE, NULL);
  if (file) {
    pixbuf = gdk_pixbuf_new_from_file (file, NULL);
    g_free (file);
  } 

  about = gnome_about_new (_("WaveLanApplet"), VERSION,
			   _("(C) 2001-2002 the Free Software Foundation"),
			   _("Yet another applet that shows the\n"
			     "waterlevel in Sortedamssøen."),
			   authors,
			   NULL, 
			   strcmp (translator_credits, "translator_credits") != 0 ? translator_credits : NULL,
			   pixbuf);
  
  gtk_window_set_wmclass (GTK_WINDOW (about), "wavelanapplet", "wavelanapplet");
  if (pixbuf) {
    gtk_window_set_icon (GTK_WINDOW (about), pixbuf);
    g_object_unref (pixbuf);
  }
  
  g_signal_connect (G_OBJECT(about), "destroy",
		    (GCallback)gtk_widget_destroyed, &about);
  
  gtk_widget_show (about);
  
}

static GList* 
applet_get_skin_loads (WaveLanApplet *pa) {
  GList *loads = NULL;
  LittleSkinLoad *load;
  load = little_skin_load_new ("signal", 101);
  loads = g_list_append (loads, load);
  load = little_skin_load_new ("broken", 0);
  loads = g_list_append (loads, load);
  load = little_skin_load_new ("no-link", 0);
  loads = g_list_append (loads, load);
  return loads;
}

static gboolean 
applet_timeout (WaveLanApplet *pa) {
  applet_set_background (pa);
  pa->info = wireless_get_device_state (pa->wireless, pa->properties.device);

  if (0) {
    applet_show_message (NULL, NULL);
    applet_show_error (NULL, NULL);
  }

  /* update tooltip */
  if (pa->info.percent != pa->oldinfo.percent) {
    char *tmp = NULL;
    if (pa->info.percent > 0) {
      tmp = g_strdup_printf ("%s is at %d%% signal strength", 
			     pa->info.device, pa->info.percent);
    } else if (pa->info.level == 0) {
      tmp = g_strdup_printf ("Connection is broken");
    } else if (pa->info.link == 0) {
      tmp = g_strdup_printf ("Connection is lost");
    }
    if (tmp) {
      gtk_tooltips_set_tip (pa->tooltips, 
			    pa->applet, 
			    tmp, 
			    "Look behind you, there's a little puprple midget with an axe!");
      g_free (tmp);
    }
  }

  if (pa->properties.show_percent) {
	  char *tmp = g_strdup_printf ("%d%%", pa->info.percent);
	  eel_labeled_image_set_text (EEL_LABELED_IMAGE (pa->label), tmp);
	  g_free (tmp);
  }

  /* fuck_off_and_die ("arse", eel_labeled_image_get_pixbuf (EEL_LABELED_IMAGE (pa->label)), NULL, NULL); */
  /* fuck_off_and_die (panel_applet_get_preferences_key (PANEL_APPLET (pa->applet)), NULL, NULL, NULL); */

  if (pa->info.percent > 0) {
    if (pa->oldinfo.link == 0 ||
	pa->oldinfo.level == 0) {
      little_skin_stop_animation (pa->skin);
      if (pa->properties.show_dialogs && !pa->message_already_showed) {
	applet_show_message (pa, "Got connection");
	pa->message_already_showed = TRUE;
      }
    }
    little_skin_set_image (pa->skin, "signal", pa->info.percent);
    pa->message_already_showed = FALSE;
  } else if (!little_skin_is_animating (pa->skin)) {
    if (pa->info.level == 0) {
      if (pa->properties.show_dialogs && !pa->message_already_showed) {
	applet_show_error (pa, "Connection broke");
	pa->message_already_showed = TRUE;
      }
      little_skin_start_animation (pa->skin, "broken", 50);    
      pa->message_already_showed = FALSE;
    } else if (pa->info.link == 0) {
      if (pa->properties.show_dialogs && !pa->message_already_showed) {
	applet_show_error (pa, "Connection lost");
	pa->message_already_showed = TRUE;
      }
      little_skin_start_animation (pa->skin, "no-link", 50);    
      pa->message_already_showed = FALSE;
    }
  }
  pa->oldinfo = pa->info;

  eel_labeled_image_set_pixbuf (EEL_LABELED_IMAGE (pa->label), gtk_image_get_pixbuf (GTK_IMAGE (little_skin_get_image_widget (pa->skin))));
  eel_labeled_image_set_show_image (EEL_LABELED_IMAGE (pa->label), TRUE);

  return TRUE;
}

static void 
applet_show_message (WaveLanApplet *pa, char *message,...) {
  va_list ap;
  char *ptr;
  va_start (ap, message);
  ptr = g_strdup_vprintf (message, ap);
  va_end (ap);
  if (pa->msg_widget) gtk_widget_hide (pa->msg_widget);
  pa->msg_widget = gnome_ok_dialog (ptr);
  g_free (ptr);
}

static void 
applet_show_error (WaveLanApplet *pa, char *message,...) {
  va_list ap;
  char *ptr;
  va_start (ap, message);
  ptr = g_strdup_vprintf (message, ap);
  va_end (ap);
  if (pa->msg_widget) gtk_widget_hide (pa->msg_widget);
  pa->msg_widget = gnome_error_dialog (ptr);
  g_free (ptr);
}

static void
applet_save_properties (WaveLanApplet *pa) {
#define SET(a,s,b)   panel_applet_gconf_set_##a(PANEL_APPLET (pa->applet), s, b, NULL)
	SET (bool, "show_percent", pa->properties.show_percent);
	SET (bool, "show_dialogs", pa->properties.show_dialogs);
	SET (string, "theme", pa->properties.theme);
	SET (string, "device", pa->properties.device);
	SET (int, "updateinterval", pa->properties.update_interval);
}

static void
applet_load_properties (WaveLanApplet *pa) {
#define GET(a,b)   panel_applet_gconf_get_##a(PANEL_APPLET (pa->applet), b, NULL)

  pa->properties.show_percent = GET (bool, "show_percent");
  pa->properties.show_dialogs = GET (bool, "show_dialogs");
  pa->properties.theme = GET (string, "theme");
  pa->properties.update_interval = GET (int, "updateinterval");
  if (pa->properties.theme == NULL) {
	  pa->properties.theme = g_strdup ("default");
  }
  pa->properties.device = GET (string, "device");
  if (pa->properties.device == NULL) {
	  pa->properties.device = g_strdup ("eth0");
  }
}

static void 
applet_change_background (GtkWidget *w, 
			  PanelAppletBackgroundType  type,
			  GdkColor *color,
			  GdkPixmap *pixmap,
			  WaveLanApplet *pa) {			 
  g_assert (pa);
  switch (type) {
  case PANEL_NO_BACKGROUND:
    little_skin_set_alpha (pa->skin, FALSE);
    break;
  case PANEL_COLOR_BACKGROUND:
    little_skin_set_alpha (pa->skin, TRUE);
    little_skin_set_alpha_color (pa->skin, color);
    break;
  case PANEL_PIXMAP_BACKGROUND: {
    int h, w;
    GdkColormap *cmap = gdk_drawable_get_colormap (pixmap);

    if (0) fuck_off_and_die ("back", NULL, NULL, pixmap);

    gdk_drawable_get_size (pixmap, &w, &h);
    alpha_buf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
				FALSE, 8, w, h);    
    gdk_pixbuf_get_from_drawable (alpha_buf, 
				  pixmap, 
				  cmap,
				  0, 0,
				  0, 0,
				  w, h);
    little_skin_set_alpha (pa->skin, TRUE);
    little_skin_set_alpha_level (pa->skin, 255);
    little_skin_set_alpha_pixbuf (pa->skin, alpha_buf);

    g_object_unref (G_OBJECT (alpha_buf));
    g_object_unref (G_OBJECT (cmap));
    g_object_unref (G_OBJECT (pixmap));
  }
    break;
  }
  little_skin_redraw (pa->skin);
}

static void 
applet_set_background (WaveLanApplet *pa) {
  PanelAppletBackgroundType b_type = PANEL_NO_BACKGROUND;
  GdkColor color;
  GdkPixmap *pixmap = NULL;

  b_type = panel_applet_get_background (PANEL_APPLET (pa->applet),
					&color,
					&pixmap);
  applet_change_background (NULL, b_type, &color, pixmap, pa);
}


