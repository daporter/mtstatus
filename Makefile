.POSIX:

CC      = cc
CFLAGS  = -std=c11 -pthread -D_DEFAULT_SOURCE
CFLAGS += -Wall -Wextra -Wpedantic -Wno-unused-parameter
CFLAGS += -Wconversion -Wno-sign-conversion -Wshadow
LDLIBS  = -lX11
PREFIX  = /usr/local

all: debug

debug: CFLAGS += -Werror -g3 -fsanitize=address,undefined
debug: CFLAGS += -fno-omit-frame-pointer -fanalyzer
debug: mtstatus

install: release
	mkdir -p $(PREFIX)/bin
	install -m 755 mtstatus $(PREFIX)/bin

uninstall:
	rm -f $(PREFIX)/bin/mtstatus

release: CFLAGS += -DNDEBUG -O2 -Wno-unused
release: mtstatus

mtstatus: mtstatus.c mtstatus.h component.c util.c util.h Makefile
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

compile_flags.txt: Makefile
	echo -xc $(CFLAGS) | tr ' ' '\n' > $@

analyse:
	clang-tidy *.c -- ${CFLAGS}

clean:
	rm -rf mtstatus
