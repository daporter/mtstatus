CC = gcc
CFLAGS = -Wall -Werror -Wpedantic -pthread
BIN = sbar

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(BIN)

