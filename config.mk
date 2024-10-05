# Configuration for cream
# cream version
VERSION = 1.0

# program name
NAME = cream
NAME_UPPERCASE = CREAM

# Customize below to fit your system
CC = gcc
CFLAGS += -Wall -Wextra -Wno-unused-parameter -O3 -g -D_FORTIFY_SOURCE=2 -march=native -mtune=native -ffast-math -fomit-frame-pointer -flto
LDFLAGS =

INSTALL = install
INSTALL_PROGRAM = $(INSTALL)
INSTALL_DATA = $(INSTALL) -m 644

prefix = /usr/local
exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin
libdir = $(exec_prefix)/lib
datarootdir = $(prefix)/share
mandir = $(datarootdir)/man
man1dir = $(mandir)/man1
datadir = $(prefix)/share
applicationsdir = $(datadir)/applications
iconsdir = $(datadir)/icons/hicolor/128x128/apps
