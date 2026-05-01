#ifndef GAME_BACKEND_H
#define GAME_BACKEND_H

#include <glib.h>

typedef struct _SgfNode SgfNode;

typedef enum {
  GAME_BACKEND_OUTCOME_ONGOING = 0,
  GAME_BACKEND_OUTCOME_SIDE_0_WIN,
  GAME_BACKEND_OUTCOME_SIDE_1_WIN,
  GAME_BACKEND_OUTCOME_DRAW,
} GameBackendOutcome;

typedef enum {
  GAME_BACKEND_SQUARE_PIECE_KIND_NONE = 0,
  GAME_BACKEND_SQUARE_PIECE_KIND_MAN,
  GAME_BACKEND_SQUARE_PIECE_KIND_KING,
  GAME_BACKEND_SQUARE_PIECE_KIND_SYMBOL_ONLY,
} GameBackendSquarePieceKind;

typedef struct {
  gboolean is_empty;
  guint side;
  GameBackendSquarePieceKind kind;
  const char *symbol;
} GameBackendSquarePieceView;

typedef struct {
  const char *id;
  const char *name;
  const char *short_name;
  const char *summary;
} GameBackendVariant;

typedef struct {
  gpointer moves;
  gsize count;
} GameBackendMoveList;

typedef struct {
  gpointer builder_state;
  gsize builder_state_size;
} GameBackendMoveBuilder;

typedef struct {
  const char *id;
  const char *display_name;
  guint variant_count;
  gsize position_size;
  gsize move_size;
  gboolean supports_move_list;
  gboolean supports_move_builder;
  gboolean supports_ai_search;

  const GameBackendVariant *(*variant_at)(guint index);
  const GameBackendVariant *(*variant_by_short_name)(const char *short_name);
  const char *(*side_label)(guint side);
  const char *(*outcome_banner_text)(GameBackendOutcome outcome);

  void (*position_init)(gpointer position, const GameBackendVariant *variant_or_null);
  void (*position_clear)(gpointer position);
  void (*position_copy)(gpointer dest, gconstpointer src);
  GameBackendOutcome (*position_outcome)(gconstpointer position);
  guint (*position_turn)(gconstpointer position);

  GameBackendMoveList (*list_moves)(gconstpointer position);
  GameBackendMoveList (*list_good_moves)(gconstpointer position, guint max_count, guint depth_hint);
  void (*move_list_free)(GameBackendMoveList *moves);
  const void *(*move_list_get)(const GameBackendMoveList *moves, gsize index);
  gboolean (*moves_equal)(gconstpointer left, gconstpointer right);
  gboolean (*move_builder_init)(gconstpointer position, GameBackendMoveBuilder *out_builder);
  void (*move_builder_clear)(GameBackendMoveBuilder *builder);
  GameBackendMoveList (*move_builder_list_candidates)(const GameBackendMoveBuilder *builder);
  gboolean (*move_builder_step)(GameBackendMoveBuilder *builder, gconstpointer candidate);
  gboolean (*move_builder_is_complete)(const GameBackendMoveBuilder *builder);
  gboolean (*move_builder_build_move)(const GameBackendMoveBuilder *builder, gpointer out_move);
  gconstpointer (*move_builder_preview_position)(const GameBackendMoveBuilder *builder);
  gboolean (*move_builder_get_selection_path)(const GameBackendMoveBuilder *builder,
                                              guint *out_length,
                                              guint *out_indices,
                                              gsize max_indices);
  gboolean (*move_builder_reset_selection)(GameBackendMoveBuilder *builder);
  gboolean (*apply_move)(gpointer position, gconstpointer move);
  gint (*evaluate_static)(gconstpointer position);
  gint (*terminal_score)(GameBackendOutcome outcome, guint ply_depth);
  guint64 (*hash_position)(gconstpointer position);
  gboolean (*format_move)(gconstpointer move, char *buffer, gsize size);
  gboolean (*parse_move)(const char *notation, gpointer out_move);
  gboolean (*sgf_apply_setup_node)(gpointer position, const SgfNode *node, GError **error);
  gboolean (*sgf_write_position_node)(gconstpointer position, SgfNode *node, GError **error);
  gboolean supports_square_grid_board;
  guint (*square_grid_rows)(gconstpointer position);
  guint (*square_grid_cols)(gconstpointer position);
  gboolean (*square_grid_square_playable)(gconstpointer position, guint row, guint col);
  gboolean (*square_grid_square_index)(gconstpointer position, guint row, guint col, guint *out_index);
  gboolean (*square_grid_index_coord)(gconstpointer position, guint index, guint *out_row, guint *out_col);
  gboolean (*square_grid_piece_view)(gconstpointer position, guint index, GameBackendSquarePieceView *out_view);
  gboolean (*square_grid_move_get_path)(gconstpointer move,
                                        guint *out_length,
                                        guint *out_indices,
                                        gsize max_indices);
  void (*square_grid_moves_collect_starts)(const GameBackendMoveList *moves,
                                           gboolean *out_starts,
                                           gsize out_count);
  void (*square_grid_moves_collect_next_destinations)(const GameBackendMoveList *moves,
                                                      const guint *path,
                                                      guint path_length,
                                                      gboolean *out_destinations,
                                                      gsize out_count);
  gboolean (*square_grid_move_has_prefix)(gconstpointer move, const guint *path, guint path_length);
} GameBackend;

#endif
