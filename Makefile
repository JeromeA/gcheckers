CC := cc
CFLAGS := -std=c99 -Wall -Wextra -Werror -Isrc
COVERAGE_CFLAGS := --coverage -O0 -g
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)
GOBJECT_CFLAGS := $(shell pkg-config --cflags gobject-2.0)
GOBJECT_LIBS := $(shell pkg-config --libs gobject-2.0)
GTK_CFLAGS := $(shell pkg-config --cflags gtk4)
GTK_LIBS := $(shell pkg-config --libs gtk4)
LDLIBS := $(GLIB_LIBS) $(GOBJECT_LIBS)

CFLAGS += $(GLIB_CFLAGS) $(GOBJECT_CFLAGS)

SRCS := src/board.c src/game.c src/game_print.c src/move_gen.c src/checkers_model.c
BOARD_SRCS := src/board.c
OBJS := $(SRCS:.c=.o)
COV_DIR := coverage
COV_OBJ_DIR := $(COV_DIR)/obj
COV_BIN_DIR := $(COV_DIR)/bin
COV_GCOV_DIR := $(COV_DIR)/gcov
COV_REPORT_DIR := $(COV_DIR)/report
COV_OBJS := $(SRCS:%.c=$(COV_OBJ_DIR)/%.o)
COV_BOARD_OBJS := $(BOARD_SRCS:%.c=$(COV_OBJ_DIR)/%.o)

.PHONY: all clean test coverage

all: libgame.a checkers gcheckers

libgame.a: $(OBJS)
	ar rcs $@ $^

%.o: %.c src/game.h src/board.h src/checkers_constants.h
	$(CC) $(CFLAGS) -c $< -o $@

test: test_game test_game_print test_board test_move_gen test_checkers_model
	./test_game
	./test_game_print
	./test_board
	./test_move_gen
	./test_checkers_model

test_game: tests/test_game.c $(SRCS) src/game.h
	$(CC) $(CFLAGS) -o $@ tests/test_game.c $(SRCS) $(LDLIBS)

test_game_print: tests/test_game_print.c $(SRCS) src/game.h
	$(CC) $(CFLAGS) -o $@ tests/test_game_print.c $(SRCS) $(LDLIBS)

test_board: tests/test_board.c $(BOARD_SRCS) src/board.h src/checkers_constants.h
	$(CC) $(CFLAGS) -o $@ tests/test_board.c $(BOARD_SRCS) $(LDLIBS)

test_move_gen: tests/test_move_gen.c $(SRCS) src/game.h
	$(CC) $(CFLAGS) -o $@ tests/test_move_gen.c $(SRCS) $(LDLIBS)

checkers: src/checkers_cli.c $(SRCS) src/game.h
	$(CC) $(CFLAGS) -o $@ src/checkers_cli.c $(SRCS) $(LDLIBS)

test_checkers_model: tests/test_checkers_model.c $(SRCS) src/checkers_model.h
	$(CC) $(CFLAGS) -o $@ tests/test_checkers_model.c $(SRCS) $(LDLIBS)

gcheckers: src/gcheckers.c src/gcheckers_application.c src/gcheckers_window.c src/gcheckers_window.h \
	src/gcheckers_application.h src/checkers_model.c src/checkers_model.h $(SRCS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ src/gcheckers.c src/gcheckers_application.c \
		src/gcheckers_window.c $(SRCS) $(LDLIBS) $(GTK_LIBS)

clean:
	rm -f $(OBJS) libgame.a test_game test_game_print test_board test_move_gen test_checkers_model \
		checkers gcheckers
	rm -rf $(COV_DIR)

$(COV_OBJ_DIR)/%.o: %.c src/game.h src/board.h src/checkers_constants.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(COVERAGE_CFLAGS) -c $< -o $@

coverage: $(COV_OBJS) $(COV_BOARD_OBJS)
	@mkdir -p $(COV_BIN_DIR) $(COV_GCOV_DIR) $(COV_REPORT_DIR)
	$(CC) $(CFLAGS) $(COVERAGE_CFLAGS) -o $(COV_BIN_DIR)/test_game tests/test_game.c $(COV_OBJS) $(LDLIBS)
	$(CC) $(CFLAGS) $(COVERAGE_CFLAGS) -o $(COV_BIN_DIR)/test_game_print \
		tests/test_game_print.c $(COV_OBJS) $(LDLIBS)
	$(CC) $(CFLAGS) $(COVERAGE_CFLAGS) -o $(COV_BIN_DIR)/test_board tests/test_board.c \
		$(COV_BOARD_OBJS) $(LDLIBS)
	$(CC) $(CFLAGS) $(COVERAGE_CFLAGS) -o $(COV_BIN_DIR)/test_move_gen tests/test_move_gen.c \
		$(COV_OBJS) $(LDLIBS)
	$(COV_BIN_DIR)/test_game
	$(COV_BIN_DIR)/test_game_print
	$(COV_BIN_DIR)/test_board
	$(COV_BIN_DIR)/test_move_gen
	gcov -o $(COV_OBJ_DIR)/src $(SRCS)
	mv *.gcov $(COV_GCOV_DIR)/
	python3 tools/coverage_report.py --gcov-dir $(COV_GCOV_DIR) --output-dir $(COV_REPORT_DIR)
