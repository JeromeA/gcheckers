CC := cc
CFLAGS := -std=c99 -Wall -Wextra -Werror -Isrc
CFLAGS += -g3
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
WIDGET_UTILS_SRCS := src/widget_utils.c
WIDGET_UTILS_HDRS := src/widget_utils.h
OBJS := $(SRCS:.c=.o)
COV_DIR := coverage
SCREENSHOT ?= gcheckers.png
BROADWAY_DISPLAY_NUM ?= 5
BROADWAY_PORT ?= 8085
SCREEN_SIZE ?= 1280x720
BROADWAYD_BIN ?= gtk4-broadwayd
CHROMIUM_BIN ?= google-chrome

.PHONY: all clean test coverage screenshot

all: libgame.a checkers gcheckers

libgame.a: $(OBJS)
	ar rcs $@ $^

%.o: %.c src/game.h src/board.h src/checkers_constants.h
	$(CC) $(CFLAGS) -c $< -o $@

test:
	@echo "No tests are currently defined."

checkers: src/checkers_cli.c $(SRCS) src/game.h
	$(CC) $(CFLAGS) -o $@ src/checkers_cli.c $(SRCS) $(LDLIBS)

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
	src/sgf_view_selection_controller.h $(SRCS) $(WIDGET_UTILS_SRCS) $(WIDGET_UTILS_HDRS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ src/gcheckers.c src/gcheckers_application.c \
		src/gcheckers_window.c src/gcheckers_style.c src/player_controls_panel.c \
		src/gcheckers_sgf_controller.c src/board_view.c src/board_grid.c src/board_square.c \
		src/board_move_overlay.c src/board_selection_controller.c src/piece_palette.c \
		src/gcheckers_man_paintable.c src/sgf_tree.c src/sgf_view.c src/sgf_view_disc_factory.c \
		src/sgf_view_layout.c src/sgf_view_link_renderer.c src/sgf_view_scroller.c \
		src/sgf_view_selection_controller.c $(WIDGET_UTILS_SRCS) $(SRCS) $(LDLIBS) $(GTK_LIBS)

clean:
	rm -f $(OBJS) libgame.a checkers gcheckers
	rm -rf $(COV_DIR)

screenshot: gcheckers tools/screenshot_gcheckers.sh
	BROADWAY_DISPLAY_NUM=$(BROADWAY_DISPLAY_NUM) BROADWAY_PORT=$(BROADWAY_PORT) \
		SCREEN_SIZE=$(SCREEN_SIZE) tools/screenshot_gcheckers.sh $(SCREENSHOT)

coverage:
	@echo "No tests are currently defined; coverage is unavailable."
