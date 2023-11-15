SRCDIR = src
OBJDIR = build
BINDIR = .

BIN  = $(BINDIR)/mtstatus
SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

CPPFLAGS = -D_DEFAULT_SOURCE -MMD -MP
CFLAGS	 = -std=c17 -pthread -Og -g3 -Wall -Wextra -Wpedantic -Wshadow -Werror -fanalyzer
LDFLAGS  =
LDLIBS	 = -lX11

.PHONY: all check clean

all: $(BIN)

$(BIN): $(OBJS) | $(BINDIR)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BINDIR) $(OBJDIR):
	mkdir -p $@

check:
	clang-tidy $(SRCS)

clean:
	@$(RM) -rv $(OBJDIR) $(BIN) core

-include $(OBJS:.o=.d)
