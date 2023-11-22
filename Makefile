.POSIX:
.SUFFIXES:

CC	 = cc
CPPFLAGS =
CFLAGS	 = -std=c17 -pthread -Og -g3 -Wall -Wextra -Wpedantic -Wshadow -Werror -fanalyzer
LDFLAGS  =
LDLIBS	 = -lX11

sources = src/mtstatus.c src/component.c src/util.c
objects = $(sources:.c=.o)

mtstatus: $(objects)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

src/mtstatus.o: src/mtstatus.c config.h src/component.h src/util.h
src/component.o: src/component.c src/component.h src/util.h
src/util.o: src/util.c src/util.h

clean:
	rm -rf mtstatus $(objects)

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<
