#include "create_puzzles_runner.h"

#include "active_game_backend.h"
#include "create_puzzles_progress.h"
#include "sgf_io.h"
#include "sgf_move_props.h"

#include <string.h>

enum {
  GGAME_CREATE_PUZZLES_MIN_GENERATION_ATTEMPTS = 10,
  GGAME_CREATE_PUZZLES_GENERATION_ATTEMPTS_PER_REQUESTED = 20,
};

static GQuark ggame_create_puzzles_runner_error_quark(void) {
  return g_quark_from_static_string("ggame-create-puzzles-runner-error");
}

guint ggame_create_puzzles_runner_generation_attempt_limit(guint wanted) {
  if (wanted > G_MAXUINT / GGAME_CREATE_PUZZLES_GENERATION_ATTEMPTS_PER_REQUESTED) {
    return G_MAXUINT;
  }

  return MAX((guint)GGAME_CREATE_PUZZLES_MIN_GENERATION_ATTEMPTS,
             wanted * GGAME_CREATE_PUZZLES_GENERATION_ATTEMPTS_PER_REQUESTED);
}

static gboolean ggame_create_puzzles_runner_validate_backend(const GameBackend *backend, GError **error) {
  if (backend == NULL) {
    g_set_error_literal(error, ggame_create_puzzles_runner_error_quark(), 1, "Missing game backend");
    return FALSE;
  }
  if (!backend->supports_ai_search) {
    g_set_error(error,
                ggame_create_puzzles_runner_error_quark(),
                2,
                "%s does not support AI search",
                backend->id != NULL ? backend->id : "backend");
    return FALSE;
  }
  if (GGAME_ACTIVE_GAME_BACKEND != backend) {
    g_set_error(error,
                ggame_create_puzzles_runner_error_quark(),
                13,
                "%s is not the active create_puzzles backend",
                backend->id != NULL ? backend->id : "backend");
    return FALSE;
  }
  if (backend->position_size == 0 || backend->move_size == 0 || backend->position_init == NULL ||
      backend->position_copy == NULL || backend->position_outcome == NULL || backend->position_turn == NULL ||
      backend->apply_move == NULL || backend->format_move == NULL || backend->parse_move == NULL ||
      backend->sgf_color_for_side == NULL) {
    g_set_error(error,
                ggame_create_puzzles_runner_error_quark(),
                3,
                "%s is missing backend operations required by create_puzzles",
                backend->id != NULL ? backend->id : "backend");
    return FALSE;
  }
  return TRUE;
}

static gpointer ggame_create_puzzles_runner_position_new(const GameBackend *backend,
                                                         const GameBackendVariant *variant) {
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(backend->position_size > 0, NULL);
  g_return_val_if_fail(backend->position_init != NULL, NULL);

  gpointer position = g_malloc0(backend->position_size);
  backend->position_init(position, variant);
  return position;
}

static void ggame_create_puzzles_runner_position_free(const GameBackend *backend, gpointer position) {
  if (position == NULL) {
    return;
  }
  if (backend != NULL && backend->position_clear != NULL) {
    backend->position_clear(position);
  }
  g_free(position);
}

static const char *ggame_create_puzzles_runner_outcome_label(const GameBackend *backend,
                                                            GameBackendOutcome outcome) {
  switch (outcome) {
    case GAME_BACKEND_OUTCOME_SIDE_0_WIN:
      return backend->side_label != NULL ? backend->side_label(0) : "side 0";
    case GAME_BACKEND_OUTCOME_SIDE_1_WIN:
      return backend->side_label != NULL ? backend->side_label(1) : "side 1";
    case GAME_BACKEND_OUTCOME_DRAW:
      return "Draw";
    case GAME_BACKEND_OUTCOME_ONGOING:
    default:
      return "None";
  }
}

static GameBackendOutcome ggame_create_puzzles_runner_no_move_outcome(const GameBackend *backend,
                                                                      gconstpointer position) {
  g_return_val_if_fail(backend != NULL, GAME_BACKEND_OUTCOME_ONGOING);
  g_return_val_if_fail(position != NULL, GAME_BACKEND_OUTCOME_ONGOING);
  g_return_val_if_fail(backend->position_turn != NULL, GAME_BACKEND_OUTCOME_ONGOING);

  return backend->position_turn(position) == 0 ? GAME_BACKEND_OUTCOME_SIDE_1_WIN : GAME_BACKEND_OUTCOME_SIDE_0_WIN;
}

SgfTree *ggame_create_puzzles_runner_generate_self_play_tree(const GGameCreatePuzzlesRunnerConfig *config,
                                                             guint *out_plies,
                                                             GameBackendOutcome *out_outcome,
                                                             GError **error) {
  g_return_val_if_fail(config != NULL, NULL);

  const GameBackend *backend = config->backend;
  if (!ggame_create_puzzles_runner_validate_backend(backend, error)) {
    return NULL;
  }

  g_autoptr(SgfTree) tree = sgf_tree_new();
  SgfNode *root = (SgfNode *)sgf_tree_get_root(tree);
  if (root == NULL) {
    g_set_error_literal(error, ggame_create_puzzles_runner_error_quark(), 4, "Failed to create SGF root node");
    return NULL;
  }
  if (!sgf_io_tree_set_variant(tree, config->variant)) {
    g_set_error_literal(error, ggame_create_puzzles_runner_error_quark(), 5, "Failed to write SGF variant metadata");
    return NULL;
  }

  gpointer position = ggame_create_puzzles_runner_position_new(backend, config->variant);
  g_return_val_if_fail(position != NULL, NULL);
  if (backend->sgf_write_position_node != NULL && !backend->sgf_write_position_node(position, root, error)) {
    ggame_create_puzzles_runner_position_free(backend, position);
    return NULL;
  }

  guint plies = 0;
  GameBackendOutcome outcome = backend->position_outcome(position);
  guint max_plies = config->max_self_play_plies > 0 ? config->max_self_play_plies : 200;
  ggame_create_puzzles_progress_start_self_play(config->self_play_depth);

  while (outcome == GAME_BACKEND_OUTCOME_ONGOING && plies < max_plies) {
    guint side = backend->position_turn(position);
    SgfColor color = backend->sgf_color_for_side(side);
    if (color != SGF_COLOR_BLACK && color != SGF_COLOR_WHITE) {
      g_set_error(error,
                  ggame_create_puzzles_runner_error_quark(),
                  6,
                  "%s produced an invalid SGF color for side %u",
                  backend->id != NULL ? backend->id : "backend",
                  side);
      ggame_create_puzzles_runner_position_free(backend, position);
      return NULL;
    }

    g_autofree gpointer move = g_malloc0(backend->move_size);
    if (!game_ai_search_choose_move(backend, position, config->self_play_depth, move)) {
      outcome = ggame_create_puzzles_runner_no_move_outcome(backend, position);
      break;
    }

    SgfNode *node = (SgfNode *)sgf_tree_append_node(tree);
    if (node == NULL) {
      g_set_error_literal(error, ggame_create_puzzles_runner_error_quark(), 7, "Failed to append SGF move node");
      ggame_create_puzzles_runner_position_free(backend, position);
      return NULL;
    }
    if (!sgf_move_props_set_move(node, color, move, error)) {
      ggame_create_puzzles_runner_position_free(backend, position);
      return NULL;
    }
    if (!backend->apply_move(position, move)) {
      g_set_error(error,
                  ggame_create_puzzles_runner_error_quark(),
                  8,
                  "%s rejected a self-play move",
                  backend->id != NULL ? backend->id : "backend");
      ggame_create_puzzles_runner_position_free(backend, position);
      return NULL;
    }

    plies++;
    outcome = backend->position_outcome(position);
  }

  ggame_create_puzzles_progress_finish_self_play(plies,
                                                 ggame_create_puzzles_runner_outcome_label(backend, outcome));
  ggame_create_puzzles_runner_position_free(backend, position);

  if (out_plies != NULL) {
    *out_plies = plies;
  }
  if (out_outcome != NULL) {
    *out_outcome = outcome;
  }
  return g_steal_pointer(&tree);
}

static gboolean ggame_create_puzzles_runner_tree_variant_matches(const GameBackendVariant *expected,
                                                                 const GameBackendVariant *actual) {
  if (expected == actual) {
    return TRUE;
  }
  if (expected == NULL || actual == NULL) {
    return FALSE;
  }
  return g_strcmp0(expected->short_name, actual->short_name) == 0;
}

gboolean ggame_create_puzzles_runner_analyze_tree(const GGameCreatePuzzlesRunnerConfig *config,
                                                  SgfTree *source_tree,
                                                  GGameCreatePuzzlesConsiderMoveFunc consider_move,
                                                  gpointer user_data,
                                                  GError **error) {
  g_return_val_if_fail(config != NULL, FALSE);
  g_return_val_if_fail(SGF_IS_TREE(source_tree), FALSE);
  g_return_val_if_fail(consider_move != NULL, FALSE);

  const GameBackend *backend = config->backend;
  if (!ggame_create_puzzles_runner_validate_backend(backend, error)) {
    return FALSE;
  }

  const GameBackendVariant *tree_variant = NULL;
  if (!sgf_io_tree_get_variant(source_tree, &tree_variant, error)) {
    return FALSE;
  }
  if (!ggame_create_puzzles_runner_tree_variant_matches(config->variant, tree_variant)) {
    g_set_error_literal(error,
                        ggame_create_puzzles_runner_error_quark(),
                        9,
                        "SGF source game variant does not match create_puzzles target");
    return FALSE;
  }

  const SgfNode *root = sgf_tree_get_root(source_tree);
  g_return_val_if_fail(root != NULL, FALSE);

  gpointer position = ggame_create_puzzles_runner_position_new(backend, tree_variant);
  g_return_val_if_fail(position != NULL, FALSE);
  if (backend->sgf_apply_setup_node != NULL && !backend->sgf_apply_setup_node(position, root, error)) {
    ggame_create_puzzles_runner_position_free(backend, position);
    return FALSE;
  }

  g_autoptr(GPtrArray) main_line = sgf_tree_build_main_line(source_tree);
  if (main_line == NULL || main_line->len == 0) {
    g_set_error_literal(error, ggame_create_puzzles_runner_error_quark(), 10, "SGF source game has no main line");
    ggame_create_puzzles_runner_position_free(backend, position);
    return FALSE;
  }

  guint move_number = 0;
  gboolean stop = FALSE;
  for (guint i = 1; i < main_line->len && !stop; ++i) {
    const SgfNode *node = g_ptr_array_index(main_line, i);
    g_return_val_if_fail(node != NULL, FALSE);

    g_autofree gpointer move = g_malloc0(backend->move_size);
    SgfColor color = SGF_COLOR_NONE;
    gboolean has_move = FALSE;
    if (!sgf_move_props_try_parse_node(node, &color, move, &has_move, error)) {
      ggame_create_puzzles_runner_position_free(backend, position);
      return FALSE;
    }
    if (!has_move) {
      continue;
    }

    guint side = backend->position_turn(position);
    SgfColor expected_color = backend->sgf_color_for_side(side);
    if (color != expected_color) {
      g_set_error(error,
                  ggame_create_puzzles_runner_error_quark(),
                  11,
                  "SGF move color does not match side to move at move %u",
                  move_number + 1);
      ggame_create_puzzles_runner_position_free(backend, position);
      return FALSE;
    }

    char move_text[128] = {0};
    gboolean has_move_text = backend->format_move(move, move_text, sizeof(move_text));
    ggame_create_puzzles_progress_consider_move(move_number + 1, has_move_text ? move_text : NULL);

    gpointer before = g_malloc0(backend->position_size);
    gpointer after = g_malloc0(backend->position_size);
    backend->position_copy(before, position);
    backend->position_copy(after, position);
    if (!backend->apply_move(after, move)) {
      g_set_error(error,
                  ggame_create_puzzles_runner_error_quark(),
                  12,
                  "%s rejected SGF move %u",
                  backend->id != NULL ? backend->id : "backend",
                  move_number + 1);
      ggame_create_puzzles_runner_position_free(backend, before);
      ggame_create_puzzles_runner_position_free(backend, after);
      ggame_create_puzzles_runner_position_free(backend, position);
      return FALSE;
    }

    GGameCreatePuzzlesMoveContext context = {
      .backend = backend,
      .variant = tree_variant,
      .source_tree = source_tree,
      .main_line = main_line,
      .node_index = i,
      .move_number = move_number + 1,
      .side = side,
      .color = color,
      .position_before = before,
      .played_move = move,
      .position_after = after,
    };
    if (!consider_move(&context, user_data, &stop, error)) {
      ggame_create_puzzles_runner_position_free(backend, before);
      ggame_create_puzzles_runner_position_free(backend, after);
      ggame_create_puzzles_runner_position_free(backend, position);
      return FALSE;
    }

    if (!stop) {
      if (backend->position_clear != NULL) {
        backend->position_clear(position);
      }
      memset(position, 0, backend->position_size);
      backend->position_copy(position, after);
    }

    ggame_create_puzzles_runner_position_free(backend, before);
    ggame_create_puzzles_runner_position_free(backend, after);
    move_number++;
  }

  ggame_create_puzzles_runner_position_free(backend, position);
  return TRUE;
}

gboolean ggame_create_puzzles_runner_save_source_game(const char *path, SgfTree *source_tree, GError **error) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(SGF_IS_TREE(source_tree), FALSE);

  return sgf_io_save_file(path, source_tree, error);
}
