CFLAGS = -pthread -Og -g3 -Wall -Wextra -pedantic -Werror
LDLIBS = -lX11
BIN    = sbar

# TODO: add a release mode that disables assertions, etc.

SRCS   = $(wildcard *.c)
OBJS   = $(SRCS:.c=.o)

.PHONY: all
all:    sbar

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDLIBS) -o $@ $^

main.o: util.h errors.h
util.o: util.h errors.h

.PHONY: clean
clean:
	$(RM) $(OBJS) $(BIN) core
