CFLAGS = -pthread -Og -g3 -Wall -Wextra -pedantic -Werror
LDLIBS = -lX11

# TODO: add a release mode that disables assertions, etc.

SRCS   = $(wildcard *.c)
OBJS   = $(SRCS:.c=.o)

.PHONY: all
all:    sbar

sbar:   $(OBJS)
	$(CC) $(CFLAGS) $(LDLIBS) -o $@ $^

sbar.o: util.h errors.h
util.o: util.h errors.h

.PHONY: clean
clean:
	$(RM) $(OBJS) sbar core
