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
CHECKERS_DIR := src/games/checkers
HOMEWORLDS_DIR := src/games/homeworlds
BOOP_DIR := src/games/boop
CHECKERS_APP_ID := io.github.jeromea.gcheckers
BOOP_APP_ID := io.github.jeromea.gboop
HOMEWORLDS_APP_ID := io.github.jeromea.ghomeworlds
CHECKERS_APP_BIN_NAME := gcheckers
BOOP_APP_BIN_NAME := gboop
HOMEWORLDS_APP_BIN_NAME := ghomeworlds
CHECKERS_DESKTOP_FILE := data/$(CHECKERS_APP_ID).desktop
CHECKERS_METAINFO_FILE := data/$(CHECKERS_APP_ID).metainfo.xml
CHECKERS_ICON_FILE := data/icons/hicolor/scalable/apps/$(CHECKERS_APP_ID).svg
CHECKERS_FLATPAK_MANIFEST := flatpak/$(CHECKERS_APP_ID).yaml
BOOP_DESKTOP_FILE := data/$(BOOP_APP_ID).desktop
BOOP_METAINFO_FILE := data/$(BOOP_APP_ID).metainfo.xml
BOOP_ICON_FILE := data/icons/hicolor/scalable/apps/$(BOOP_APP_ID).svg
BOOP_FLATPAK_MANIFEST := flatpak/$(BOOP_APP_ID).yaml
DESKTOP_FILES := $(CHECKERS_DESKTOP_FILE) $(BOOP_DESKTOP_FILE)
METAINFO_FILES := $(CHECKERS_METAINFO_FILE) $(BOOP_METAINFO_FILE)
ICON_FILES := $(CHECKERS_ICON_FILE) $(BOOP_ICON_FILE)
FLATPAK_MANIFESTS := $(CHECKERS_FLATPAK_MANIFEST) $(BOOP_FLATPAK_MANIFEST)
HOMEWORLDS_GAME_SRCS := $(HOMEWORLDS_DIR)/homeworlds_game.c $(HOMEWORLDS_DIR)/homeworlds_move_builder.c
HOMEWORLDS_BACKEND_SRCS := $(HOMEWORLDS_DIR)/homeworlds_backend.c
HOMEWORLDS_APP_WINDOW_SRCS := $(HOMEWORLDS_DIR)/homeworlds_app_window.c
HOMEWORLDS_APP_WINDOW_STUB_SRCS := $(HOMEWORLDS_DIR)/homeworlds_app_window_stub.c
HOMEWORLDS_ALL_SRCS := $(HOMEWORLDS_GAME_SRCS) $(HOMEWORLDS_BACKEND_SRCS)
BOOP_GAME_SRCS := $(BOOP_DIR)/boop_game.c
BOOP_BACKEND_SRCS := $(BOOP_DIR)/boop_backend.c $(BOOP_DIR)/boop_sgf_position.c
BOOP_STUB_SRCS := $(BOOP_DIR)/boop_controls_stub.c
BOOP_UI_SRCS := $(BOOP_DIR)/boop_controls.c
BOOP_RULES_SRCS := $(BOOP_GAME_SRCS) $(BOOP_BACKEND_SRCS) $(BOOP_STUB_SRCS)
CHECKERS_BACKEND_SRCS := $(CHECKERS_DIR)/checkers_backend.c $(CHECKERS_DIR)/checkers_sgf_position.c

CFLAGS += $(GLIB_CFLAGS) $(GOBJECT_CFLAGS) $(GIO_CFLAGS) $(GTK_CFLAGS) $(CURL_CFLAGS)

BUILD_DIR := build
BIN_DIR := $(BUILD_DIR)/bin
TOOLS_DIR := $(BUILD_DIR)/tools
TESTS_DIR := $(BUILD_DIR)/tests
LIB_DIR := $(BUILD_DIR)/lib
OBJ_DIR := $(BUILD_DIR)/obj
CALLGRIND_DIR := $(BUILD_DIR)/callgrind

APP_PATHS_SRCS := src/app_paths.c
PUZZLE_PROGRESS_SRCS := src/puzzle_progress.c
PUZZLE_CATALOG_SRCS := src/puzzle_catalog.c
APP_MAIN_SRCS := src/app_main.c
TEST_PROFILE_UTILS_SRCS := tests/test_profile_utils.c
CHECKERS_SRCS := $(CHECKERS_DIR)/board.c $(CHECKERS_DIR)/board_geometry.c $(CHECKERS_DIR)/game.c \
	$(CHECKERS_DIR)/game_print.c $(CHECKERS_DIR)/move_gen.c $(CHECKERS_DIR)/rulesets.c \
	$(CHECKERS_DIR)/ai_alpha_beta.c $(CHECKERS_DIR)/ai_transposition_table.c $(CHECKERS_DIR)/ai_zobrist.c \
	$(CHECKERS_DIR)/checkers_model.c
SRCS := $(CHECKERS_SRCS) $(CHECKERS_BACKEND_SRCS) \
	$(HOMEWORLDS_GAME_SRCS) $(HOMEWORLDS_BACKEND_SRCS) $(HOMEWORLDS_APP_WINDOW_STUB_SRCS) \
	$(BOOP_RULES_SRCS) src/ai_search.c src/game_model.c src/game_app_profile.c
BOARD_SRCS := $(CHECKERS_DIR)/board.c
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
OBJS := $(SRCS:%.c=$(OBJ_DIR)/%.o)
BACKEND_CODEC_SRCS := $(SRCS) $(SGF_TREE_SRCS)
BACKEND_CODEC_SRCS_NO_RULESETS := $(filter-out $(CHECKERS_DIR)/rulesets.c,$(BACKEND_CODEC_SRCS))
COV_DIR := coverage
COV_OBJ_DIR := $(COV_DIR)/obj
COV_BIN_DIR := $(COV_DIR)/bin
COV_GCOV_DIR := $(COV_DIR)/gcov
COV_REPORT_DIR := $(COV_DIR)/report
COV_OBJS := $(SRCS:%.c=$(COV_OBJ_DIR)/%.o)
COV_BOARD_OBJS := $(BOARD_SRCS:%.c=$(COV_OBJ_DIR)/%.o)
GSETTINGS_SCHEMA_DIR := data/schemas
GSETTINGS_SCHEMA_XMLS := $(GSETTINGS_SCHEMA_DIR)/$(CHECKERS_APP_ID).gschema.xml \
	$(GSETTINGS_SCHEMA_DIR)/$(BOOP_APP_ID).gschema.xml
GSETTINGS_SCHEMA_COMPILED := $(GSETTINGS_SCHEMA_DIR)/gschemas.compiled
PUZZLE_VARIANTS := american international russian
PREFIX ?= /usr/local
DESTDIR ?=
BINDIR := $(PREFIX)/bin
DATADIR := $(PREFIX)/share
APPLICATIONS_DIR := $(DATADIR)/applications
METAINFO_DIR := $(DATADIR)/metainfo
ICONS_DIR := $(DATADIR)/icons/hicolor/scalable/apps
PUZZLES_INSTALL_DIR := $(DATADIR)/gcheckers/puzzles
SCHEMAS_INSTALL_DIR := $(DATADIR)/glib-2.0/schemas
INSTALL ?= install

LIBGAME_A := $(LIB_DIR)/libgame.a
CHECKERS_APP_BIN := $(BIN_DIR)/$(CHECKERS_APP_BIN_NAME)
BOOP_APP_BIN := $(BIN_DIR)/$(BOOP_APP_BIN_NAME)
HOMEWORLDS_APP_BIN := $(BIN_DIR)/$(HOMEWORLDS_APP_BIN_NAME)
APP_BINS := $(CHECKERS_APP_BIN) $(BOOP_APP_BIN) $(HOMEWORLDS_APP_BIN)
CREATE_PUZZLES_BIN := $(TOOLS_DIR)/checkers_create_puzzles
TEST_GAME_BIN := $(TESTS_DIR)/test_game
TEST_GAME_PRINT_BIN := $(TESTS_DIR)/test_game_print
TEST_GAME_BACKEND_BIN := $(TESTS_DIR)/test_game_backend
TEST_GAME_MODEL_BIN := $(TESTS_DIR)/test_game_model
TEST_HOMEWORLDS_GAME_BIN := $(TESTS_DIR)/test_homeworlds_game
TEST_HOMEWORLDS_BACKEND_BIN := $(TESTS_DIR)/test_homeworlds_backend
TEST_BOOP_GAME_BIN := $(TESTS_DIR)/test_boop_game
TEST_BOOP_BACKEND_BIN := $(TESTS_DIR)/test_boop_backend
TEST_BOARD_BIN := $(TESTS_DIR)/test_board
TEST_BOARD_GEOMETRY_BIN := $(TESTS_DIR)/test_board_geometry
TEST_MOVE_GEN_BIN := $(TESTS_DIR)/test_move_gen
TEST_CREATE_PUZZLES_CLI_BIN := $(TESTS_DIR)/test_create_puzzles_cli
TEST_CREATE_PUZZLES_CHECK_BIN := $(TESTS_DIR)/test_create_puzzles_check
TEST_CHECKERS_MODEL_BIN := $(TESTS_DIR)/test_checkers_model
TEST_AI_SEARCH_BIN := $(TESTS_DIR)/test_ai_search
TEST_AI_TRANSPOSITION_TABLE_BIN := $(TESTS_DIR)/test_ai_transposition_table
TEST_BGA_CLIENT_BIN := $(TESTS_DIR)/test_bga_client
TEST_FILE_DIALOG_HISTORY_BIN := $(TESTS_DIR)/test_file_dialog_history
TEST_APP_SETTINGS_BIN := $(TESTS_DIR)/test_app_settings
TEST_APP_PATHS_BIN := $(TESTS_DIR)/test_app_paths
TEST_DESKTOP_METADATA_BIN := $(TESTS_DIR)/test_desktop_metadata
TEST_FLATPAK_MANIFEST_BIN := $(TESTS_DIR)/test_flatpak_manifest
TEST_SGF_TREE_BIN := $(TESTS_DIR)/test_sgf_tree
TEST_SGF_IO_BIN := $(TESTS_DIR)/test_sgf_io
TEST_SGF_VIEW_BIN := $(TESTS_DIR)/test_sgf_view
TEST_BOARD_VIEW_BIN := $(TESTS_DIR)/test_board_view
TEST_PLAYER_CONTROLS_PANEL_BIN := $(TESTS_DIR)/test_player_controls_panel
TEST_SGF_CONTROLLER_BIN := $(TESTS_DIR)/test_sgf_controller
TEST_WINDOW_BIN := $(TESTS_DIR)/test_window
TEST_WINDOW_BOOP_BIN := $(TESTS_DIR)/test_window_boop
TEST_PUZZLE_GENERATION_BIN := $(TESTS_DIR)/test_puzzle_generation
TEST_PUZZLE_CATALOG_BIN := $(TESTS_DIR)/test_puzzle_catalog
TEST_PIECE_PALETTE_BIN := $(TESTS_DIR)/test_piece_palette
TEST_PUZZLE_PROGRESS_BIN := $(TESTS_DIR)/test_puzzle_progress
TEST_PUZZLE_PROGRESS_REPORT_SERVER_BIN := $(TESTS_DIR)/test_puzzle_progress_report_server
CALLGRIND_OUT := $(CALLGRIND_DIR)/callgrind.out
CALLGRIND_ANNOTATION := $(CALLGRIND_DIR)/callgrind.annotated
PROFILE_BIN ?= $(CREATE_PUZZLES_BIN)
PROFILE_ARGS ?= 1
PROFILE_CMD = $(PROFILE_BIN) $(PROFILE_ARGS)
TEST_BINS := $(TEST_GAME_BIN) $(TEST_GAME_PRINT_BIN) $(TEST_GAME_BACKEND_BIN) $(TEST_GAME_MODEL_BIN) \
	$(TEST_HOMEWORLDS_GAME_BIN) $(TEST_HOMEWORLDS_BACKEND_BIN) $(TEST_BOOP_GAME_BIN) $(TEST_BOOP_BACKEND_BIN) \
	$(TEST_BOARD_BIN) $(TEST_BOARD_GEOMETRY_BIN) $(TEST_MOVE_GEN_BIN) $(TEST_CREATE_PUZZLES_CLI_BIN) \
	$(TEST_CREATE_PUZZLES_CHECK_BIN) $(TEST_CHECKERS_MODEL_BIN) $(TEST_AI_SEARCH_BIN) \
	$(TEST_AI_TRANSPOSITION_TABLE_BIN) $(TEST_BGA_CLIENT_BIN) $(TEST_FILE_DIALOG_HISTORY_BIN) \
	$(TEST_APP_SETTINGS_BIN) $(TEST_APP_PATHS_BIN) $(TEST_DESKTOP_METADATA_BIN) $(TEST_FLATPAK_MANIFEST_BIN) \
	$(TEST_SGF_TREE_BIN) $(TEST_SGF_IO_BIN) $(TEST_SGF_VIEW_BIN) $(TEST_BOARD_VIEW_BIN) \
	$(TEST_PLAYER_CONTROLS_PANEL_BIN) $(TEST_SGF_CONTROLLER_BIN) $(TEST_WINDOW_BIN) $(TEST_WINDOW_BOOP_BIN) \
	$(TEST_PUZZLE_GENERATION_BIN) $(TEST_PUZZLE_CATALOG_BIN) $(TEST_PIECE_PALETTE_BIN) \
	$(TEST_PUZZLE_PROGRESS_BIN)
TEST_CHECKERS_PROFILE_BINS := $(TEST_AI_SEARCH_BIN) $(TEST_APP_SETTINGS_BIN) $(TEST_FILE_DIALOG_HISTORY_BIN) \
	$(TEST_CREATE_PUZZLES_CHECK_BIN) $(TEST_PUZZLE_CATALOG_BIN) $(TEST_PUZZLE_PROGRESS_BIN) \
	$(TEST_GAME_BACKEND_BIN) $(TEST_GAME_MODEL_BIN) $(TEST_SGF_IO_BIN) $(TEST_BOARD_VIEW_BIN) \
	$(TEST_SGF_CONTROLLER_BIN) $(TEST_WINDOW_BIN)
TEST_BOOP_PROFILE_BINS := $(TEST_APP_SETTINGS_BIN) $(TEST_FILE_DIALOG_HISTORY_BIN) $(TEST_GAME_BACKEND_BIN) \
	$(TEST_GAME_MODEL_BIN) $(TEST_SGF_IO_BIN) $(TEST_BOARD_VIEW_BIN) $(TEST_SGF_CONTROLLER_BIN) \
	$(TEST_WINDOW_BOOP_BIN)
TEST_HOMEWORLDS_PROFILE_BINS := $(TEST_GAME_BACKEND_BIN) $(TEST_GAME_MODEL_BIN) $(TEST_SGF_IO_BIN)
TEST_PROFILE_BINS := $(sort $(TEST_CHECKERS_PROFILE_BINS) $(TEST_BOOP_PROFILE_BINS) $(TEST_HOMEWORLDS_PROFILE_BINS))
TEST_NO_PROFILE_BINS := $(filter-out $(TEST_PROFILE_BINS),$(TEST_BINS))

.PHONY: all clean test coverage install install-checkers install-boop install-homeworlds install-schemas \
	validate-desktop-metadata \
	gcheckers gboop ghomeworlds all-checkers all-boop all-homeworlds create_puzzles libgame.a \
	test_game test_game_print test_game_backend test_game_model test_homeworlds_game test_homeworlds_backend test_boop_game test_boop_backend test_board test_board_geometry test_move_gen test_create_puzzles_cli test_create_puzzles_check \
	test_checkers_model test_ai_search test_ai_transposition_table test_bga_client \
	test_file_dialog_history test_app_settings test_app_paths test_desktop_metadata test_flatpak_manifest test_sgf_tree test_sgf_io \
	test_sgf_view test_board_view test_player_controls_panel test_sgf_controller test_window test_window_boop test_puzzle_generation test_puzzle_catalog \
	test_piece_palette test_puzzle_progress test_puzzle_progress_report_server callgrind-run \
	callgrind-annotate

all: $(GSETTINGS_SCHEMA_COMPILED) $(LIBGAME_A) $(CREATE_PUZZLES_BIN) $(APP_BINS)

gcheckers all-checkers: $(CHECKERS_APP_BIN)
gboop all-boop: $(BOOP_APP_BIN)
ghomeworlds all-homeworlds: $(HOMEWORLDS_APP_BIN)
libgame.a: $(LIBGAME_A)

create_puzzles: $(CREATE_PUZZLES_BIN)

$(LIBGAME_A): $(OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_BINS)
	@/bin/bash -lc 'set -eu; \
		for test_bin in $(TEST_NO_PROFILE_BINS); do "$$test_bin"; done; \
		for test_bin in $(TEST_CHECKERS_PROFILE_BINS); do "$$test_bin" --profile=checkers; done; \
		for test_bin in $(TEST_BOOP_PROFILE_BINS); do "$$test_bin" --profile=boop; done; \
		for test_bin in $(TEST_HOMEWORLDS_PROFILE_BINS); do "$$test_bin" --profile=homeworlds; done'

test_game: $(TEST_GAME_BIN)
$(TEST_GAME_BIN): tests/test_game.c $(BACKEND_CODEC_SRCS) $(CHECKERS_DIR)/game.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_game.c $(BACKEND_CODEC_SRCS) $(LDLIBS)

test_game_print: $(TEST_GAME_PRINT_BIN)
$(TEST_GAME_PRINT_BIN): tests/test_game_print.c $(BACKEND_CODEC_SRCS) $(CHECKERS_DIR)/game.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_game_print.c $(BACKEND_CODEC_SRCS) $(LDLIBS)

test_game_backend: $(TEST_GAME_BACKEND_BIN)
$(TEST_GAME_BACKEND_BIN): tests/test_game_backend.c src/active_game_backend.h src/game_backend.h \
	src/board_selection_controller.c src/board_selection_controller.h $(TEST_PROFILE_UTILS_SRCS) \
	$(BACKEND_CODEC_SRCS) $(CHECKERS_DIR)/rulesets.h $(CHECKERS_DIR)/ruleset.h $(CHECKERS_DIR)/game.h \
	$(CHECKERS_DIR)/board.h \
	$(CHECKERS_DIR)/checkers_constants.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_game_backend.c $(TEST_PROFILE_UTILS_SRCS) src/board_selection_controller.c \
		$(BACKEND_CODEC_SRCS) $(LDLIBS)

test_game_model: $(TEST_GAME_MODEL_BIN)
$(TEST_GAME_MODEL_BIN): tests/test_game_model.c src/active_game_backend.h src/game_backend.h src/game_model.h \
	$(TEST_PROFILE_UTILS_SRCS) $(BACKEND_CODEC_SRCS) $(CHECKERS_DIR)/rulesets.h $(CHECKERS_DIR)/ruleset.h \
	$(CHECKERS_DIR)/game.h $(CHECKERS_DIR)/board.h \
	$(CHECKERS_DIR)/checkers_constants.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_game_model.c $(TEST_PROFILE_UTILS_SRCS) $(BACKEND_CODEC_SRCS) $(LDLIBS)

test_homeworlds_game: $(TEST_HOMEWORLDS_GAME_BIN)
$(TEST_HOMEWORLDS_GAME_BIN): tests/test_homeworlds_game.c $(HOMEWORLDS_GAME_SRCS) $(HOMEWORLDS_DIR)/homeworlds_game.h $(HOMEWORLDS_DIR)/homeworlds_types.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_homeworlds_game.c $(HOMEWORLDS_GAME_SRCS) $(LDLIBS)

test_homeworlds_backend: $(TEST_HOMEWORLDS_BACKEND_BIN)
$(TEST_HOMEWORLDS_BACKEND_BIN): tests/test_homeworlds_backend.c $(HOMEWORLDS_ALL_SRCS) $(HOMEWORLDS_DIR)/homeworlds_backend.h $(HOMEWORLDS_DIR)/homeworlds_game.h $(HOMEWORLDS_DIR)/homeworlds_move_builder.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_homeworlds_backend.c $(HOMEWORLDS_ALL_SRCS) $(LDLIBS)

test_boop_game: $(TEST_BOOP_GAME_BIN)
$(TEST_BOOP_GAME_BIN): tests/test_boop_game.c $(BOOP_GAME_SRCS) $(BOOP_DIR)/boop_game.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_boop_game.c $(BOOP_GAME_SRCS) $(LDLIBS)

test_boop_backend: $(TEST_BOOP_BACKEND_BIN)
$(TEST_BOOP_BACKEND_BIN): tests/test_boop_backend.c $(BOOP_RULES_SRCS) $(SGF_TREE_SRCS) \
	$(BOOP_DIR)/boop_backend.h $(BOOP_DIR)/boop_game.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_boop_backend.c $(BOOP_RULES_SRCS) $(SGF_TREE_SRCS) $(LDLIBS)

test_board: $(TEST_BOARD_BIN)
$(TEST_BOARD_BIN): tests/test_board.c $(BOARD_SRCS) $(CHECKERS_DIR)/board.h $(CHECKERS_DIR)/checkers_constants.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_board.c $(BOARD_SRCS) $(LDLIBS)

test_board_geometry: $(TEST_BOARD_GEOMETRY_BIN)
$(TEST_BOARD_GEOMETRY_BIN): tests/test_board_geometry.c $(CHECKERS_DIR)/board_geometry.c \
	$(CHECKERS_DIR)/board_geometry.h $(CHECKERS_DIR)/board.c $(CHECKERS_DIR)/board.h \
	$(CHECKERS_DIR)/checkers_constants.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_board_geometry.c $(CHECKERS_DIR)/board_geometry.c $(CHECKERS_DIR)/board.c \
		$(LDLIBS)

test_move_gen: $(TEST_MOVE_GEN_BIN)
$(TEST_MOVE_GEN_BIN): tests/test_move_gen.c $(BACKEND_CODEC_SRCS) $(CHECKERS_DIR)/game.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_move_gen.c $(BACKEND_CODEC_SRCS) $(LDLIBS)

$(CREATE_PUZZLES_BIN): src/create_puzzles.c $(CHECKERS_DIR)/create_puzzles_cli.c $(CHECKERS_DIR)/create_puzzles_cli.h \
	$(CHECKERS_DIR)/puzzle_generation.c $(CHECKERS_DIR)/puzzle_generation.h src/sgf_io.c src/sgf_io.h src/sgf_tree.c src/sgf_tree.h \
	src/sgf_move_props.c src/sgf_move_props.h $(SRCS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ src/create_puzzles.c $(CHECKERS_DIR)/create_puzzles_cli.c \
		$(CHECKERS_DIR)/puzzle_generation.c src/sgf_io.c src/sgf_tree.c src/sgf_move_props.c $(SRCS) $(LDLIBS)

test_create_puzzles_cli: $(TEST_CREATE_PUZZLES_CLI_BIN)
$(TEST_CREATE_PUZZLES_CLI_BIN): tests/test_create_puzzles_cli.c $(CHECKERS_DIR)/create_puzzles_cli.c \
	$(CHECKERS_DIR)/create_puzzles_cli.h $(CHECKERS_DIR)/rulesets.c $(CHECKERS_DIR)/rulesets.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_create_puzzles_cli.c $(CHECKERS_DIR)/create_puzzles_cli.c \
		$(CHECKERS_DIR)/rulesets.c $(LDLIBS)

test_create_puzzles_check: $(TEST_CREATE_PUZZLES_CHECK_BIN)
$(TEST_CREATE_PUZZLES_CHECK_BIN): $(CREATE_PUZZLES_BIN) tests/test_create_puzzles_check.c src/sgf_io.c \
	src/sgf_tree.c src/sgf_move_props.c $(TEST_PROFILE_UTILS_SRCS) $(SRCS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DGCHECKERS_CREATE_PUZZLES_PATH=\"$(CREATE_PUZZLES_BIN)\" -o $@ \
		tests/test_create_puzzles_check.c $(TEST_PROFILE_UTILS_SRCS) src/sgf_io.c src/sgf_tree.c \
		src/sgf_move_props.c $(SRCS) $(LDLIBS)

test_checkers_model: $(TEST_CHECKERS_MODEL_BIN)
$(TEST_CHECKERS_MODEL_BIN): tests/test_checkers_model.c $(BACKEND_CODEC_SRCS) $(CHECKERS_DIR)/checkers_model.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_checkers_model.c $(BACKEND_CODEC_SRCS) $(LDLIBS)

test_ai_search: $(TEST_AI_SEARCH_BIN)
$(TEST_AI_SEARCH_BIN): tests/test_ai_search.c src/active_game_backend.h src/game_backend.h src/ai_search.h \
	$(TEST_PROFILE_UTILS_SRCS) $(BACKEND_CODEC_SRCS) $(CHECKERS_DIR)/rulesets.h $(CHECKERS_DIR)/ruleset.h \
	$(CHECKERS_DIR)/game.h $(CHECKERS_DIR)/board.h \
	$(CHECKERS_DIR)/checkers_constants.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_ai_search.c $(TEST_PROFILE_UTILS_SRCS) $(BACKEND_CODEC_SRCS) $(LDLIBS)

test_ai_transposition_table: $(TEST_AI_TRANSPOSITION_TABLE_BIN)
$(TEST_AI_TRANSPOSITION_TABLE_BIN): tests/test_ai_transposition_table.c $(BACKEND_CODEC_SRCS) \
	$(CHECKERS_DIR)/ai_transposition_table.h $(CHECKERS_DIR)/ai_zobrist.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_ai_transposition_table.c $(BACKEND_CODEC_SRCS) $(LDLIBS)

test_bga_client: $(TEST_BGA_CLIENT_BIN)
$(TEST_BGA_CLIENT_BIN): tests/test_bga_client.c src/bga_client.c src/bga_client.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_bga_client.c src/bga_client.c $(LDLIBS)

test_file_dialog_history: $(TEST_FILE_DIALOG_HISTORY_BIN)
$(TEST_FILE_DIALOG_HISTORY_BIN): $(GSETTINGS_SCHEMA_COMPILED) tests/test_file_dialog_history.c \
	src/file_dialog_history.c src/file_dialog_history.h src/app_settings.c src/app_settings.h \
	$(TEST_PROFILE_UTILS_SRCS) $(BACKEND_CODEC_SRCS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_file_dialog_history.c $(TEST_PROFILE_UTILS_SRCS) src/file_dialog_history.c \
		src/app_settings.c $(BACKEND_CODEC_SRCS) $(LDLIBS)

test_app_settings: $(TEST_APP_SETTINGS_BIN)
$(TEST_APP_SETTINGS_BIN): $(GSETTINGS_SCHEMA_COMPILED) tests/test_app_settings.c src/app_settings.c \
	src/app_settings.h $(TEST_PROFILE_UTILS_SRCS) $(BACKEND_CODEC_SRCS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_app_settings.c $(TEST_PROFILE_UTILS_SRCS) src/app_settings.c \
		$(BACKEND_CODEC_SRCS) $(LDLIBS)

test_app_paths: $(TEST_APP_PATHS_BIN)
$(TEST_APP_PATHS_BIN): tests/test_app_paths.c $(APP_PATHS_SRCS) src/app_paths.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_app_paths.c $(APP_PATHS_SRCS) $(LDLIBS)

test_puzzle_progress: $(TEST_PUZZLE_PROGRESS_BIN)
$(TEST_PUZZLE_PROGRESS_BIN): $(GSETTINGS_SCHEMA_COMPILED) tests/test_puzzle_progress.c $(APP_PATHS_SRCS) \
	src/puzzle_progress.c src/puzzle_progress.h src/file_dialog_history.c src/file_dialog_history.h \
	$(TEST_PROFILE_UTILS_SRCS) \
	$(CHECKERS_DIR)/rulesets.c $(CHECKERS_DIR)/rulesets.h $(CHECKERS_DIR)/game.h $(CHECKERS_DIR)/board.h \
	$(BACKEND_CODEC_SRCS_NO_RULESETS) \
	$(CHECKERS_DIR)/checkers_constants.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_puzzle_progress.c $(TEST_PROFILE_UTILS_SRCS) $(APP_PATHS_SRCS) \
		src/puzzle_progress.c src/file_dialog_history.c $(CHECKERS_DIR)/rulesets.c \
		$(BACKEND_CODEC_SRCS_NO_RULESETS) $(LDLIBS)

test_puzzle_progress_report_server: $(TEST_PUZZLE_PROGRESS_REPORT_SERVER_BIN)
	@set -eu; \
	store_dir=$$(mktemp -d /tmp/gcheckers-puzzle-report-store-XXXXXX); \
	log_file=$$(mktemp /tmp/gcheckers-puzzle-report-server-log-XXXXXX); \
	server_pid=''; \
	cleanup() { \
		if [ -n "$$server_pid" ]; then \
			kill "$$server_pid" 2>/dev/null || true; \
			wait "$$server_pid" 2>/dev/null || true; \
		fi; \
		rm -rf "$$store_dir"; \
		rm -f "$$log_file"; \
	}; \
	trap cleanup EXIT INT TERM; \
	for port in 18082 18083 18084 18085; do \
		GCHECKERS_PUZZLE_REPORT_STORE_DIR="$$store_dir" php -S 127.0.0.1:$$port -t tests >"$$log_file" 2>&1 & \
		server_pid=$$!; \
		ready=0; \
		for _attempt in 1 2 3 4 5 6 7 8 9 10; do \
			if code=$$(curl -sS -o /dev/null -w '%{http_code}' "http://127.0.0.1:$$port/puzzle_progress_report_server.php" 2>/dev/null) && \
				[ "$$code" = "405" ]; then \
				ready=1; \
				break; \
			fi; \
			if ! kill -0 "$$server_pid" 2>/dev/null; then \
				break; \
			fi; \
			sleep 0.2; \
		done; \
		if [ "$$ready" -eq 1 ]; then \
			GCHECKERS_PUZZLE_REPORT_URL="http://127.0.0.1:$$port/puzzle_progress_report_server.php" \
			GCHECKERS_PUZZLE_REPORT_STORE_DIR="$$store_dir" \
			"$(TEST_PUZZLE_PROGRESS_REPORT_SERVER_BIN)"; \
			exit 0; \
		fi; \
		kill "$$server_pid" 2>/dev/null || true; \
		wait "$$server_pid" 2>/dev/null || true; \
		server_pid=''; \
	done; \
	echo "Failed to start PHP test server. Last log:" >&2; \
	cat "$$log_file" >&2; \
	exit 1

$(TEST_PUZZLE_PROGRESS_REPORT_SERVER_BIN): tests/test_puzzle_progress_report_server.sh \
	tests/puzzle_progress_report_server.php
	@mkdir -p $(dir $@)
	cp tests/test_puzzle_progress_report_server.sh $@
	chmod +x $@

test_desktop_metadata: $(TEST_DESKTOP_METADATA_BIN)
$(TEST_DESKTOP_METADATA_BIN): tests/test_desktop_metadata.sh Makefile $(DESKTOP_FILES) $(METAINFO_FILES) $(ICON_FILES) \
	$(GSETTINGS_SCHEMA_XMLS)
	@mkdir -p $(dir $@)
	cp tests/test_desktop_metadata.sh $@
	chmod +x $@

test_flatpak_manifest: $(TEST_FLATPAK_MANIFEST_BIN)
$(TEST_FLATPAK_MANIFEST_BIN): tests/test_flatpak_manifest.sh Makefile $(FLATPAK_MANIFESTS)
	@mkdir -p $(dir $@)
	cp tests/test_flatpak_manifest.sh $@
	chmod +x $@

test_sgf_tree: $(TEST_SGF_TREE_BIN)
$(TEST_SGF_TREE_BIN): tests/test_sgf_tree.c $(SGF_TREE_SRCS) src/sgf_tree.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_sgf_tree.c $(SGF_TREE_SRCS) $(LDLIBS)

test_sgf_io: $(TEST_SGF_IO_BIN)
$(TEST_SGF_IO_BIN): tests/test_sgf_io.c src/sgf_io.c src/sgf_io.h src/sgf_tree.c src/sgf_tree.h src/sgf_move_props.c \
	src/sgf_move_props.h $(TEST_PROFILE_UTILS_SRCS) $(SRCS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_sgf_io.c $(TEST_PROFILE_UTILS_SRCS) src/sgf_io.c src/sgf_tree.c \
		src/sgf_move_props.c $(SRCS) $(LDLIBS)

test_sgf_view: $(TEST_SGF_VIEW_BIN)
$(TEST_SGF_VIEW_BIN): tests/test_sgf_view.c $(SGF_VIEW_SRCS) $(SGF_TREE_SRCS) $(WIDGET_UTILS_SRCS) \
	src/sgf_view.h src/sgf_tree.h $(WIDGET_UTILS_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_sgf_view.c $(SGF_VIEW_SRCS) $(SGF_TREE_SRCS) \
		$(WIDGET_UTILS_SRCS) $(LDLIBS) $(GTK_LIBS)

test_board_view: $(TEST_BOARD_VIEW_BIN)
$(TEST_BOARD_VIEW_BIN): tests/test_board_view.c src/board_view.c src/board_view.h src/board_grid.c src/board_grid.h \
	src/board_square.c src/board_square.h src/board_move_overlay.c src/board_move_overlay.h \
	src/board_selection_controller.c src/board_selection_controller.h src/piece_palette.c src/piece_palette.h \
	src/man_paintable.c src/man_paintable.h src/sgf_controller.c src/sgf_controller.h src/sgf_io.c src/sgf_io.h \
	src/sgf_move_props.c src/sgf_move_props.h src/sgf_tree.c src/sgf_tree.h src/sgf_view.c src/sgf_view.h \
	src/sgf_view_disc_factory.c src/sgf_view_disc_factory.h src/sgf_view_layout.c src/sgf_view_layout.h \
	src/sgf_view_link_renderer.c src/sgf_view_link_renderer.h src/sgf_view_scroller.c src/sgf_view_scroller.h \
	src/sgf_view_selection_controller.c src/sgf_view_selection_controller.h $(CHECKERS_DIR)/checkers_model.h \
	$(TEST_PROFILE_UTILS_SRCS) $(SRCS) $(WIDGET_UTILS_SRCS) $(WIDGET_UTILS_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_board_view.c $(TEST_PROFILE_UTILS_SRCS) src/board_view.c src/board_grid.c \
		src/board_square.c src/board_move_overlay.c src/board_selection_controller.c src/piece_palette.c \
		src/man_paintable.c src/sgf_controller.c src/sgf_io.c src/sgf_move_props.c src/sgf_tree.c src/sgf_view.c \
		src/sgf_view_disc_factory.c src/sgf_view_layout.c src/sgf_view_link_renderer.c src/sgf_view_scroller.c \
		src/sgf_view_selection_controller.c $(WIDGET_UTILS_SRCS) $(SRCS) $(LDLIBS) $(GTK_LIBS)

test_player_controls_panel: $(TEST_PLAYER_CONTROLS_PANEL_BIN)
$(TEST_PLAYER_CONTROLS_PANEL_BIN): tests/test_player_controls_panel.c src/player_controls_panel.c \
	src/player_controls_panel.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_player_controls_panel.c src/player_controls_panel.c \
		$(LDLIBS) $(GTK_LIBS)

test_sgf_controller: $(TEST_SGF_CONTROLLER_BIN)
$(TEST_SGF_CONTROLLER_BIN): tests/test_sgf_controller.c src/sgf_controller.c src/sgf_controller.h src/board_view.c \
	src/board_view.h src/board_grid.c src/board_grid.h src/board_square.c src/board_square.h \
	src/board_move_overlay.c src/board_move_overlay.h src/board_selection_controller.c \
	src/board_selection_controller.h src/piece_palette.c src/piece_palette.h src/man_paintable.c \
	src/man_paintable.h $(CHECKERS_DIR)/checkers_model.c $(CHECKERS_DIR)/checkers_model.h src/sgf_io.c src/sgf_io.h \
	src/sgf_move_props.c src/sgf_move_props.h src/sgf_tree.c src/sgf_tree.h src/sgf_view.c src/sgf_view.h \
	src/sgf_view_disc_factory.c src/sgf_view_disc_factory.h src/sgf_view_layout.c src/sgf_view_layout.h \
	src/sgf_view_link_renderer.c src/sgf_view_link_renderer.h src/sgf_view_scroller.c \
	src/sgf_view_scroller.h src/sgf_view_selection_controller.c src/sgf_view_selection_controller.h \
	$(TEST_PROFILE_UTILS_SRCS) $(SRCS) $(WIDGET_UTILS_SRCS) $(WIDGET_UTILS_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_sgf_controller.c $(TEST_PROFILE_UTILS_SRCS) src/sgf_controller.c src/board_view.c \
		src/board_grid.c src/board_square.c src/board_move_overlay.c src/board_selection_controller.c \
		src/piece_palette.c src/man_paintable.c src/sgf_io.c src/sgf_move_props.c src/sgf_tree.c src/sgf_view.c \
		src/sgf_view_disc_factory.c src/sgf_view_layout.c src/sgf_view_link_renderer.c src/sgf_view_scroller.c \
		src/sgf_view_selection_controller.c $(WIDGET_UTILS_SRCS) $(SRCS) $(LDLIBS) $(GTK_LIBS)

test_window: $(TEST_WINDOW_BIN)
$(TEST_WINDOW_BIN): $(GSETTINGS_SCHEMA_COMPILED) tests/test_window.c src/window.c src/application.c \
	src/application.h $(APP_PATHS_SRCS) $(PUZZLE_PROGRESS_SRCS) $(PUZZLE_CATALOG_SRCS) src/app_settings.c \
	src/app_settings.h \
	src/new_game_dialog.c src/puzzle_dialog.c src/puzzle_dialog.h $(CHECKERS_DIR)/rulesets.c $(CHECKERS_DIR)/rulesets.h \
	src/import_dialog.c src/settings_dialog.c src/settings_dialog.h src/sgf_file_actions.c \
	src/sgf_file_actions.h src/file_dialog_history.c src/file_dialog_history.h src/bga_client.c src/bga_client.h \
	src/window.h src/style.c src/style.h src/player_controls_panel.c src/player_controls_panel.h \
	src/analysis_graph.c src/analysis_graph.h src/sgf_controller.c src/sgf_controller.h src/board_view.c \
	src/board_view.h src/board_grid.c src/board_grid.h src/board_square.c src/board_square.h \
	src/board_move_overlay.c src/board_move_overlay.h src/board_selection_controller.c \
	src/board_selection_controller.h src/piece_palette.c src/piece_palette.h src/man_paintable.c \
	src/man_paintable.h $(CHECKERS_DIR)/checkers_model.c $(CHECKERS_DIR)/checkers_model.h src/sgf_io.c src/sgf_io.h \
	src/sgf_move_props.c src/sgf_move_props.h src/sgf_tree.c src/sgf_tree.h src/sgf_view.c src/sgf_view.h \
	src/sgf_view_disc_factory.c src/sgf_view_disc_factory.h src/sgf_view_layout.c src/sgf_view_layout.h \
	src/sgf_view_link_renderer.c src/sgf_view_link_renderer.h src/sgf_view_scroller.c \
	src/sgf_view_scroller.h src/sgf_view_selection_controller.c src/sgf_view_selection_controller.h \
	$(TEST_PROFILE_UTILS_SRCS) $(SRCS) $(BOOP_UI_SRCS) $(WIDGET_UTILS_SRCS) $(WIDGET_UTILS_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_window.c $(TEST_PROFILE_UTILS_SRCS) src/application.c src/window.c \
		$(APP_PATHS_SRCS) $(PUZZLE_PROGRESS_SRCS) $(PUZZLE_CATALOG_SRCS) src/app_settings.c \
		src/new_game_dialog.c src/puzzle_dialog.c src/import_dialog.c src/settings_dialog.c src/file_dialog_history.c \
		src/sgf_file_actions.c src/bga_client.c src/style.c src/player_controls_panel.c \
		src/sgf_controller.c src/analysis_graph.c src/board_view.c src/board_grid.c src/board_square.c \
		src/board_move_overlay.c src/board_selection_controller.c src/piece_palette.c src/man_paintable.c \
		src/sgf_io.c src/sgf_move_props.c src/sgf_tree.c src/sgf_view.c src/sgf_view_disc_factory.c \
		src/sgf_view_layout.c src/sgf_view_link_renderer.c src/sgf_view_scroller.c \
		src/sgf_view_selection_controller.c $(WIDGET_UTILS_SRCS) $(SRCS) $(BOOP_UI_SRCS) $(LDLIBS) $(GTK_LIBS)

test_window_boop: $(TEST_WINDOW_BOOP_BIN)
$(TEST_WINDOW_BOOP_BIN): $(GSETTINGS_SCHEMA_COMPILED) tests/test_window_boop.c src/window.c src/application.c \
	src/application.h $(APP_PATHS_SRCS) $(PUZZLE_PROGRESS_SRCS) $(PUZZLE_CATALOG_SRCS) src/app_settings.c \
	src/app_settings.h \
	src/new_game_dialog.c src/puzzle_dialog.c src/puzzle_dialog.h $(CHECKERS_DIR)/rulesets.c $(CHECKERS_DIR)/rulesets.h \
	src/import_dialog.c src/settings_dialog.c src/settings_dialog.h src/sgf_file_actions.c \
	src/sgf_file_actions.h src/file_dialog_history.c src/file_dialog_history.h src/bga_client.c src/bga_client.h \
	src/window.h src/style.c src/style.h src/player_controls_panel.c src/player_controls_panel.h \
	src/analysis_graph.c src/analysis_graph.h src/sgf_controller.c src/sgf_controller.h src/board_view.c \
	src/board_view.h src/board_grid.c src/board_grid.h src/board_square.c src/board_square.h \
	src/board_move_overlay.c src/board_move_overlay.h src/board_selection_controller.c \
	src/board_selection_controller.h src/piece_palette.c src/piece_palette.h src/man_paintable.c \
	src/man_paintable.h $(CHECKERS_DIR)/checkers_model.c $(CHECKERS_DIR)/checkers_model.h src/sgf_io.c src/sgf_io.h \
	src/sgf_move_props.c src/sgf_move_props.h src/sgf_tree.c src/sgf_tree.h src/sgf_view.c src/sgf_view.h \
	src/sgf_view_disc_factory.c src/sgf_view_disc_factory.h src/sgf_view_layout.c src/sgf_view_layout.h \
	src/sgf_view_link_renderer.c src/sgf_view_link_renderer.h src/sgf_view_scroller.c \
	src/sgf_view_scroller.h src/sgf_view_selection_controller.c src/sgf_view_selection_controller.h \
	$(TEST_PROFILE_UTILS_SRCS) $(SRCS) $(BOOP_UI_SRCS) $(WIDGET_UTILS_SRCS) $(WIDGET_UTILS_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_window_boop.c $(TEST_PROFILE_UTILS_SRCS) src/application.c src/window.c \
		$(APP_PATHS_SRCS) $(PUZZLE_PROGRESS_SRCS) $(PUZZLE_CATALOG_SRCS) src/app_settings.c \
		src/new_game_dialog.c src/puzzle_dialog.c src/import_dialog.c src/settings_dialog.c src/file_dialog_history.c \
		src/sgf_file_actions.c src/bga_client.c src/style.c src/player_controls_panel.c \
		src/sgf_controller.c src/analysis_graph.c src/board_view.c src/board_grid.c src/board_square.c \
		src/board_move_overlay.c src/board_selection_controller.c src/piece_palette.c src/man_paintable.c \
		src/sgf_io.c src/sgf_move_props.c src/sgf_tree.c src/sgf_view.c src/sgf_view_disc_factory.c \
		src/sgf_view_layout.c src/sgf_view_link_renderer.c src/sgf_view_scroller.c \
		src/sgf_view_selection_controller.c $(WIDGET_UTILS_SRCS) $(SRCS) $(BOOP_UI_SRCS) $(LDLIBS) $(GTK_LIBS)

test_puzzle_generation: $(TEST_PUZZLE_GENERATION_BIN)
$(TEST_PUZZLE_GENERATION_BIN): tests/test_puzzle_generation.c $(CHECKERS_DIR)/puzzle_generation.c \
	$(CHECKERS_DIR)/puzzle_generation.h $(CHECKERS_DIR)/ai_alpha_beta.h $(CHECKERS_DIR)/board.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_puzzle_generation.c $(CHECKERS_DIR)/puzzle_generation.c $(LDLIBS)

test_puzzle_catalog: $(TEST_PUZZLE_CATALOG_BIN)
$(TEST_PUZZLE_CATALOG_BIN): tests/test_puzzle_catalog.c $(APP_PATHS_SRCS) $(PUZZLE_CATALOG_SRCS) \
	src/puzzle_catalog.h src/app_paths.h $(TEST_PROFILE_UTILS_SRCS) $(BACKEND_CODEC_SRCS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_puzzle_catalog.c $(TEST_PROFILE_UTILS_SRCS) $(APP_PATHS_SRCS) \
		$(PUZZLE_CATALOG_SRCS) $(BACKEND_CODEC_SRCS) $(LDLIBS)

test_piece_palette: $(TEST_PIECE_PALETTE_BIN)
$(TEST_PIECE_PALETTE_BIN): tests/test_piece_palette.c src/piece_palette.c src/piece_palette.h \
	src/man_paintable.c src/man_paintable.h $(CHECKERS_DIR)/board.h $(CHECKERS_DIR)/checkers_constants.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ tests/test_piece_palette.c src/piece_palette.c \
		src/man_paintable.c $(LDLIBS) $(GTK_LIBS)

$(CHECKERS_APP_BIN): $(GSETTINGS_SCHEMA_COMPILED) src/gcheckers.c $(APP_MAIN_SRCS) src/application.c src/window.c \
	$(APP_PATHS_SRCS) \
	$(PUZZLE_PROGRESS_SRCS) $(PUZZLE_CATALOG_SRCS) src/app_settings.c src/app_settings.h src/settings_dialog.c \
	src/settings_dialog.h \
	src/new_game_dialog.c src/puzzle_dialog.c src/puzzle_dialog.h $(CHECKERS_DIR)/rulesets.c $(CHECKERS_DIR)/rulesets.h \
	src/import_dialog.c src/sgf_file_actions.c \
	src/sgf_file_actions.h src/file_dialog_history.c src/file_dialog_history.h src/bga_client.c src/bga_client.h \
	src/window.h src/style.c src/style.h src/player_controls_panel.c src/player_controls_panel.h \
	src/analysis_graph.c src/analysis_graph.h src/sgf_controller.c src/sgf_controller.h src/board_view.c \
	src/board_view.h src/board_grid.c src/board_grid.h src/board_square.c src/board_square.h \
	src/board_move_overlay.c src/board_move_overlay.h src/board_selection_controller.c \
	src/board_selection_controller.h src/piece_palette.c src/piece_palette.h src/application.h src/man_paintable.c \
	src/man_paintable.h $(CHECKERS_DIR)/checkers_model.c $(CHECKERS_DIR)/checkers_model.h src/sgf_io.c src/sgf_io.h \
	src/sgf_move_props.c src/sgf_move_props.h src/sgf_tree.c src/sgf_tree.h src/sgf_view.c src/sgf_view.h \
	src/sgf_view_disc_factory.c src/sgf_view_disc_factory.h src/sgf_view_layout.c src/sgf_view_layout.h \
	src/sgf_view_link_renderer.c src/sgf_view_link_renderer.h src/sgf_view_scroller.c \
	src/sgf_view_scroller.h src/sgf_view_selection_controller.c src/sgf_view_selection_controller.h \
	$(SRCS) $(BOOP_UI_SRCS) $(WIDGET_UTILS_SRCS) $(WIDGET_UTILS_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ src/gcheckers.c $(APP_MAIN_SRCS) src/application.c $(APP_PATHS_SRCS) \
		$(PUZZLE_PROGRESS_SRCS) $(PUZZLE_CATALOG_SRCS) src/app_settings.c src/window.c src/settings_dialog.c \
		src/new_game_dialog.c src/puzzle_dialog.c src/import_dialog.c src/file_dialog_history.c src/style.c \
		src/player_controls_panel.c src/analysis_graph.c src/sgf_file_actions.c src/bga_client.c \
		src/sgf_controller.c src/board_view.c src/board_grid.c src/board_square.c src/board_move_overlay.c \
		src/board_selection_controller.c src/piece_palette.c src/man_paintable.c src/sgf_io.c \
		src/sgf_move_props.c src/sgf_tree.c src/sgf_view.c src/sgf_view_disc_factory.c src/sgf_view_layout.c \
		src/sgf_view_link_renderer.c src/sgf_view_scroller.c src/sgf_view_selection_controller.c \
		$(WIDGET_UTILS_SRCS) $(SRCS) $(BOOP_UI_SRCS) $(LDLIBS) $(GTK_LIBS)

$(BOOP_APP_BIN): $(GSETTINGS_SCHEMA_COMPILED) src/gboop.c $(APP_MAIN_SRCS) src/application.c src/window.c \
	$(APP_PATHS_SRCS) \
	$(PUZZLE_PROGRESS_SRCS) $(PUZZLE_CATALOG_SRCS) src/app_settings.c src/app_settings.h src/settings_dialog.c \
	src/settings_dialog.h \
	src/new_game_dialog.c src/puzzle_dialog.c src/puzzle_dialog.h $(CHECKERS_DIR)/rulesets.c $(CHECKERS_DIR)/rulesets.h \
	src/import_dialog.c src/sgf_file_actions.c \
	src/sgf_file_actions.h src/file_dialog_history.c src/file_dialog_history.h src/bga_client.c src/bga_client.h \
	src/window.h src/style.c src/style.h src/player_controls_panel.c src/player_controls_panel.h \
	src/analysis_graph.c src/analysis_graph.h src/sgf_controller.c src/sgf_controller.h src/board_view.c \
	src/board_view.h src/board_grid.c src/board_grid.h src/board_square.c src/board_square.h \
	src/board_move_overlay.c src/board_move_overlay.h src/board_selection_controller.c \
	src/board_selection_controller.h src/piece_palette.c src/piece_palette.h src/application.h src/man_paintable.c \
	src/man_paintable.h $(CHECKERS_DIR)/checkers_model.c $(CHECKERS_DIR)/checkers_model.h src/sgf_io.c src/sgf_io.h \
	src/sgf_move_props.c src/sgf_move_props.h src/sgf_tree.c src/sgf_tree.h src/sgf_view.c src/sgf_view.h \
	src/sgf_view_disc_factory.c src/sgf_view_disc_factory.h src/sgf_view_layout.c src/sgf_view_layout.h \
	src/sgf_view_link_renderer.c src/sgf_view_link_renderer.h src/sgf_view_scroller.c \
	src/sgf_view_scroller.h src/sgf_view_selection_controller.c src/sgf_view_selection_controller.h \
	$(SRCS) $(BOOP_UI_SRCS) $(WIDGET_UTILS_SRCS) $(WIDGET_UTILS_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ src/gboop.c $(APP_MAIN_SRCS) src/application.c $(APP_PATHS_SRCS) \
		$(PUZZLE_PROGRESS_SRCS) $(PUZZLE_CATALOG_SRCS) src/app_settings.c src/window.c src/settings_dialog.c \
		src/new_game_dialog.c src/puzzle_dialog.c src/import_dialog.c src/file_dialog_history.c src/style.c \
		src/player_controls_panel.c src/analysis_graph.c src/sgf_file_actions.c src/bga_client.c \
		src/sgf_controller.c src/board_view.c src/board_grid.c src/board_square.c src/board_move_overlay.c \
		src/board_selection_controller.c src/piece_palette.c src/man_paintable.c src/sgf_io.c \
		src/sgf_move_props.c src/sgf_tree.c src/sgf_view.c src/sgf_view_disc_factory.c src/sgf_view_layout.c \
		src/sgf_view_link_renderer.c src/sgf_view_scroller.c src/sgf_view_selection_controller.c \
		$(WIDGET_UTILS_SRCS) $(SRCS) $(BOOP_UI_SRCS) $(LDLIBS) $(GTK_LIBS)

$(HOMEWORLDS_APP_BIN): $(GSETTINGS_SCHEMA_COMPILED) src/ghomeworlds.c $(APP_MAIN_SRCS) $(HOMEWORLDS_APP_WINDOW_SRCS) \
	src/application.c src/window.c \
	$(APP_PATHS_SRCS) $(PUZZLE_PROGRESS_SRCS) $(PUZZLE_CATALOG_SRCS) src/app_settings.c src/app_settings.h \
	src/settings_dialog.c src/settings_dialog.h src/new_game_dialog.c src/puzzle_dialog.c src/puzzle_dialog.h \
	$(CHECKERS_DIR)/rulesets.c $(CHECKERS_DIR)/rulesets.h src/import_dialog.c src/sgf_file_actions.c \
	src/sgf_file_actions.h src/file_dialog_history.c src/file_dialog_history.h src/bga_client.c src/bga_client.h \
	src/window.h src/style.c src/style.h src/player_controls_panel.c src/player_controls_panel.h \
	src/analysis_graph.c src/analysis_graph.h src/sgf_controller.c src/sgf_controller.h src/board_view.c \
	src/board_view.h src/board_grid.c src/board_grid.h src/board_square.c src/board_square.h \
	src/board_move_overlay.c src/board_move_overlay.h src/board_selection_controller.c \
	src/board_selection_controller.h src/piece_palette.c src/piece_palette.h src/application.h src/man_paintable.c \
	src/man_paintable.h $(CHECKERS_DIR)/checkers_model.c $(CHECKERS_DIR)/checkers_model.h src/sgf_io.c src/sgf_io.h \
	src/sgf_move_props.c src/sgf_move_props.h src/sgf_tree.c src/sgf_tree.h src/sgf_view.c src/sgf_view.h \
	src/sgf_view_disc_factory.c src/sgf_view_disc_factory.h src/sgf_view_layout.c src/sgf_view_layout.h \
	src/sgf_view_link_renderer.c src/sgf_view_link_renderer.h src/sgf_view_scroller.c \
	src/sgf_view_scroller.h src/sgf_view_selection_controller.c src/sgf_view_selection_controller.h \
	$(SRCS) $(BOOP_UI_SRCS) $(WIDGET_UTILS_SRCS) $(WIDGET_UTILS_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ src/ghomeworlds.c $(APP_MAIN_SRCS) $(HOMEWORLDS_APP_WINDOW_SRCS) \
		src/application.c $(APP_PATHS_SRCS) \
		$(PUZZLE_PROGRESS_SRCS) $(PUZZLE_CATALOG_SRCS) src/app_settings.c src/window.c src/settings_dialog.c \
		src/new_game_dialog.c src/puzzle_dialog.c src/import_dialog.c src/file_dialog_history.c src/style.c \
		src/player_controls_panel.c src/analysis_graph.c src/sgf_file_actions.c src/bga_client.c \
		src/sgf_controller.c src/board_view.c src/board_grid.c src/board_square.c src/board_move_overlay.c \
		src/board_selection_controller.c src/piece_palette.c src/man_paintable.c src/sgf_io.c \
		src/sgf_move_props.c src/sgf_tree.c src/sgf_view.c src/sgf_view_disc_factory.c src/sgf_view_layout.c \
		src/sgf_view_link_renderer.c src/sgf_view_scroller.c src/sgf_view_selection_controller.c \
		$(WIDGET_UTILS_SRCS) $(SRCS) $(BOOP_UI_SRCS) $(LDLIBS) $(GTK_LIBS)

$(GSETTINGS_SCHEMA_COMPILED): $(GSETTINGS_SCHEMA_XMLS)
	glib-compile-schemas $(GSETTINGS_SCHEMA_DIR)

install: install-checkers install-boop install-homeworlds install-schemas

install-checkers: all $(CHECKERS_DESKTOP_FILE) $(CHECKERS_METAINFO_FILE) $(CHECKERS_ICON_FILE)
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 755 $(CHECKERS_APP_BIN) $(DESTDIR)$(BINDIR)/$(CHECKERS_APP_BIN_NAME)
	$(INSTALL) -d $(DESTDIR)$(APPLICATIONS_DIR)
	$(INSTALL) -m 644 $(CHECKERS_DESKTOP_FILE) $(DESTDIR)$(APPLICATIONS_DIR)/$(CHECKERS_APP_ID).desktop
	$(INSTALL) -d $(DESTDIR)$(METAINFO_DIR)
	$(INSTALL) -m 644 $(CHECKERS_METAINFO_FILE) $(DESTDIR)$(METAINFO_DIR)/$(CHECKERS_APP_ID).metainfo.xml
	$(INSTALL) -d $(DESTDIR)$(ICONS_DIR)
	$(INSTALL) -m 644 $(CHECKERS_ICON_FILE) $(DESTDIR)$(ICONS_DIR)/$(CHECKERS_APP_ID).svg
	$(INSTALL) -d $(DESTDIR)$(PUZZLES_INSTALL_DIR)
	$(INSTALL) -d $(DESTDIR)$(PUZZLES_INSTALL_DIR)/checkers
	@for dir in $(PUZZLE_VARIANTS); do \
		$(INSTALL) -d $(DESTDIR)$(PUZZLES_INSTALL_DIR)/checkers/$$dir; \
		if test -d puzzles/checkers/$$dir; then \
			for file in puzzles/checkers/$$dir/*.sgf; do \
				if test -f "$$file"; then \
					$(INSTALL) -m 644 "$$file" "$(DESTDIR)$(PUZZLES_INSTALL_DIR)/checkers/$$dir/"; \
				fi; \
			done; \
		fi; \
	done

install-boop: all $(BOOP_DESKTOP_FILE) $(BOOP_METAINFO_FILE) $(BOOP_ICON_FILE)
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 755 $(BOOP_APP_BIN) $(DESTDIR)$(BINDIR)/$(BOOP_APP_BIN_NAME)
	$(INSTALL) -d $(DESTDIR)$(APPLICATIONS_DIR)
	$(INSTALL) -m 644 $(BOOP_DESKTOP_FILE) $(DESTDIR)$(APPLICATIONS_DIR)/$(BOOP_APP_ID).desktop
	$(INSTALL) -d $(DESTDIR)$(METAINFO_DIR)
	$(INSTALL) -m 644 $(BOOP_METAINFO_FILE) $(DESTDIR)$(METAINFO_DIR)/$(BOOP_APP_ID).metainfo.xml
	$(INSTALL) -d $(DESTDIR)$(ICONS_DIR)
	$(INSTALL) -m 644 $(BOOP_ICON_FILE) $(DESTDIR)$(ICONS_DIR)/$(BOOP_APP_ID).svg

install-homeworlds: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 755 $(HOMEWORLDS_APP_BIN) $(DESTDIR)$(BINDIR)/$(HOMEWORLDS_APP_BIN_NAME)

install-schemas: $(GSETTINGS_SCHEMA_XMLS)
	$(INSTALL) -d $(DESTDIR)$(SCHEMAS_INSTALL_DIR)
	$(INSTALL) -m 644 $(GSETTINGS_SCHEMA_DIR)/$(CHECKERS_APP_ID).gschema.xml \
		$(DESTDIR)$(SCHEMAS_INSTALL_DIR)/$(CHECKERS_APP_ID).gschema.xml
	$(INSTALL) -m 644 $(GSETTINGS_SCHEMA_DIR)/$(BOOP_APP_ID).gschema.xml \
		$(DESTDIR)$(SCHEMAS_INSTALL_DIR)/$(BOOP_APP_ID).gschema.xml
	glib-compile-schemas $(DESTDIR)$(SCHEMAS_INSTALL_DIR)

validate-desktop-metadata: $(DESKTOP_FILES) $(METAINFO_FILES)
	@if command -v desktop-file-validate >/dev/null 2>&1; then \
		desktop-file-validate $(CHECKERS_DESKTOP_FILE) $(BOOP_DESKTOP_FILE); \
	else \
		echo "desktop-file-validate not found; skipping"; \
	fi
	@if command -v appstreamcli >/dev/null 2>&1; then \
		appstreamcli validate --no-net $(CHECKERS_METAINFO_FILE) $(BOOP_METAINFO_FILE); \
	else \
		echo "appstreamcli not found; skipping"; \
	fi

clean:
	rm -rf $(BUILD_DIR)
	rm -f checkers gcheckers gboop ghomeworlds libgame.a
	rm -f test_game test_game_print test_board test_move_gen test_checkers_model test_ai_transposition_table
	rm -f test_bga_client test_file_dialog_history test_app_paths
	rm -f test_desktop_metadata test_flatpak_manifest test_create_puzzles_cli test_create_puzzles_check
	rm -f test_puzzle_generation test_board_view test_player_controls_panel test_sgf_controller test_sgf_io
	rm -f test_sgf_tree test_sgf_view test_window
	rm -f src/*.o
	rm -f $(GSETTINGS_SCHEMA_COMPILED)
	rm -rf $(COV_DIR)

$(COV_OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(COVERAGE_CFLAGS) -c $< -o $@

coverage: $(COV_OBJS) $(COV_BOARD_OBJS)
	@mkdir -p $(COV_BIN_DIR) $(COV_GCOV_DIR) $(COV_REPORT_DIR)
	$(CC) $(CFLAGS) $(COVERAGE_CFLAGS) -o $(COV_BIN_DIR)/test_game tests/test_game.c $(COV_OBJS) $(LDLIBS)
	$(CC) $(CFLAGS) $(COVERAGE_CFLAGS) -o $(COV_BIN_DIR)/test_game_print tests/test_game_print.c $(COV_OBJS) $(LDLIBS)
	$(CC) $(CFLAGS) $(COVERAGE_CFLAGS) -o $(COV_BIN_DIR)/test_board tests/test_board.c $(COV_BOARD_OBJS) $(LDLIBS)
	$(CC) $(CFLAGS) $(COVERAGE_CFLAGS) -o $(COV_BIN_DIR)/test_move_gen tests/test_move_gen.c $(COV_OBJS) $(LDLIBS)
	$(COV_BIN_DIR)/test_game
	$(COV_BIN_DIR)/test_game_print
	$(COV_BIN_DIR)/test_board
	$(COV_BIN_DIR)/test_move_gen
	gcov -o $(COV_OBJ_DIR)/src $(SRCS)
	mv *.gcov $(COV_GCOV_DIR)/
	python3 tools/coverage_report.py --gcov-dir $(COV_GCOV_DIR) --output-dir $(COV_REPORT_DIR)

# Split the profiling flow so an interrupted callgrind run can still be
# annotated later with `make callgrind-annotate` if $(CALLGRIND_OUT) exists.
callgrind-run: $(PROFILE_BIN)
	@mkdir -p $(CALLGRIND_DIR)
	@if ! command -v valgrind >/dev/null 2>&1; then \
		echo "valgrind not found"; \
		exit 1; \
	fi
	valgrind --tool=callgrind --dump-instr=yes --collect-jumps=yes --callgrind-out-file=$(CALLGRIND_OUT) $(PROFILE_CMD)

callgrind-annotate:
	@if ! command -v callgrind_annotate >/dev/null 2>&1; then \
		echo "callgrind_annotate not found"; \
		exit 1; \
	fi
	@if [ ! -s $(CALLGRIND_OUT) ]; then \
		echo "$(CALLGRIND_OUT) was not created"; \
		exit 1; \
	fi
	callgrind_annotate --auto=yes $(CALLGRIND_OUT) | tee $(CALLGRIND_ANNOTATION)
