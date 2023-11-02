CFLAGS = -pedantic -pthread -Og -g3 -Wall -Wextra -Werror
LDLIBS = -lX11
BIN    = sbar
CPPFLAGS = -D_DEFAULT_SOURCE

# TODO: add a release mode that disables assertions, etc.

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

.PHONY: all
all:    sbar

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDLIBS) -o $@ $^

components.o: components.h config.h errors.h util.h
errors.o: errors.h
main.o: components.h config.h errors.h util.h
util.o: util.h errors.h

.PHONY: clean
clean:
	$(RM) $(OBJS) $(BIN) core
