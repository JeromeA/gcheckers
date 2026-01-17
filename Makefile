CC := cc
CFLAGS := -std=c99 -Wall -Wextra -Werror -Isrc

SRCS := src/game.c
OBJS := $(SRCS:.c=.o)

.PHONY: all clean test

all: libgame.a

libgame.a: $(OBJS)
	ar rcs $@ $^

%.o: %.c src/game.h
	$(CC) $(CFLAGS) -c $< -o $@

test: test_game
	./test_game

test_game: tests/test_game.c $(SRCS) src/game.h
	$(CC) $(CFLAGS) -o $@ tests/test_game.c $(SRCS)

clean:
	rm -f $(OBJS) libgame.a test_game
