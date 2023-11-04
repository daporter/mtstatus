CPPFLAGS = -D_DEFAULT_SOURCE
CFLAGS	 = -std=c17 -pedantic -pthread -Og -g3 -Wall -Wextra -Werror -fanalyzer
LDLIBS	 = -lX11
BIN	 = mtstatus

# TODO: add a release mode that disables assertions, etc.

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

.PHONY: all
all:    $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDLIBS) -o $@ $^

$(BIN).o: components.h errors.h sbar.h util.h
components.o: components.h errors.h util.h
errors.o: errors.h
sbar.o: sbar.h errors.h util.h
util.o: util.h errors.h

.PHONY: clean
clean:
	$(RM) $(OBJS) $(BIN) core
