## Process this file with automake to produce Makefile.in

NULL=

INCLUDES = 							\
	-I$(top_srcdir)/intl 					\
	$(GNOME_INCLUDEDIR)					\
	$(LIBGLADE_CFLAGS)					\
	$(ORBIT_CFLAGS)						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"		\
	-DGNOME_ICONDIR=\""$(datadir)/pixmaps"\"		\
	$(NULL)

bin_PROGRAMS = wavelan-applet

wavelan_applet_SOURCES = 	\
	wavelan-applet.c	\
	$(NULL)

wavelan_applet_LDADD = 		\
	$(GNOME_LIBDIR) 	\
	$(GNOMEUI_LIBS) 	\
	$(GNOME_APPLETS_LIBS)	\
	$(INTLLIBS)		\
	$(LIBGLADE_LIBS)	\
	$(NULL)

sysdir = $(datadir)/applets/Monitors
sys_DATA = wavelan-applet.desktop

gladedir = $(datadir)/$(PACKAGE)
glade_DATA = wavelan-applet.glade

pixmapdir = $(datadir)/pixmaps
pixmap_DATA = wavelan-applet.png

gnorbadir = $(sysconfdir)/CORBA/servers
gnorba_DATA = wavelan-applet.gnorba

EXTRA_DIST = 			\
	$(glade_DATA)		\
	$(sys_DATA)		\
	$(pixmap_DATA)		\
	$(gnorba_DATA)		\
	$(NULL)
