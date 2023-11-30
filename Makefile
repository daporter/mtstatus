.POSIX:

CC	 = cc
CPPFLAGS =
CFLAGS	 = -std=c11 -pthread -g3 -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wconversion -Wno-sign-conversion -Wshadow -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fanalyzer
LDFLAGS  =
LDLIBS	 = -lX11

mtstatus: mtstatus.c component.c util.c util.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -rf mtstatus
