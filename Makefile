CFLAGS = -std=c17 -pthread -Og -g3 -Wall -Wextra -pedantic -Werror

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
