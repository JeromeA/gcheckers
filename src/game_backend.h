#ifndef GAME_BACKEND_H
#define GAME_BACKEND_H

#include <glib.h>

typedef enum {
  GAME_BACKEND_OUTCOME_ONGOING = 0,
  GAME_BACKEND_OUTCOME_SIDE_0_WIN,
  GAME_BACKEND_OUTCOME_SIDE_1_WIN,
  GAME_BACKEND_OUTCOME_DRAW,
} GameBackendOutcome;

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
  const char *id;
  const char *display_name;
  guint variant_count;
  gsize position_size;
  gsize move_size;

  const GameBackendVariant *(*variant_at)(guint index);
  const GameBackendVariant *(*variant_by_short_name)(const char *short_name);

  void (*position_init)(gpointer position, const GameBackendVariant *variant_or_null);
  void (*position_clear)(gpointer position);
  void (*position_copy)(gpointer dest, gconstpointer src);
  GameBackendOutcome (*position_outcome)(gconstpointer position);
  guint (*position_turn)(gconstpointer position);

  GameBackendMoveList (*list_moves)(gconstpointer position);
  void (*move_list_free)(GameBackendMoveList *moves);
  const void *(*move_list_get)(const GameBackendMoveList *moves, gsize index);
  gboolean (*moves_equal)(gconstpointer left, gconstpointer right);
  gboolean (*apply_move)(gpointer position, gconstpointer move);
  gint (*evaluate_static)(gconstpointer position);
  gint (*terminal_score)(GameBackendOutcome outcome, guint ply_depth);
  guint64 (*hash_position)(gconstpointer position);
  gboolean (*format_move)(gconstpointer move, char *buffer, gsize size);
} GameBackend;

#endif
