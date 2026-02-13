CC := cc
CFLAGS := -std=c99 -Wall -Wextra -Werror -Isrc
CFLAGS += -g3
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)
GOBJECT_CFLAGS := $(shell pkg-config --cflags gobject-2.0)
GOBJECT_LIBS := $(shell pkg-config --libs gobject-2.0)
GTK_CFLAGS := $(shell pkg-config --cflags gtk4)
GTK_LIBS := $(shell pkg-config --libs gtk4)
LDLIBS := $(GLIB_LIBS) $(GOBJECT_LIBS) -lm

CFLAGS += $(GLIB_CFLAGS) $(GOBJECT_CFLAGS)

CORE_SRCS := src/board.c src/game.c src/move_gen.c src/checkers_model.c
WIDGET_UTILS_SRCS := src/widget_utils.c

BROADWAY_TEST_DISPLAY ?= 40
BROADWAY_TEST_PORT ?= 8120
BROADWAY_STARTUP_DELAY ?= 0.3
BROADWAYD_BIN ?= gtk4-broadwayd
XDG_RUNTIME_DIR ?= /tmp/xdg-runtime
BROADWAY_TEST_LOG ?= /tmp/broadwayd-$(BROADWAY_TEST_PORT).log

.PHONY: all clean test reproduce_bug_broadway

all: gcheckers

gcheckers: src/gcheckers.c src/gcheckers_application.c src/gcheckers_window.c src/gcheckers_window.h \
	src/gcheckers_sgf_controller.c src/gcheckers_sgf_controller.h src/board_view.c src/board_view.h \
	src/board_grid.c src/board_grid.h src/board_square.c src/board_square.h \
	src/board_selection_controller.c src/board_selection_controller.h \
	src/gcheckers_application.h src/checkers_model.c src/checkers_model.h src/sgf_tree.c src/sgf_tree.h \
	src/sgf_view.c src/sgf_view.h src/sgf_view_disc_factory.c src/sgf_view_disc_factory.h \
	src/sgf_view_layout.c src/sgf_view_layout.h \
	src/sgf_view_scroller.c src/sgf_view_scroller.h src/sgf_view_selection_controller.c \
	src/sgf_view_selection_controller.h $(WIDGET_UTILS_SRCS) $(CORE_SRCS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ src/gcheckers.c src/gcheckers_application.c \
		src/gcheckers_window.c \
		src/gcheckers_sgf_controller.c src/board_view.c src/board_grid.c src/board_square.c \
		src/board_selection_controller.c src/sgf_tree.c src/sgf_view.c \
		src/sgf_view_disc_factory.c \
		src/sgf_view_layout.c src/sgf_view_scroller.c \
		src/sgf_view_selection_controller.c $(WIDGET_UTILS_SRCS) $(CORE_SRCS) $(LDLIBS) $(GTK_LIBS)

test: gcheckers tests/test_repro_bug.sh
	@tests/test_repro_bug.sh

reproduce_bug_broadway: gcheckers
	@if ! command -v $(BROADWAYD_BIN) >/dev/null 2>&1; then \
		echo "Skipping bug reproduction: $(BROADWAYD_BIN) not available."; \
		exit 0; \
	fi; \
	mkdir -p "$(XDG_RUNTIME_DIR)"; \
	chmod 700 "$(XDG_RUNTIME_DIR)"; \
	broadway_pid=""; \
	cleanup() { \
		if [ -n "$$broadway_pid" ]; then \
			kill "$$broadway_pid" >/dev/null 2>&1 || true; \
			wait "$$broadway_pid" >/dev/null 2>&1 || true; \
		fi; \
	}; \
	trap cleanup EXIT INT TERM; \
	XDG_RUNTIME_DIR="$(XDG_RUNTIME_DIR)" $(BROADWAYD_BIN) --port "$(BROADWAY_TEST_PORT)" \
		":$(BROADWAY_TEST_DISPLAY)" >"$(BROADWAY_TEST_LOG)" 2>&1 & \
	broadway_pid=$$!; \
	sleep "$(BROADWAY_STARTUP_DELAY)"; \
	XDG_RUNTIME_DIR="$(XDG_RUNTIME_DIR)" GDK_BACKEND=broadway \
		BROADWAY_DISPLAY=":$(BROADWAY_TEST_DISPLAY)" G_MESSAGES_DEBUG=all ./gcheckers

clean:
	rm -f gcheckers
