## Tools Makefile for Preheat

bin_PROGRAMS = preheat-ctl

preheat_ctl_SOURCES = \
	preheat-ctl.c \
	ctl_daemon.c \
	ctl_daemon.h \
	ctl_display.c \
	ctl_display.h \
	ctl_state.c \
	ctl_state.h \
	ctl_config.c \
	ctl_config.h \
	ctl_commands.h \
	ctl_cmd_basic.c \
	ctl_cmd_stats.c \
	ctl_cmd_apps.c \
	ctl_cmd_io.c

preheat_ctl_CPPFLAGS = \
	-I$(top_srcdir)/include \
	$(GLIB_CFLAGS) \
	-DSYSCONFDIR='"$(sysconfdir)"' \
	-DPKGLOCALSTATEDIR='"$(pkglocalstatedir)"'

preheat_ctl_LDADD = $(GLIB_LIBS) -lm
