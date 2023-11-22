.POSIX:

CC	 = cc
CPPFLAGS =
CFLAGS	 = -std=c17 -pthread -Og -g3 -Wall -Wextra -Wpedantic -Wshadow -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fanalyzer
LDFLAGS  =
LDLIBS	 = -lX11

mtstatus: src/all.c src/mtstatus.c src/component.c src/util.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ src/all.c $(LDLIBS)

clean:
	rm -rf mtstatus
