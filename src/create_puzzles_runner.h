#ifndef GGAME_CREATE_PUZZLES_RUNNER_H
#define GGAME_CREATE_PUZZLES_RUNNER_H

#include "ai_search.h"
#include "game_backend.h"
#include "sgf_tree.h"

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
  const GameBackend *backend;
  const GameBackendVariant *variant;
  guint self_play_depth;
  guint max_self_play_plies;
} GGameCreatePuzzlesRunnerConfig;

typedef struct {
  const GameBackend *backend;
  const GameBackendVariant *variant;
  SgfTree *source_tree;
  const GPtrArray *main_line;
  guint node_index;
  guint move_number;
  guint side;
  SgfColor color;
  gconstpointer position_before;
  gconstpointer played_move;
  gconstpointer position_after;
} GGameCreatePuzzlesMoveContext;

typedef gboolean (*GGameCreatePuzzlesConsiderMoveFunc)(const GGameCreatePuzzlesMoveContext *context,
                                                       gpointer user_data,
                                                       gboolean *out_stop,
                                                       GError **error);

SgfTree *ggame_create_puzzles_runner_generate_self_play_tree(const GGameCreatePuzzlesRunnerConfig *config,
                                                             guint *out_plies,
                                                             GameBackendOutcome *out_outcome,
                                                             GError **error);
gboolean ggame_create_puzzles_runner_analyze_tree(const GGameCreatePuzzlesRunnerConfig *config,
                                                  SgfTree *source_tree,
                                                  GGameCreatePuzzlesConsiderMoveFunc consider_move,
                                                  gpointer user_data,
                                                  GError **error);
gboolean ggame_create_puzzles_runner_save_source_game(const char *path, SgfTree *source_tree, GError **error);
guint ggame_create_puzzles_runner_generation_attempt_limit(guint wanted);

G_END_DECLS

#endif
