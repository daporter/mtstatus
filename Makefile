CFLAGS = -pthread -pedantic -g -Wall -Wextra -Wno-unused-parameter

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
