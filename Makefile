CFLAGS = -pthread -pedantic -g -Wall -Wextra -Wno-unused-parameter

SRCS   = $(wildcard *.c)
OBJS   = $(SRCS:.c=.o)

.PHONY: all
all:    sbar

sbar:   $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# $(OBJS): foo.h bar.h

.PHONY: clean
clean:
	$(RM) $(OBJS) sbar core
