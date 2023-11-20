.POSIX:
.SUFFIXES:

CC	 = cc
CPPFLAGS = -D_DEFAULT_SOURCE -MMD -MP
CFLAGS	 = -std=c17 -pthread -Og -g3 -Wall -Wextra -Wpedantic -Wshadow -Werror -fanalyzer
LDFLAGS  =
LDLIBS	 = -lX11
PARALLEL = parallel-moreutils
SRCDIR	 = src
OBJDIR	 = build
BINDIR	 = .
BIN	 = $(BINDIR)/mtstatus
SRCS	 = $(wildcard $(SRCDIR)/*.c)
OBJS	 = $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

all: $(BIN)

$(BIN): $(OBJS) | $(BINDIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $<

$(BINDIR) $(OBJDIR):
	mkdir -p $@

format:
	$(PARALLEL) clang-format -i -- $(wildcard $(SRCDIR)/*.[ch])

check:
	$(PARALLEL) clang-tidy --quiet -- $(wildcard $(SRCDIR)/*.[ch])

clean:
	rm -r $(OBJDIR) $(BIN) core

-include $(OBJS:.o=.d)
