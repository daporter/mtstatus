CFLAGS = -pthread -Og -g3 -Wall -Wextra -pedantic -Werror

# TODO: add a release mode that disables assertions, etc.

SRCS   = $(wildcard *.c)
OBJS   = $(SRCS:.c=.o)

.PHONY: all
all:    sbar

sbar:   $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

sbar.o: errors.h

.PHONY: clean
clean:
	$(RM) $(OBJS) sbar core
