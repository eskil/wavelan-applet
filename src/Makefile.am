## Process this file with automake to produce Makefile.in

THEMEDIR=$(datadir)/$(PACKAGE)/themes
DATADIR=$(datadir)/$(PACKAGE)/data

NULL=
CFLAGS=-Werror -Wall -g
INCLUDES = 							\
	-I$(top_srcdir)/intl 					\
	$(GNOME_INCLUDEDIR)					\
	$(WAVELANAPPLET_CFLAGS)					\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"		\
	-DGNOME_ICONDIR=\""$(datadir)/pixmaps"\"		\
	-DDATADIR=\""$(DATADIR)"\"				\
	-DTHEMEDIR=\""$(THEMEDIR)"\"				\
	$(NULL)

bin_PROGRAMS = wavelan-applet

wavelan_applet_SOURCES = 	\
	wavelan-applet.c	\
	wireless.c		\
	$(NULL)

wavelan_applet_LDADD = 		\
	$(WAVELANAPPLET_LIBS) 	\
	$(INTLLIBS)		\
	$(NULL)

gladedir = $(DATADIR)
glade_DATA = wavelan-applet.glade

pixmapdir = $(datadir)/pixmaps
pixmap_DATA = wavelan-applet.png

uidir   = $(datadir)/gnome-2.0/ui/
ui_DATA = GNOME_WaveLanApplet.xml

serverdir = $(libdir)/bonobo/servers
server_in_files = GNOME_WaveLanApplet.server.in
server_DATA = $(server_in_files:.server.in=.server)

@INTLTOOL_SERVER_RULE@

schemadir   = @GCONF_SCHEMA_FILE_DIR@
schema_DATA = wavelan-applet.schemas

install-data-local:
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(srcdir)/$(schema_DATA) ;

EXTRA_DIST = 				\
	$(glade_DATA)			\
	$(pixmap_DATA)			\
	$(server_DATA)			\
	wavelan-applet-strings.c	\
	$(server_in_files)		\
	$(schema_DATA)			\
	$(NULL)
