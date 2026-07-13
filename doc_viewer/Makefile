# doc_viewer/Makefile — build the "doc_viewer" mod plugin.
#
# Compiles against ONLY the SDK public header (roth_sdk.h) with the strict flags the SDK promises
# plugin authors. -D_GNU_SOURCE only enables the POSIX strcasecmp used to parse doc_viewer.cfg
# (a feature-test macro, not an engine header — the SDK-only contract is intact). Drop-in:
# mods/doc_viewer/{plugin.so,doc_viewer.cfg}.
#
# ROTH_SDK points at the SDK root (the directory that contains include/roth_sdk.h). The default
# assumes a ROTH.C engine checkout sitting next to this repo; override it to point at any engine
# checkout or an unpacked SDK release, e.g. `make ROTH_SDK=/path/to/ROTH.C/sdk`.
ROTH_SDK ?= ../../ROTH.C/sdk

CC      := gcc
CFLAGS  := -m32 -shared -fPIC -std=c11 -Wall -Wextra -Werror -D_GNU_SOURCE -I$(ROTH_SDK)/include

all: plugin.so

plugin.so: doc_viewer.c $(ROTH_SDK)/include/roth_sdk.h $(ROTH_SDK)/include/roth_sdk_overrides.h
	$(CC) $(CFLAGS) -o $@ doc_viewer.c

clean:
	rm -f plugin.so
