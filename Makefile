.POSIX:

CC      = cc
CFLAGS  = -std=c11 -pthread -Wall -Wextra -Wpedantic -Wno-unused-parameter
CFLAGS += -Wconversion -Wno-sign-conversion -Wshadow
LDLIBS  = -lX11

all: debug

debug: CFLAGS += -Werror -g3 -fsanitize=address,undefined
debug: CFLAGS += -fno-omit-frame-pointer -fanalyzer
debug: mtstatus

release: CFLAGS += -DNDEBUG -O2 -Wno-unused
release: mtstatus

mtstatus: mtstatus.c mtstatus.h component.c util.c util.h
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -rf mtstatus
