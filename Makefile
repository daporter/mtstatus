prefix = /usr/local
bindir = $(prefix)/bin

INSTALL = install

CPPFLAGS = -D_DEFAULT_SOURCE
CFLAGS   = -std=c11 -pthread -g3 -MMD -fanalyzer \
	   -Wall -Wextra -Wpedantic -Wno-unused-parameter \
	   -Wconversion -Wno-sign-conversion -Wshadow
LDLIBS   = -lX11

SRCS = mtstatus.c component.c util.c
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

all: release

release: CPPFLAGS += -DNDEBUG
release: CFLAGS   += -Wno-unused -O2
release: mtstatus

debug: CFLAGS  += -O0 -fno-omit-frame-pointer
debug: LDFLAGS  = -fsanitize=address,undefined
debug: mtstatus

mtstatus: $(OBJS)

clean:
	rm -f $(OBJS) $(DEPS) mtstatus

install: all
	mkdir -p $(DESTDIR)$(bindir)
	$(INSTALL) mtstatus $(DESTDIR)$(bindir)

uninstall:
	$(RM) $(DESTDIR)$(bindir)/mtstatus

compile_flags.txt: Makefile
	echo -xc $(CPPFLAGS) $(CFLAGS) | tr ' ' '\n' > $@

analyse:
	clang-tidy *.c -- $(CPPFLAGS) $(CFLAGS)

mtstatus.o: config.h
-include $(DEPS)

config.h:
	cp config.def.h $@

.PHONY: all release debug clean install uninstall analyse
