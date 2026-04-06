CC := cc
CFLAGS := -std=c99 -Wall -Wextra -Werror -Isrc
CFLAGS += -g3
COVERAGE_CFLAGS := --coverage -O0 -g
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)
GOBJECT_CFLAGS := $(shell pkg-config --cflags gobject-2.0)
GOBJECT_LIBS := $(shell pkg-config --libs gobject-2.0)
GIO_CFLAGS := $(shell pkg-config --cflags gio-2.0)
GIO_LIBS := $(shell pkg-config --libs gio-2.0)
GTK_CFLAGS := $(shell pkg-config --cflags gtk4)
GTK_LIBS := $(shell pkg-config --libs gtk4)
CURL_CFLAGS := $(shell pkg-config --cflags libcurl)
CURL_LIBS := $(shell pkg-config --libs libcurl)
LDLIBS := $(GLIB_LIBS) $(GOBJECT_LIBS) $(GIO_LIBS) $(CURL_LIBS) -lm

CFLAGS += $(GLIB_CFLAGS) $(GOBJECT_CFLAGS) $(GIO_CFLAGS) $(CURL_CFLAGS)

SRCS := src/board.c src/game.c src/game_print.c src/move_gen.c src/ai_alpha_beta.c \
	src/rulesets.c \
	src/ai_transposition_table.c src/ai_zobrist.c src/checkers_model.c
POSITION_SRCS := src/position_search.c src/position_predicate.c src/position_format.c
BOARD_SRCS := src/board.c
SGF_TREE_SRCS := src/sgf_tree.c
SGF_MOVE_PROPS_SRCS := src/sgf_move_props.c
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
COV_OBJ_DIR := $(COV_DIR)/obj
COV_BIN_DIR := $(COV_DIR)/bin
COV_GCOV_DIR := $(COV_DIR)/gcov
COV_REPORT_DIR := $(COV_DIR)/report
COV_OBJS := $(SRCS:%.c=$(COV_OBJ_DIR)/%.o)
COV_BOARD_OBJS := $(BOARD_SRCS:%.c=$(COV_OBJ_DIR)/%.o)
SCREENSHOT ?= gcheckers.png
BROADWAY_DISPLAY_NUM ?= 5
BROADWAY_PORT ?= 8085
BROADWAY_TEST_DISPLAY ?= 40
BROADWAY_TEST_PORT ?= 8120
BROADWAY_STARTUP_DELAY ?= 0.3
SCREEN_SIZE ?= 1280x720
BROADWAYD_BIN ?= gtk4-broadwayd
CHROMIUM_BIN ?= google-chrome
XDG_RUNTIME_DIR ?= /tmp/xdg-runtime
BROADWAY_TEST_LOG ?= /tmp/broadwayd-$(BROADWAY_TEST_PORT).log
GSETTINGS_SCHEMA_DIR := data/schemas
GSETTINGS_SCHEMA_XML := $(GSETTINGS_SCHEMA_DIR)/com.example.gcheckers.gschema.xml
GSETTINGS_SCHEMA_COMPILED := $(GSETTINGS_SCHEMA_DIR)/gschemas.compiled

.PHONY: all clean test coverage screenshot test_screenshot test_sgf_view_broadway

all: $(GSETTINGS_SCHEMA_COMPILED) libgame.a create_puzzles find_position gcheckers

libgame.a: $(OBJS)
	ar rcs $@ $^

%.o: %.c src/game.h src/board.h src/checkers_constants.h
	$(CC) $(CFLAGS) -c $< -o $@

test: test_game test_game_print test_board test_move_gen test_checkers_model \
	test_ai_transposition_table test_position_search \
	test_position_predicate test_sgf_tree test_sgf_io test_sgf_view test_bga_client \
	test_file_dialog_history test_board_view test_player_controls_panel test_sgf_controller test_window \
	test_puzzle_generation test_screenshot
	./test_game
	./test_game_print
	./test_board
	./test_move_gen
	./test_checkers_model
	./test_ai_transposition_table
	./test_position_search
	./test_position_predicate
	./test_sgf_tree
	./test_sgf_io
	./test_bga_client
	./test_file_dialog_history
	./test_puzzle_generation
	$(MAKE) test_sgf_view_broadway
	$(MAKE) test_gtk_broadway

test_game: tests/test_game.c $(SRCS) src/game.h
	$(CC) $(CFLAGS) -o $@ tests/test_game.c $(SRCS) $(LDLIBS)

test_game_print: tests/test_game_print.c $(SRCS) src/game.h
	$(CC) $(CFLAGS) -o $@ tests/test_game_print.c $(SRCS) $(LDLIBS)

test_board: tests/test_board.c $(BOARD_SRCS) src/board.h src/checkers_constants.h
	$(CC) $(CFLAGS) -o $@ tests/test_board.c $(BOARD_SRCS) $(LDLIBS)

test_move_gen: tests/test_move_gen.c $(SRCS) src/game.h
	$(CC) $(CFLAGS) -o $@ tests/test_move_gen.c $(SRCS) $(LDLIBS)

create_puzzles: src/create_puzzles.c src/puzzle_generation.c src/puzzle_generation.h \
	src/sgf_io.c src/sgf_io.h src/sgf_tree.c src/sgf_tree.h src/sgf_move_props.c src/sgf_move_props.h \
	$(SRCS)
	$(CC) $(CFLAGS) -o $@ src/create_puzzles.c src/puzzle_generation.c src/sgf_io.c src/sgf_tree.c \
		src/sgf_move_props.c $(SRCS) $(LDLIBS)

find_position: src/find_position.c $(POSITION_SRCS) $(SRCS) src/position_search.h src/position_predicate.h \
	src/position_format.h
	$(CC) $(CFLAGS) -o $@ src/find_position.c $(POSITION_SRCS) $(SRCS) $(LDLIBS)

test_checkers_model: tests/test_checkers_model.c $(SRCS) src/checkers_model.h
	$(CC) $(CFLAGS) -o $@ tests/test_checkers_model.c $(SRCS) $(LDLIBS)

test_ai_transposition_table: tests/test_ai_transposition_table.c $(SRCS) src/ai_transposition_table.h src/ai_zobrist.h
	$(CC) $(CFLAGS) -o $@ tests/test_ai_transposition_table.c $(SRCS) $(LDLIBS)

test_position_search: tests/test_position_search.c $(POSITION_SRCS) $(SRCS) src/position_search.h
	$(CC) $(CFLAGS) -o $@ tests/test_position_search.c $(POSITION_SRCS) $(SRCS) $(LDLIBS)

test_position_predicate: tests/test_position_predicate.c $(POSITION_SRCS) $(SRCS) src/position_predicate.h
	$(CC) $(CFLAGS) -o $@ tests/test_position_predicate.c $(POSITION_SRCS) $(SRCS) $(LDLIBS)

test_bga_client: tests/test_bga_client.c src/bga_client.c src/bga_client.h
	$(CC) $(CFLAGS) -o $@ tests/test_bga_client.c src/bga_client.c $(LDLIBS)

test_file_dialog_history: $(GSETTINGS_SCHEMA_COMPILED) tests/test_file_dialog_history.c \
	src/file_dialog_history.c src/file_dialog_history.h
	$(CC) $(CFLAGS) -o $@ tests/test_file_dialog_history.c src/file_dialog_history.c $(LDLIBS)

test_sgf_tree: tests/test_sgf_tree.c $(SGF_TREE_SRCS) src/sgf_tree.h
	$(CC) $(CFLAGS) -o $@ tests/test_sgf_tree.c $(SGF_TREE_SRCS) $(LDLIBS)

test_sgf_io: tests/test_sgf_io.c src/sgf_io.c src/sgf_io.h src/sgf_tree.c src/sgf_tree.h src/sgf_move_props.c \
	src/sgf_move_props.h \
	src/game.h src/game_print.c src/board.c
	$(CC) $(CFLAGS) -o $@ tests/test_sgf_io.c src/sgf_io.c src/sgf_tree.c src/sgf_move_props.c src/game_print.c \
		src/board.c $(LDLIBS)

test_sgf_view: tests/test_sgf_view.c $(SGF_VIEW_SRCS) $(SGF_TREE_SRCS) $(WIDGET_UTILS_SRCS) \
	src/sgf_view.h src/sgf_tree.h $(WIDGET_UTILS_HDRS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_sgf_view.c $(SGF_VIEW_SRCS) $(SGF_TREE_SRCS) \
		$(WIDGET_UTILS_SRCS) $(LDLIBS) $(GTK_LIBS)

test_sgf_view_broadway: test_sgf_view
	@if ! command -v $(BROADWAYD_BIN) >/dev/null 2>&1; then \
		echo "Skipping Broadway SGF view test: $(BROADWAYD_BIN) not available."; \
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
		BROADWAY_DISPLAY=":$(BROADWAY_TEST_DISPLAY)" ./test_sgf_view

test_gtk_broadway: test_board_view test_player_controls_panel test_sgf_controller test_window
	@if ! command -v $(BROADWAYD_BIN) >/dev/null 2>&1; then \
		echo "Skipping GTK tests: $(BROADWAYD_BIN) not available."; \
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
		BROADWAY_DISPLAY=":$(BROADWAY_TEST_DISPLAY)" ./test_board_view; \
	XDG_RUNTIME_DIR="$(XDG_RUNTIME_DIR)" GDK_BACKEND=broadway \
		BROADWAY_DISPLAY=":$(BROADWAY_TEST_DISPLAY)" ./test_player_controls_panel; \
	XDG_RUNTIME_DIR="$(XDG_RUNTIME_DIR)" GDK_BACKEND=broadway \
		BROADWAY_DISPLAY=":$(BROADWAY_TEST_DISPLAY)" ./test_sgf_controller; \
	XDG_RUNTIME_DIR="$(XDG_RUNTIME_DIR)" GDK_BACKEND=broadway \
		BROADWAY_DISPLAY=":$(BROADWAY_TEST_DISPLAY)" ./test_window

test_board_view: tests/test_board_view.c src/board_view.c src/board_view.h src/board_grid.c src/board_grid.h \
	src/board_square.c src/board_square.h src/board_move_overlay.c src/board_move_overlay.h \
	src/board_selection_controller.c src/board_selection_controller.h src/piece_palette.c \
	src/piece_palette.h src/man_paintable.c src/man_paintable.h src/checkers_model.h \
	$(SRCS) $(WIDGET_UTILS_SRCS) $(WIDGET_UTILS_HDRS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_board_view.c src/board_view.c src/board_grid.c \
		src/board_square.c src/board_move_overlay.c src/board_selection_controller.c src/piece_palette.c \
		src/man_paintable.c $(WIDGET_UTILS_SRCS) $(SRCS) $(LDLIBS) $(GTK_LIBS)

test_player_controls_panel: tests/test_player_controls_panel.c src/player_controls_panel.c src/player_controls_panel.h
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_player_controls_panel.c src/player_controls_panel.c \
		$(LDLIBS) $(GTK_LIBS)

test_sgf_controller: tests/test_sgf_controller.c src/sgf_controller.c \
	src/sgf_controller.h src/board_view.c src/board_view.h src/board_grid.c src/board_grid.h \
	src/board_square.c src/board_square.h src/board_move_overlay.c src/board_move_overlay.h \
	src/board_selection_controller.c src/board_selection_controller.h src/piece_palette.c \
	src/piece_palette.h src/man_paintable.c src/man_paintable.h src/checkers_model.c \
	src/checkers_model.h src/sgf_io.c src/sgf_io.h src/sgf_move_props.c src/sgf_move_props.h src/sgf_tree.c \
	src/sgf_tree.h src/sgf_view.c src/sgf_view.h \
	src/sgf_view_disc_factory.c src/sgf_view_disc_factory.h src/sgf_view_layout.c src/sgf_view_layout.h \
	src/sgf_view_link_renderer.c src/sgf_view_link_renderer.h src/sgf_view_scroller.c \
	src/sgf_view_scroller.h src/sgf_view_selection_controller.c src/sgf_view_selection_controller.h \
	$(SRCS) $(WIDGET_UTILS_SRCS) $(WIDGET_UTILS_HDRS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_sgf_controller.c \
	src/sgf_controller.c src/board_view.c src/board_grid.c src/board_square.c \
	src/board_move_overlay.c src/board_selection_controller.c src/piece_palette.c \
	src/man_paintable.c src/sgf_io.c src/sgf_move_props.c src/sgf_tree.c src/sgf_view.c src/sgf_view_disc_factory.c \
	src/sgf_view_layout.c src/sgf_view_link_renderer.c src/sgf_view_scroller.c \
	src/sgf_view_selection_controller.c $(WIDGET_UTILS_SRCS) $(SRCS) $(LDLIBS) $(GTK_LIBS)

test_window: $(GSETTINGS_SCHEMA_COMPILED) tests/test_window.c src/window.c \
	src/new_game_dialog.c \
	src/rulesets.c src/rulesets.h \
	src/import_dialog.c \
	src/sgf_file_actions.c src/sgf_file_actions.h src/file_dialog_history.c src/file_dialog_history.h \
	src/bga_client.c src/bga_client.h \
	src/window.h \
	src/style.c src/style.h src/player_controls_panel.c src/player_controls_panel.h \
	src/analysis_graph.c src/analysis_graph.h \
	src/sgf_controller.c src/sgf_controller.h src/board_view.c src/board_view.h \
	src/board_grid.c src/board_grid.h src/board_square.c src/board_square.h src/board_move_overlay.c \
	src/board_move_overlay.h src/board_selection_controller.c src/board_selection_controller.h \
	src/piece_palette.c src/piece_palette.h src/man_paintable.c src/man_paintable.h \
	src/checkers_model.c src/checkers_model.h src/sgf_io.c src/sgf_io.h src/sgf_move_props.c src/sgf_move_props.h \
	src/sgf_tree.c src/sgf_tree.h \
	src/sgf_view.c src/sgf_view.h \
	src/sgf_view_disc_factory.c src/sgf_view_disc_factory.h src/sgf_view_layout.c src/sgf_view_layout.h \
	src/sgf_view_link_renderer.c src/sgf_view_link_renderer.h src/sgf_view_scroller.c \
	src/sgf_view_scroller.h src/sgf_view_selection_controller.c src/sgf_view_selection_controller.h \
	$(SRCS) $(WIDGET_UTILS_SRCS) $(WIDGET_UTILS_HDRS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_window.c src/window.c \
		src/new_game_dialog.c \
		src/import_dialog.c src/file_dialog_history.c \
		src/sgf_file_actions.c \
		src/bga_client.c \
	src/style.c src/player_controls_panel.c src/sgf_controller.c \
	src/analysis_graph.c \
	src/board_view.c src/board_grid.c src/board_square.c src/board_move_overlay.c \
	src/board_selection_controller.c src/piece_palette.c src/man_paintable.c \
	src/sgf_io.c src/sgf_move_props.c src/sgf_tree.c src/sgf_view.c src/sgf_view_disc_factory.c src/sgf_view_layout.c \
	src/sgf_view_link_renderer.c src/sgf_view_scroller.c src/sgf_view_selection_controller.c \
	$(WIDGET_UTILS_SRCS) $(SRCS) $(LDLIBS) $(GTK_LIBS)

test_puzzle_generation: tests/test_puzzle_generation.c src/puzzle_generation.c src/puzzle_generation.h src/ai_alpha_beta.h \
	src/board.h
	$(CC) $(CFLAGS) -o $@ tests/test_puzzle_generation.c src/puzzle_generation.c $(LDLIBS)

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

gcheckers: $(GSETTINGS_SCHEMA_COMPILED) src/gcheckers.c src/application.c src/window.c \
	src/new_game_dialog.c \
	src/rulesets.c src/rulesets.h \
	src/import_dialog.c \
	src/sgf_file_actions.c src/sgf_file_actions.h src/file_dialog_history.c src/file_dialog_history.h \
	src/bga_client.c src/bga_client.h \
	src/window.h \
	src/style.c src/style.h src/player_controls_panel.c src/player_controls_panel.h \
	src/analysis_graph.c src/analysis_graph.h \
	src/sgf_controller.c src/sgf_controller.h src/board_view.c src/board_view.h \
	src/board_grid.c src/board_grid.h src/board_square.c src/board_square.h src/board_move_overlay.c \
	src/board_move_overlay.h src/board_selection_controller.c src/board_selection_controller.h \
	src/piece_palette.c src/piece_palette.h src/application.h src/man_paintable.c \
	src/man_paintable.h src/checkers_model.c src/checkers_model.h src/sgf_io.c src/sgf_io.h \
	src/sgf_move_props.c src/sgf_move_props.h \
	src/sgf_tree.c src/sgf_tree.h src/sgf_view.c src/sgf_view.h src/sgf_view_disc_factory.c \
	src/sgf_view_disc_factory.h \
	src/sgf_view_layout.c src/sgf_view_layout.h src/sgf_view_link_renderer.c src/sgf_view_link_renderer.h \
	src/sgf_view_scroller.c src/sgf_view_scroller.h src/sgf_view_selection_controller.c \
	src/sgf_view_selection_controller.h $(SRCS) $(WIDGET_UTILS_SRCS) $(WIDGET_UTILS_HDRS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ src/gcheckers.c src/application.c \
		src/window.c src/new_game_dialog.c src/import_dialog.c src/file_dialog_history.c src/style.c \
	src/player_controls_panel.c src/analysis_graph.c src/sgf_file_actions.c src/bga_client.c \
	src/sgf_controller.c src/board_view.c src/board_grid.c src/board_square.c \
	src/board_move_overlay.c src/board_selection_controller.c src/piece_palette.c \
	src/man_paintable.c src/sgf_io.c src/sgf_move_props.c src/sgf_tree.c src/sgf_view.c \
	src/sgf_view_disc_factory.c \
	src/sgf_view_layout.c src/sgf_view_link_renderer.c src/sgf_view_scroller.c \
	src/sgf_view_selection_controller.c $(WIDGET_UTILS_SRCS) $(SRCS) $(LDLIBS) $(GTK_LIBS)

$(GSETTINGS_SCHEMA_COMPILED): $(GSETTINGS_SCHEMA_XML)
	glib-compile-schemas $(GSETTINGS_SCHEMA_DIR)

clean:
	rm -f $(OBJS) libgame.a test_game test_game_print test_board test_move_gen test_checkers_model \
		test_ai_transposition_table test_position_search test_position_predicate test_sgf_tree test_sgf_io test_sgf_view \
		test_bga_client test_file_dialog_history test_board_view test_player_controls_panel test_sgf_controller \
		test_window test_screenshot find_position gcheckers
	rm -f $(GSETTINGS_SCHEMA_COMPILED)
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
