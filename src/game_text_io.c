#include "game_text_io.h"

#include <errno.h>
#include <string.h>

static GQuark ggame_text_game_io_error_quark(void) {
  return g_quark_from_static_string("game-text-io-error");
}

static gboolean ggame_text_game_io_backend_is_ready(const GameBackend *backend, GError **error) {
  g_return_val_if_fail(backend != NULL, FALSE);

  if (!backend->supports_ascii_game_io) {
    g_set_error(error,
                ggame_text_game_io_error_quark(),
                1,
                "%s does not support ASCII game files",
                backend->display_name != NULL ? backend->display_name : "The active backend");
    return FALSE;
  }
  if (backend->position_size == 0 ||
      backend->move_size == 0 ||
      backend->position_init == NULL ||
      backend->position_clear == NULL ||
      backend->position_turn == NULL ||
      backend->sgf_color_for_side == NULL ||
      backend->apply_move == NULL ||
      backend->parse_move == NULL ||
      backend->format_move == NULL) {
    g_set_error(error,
                ggame_text_game_io_error_quark(),
                2,
                "%s has incomplete ASCII game file support",
                backend->display_name != NULL ? backend->display_name : "The active backend");
    return FALSE;
  }

  return TRUE;
}

gboolean ggame_text_game_io_backend_supports_path(const GameBackend *backend, const char *path) {
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  if (!backend->supports_ascii_game_io ||
      backend->ascii_game_file_extension == NULL ||
      backend->ascii_game_file_extension[0] == '\0') {
    return FALSE;
  }

  const char *dot = strrchr(path, '.');
  return dot != NULL && g_ascii_strcasecmp(dot + 1, backend->ascii_game_file_extension) == 0;
}

static gboolean ggame_text_game_io_parse_line_number(const char *line,
                                                     guint expected_number,
                                                     guint physical_line,
                                                     const char **out_notation,
                                                     GError **error) {
  const char *cursor = line;
  guint64 parsed_number = 0;
  char *end_ptr = NULL;

  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(out_notation != NULL, FALSE);

  while (g_ascii_isspace(*cursor)) {
    cursor++;
  }
  if (!g_ascii_isdigit(*cursor)) {
    g_set_error(error,
                ggame_text_game_io_error_quark(),
                3,
                "Expected move number on line %u",
                physical_line);
    return FALSE;
  }

  errno = 0;
  parsed_number = g_ascii_strtoull(cursor, &end_ptr, 10);
  if (errno == ERANGE || end_ptr == cursor || parsed_number > G_MAXUINT || end_ptr == NULL || *end_ptr != '.') {
    g_set_error(error,
                ggame_text_game_io_error_quark(),
                3,
                "Invalid move number on line %u",
                physical_line);
    return FALSE;
  }
  if ((guint) parsed_number != expected_number) {
    g_set_error(error,
                ggame_text_game_io_error_quark(),
                4,
                "Expected move number %u on line %u",
                expected_number,
                physical_line);
    return FALSE;
  }

  cursor = end_ptr + 1;
  while (g_ascii_isspace(*cursor)) {
    cursor++;
  }
  if (*cursor == '\0') {
    g_set_error(error,
                ggame_text_game_io_error_quark(),
                5,
                "Missing move notation on line %u",
                physical_line);
    return FALSE;
  }

  *out_notation = cursor;
  return TRUE;
}

static gboolean ggame_text_game_io_format_move(const GameBackend *backend,
                                               gconstpointer move,
                                               char *buffer,
                                               gsize size,
                                               guint line_number,
                                               GError **error) {
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(size > 0, FALSE);

  if (!backend->format_move(move, buffer, size)) {
    g_set_error(error,
                ggame_text_game_io_error_quark(),
                6,
                "Unable to format move on line %u",
                line_number);
    return FALSE;
  }

  return TRUE;
}

gboolean ggame_text_game_io_load_data(const GameBackend *backend,
                                      const GameBackendVariant *variant_or_null,
                                      const char *content,
                                      SgfTree **out_tree,
                                      GError **error) {
  g_autoptr(SgfTree) tree = NULL;
  g_autofree guint8 *position = NULL;
  g_autofree guint8 *move = NULL;
  guint next_move_number = 1;

  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(content != NULL, FALSE);
  g_return_val_if_fail(out_tree != NULL, FALSE);

  if (!ggame_text_game_io_backend_is_ready(backend, error)) {
    return FALSE;
  }

  tree = sgf_tree_new();
  position = g_malloc0(backend->position_size);
  move = g_malloc0(backend->move_size);
  if (tree == NULL || position == NULL || move == NULL) {
    g_set_error_literal(error,
                        ggame_text_game_io_error_quark(),
                        7,
                        "Unable to allocate ASCII game load state");
    return FALSE;
  }
  backend->position_init(position, variant_or_null);

  g_auto(GStrv) lines = g_strsplit(content, "\n", -1);
  for (guint i = 0; lines != NULL && lines[i] != NULL; ++i) {
    g_autofree char *line = g_strdup(lines[i]);
    char *stripped = NULL;
    const char *notation = NULL;
    SgfColor color = SGF_COLOR_NONE;
    char canonical[256] = {0};

    if (line == NULL) {
      backend->position_clear(position);
      g_set_error_literal(error,
                          ggame_text_game_io_error_quark(),
                          7,
                          "Unable to allocate ASCII game line");
      return FALSE;
    }
    stripped = g_strstrip(line);
    if (stripped[0] == '\0') {
      continue;
    }

    if (!ggame_text_game_io_parse_line_number(stripped, next_move_number, i + 1, &notation, error)) {
      backend->position_clear(position);
      return FALSE;
    }
    memset(move, 0, backend->move_size);
    if (!backend->parse_move(notation, move)) {
      backend->position_clear(position);
      g_set_error(error,
                  ggame_text_game_io_error_quark(),
                  8,
                  "Invalid move notation on line %u: %s",
                  i + 1,
                  notation);
      return FALSE;
    }
    if (!ggame_text_game_io_format_move(backend, move, canonical, sizeof(canonical), i + 1, error)) {
      backend->position_clear(position);
      return FALSE;
    }

    color = backend->sgf_color_for_side(backend->position_turn(position));
    if (color == SGF_COLOR_NONE) {
      backend->position_clear(position);
      g_set_error(error,
                  ggame_text_game_io_error_quark(),
                  9,
                  "Unable to determine side to move on line %u",
                  i + 1);
      return FALSE;
    }
    if (sgf_tree_append_move(tree, color, canonical) == NULL) {
      backend->position_clear(position);
      g_set_error(error,
                  ggame_text_game_io_error_quark(),
                  10,
                  "Unable to append move on line %u",
                  i + 1);
      return FALSE;
    }
    if (!backend->apply_move(position, move)) {
      backend->position_clear(position);
      g_set_error(error,
                  ggame_text_game_io_error_quark(),
                  11,
                  "Illegal move on line %u: %s",
                  i + 1,
                  notation);
      return FALSE;
    }

    next_move_number++;
  }

  backend->position_clear(position);
  if (!sgf_tree_set_current(tree, sgf_tree_get_root(tree))) {
    g_set_error_literal(error,
                        ggame_text_game_io_error_quark(),
                        13,
                        "Unable to select ASCII game root after load");
    return FALSE;
  }
  g_clear_object(out_tree);
  *out_tree = g_steal_pointer(&tree);
  return TRUE;
}

gboolean ggame_text_game_io_load_file(const GameBackend *backend,
                                      const GameBackendVariant *variant_or_null,
                                      const char *path,
                                      SgfTree **out_tree,
                                      GError **error) {
  g_autofree char *content = NULL;
  gsize len = 0;

  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(out_tree != NULL, FALSE);

  if (!g_file_get_contents(path, &content, &len, error)) {
    return FALSE;
  }
  if (len == 0) {
    g_set_error(error,
                ggame_text_game_io_error_quark(),
                12,
                "Empty ASCII game file: %s",
                path);
    return FALSE;
  }

  return ggame_text_game_io_load_data(backend, variant_or_null, content, out_tree, error);
}

static gboolean ggame_text_game_io_node_has_move(const SgfNode *node,
                                                 SgfColor *out_color,
                                                 const char **out_notation,
                                                 GError **error) {
  const GPtrArray *black_values = NULL;
  const GPtrArray *white_values = NULL;

  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(out_color != NULL, FALSE);
  g_return_val_if_fail(out_notation != NULL, FALSE);

  black_values = sgf_node_get_property_values(node, "B");
  white_values = sgf_node_get_property_values(node, "W");
  if (black_values != NULL && white_values != NULL) {
    g_set_error_literal(error,
                        ggame_text_game_io_error_quark(),
                        13,
                        "Cannot save a node with both B[] and W[] as an ASCII game");
    return FALSE;
  }
  if ((black_values != NULL && black_values->len != 1) ||
      (white_values != NULL && white_values->len != 1)) {
    g_set_error_literal(error,
                        ggame_text_game_io_error_quark(),
                        14,
                        "Cannot save a node with repeated move properties as an ASCII game");
    return FALSE;
  }
  if (black_values == NULL && white_values == NULL) {
    *out_color = SGF_COLOR_NONE;
    *out_notation = NULL;
    return TRUE;
  }

  *out_color = black_values != NULL ? SGF_COLOR_BLACK : SGF_COLOR_WHITE;
  *out_notation = g_ptr_array_index(black_values != NULL ? black_values : white_values, 0);
  return *out_notation != NULL;
}

char *ggame_text_game_io_save_data(const GameBackend *backend,
                                   const GameBackendVariant *variant_or_null,
                                   SgfTree *tree,
                                   GError **error) {
  g_autofree guint8 *position = NULL;
  g_autofree guint8 *move = NULL;
  g_autoptr(GPtrArray) branch = NULL;
  GString *text = NULL;
  guint saved_move_number = 1;

  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(SGF_IS_TREE(tree), NULL);

  if (!ggame_text_game_io_backend_is_ready(backend, error)) {
    return NULL;
  }

  position = g_malloc0(backend->position_size);
  move = g_malloc0(backend->move_size);
  text = g_string_new(NULL);
  if (position == NULL || move == NULL || text == NULL) {
    if (text != NULL) {
      g_string_free(text, TRUE);
    }
    g_set_error_literal(error,
                        ggame_text_game_io_error_quark(),
                        15,
                        "Unable to allocate ASCII game save state");
    return NULL;
  }
  backend->position_init(position, variant_or_null);

  branch = sgf_tree_build_current_branch(tree);
  if (branch == NULL) {
    backend->position_clear(position);
    g_string_free(text, TRUE);
    g_set_error_literal(error,
                        ggame_text_game_io_error_quark(),
                        16,
                        "Unable to build ASCII game branch");
    return NULL;
  }

  for (guint i = 1; i < branch->len; ++i) {
    const SgfNode *node = g_ptr_array_index(branch, i);
    SgfColor color = SGF_COLOR_NONE;
    SgfColor expected_color = SGF_COLOR_NONE;
    const char *notation = NULL;
    char canonical[256] = {0};

    if (!ggame_text_game_io_node_has_move(node, &color, &notation, error)) {
      backend->position_clear(position);
      g_string_free(text, TRUE);
      return NULL;
    }
    if (notation == NULL) {
      backend->position_clear(position);
      g_string_free(text, TRUE);
      g_set_error(error,
                  ggame_text_game_io_error_quark(),
                  17,
                  "Cannot save non-move node %u as an ASCII game",
                  sgf_node_get_move_number(node));
      return NULL;
    }

    expected_color = backend->sgf_color_for_side(backend->position_turn(position));
    if (expected_color == SGF_COLOR_NONE || color != expected_color) {
      backend->position_clear(position);
      g_string_free(text, TRUE);
      g_set_error(error,
                  ggame_text_game_io_error_quark(),
                  18,
                  "Unexpected side to move at ASCII game move %u",
                  saved_move_number);
      return NULL;
    }

    memset(move, 0, backend->move_size);
    if (!backend->parse_move(notation, move)) {
      backend->position_clear(position);
      g_string_free(text, TRUE);
      g_set_error(error,
                  ggame_text_game_io_error_quark(),
                  19,
                  "Invalid stored move at ASCII game move %u: %s",
                  saved_move_number,
                  notation);
      return NULL;
    }
    if (!ggame_text_game_io_format_move(backend, move, canonical, sizeof(canonical), saved_move_number, error)) {
      backend->position_clear(position);
      g_string_free(text, TRUE);
      return NULL;
    }
    if (!backend->apply_move(position, move)) {
      backend->position_clear(position);
      g_string_free(text, TRUE);
      g_set_error(error,
                  ggame_text_game_io_error_quark(),
                  20,
                  "Illegal stored move at ASCII game move %u: %s",
                  saved_move_number,
                  notation);
      return NULL;
    }

    g_string_append_printf(text, "%u. %s\n", saved_move_number, canonical);
    saved_move_number++;
  }

  backend->position_clear(position);
  return g_string_free(text, FALSE);
}

gboolean ggame_text_game_io_save_file(const GameBackend *backend,
                                      const GameBackendVariant *variant_or_null,
                                      const char *path,
                                      SgfTree *tree,
                                      GError **error) {
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(SGF_IS_TREE(tree), FALSE);

  g_autofree char *content = ggame_text_game_io_save_data(backend, variant_or_null, tree, error);
  if (content == NULL) {
    return FALSE;
  }

  return g_file_set_contents(path, content, -1, error);
}
