## Tools Makefile for Preheat

bin_PROGRAMS = preheat-ctl

preheat_ctl_SOURCES = preheat-ctl.c

preheat_ctl_CPPFLAGS = \
	-I$(top_srcdir)/include \
	$(GLIB_CFLAGS) \
	-DSYSCONFDIR='"$(sysconfdir)"' \
	-DPKGLOCALSTATEDIR='"$(pkglocalstatedir)"'

preheat_ctl_LDADD = $(GLIB_LIBS) -lm
