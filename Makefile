CC := cc
CFLAGS := -std=c99 -Wall -Wextra -Werror -Isrc
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)
LDLIBS := $(GLIB_LIBS)

CFLAGS += $(GLIB_CFLAGS)

SRCS := src/game.c
OBJS := $(SRCS:.c=.o)

.PHONY: all clean test

all: libgame.a checkers

libgame.a: $(OBJS)
	ar rcs $@ $^

%.o: %.c src/game.h
	$(CC) $(CFLAGS) -c $< -o $@

test: test_game
	./test_game

test_game: tests/test_game.c $(SRCS) src/game.h
	$(CC) $(CFLAGS) -o $@ tests/test_game.c $(SRCS) $(LDLIBS)

checkers: src/checkers_cli.c $(SRCS) src/game.h
	$(CC) $(CFLAGS) -o $@ src/checkers_cli.c $(SRCS) $(LDLIBS)

clean:
	rm -f $(OBJS) libgame.a test_game checkers
