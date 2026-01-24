CC := cc
CFLAGS := -std=c99 -Wall -Wextra -Werror -Isrc
COVERAGE_CFLAGS := --coverage -O0 -g
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)
GOBJECT_CFLAGS := $(shell pkg-config --cflags gobject-2.0)
GOBJECT_LIBS := $(shell pkg-config --libs gobject-2.0)
GTK_CFLAGS := $(shell pkg-config --cflags gtk4)
GTK_LIBS := $(shell pkg-config --libs gtk4)
LDLIBS := $(GLIB_LIBS) $(GOBJECT_LIBS) -lm

CFLAGS += $(GLIB_CFLAGS) $(GOBJECT_CFLAGS)

SRCS := src/board.c src/game.c src/game_print.c src/move_gen.c src/checkers_model.c
BOARD_SRCS := src/board.c
SGF_TREE_SRCS := src/sgf_tree.c
SGF_VIEW_SRCS := \
  src/sgf_view.c \
  src/sgf_view_disc_factory.c \
  src/sgf_view_layout.c \
  src/sgf_view_link_renderer.c \
  src/sgf_view_scroller.c \
  src/sgf_view_selection_controller.c
OBJS := $(SRCS:.c=.o)
COV_DIR := coverage
COV_OBJ_DIR := $(COV_DIR)/obj
COV_BIN_DIR := $(COV_DIR)/bin
COV_GCOV_DIR := $(COV_DIR)/gcov
COV_REPORT_DIR := $(COV_DIR)/report
COV_OBJS := $(SRCS:%.c=$(COV_OBJ_DIR)/%.o)
COV_BOARD_OBJS := $(BOARD_SRCS:%.c=$(COV_OBJ_DIR)/%.o)
SCREENSHOT ?= gcheckers.png
BROADWAY_DISPLAY_NUM ?= 5
BROADWAY_PORT ?= 8085
SCREEN_SIZE ?= 1280x720
BROADWAYD_BIN ?= gtk4-broadwayd
CHROMIUM_BIN ?= google-chrome

.PHONY: all clean test coverage screenshot test_screenshot

all: libgame.a checkers gcheckers

libgame.a: $(OBJS)
	ar rcs $@ $^

%.o: %.c src/game.h src/board.h src/checkers_constants.h
	$(CC) $(CFLAGS) -c $< -o $@

test: test_game test_game_print test_board test_move_gen test_checkers_model test_sgf_tree test_sgf_view \
	test_player_controls_panel test_gcheckers_sgf_controller test_screenshot
	./test_game
	./test_game_print
	./test_board
	./test_move_gen
	./test_checkers_model
	./test_sgf_tree
	./test_sgf_view
	./test_player_controls_panel
	./test_gcheckers_sgf_controller

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

test_sgf_tree: tests/test_sgf_tree.c $(SGF_TREE_SRCS) src/sgf_tree.h
	$(CC) $(CFLAGS) -o $@ tests/test_sgf_tree.c $(SGF_TREE_SRCS) $(LDLIBS)

test_sgf_view: tests/test_sgf_view.c $(SGF_VIEW_SRCS) $(SGF_TREE_SRCS) src/sgf_view.h src/sgf_tree.h
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_sgf_view.c $(SGF_VIEW_SRCS) $(SGF_TREE_SRCS) \
		$(LDLIBS) $(GTK_LIBS)

test_player_controls_panel: tests/test_player_controls_panel.c src/player_controls_panel.c src/player_controls_panel.h
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_player_controls_panel.c src/player_controls_panel.c \
		$(LDLIBS) $(GTK_LIBS)

test_gcheckers_sgf_controller: tests/test_gcheckers_sgf_controller.c src/gcheckers_sgf_controller.c \
	src/gcheckers_sgf_controller.h src/board_view.c src/board_view.h src/board_grid.c src/board_grid.h \
	src/board_square.c src/board_square.h src/board_move_overlay.c src/board_move_overlay.h \
	src/board_selection_controller.c src/board_selection_controller.h src/piece_palette.c \
	src/piece_palette.h src/gcheckers_man_paintable.c src/gcheckers_man_paintable.h src/checkers_model.c \
	src/checkers_model.h src/sgf_tree.c src/sgf_tree.h src/sgf_view.c src/sgf_view.h \
	src/sgf_view_disc_factory.c src/sgf_view_disc_factory.h src/sgf_view_layout.c src/sgf_view_layout.h \
	src/sgf_view_link_renderer.c src/sgf_view_link_renderer.h src/sgf_view_scroller.c \
	src/sgf_view_scroller.h src/sgf_view_selection_controller.c src/sgf_view_selection_controller.h $(SRCS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_gcheckers_sgf_controller.c \
		src/gcheckers_sgf_controller.c src/board_view.c src/board_grid.c src/board_square.c \
		src/board_move_overlay.c src/board_selection_controller.c src/piece_palette.c \
		src/gcheckers_man_paintable.c src/sgf_tree.c src/sgf_view.c src/sgf_view_disc_factory.c \
		src/sgf_view_layout.c src/sgf_view_link_renderer.c src/sgf_view_scroller.c \
		src/sgf_view_selection_controller.c $(SRCS) $(LDLIBS) $(GTK_LIBS)

test_screenshot: gcheckers tools/screenshot_gcheckers.sh
	@if ! command -v $(BROADWAYD_BIN) >/dev/null 2>&1; then \
		echo "Skipping screenshot test: $(BROADWAYD_BIN) not available."; \
		exit 0; \
	fi; \
	if ! command -v $(CHROMIUM_BIN) >/dev/null 2>&1; then \
		echo "Skipping screenshot test: Chrome not available."; \
		exit 0; \
	fi; \
	tmp_file=$$(mktemp -t gcheckers_screenshot.XXXXXX.png); \
		BROADWAY_DISPLAY_NUM=5 BROADWAY_PORT=8085 SCREEN_SIZE=1280x720 \
		CHROMIUM_BIN="$(CHROMIUM_BIN)" tools/screenshot_gcheckers.sh "$$tmp_file"; \
		test -s "$$tmp_file"; \
		rm -f "$$tmp_file"

gcheckers: src/gcheckers.c src/gcheckers_application.c src/gcheckers_window.c src/gcheckers_window.h \
	src/gcheckers_style.c src/gcheckers_style.h src/player_controls_panel.c src/player_controls_panel.h \
	src/gcheckers_sgf_controller.c src/gcheckers_sgf_controller.h src/board_view.c src/board_view.h \
	src/board_grid.c src/board_grid.h src/board_square.c src/board_square.h src/board_move_overlay.c \
	src/board_move_overlay.h src/board_selection_controller.c src/board_selection_controller.h \
	src/piece_palette.c src/piece_palette.h src/gcheckers_application.h src/gcheckers_man_paintable.c \
	src/gcheckers_man_paintable.h src/checkers_model.c src/checkers_model.h src/sgf_tree.c src/sgf_tree.h \
	src/sgf_view.c src/sgf_view.h src/sgf_view_disc_factory.c src/sgf_view_disc_factory.h \
	src/sgf_view_layout.c src/sgf_view_layout.h src/sgf_view_link_renderer.c src/sgf_view_link_renderer.h \
	src/sgf_view_scroller.c src/sgf_view_scroller.h src/sgf_view_selection_controller.c \
	src/sgf_view_selection_controller.h $(SRCS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ src/gcheckers.c src/gcheckers_application.c \
		src/gcheckers_window.c src/gcheckers_style.c src/player_controls_panel.c \
		src/gcheckers_sgf_controller.c src/board_view.c src/board_grid.c src/board_square.c \
		src/board_move_overlay.c src/board_selection_controller.c src/piece_palette.c \
		src/gcheckers_man_paintable.c src/sgf_tree.c src/sgf_view.c src/sgf_view_disc_factory.c \
		src/sgf_view_layout.c src/sgf_view_link_renderer.c src/sgf_view_scroller.c \
		src/sgf_view_selection_controller.c $(SRCS) $(LDLIBS) $(GTK_LIBS)

clean:
	rm -f $(OBJS) libgame.a test_game test_game_print test_board test_move_gen test_checkers_model \
		test_sgf_tree test_sgf_view test_player_controls_panel test_gcheckers_sgf_controller test_screenshot \
		checkers gcheckers
	rm -rf $(COV_DIR)

screenshot: gcheckers tools/screenshot_gcheckers.sh
	BROADWAY_DISPLAY_NUM=$(BROADWAY_DISPLAY_NUM) BROADWAY_PORT=$(BROADWAY_PORT) \
		SCREEN_SIZE=$(SCREEN_SIZE) tools/screenshot_gcheckers.sh $(SCREENSHOT)

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
