#include "homeworlds_backend.h"

#include "homeworlds_game.h"
#include "homeworlds_move_builder.h"
#include "homeworlds_position_text.h"
#include "homeworlds_sgf_position.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

enum {
  HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_LIMIT = 512,
  HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_WINDOW = 50,
  HOMEWORLDS_LARGE_MOVE_DUMP_DEFAULT_THRESHOLD = 7000000,
};

static const char *HOMEWORLDS_LARGE_MOVE_DUMP_THRESHOLD_ENV =
    "GCHECKERS_HOMEWORLDS_LARGE_MOVE_DUMP_THRESHOLD";

typedef struct {
  guint system_index;
  HomeworldsColor color;
  HomeworldsSystemRef system_ref;
} HomeworldsProfitableCatastrophe;

typedef struct {
  HomeworldsProfitableCatastrophe root_catastrophes[HOMEWORLDS_SYSTEM_SLOT_COUNT * 4];
  guint root_catastrophe_count;
} HomeworldsGoodMoveContext;

typedef struct {
  HomeworldsMove move;
  gint score;
  gsize original_index;
} HomeworldsScoredMove;

typedef struct {
  const HomeworldsPosition *position;
  HomeworldsScoredMove *moves;
  GHashTable *seen_moves;
  gsize count;
  gsize capacity;
  gsize leaves_seen;
  gsize scored_moves;
  gsize next_original_index;
  guint side;
  gint best_score;
  gboolean has_best_score;
  gboolean prune_by_score;
} HomeworldsMoveBuffer;

static HomeworldsGoodMoveTraceFunc homeworlds_backend_good_move_trace_func = NULL;
static gpointer homeworlds_backend_good_move_trace_user_data = NULL;

static gboolean homeworlds_backend_score_after_move(const HomeworldsPosition *position,
                                                    const HomeworldsMove *move,
                                                    gint *out_score);
static gboolean homeworlds_backend_score_is_inside_prune_window(guint side, gint score, gint best_score);

void homeworlds_backend_set_good_move_trace(HomeworldsGoodMoveTraceFunc trace_func, gpointer user_data) {
  homeworlds_backend_good_move_trace_func = trace_func;
  homeworlds_backend_good_move_trace_user_data = user_data;
}

static gboolean homeworlds_backend_parse_gsize(const char *text, gsize *out_value) {
  guint64 value = 0;
  char *end_ptr = NULL;

  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(out_value != NULL, FALSE);

  if (*text == '\0') {
    return FALSE;
  }

  errno = 0;
  value = g_ascii_strtoull(text, &end_ptr, 10);
  if (errno != 0 || end_ptr == text || *end_ptr != '\0' || value > G_MAXSIZE) {
    return FALSE;
  }

  *out_value = (gsize)value;
  return TRUE;
}

static gsize homeworlds_backend_large_move_dump_threshold(void) {
  const char *threshold_text = g_getenv(HOMEWORLDS_LARGE_MOVE_DUMP_THRESHOLD_ENV);
  gsize threshold = HOMEWORLDS_LARGE_MOVE_DUMP_DEFAULT_THRESHOLD;

  if (threshold_text == NULL) {
    return threshold;
  }
  if (!homeworlds_backend_parse_gsize(threshold_text, &threshold)) {
    g_debug("Ignoring invalid %s value", HOMEWORLDS_LARGE_MOVE_DUMP_THRESHOLD_ENV);
    threshold = HOMEWORLDS_LARGE_MOVE_DUMP_DEFAULT_THRESHOLD;
  }
  return threshold;
}

static void homeworlds_backend_dump_large_move_position(const HomeworldsPosition *position,
                                                        guint depth_hint,
                                                        gsize good_moves_generated,
                                                        gsize total_possible_moves) {
  char *ascii = NULL;

  g_return_if_fail(position != NULL);

  ascii = homeworlds_position_format_ascii(position);
  if (ascii == NULL) {
    g_debug("Failed to format large Homeworlds move-generation position");
    return;
  }

  g_printerr("large-move-count,side,depth_hint,good_moves_generated,total_possible_moves\n");
  g_printerr("large-move-count,%u,%u,%" G_GSIZE_FORMAT ",%" G_GSIZE_FORMAT "\n",
             position->turn,
             depth_hint,
             good_moves_generated,
             total_possible_moves);
  g_printerr("large-move-position-ascii-begin\n%s", ascii);
  if (!g_str_has_suffix(ascii, "\n")) {
    g_printerr("\n");
  }
  g_printerr("large-move-position-ascii-end\n");
  g_free(ascii);
}

static void homeworlds_backend_maybe_dump_large_move_position(const HomeworldsPosition *position,
                                                              guint depth_hint,
                                                              gsize good_moves_generated) {
  gsize total_possible_moves = 0;
  gsize threshold = homeworlds_backend_large_move_dump_threshold();

  g_return_if_fail(position != NULL);

  if (good_moves_generated <= threshold) {
    return;
  }
  if (!homeworlds_position_count_all_move_paths(position, &total_possible_moves)) {
    g_debug("Failed to count total Homeworlds move paths for large move-generation dump");
    return;
  }

  homeworlds_backend_dump_large_move_position(position, depth_hint, good_moves_generated, total_possible_moves);
}

static const char *homeworlds_backend_side_label(guint side) {
  switch (side) {
    case 0:
      return "Player 1";
    case 1:
      return "Player 2";
    default:
      g_debug("Unsupported Homeworlds side index");
      return "Player";
  }
}

static const char *homeworlds_backend_outcome_banner_text(GameBackendOutcome outcome) {
  switch (outcome) {
    case GAME_BACKEND_OUTCOME_SIDE_0_WIN:
      return "Player 1 wins";
    case GAME_BACKEND_OUTCOME_SIDE_1_WIN:
      return "Player 2 wins";
    case GAME_BACKEND_OUTCOME_DRAW:
      return "Draw";
    case GAME_BACKEND_OUTCOME_ONGOING:
    default:
      return NULL;
  }
}

static SgfColor homeworlds_backend_sgf_color_for_side(guint side) {
  switch (side) {
    case 0:
      return SGF_COLOR_BLACK;
    case 1:
      return SGF_COLOR_WHITE;
    default:
      g_debug("Unsupported Homeworlds side index for SGF color");
      return SGF_COLOR_NONE;
  }
}

static void homeworlds_backend_position_init(gpointer position, const GameBackendVariant * /*variant_or_null*/) {
  HomeworldsPosition *homeworlds_position = position;

  g_return_if_fail(homeworlds_position != NULL);

  homeworlds_position_init(homeworlds_position);
}

static void homeworlds_backend_position_clear(gpointer position) {
  HomeworldsPosition *homeworlds_position = position;

  g_return_if_fail(homeworlds_position != NULL);

  homeworlds_position_clear(homeworlds_position);
}

static void homeworlds_backend_position_copy(gpointer dest, gconstpointer src) {
  HomeworldsPosition *dest_position = dest;
  const HomeworldsPosition *src_position = src;

  g_return_if_fail(dest_position != NULL);
  g_return_if_fail(src_position != NULL);

  homeworlds_position_copy(dest_position, src_position);
}

static GameBackendOutcome homeworlds_backend_position_outcome(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, GAME_BACKEND_OUTCOME_ONGOING);

  return homeworlds_position_outcome(homeworlds_position);
}

static guint homeworlds_backend_position_turn(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, 0);

  return homeworlds_position_turn(homeworlds_position);
}

static void homeworlds_backend_move_list_free(GameBackendMoveList *moves) {
  g_return_if_fail(moves != NULL);

  homeworlds_move_list_free(moves);
}

static const void *homeworlds_backend_move_list_get(const GameBackendMoveList *moves, gsize index) {
  g_return_val_if_fail(moves != NULL, NULL);

  return homeworlds_move_list_get(moves, index);
}

static gboolean homeworlds_backend_system_refs_equal(const HomeworldsSystemRef *left,
                                                     const HomeworldsSystemRef *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->kind != right->kind) {
    return FALSE;
  }

  switch ((HomeworldsSystemRefKind)left->kind) {
    case HOMEWORLDS_SYSTEM_REF_HOMEWORLD:
      return left->homeworld_side == right->homeworld_side;
    case HOMEWORLDS_SYSTEM_REF_SYSTEM:
      return left->system_index == right->system_index && left->star == right->star;
    case HOMEWORLDS_SYSTEM_REF_NONE:
    default:
      return TRUE;
  }
}

static gint homeworlds_backend_compare_system_refs(const HomeworldsSystemRef *left,
                                                   const HomeworldsSystemRef *right) {
  g_return_val_if_fail(left != NULL, 0);
  g_return_val_if_fail(right != NULL, 0);

  if (left->kind != right->kind) {
    return left->kind < right->kind ? -1 : 1;
  }

  switch ((HomeworldsSystemRefKind) left->kind) {
    case HOMEWORLDS_SYSTEM_REF_HOMEWORLD:
      if (left->homeworld_side != right->homeworld_side) {
        return left->homeworld_side < right->homeworld_side ? -1 : 1;
      }
      return 0;
    case HOMEWORLDS_SYSTEM_REF_SYSTEM:
      if (left->system_index != right->system_index) {
        return left->system_index < right->system_index ? -1 : 1;
      }
      if (left->star != right->star) {
        return left->star < right->star ? -1 : 1;
      }
      return 0;
    case HOMEWORLDS_SYSTEM_REF_NONE:
    default:
      return 0;
  }
}

static gint homeworlds_backend_compare_trade_steps(const HomeworldsTurnStep *left,
                                                   const HomeworldsTurnStep *right) {
  gint system_order = 0;

  g_return_val_if_fail(left != NULL, 0);
  g_return_val_if_fail(right != NULL, 0);
  g_return_val_if_fail(left->kind == HOMEWORLDS_STEP_TRADE, 0);
  g_return_val_if_fail(right->kind == HOMEWORLDS_STEP_TRADE, 0);

  system_order = homeworlds_backend_compare_system_refs(&left->actor.system, &right->actor.system);
  if (system_order != 0) {
    return system_order;
  }
  if (left->actor.ship != right->actor.ship) {
    return left->actor.ship < right->actor.ship ? -1 : 1;
  }
  if (left->target_color != right->target_color) {
    return left->target_color < right->target_color ? -1 : 1;
  }
  return 0;
}

static gint homeworlds_backend_compare_build_steps(const HomeworldsTurnStep *left,
                                                   const HomeworldsTurnStep *right) {
  gint system_order = 0;

  g_return_val_if_fail(left != NULL, 0);
  g_return_val_if_fail(right != NULL, 0);
  g_return_val_if_fail(left->kind == HOMEWORLDS_STEP_BUILD, 0);
  g_return_val_if_fail(right->kind == HOMEWORLDS_STEP_BUILD, 0);

  system_order = homeworlds_backend_compare_system_refs(&left->actor.system, &right->actor.system);
  if (system_order != 0) {
    return system_order;
  }
  if (left->target_color != right->target_color) {
    return left->target_color < right->target_color ? -1 : 1;
  }
  return 0;
}

static gboolean homeworlds_backend_count_pyramid(HomeworldsPyramid pyramid, guint counts[13]) {
  g_return_val_if_fail(counts != NULL, FALSE);

  if (homeworlds_pyramid_is_unused(pyramid)) {
    return TRUE;
  }
  if (!homeworlds_pyramid_is_valid(pyramid)) {
    return FALSE;
  }

  counts[pyramid]++;
  return TRUE;
}

static gboolean homeworlds_backend_pyramid_counts_equal(const guint left[13], const guint right[13]) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  for (guint i = 0; i < 13; ++i) {
    if (left[i] != right[i]) {
      return FALSE;
    }
  }
  return TRUE;
}

static void homeworlds_backend_adjust_ship_color_count(HomeworldsSystem *system,
                                                       guint side,
                                                       HomeworldsPyramid pyramid,
                                                       gint delta) {
  g_return_if_fail(system != NULL);
  g_return_if_fail(side < 2);
  g_return_if_fail(homeworlds_pyramid_is_valid(pyramid));
  g_return_if_fail(delta == 1 || delta == -1);

  HomeworldsColor color = homeworlds_pyramid_color(pyramid);
  if (delta < 0) {
    g_return_if_fail(system->ship_color_counts[side][color] > 0);
    system->ship_color_counts[side][color]--;
  } else {
    system->ship_color_counts[side][color]++;
  }
}

static void homeworlds_backend_set_ship_slot(HomeworldsSystem *system,
                                             guint side,
                                             guint slot,
                                             HomeworldsPyramid pyramid) {
  g_return_if_fail(system != NULL);
  g_return_if_fail(side < 2);
  g_return_if_fail(slot < HOMEWORLDS_SHIP_SLOT_COUNT);
  g_return_if_fail(pyramid == 0 || homeworlds_pyramid_is_valid(pyramid));

  HomeworldsPyramid old_pyramid = system->ships[side][slot];
  if (pyramid == 0) {
    g_return_if_fail(homeworlds_pyramid_is_valid(old_pyramid));

    homeworlds_backend_adjust_ship_color_count(system, side, old_pyramid, -1);
    for (guint next_slot = slot + 1; next_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++next_slot) {
      HomeworldsPyramid next_pyramid = system->ships[side][next_slot];

      system->ships[side][next_slot - 1] = next_pyramid;
      if (!homeworlds_pyramid_is_valid(next_pyramid)) {
        return;
      }
    }
    system->ships[side][HOMEWORLDS_SHIP_SLOT_COUNT - 1] = 0;
    return;
  }

  if (homeworlds_pyramid_is_valid(old_pyramid)) {
    homeworlds_backend_adjust_ship_color_count(system, side, old_pyramid, -1);
  }
  system->ships[side][slot] = pyramid;
  if (homeworlds_pyramid_is_valid(pyramid)) {
    homeworlds_backend_adjust_ship_color_count(system, side, pyramid, 1);
  }
}

static gboolean homeworlds_backend_bank_contents_equal(const HomeworldsPosition *left,
                                                       const HomeworldsPosition *right) {
  guint left_counts[13] = {0};
  guint right_counts[13] = {0};

  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    if (!homeworlds_backend_count_pyramid(left->bank[i], left_counts) ||
        !homeworlds_backend_count_pyramid(right->bank[i], right_counts)) {
      return FALSE;
    }
  }
  return homeworlds_backend_pyramid_counts_equal(left_counts, right_counts);
}

static gboolean homeworlds_backend_systems_equal(const HomeworldsSystem *left, const HomeworldsSystem *right) {
  guint left_stars[13] = {0};
  guint right_stars[13] = {0};

  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    if (!homeworlds_backend_count_pyramid(left->stars[i], left_stars) ||
        !homeworlds_backend_count_pyramid(right->stars[i], right_stars)) {
      return FALSE;
    }
  }
  if (!homeworlds_backend_pyramid_counts_equal(left_stars, right_stars)) {
    return FALSE;
  }

  for (guint side = 0; side < 2; ++side) {
    guint left_ships[13] = {0};
    guint right_ships[13] = {0};

    for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
      HomeworldsPyramid left_ship = left->ships[side][slot];
      HomeworldsPyramid right_ship = right->ships[side][slot];

      if (!homeworlds_pyramid_is_valid(left_ship) && !homeworlds_pyramid_is_valid(right_ship)) {
        if (left_ship != 0 || right_ship != 0) {
          return FALSE;
        }
        break;
      }
      if (!homeworlds_backend_count_pyramid(left_ship, left_ships) ||
          !homeworlds_backend_count_pyramid(right_ship, right_ships)) {
        return FALSE;
      }
    }
    if (!homeworlds_backend_pyramid_counts_equal(left_ships, right_ships)) {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean homeworlds_backend_positions_equal(const HomeworldsPosition *left,
                                                   const HomeworldsPosition *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->phase != right->phase || left->turn != right->turn) {
    return FALSE;
  }
  if (!homeworlds_backend_bank_contents_equal(left, right)) {
    return FALSE;
  }
  for (guint i = 0; i < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++i) {
    if (!homeworlds_backend_systems_equal(&left->systems[i], &right->systems[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean homeworlds_backend_moves_equal(gconstpointer left, gconstpointer right) {
  const HomeworldsMove *left_move = left;
  const HomeworldsMove *right_move = right;

  g_return_val_if_fail(left_move != NULL, FALSE);
  g_return_val_if_fail(right_move != NULL, FALSE);

  return homeworlds_moves_equal(left_move, right_move);
}

static gboolean homeworlds_backend_profitable_catastrophes_equal(const HomeworldsProfitableCatastrophe *left,
                                                                 const HomeworldsProfitableCatastrophe *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  return left->color == right->color &&
         homeworlds_backend_system_refs_equal(&left->system_ref, &right->system_ref);
}

static gboolean homeworlds_backend_move_has_profitable_catastrophe(
    const HomeworldsMove *move,
    const HomeworldsProfitableCatastrophe *catastrophe) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(catastrophe != NULL, FALSE);

  if (move->kind != HOMEWORLDS_MOVE_KIND_TURN) {
    return FALSE;
  }

  for (guint i = 0; i < move->step_count; ++i) {
    const HomeworldsTurnStep *step = &move->steps[i];

    if (step->kind == HOMEWORLDS_STEP_CATASTROPHE &&
        step->target_color == catastrophe->color &&
        homeworlds_backend_system_refs_equal(&step->target_system, &catastrophe->system_ref)) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_backend_move_satisfies_root_catastrophe_requirement(
    const HomeworldsMove *move,
    const HomeworldsGoodMoveContext *context) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);

  if (context->root_catastrophe_count == 0) {
    return TRUE;
  }

  for (guint i = 0; i < context->root_catastrophe_count; ++i) {
    if (homeworlds_backend_move_has_profitable_catastrophe(move, &context->root_catastrophes[i])) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_backend_catastrophe_is_root_required(
    const HomeworldsGoodMoveContext *context,
    const HomeworldsProfitableCatastrophe *catastrophe) {
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(catastrophe != NULL, FALSE);

  for (guint i = 0; i < context->root_catastrophe_count; ++i) {
    if (homeworlds_backend_profitable_catastrophes_equal(catastrophe, &context->root_catastrophes[i])) {
      return TRUE;
    }
  }

  return FALSE;
}

static void homeworlds_backend_move_buffer_clear(HomeworldsMoveBuffer *buffer) {
  g_return_if_fail(buffer != NULL);

  g_clear_pointer(&buffer->seen_moves, g_hash_table_unref);
  g_clear_pointer(&buffer->moves, g_free);
  buffer->position = NULL;
  buffer->count = 0;
  buffer->capacity = 0;
  buffer->leaves_seen = 0;
  buffer->scored_moves = 0;
  buffer->next_original_index = 0;
  buffer->side = 0;
  buffer->best_score = 0;
  buffer->has_best_score = FALSE;
  buffer->prune_by_score = FALSE;
}

static void homeworlds_backend_move_buffer_init(HomeworldsMoveBuffer *buffer, const HomeworldsPosition *position) {
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(position != NULL);

  memset(buffer, 0, sizeof(*buffer));
  buffer->position = position;
  buffer->side = position->turn;
  buffer->prune_by_score = position->phase == HOMEWORLDS_PHASE_PLAY;
}

static gboolean homeworlds_backend_move_buffer_ensure_seen_moves(HomeworldsMoveBuffer *buffer) {
  g_return_val_if_fail(buffer != NULL, FALSE);

  if (buffer->seen_moves != NULL) {
    return TRUE;
  }

  buffer->seen_moves = g_hash_table_new_full(homeworlds_move_hash, homeworlds_backend_moves_equal, g_free, NULL);
  g_return_val_if_fail(buffer->seen_moves != NULL, FALSE);
  return TRUE;
}

static gboolean homeworlds_backend_move_buffer_add_seen_move(HomeworldsMoveBuffer *buffer,
                                                             const HomeworldsMove *move) {
  HomeworldsMove *key = NULL;

  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (!homeworlds_backend_move_buffer_ensure_seen_moves(buffer)) {
    return FALSE;
  }

  key = g_new(HomeworldsMove, 1);
  *key = *move;
  g_hash_table_add(buffer->seen_moves, key);
  return TRUE;
}

static void homeworlds_backend_move_buffer_remove_seen_move(HomeworldsMoveBuffer *buffer,
                                                            const HomeworldsMove *move) {
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(move != NULL);

  if (buffer->seen_moves != NULL) {
    g_hash_table_remove(buffer->seen_moves, move);
  }
}

static gboolean homeworlds_backend_move_buffer_has_seen_move(const HomeworldsMoveBuffer *buffer,
                                                             const HomeworldsMove *move) {
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  return buffer->seen_moves != NULL && g_hash_table_contains(buffer->seen_moves, move);
}

static gboolean homeworlds_backend_score_is_better(guint side, gint score, gint other_score) {
  g_return_val_if_fail(side < 2, FALSE);

  if (side == 0) {
    return score > other_score;
  }
  return score < other_score;
}

static gint homeworlds_backend_scored_move_order_compare(guint side,
                                                         const HomeworldsScoredMove *left,
                                                         const HomeworldsScoredMove *right) {
  g_return_val_if_fail(side < 2, 0);
  g_return_val_if_fail(left != NULL, 0);
  g_return_val_if_fail(right != NULL, 0);

  if (homeworlds_backend_score_is_better(side, left->score, right->score)) {
    return -1;
  }
  if (homeworlds_backend_score_is_better(side, right->score, left->score)) {
    return 1;
  }
  if (left->original_index < right->original_index) {
    return -1;
  }
  if (left->original_index > right->original_index) {
    return 1;
  }
  return 0;
}

static gboolean homeworlds_backend_move_buffer_reserve_slot(HomeworldsMoveBuffer *buffer) {
  gsize next_capacity = 0;

  g_return_val_if_fail(buffer != NULL, FALSE);

  if (buffer->count < buffer->capacity) {
    return TRUE;
  }

  next_capacity = buffer->capacity == 0 ? 16 : buffer->capacity * 2;
  if (buffer->prune_by_score) {
    next_capacity = MIN(next_capacity, (gsize) HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_LIMIT);
  }
  g_return_val_if_fail(next_capacity > buffer->capacity, FALSE);

  HomeworldsScoredMove *next_moves = g_realloc_n(buffer->moves, next_capacity, sizeof(*next_moves));
  g_return_val_if_fail(next_moves != NULL, FALSE);
  buffer->moves = next_moves;
  buffer->capacity = next_capacity;
  return TRUE;
}

static void homeworlds_backend_move_buffer_remove_at(HomeworldsMoveBuffer *buffer, gsize index) {
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(index < buffer->count);

  homeworlds_backend_move_buffer_remove_seen_move(buffer, &buffer->moves[index].move);
  if (index + 1 < buffer->count) {
    memmove(&buffer->moves[index],
            &buffer->moves[index + 1],
            (buffer->count - index - 1) * sizeof(buffer->moves[0]));
  }
  buffer->count--;
}

static void homeworlds_backend_move_buffer_prune_score_window(HomeworldsMoveBuffer *buffer) {
  gsize i = 0;

  g_return_if_fail(buffer != NULL);

  while (i < buffer->count) {
    if (homeworlds_backend_score_is_inside_prune_window(buffer->side, buffer->moves[i].score, buffer->best_score)) {
      i++;
      continue;
    }
    homeworlds_backend_move_buffer_remove_at(buffer, i);
  }
}

static gboolean homeworlds_backend_move_buffer_insert_scored(HomeworldsMoveBuffer *buffer,
                                                             const HomeworldsScoredMove *scored_move) {
  gsize index = 0;

  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(scored_move != NULL, FALSE);

  if (!homeworlds_backend_move_buffer_reserve_slot(buffer)) {
    return FALSE;
  }

  while (index < buffer->count &&
         homeworlds_backend_scored_move_order_compare(buffer->side, &buffer->moves[index], scored_move) <= 0) {
    index++;
  }
  if (index < buffer->count) {
    memmove(&buffer->moves[index + 1], &buffer->moves[index], (buffer->count - index) * sizeof(buffer->moves[0]));
  }

  buffer->moves[index] = *scored_move;
  buffer->count++;
  return homeworlds_backend_move_buffer_add_seen_move(buffer, &scored_move->move);
}

static gboolean homeworlds_backend_move_buffer_append_unsorted(HomeworldsMoveBuffer *buffer,
                                                               const HomeworldsMove *move) {
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (!homeworlds_backend_move_buffer_ensure_seen_moves(buffer)) {
    return FALSE;
  }
  if (g_hash_table_contains(buffer->seen_moves, move)) {
    return TRUE;
  }

  if (!homeworlds_backend_move_buffer_reserve_slot(buffer)) {
    return FALSE;
  }

  buffer->moves[buffer->count++] = (HomeworldsScoredMove){
    .move = *move,
    .score = 0,
    .original_index = buffer->next_original_index++,
  };
  return homeworlds_backend_move_buffer_add_seen_move(buffer, move);
}

static gboolean homeworlds_backend_move_buffer_append_scored(HomeworldsMoveBuffer *buffer, const HomeworldsMove *move) {
  HomeworldsScoredMove scored_move = {0};
  gint score = 0;

  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(buffer->position != NULL, FALSE);

  if (homeworlds_backend_move_buffer_has_seen_move(buffer, move)) {
    return TRUE;
  }
  if (!homeworlds_backend_score_after_move(buffer->position, move, &score)) {
    return FALSE;
  }

  buffer->scored_moves++;
  scored_move = (HomeworldsScoredMove){
    .move = *move,
    .score = score,
    .original_index = buffer->next_original_index++,
  };

  if (!buffer->has_best_score || homeworlds_backend_score_is_better(buffer->side, score, buffer->best_score)) {
    buffer->best_score = score;
    buffer->has_best_score = TRUE;
    homeworlds_backend_move_buffer_prune_score_window(buffer);
  } else if (!homeworlds_backend_score_is_inside_prune_window(buffer->side, score, buffer->best_score)) {
    return TRUE;
  }

  if (buffer->count == HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_LIMIT) {
    g_return_val_if_fail(buffer->count > 0, FALSE);
    if (homeworlds_backend_scored_move_order_compare(buffer->side, &scored_move, &buffer->moves[buffer->count - 1]) >=
        0) {
      return TRUE;
    }
    homeworlds_backend_move_buffer_remove_at(buffer, buffer->count - 1);
  }

  return homeworlds_backend_move_buffer_insert_scored(buffer, &scored_move);
}

static gboolean homeworlds_backend_move_buffer_append(HomeworldsMoveBuffer *buffer, const HomeworldsMove *move) {
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  buffer->leaves_seen++;
  if (buffer->prune_by_score) {
    return homeworlds_backend_move_buffer_append_scored(buffer, move);
  }
  return homeworlds_backend_move_buffer_append_unsorted(buffer, move);
}

static HomeworldsMove *homeworlds_backend_move_buffer_copy_moves(const HomeworldsMoveBuffer *buffer) {
  HomeworldsMove *moves = NULL;

  g_return_val_if_fail(buffer != NULL, NULL);

  if (buffer->count == 0) {
    return NULL;
  }

  moves = g_new0(HomeworldsMove, buffer->count);
  for (gsize i = 0; i < buffer->count; ++i) {
    moves[i] = buffer->moves[i].move;
  }
  return moves;
}

static guint homeworlds_backend_setup_star_size_mask(const HomeworldsMove *move) {
  guint mask = 0;

  g_return_val_if_fail(move != NULL, 0);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = move->setup_stars[i];
    if (!homeworlds_pyramid_is_valid(star)) {
      return 0;
    }

    mask |= 1u << (homeworlds_pyramid_size(star) - 1);
  }

  return mask;
}

static guint homeworlds_backend_homeworld_star_size_mask(const HomeworldsPosition *position, guint side) {
  guint mask = 0;

  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(side < 2, 0);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = position->systems[side].stars[i];
    if (!homeworlds_pyramid_is_valid(star)) {
      return 0;
    }

    mask |= 1u << (homeworlds_pyramid_size(star) - 1);
  }

  return mask;
}

static gboolean homeworlds_backend_setup_colors_are_distinct(const HomeworldsMove *move) {
  gboolean seen_colors[4] = {FALSE};

  g_return_val_if_fail(move != NULL, FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = move->setup_stars[i];
    if (!homeworlds_pyramid_is_valid(star)) {
      return FALSE;
    }

    HomeworldsColor color = homeworlds_pyramid_color(star);
    if (seen_colors[color]) {
      return FALSE;
    }
    seen_colors[color] = TRUE;
  }

  if (!homeworlds_pyramid_is_valid(move->setup_ship)) {
    return FALSE;
  }

  HomeworldsColor ship_color = homeworlds_pyramid_color(move->setup_ship);
  if (seen_colors[ship_color]) {
    return FALSE;
  }

  return TRUE;
}

static gboolean homeworlds_backend_setup_has_required_colors(const HomeworldsMove *move) {
  gboolean seen_colors[4] = {FALSE};

  g_return_val_if_fail(move != NULL, FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = move->setup_stars[i];
    if (!homeworlds_pyramid_is_valid(star)) {
      return FALSE;
    }

    seen_colors[homeworlds_pyramid_color(star)] = TRUE;
  }

  if (!homeworlds_pyramid_is_valid(move->setup_ship)) {
    return FALSE;
  }
  seen_colors[homeworlds_pyramid_color(move->setup_ship)] = TRUE;

  return seen_colors[HOMEWORLDS_COLOR_GREEN] &&
         seen_colors[HOMEWORLDS_COLOR_BLUE] &&
         (seen_colors[HOMEWORLDS_COLOR_RED] || seen_colors[HOMEWORLDS_COLOR_YELLOW]);
}

static gboolean homeworlds_backend_setup_move_is_good(const HomeworldsMoveBuilderState *state,
                                                      const HomeworldsMove *move) {
  guint star_size_mask = 0;
  guint side = 0;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  side = state->working_position.turn;
  if (move->kind != HOMEWORLDS_MOVE_KIND_SETUP || side > 1 || !homeworlds_backend_setup_colors_are_distinct(move) ||
      homeworlds_pyramid_size(move->setup_ship) != HOMEWORLDS_SIZE_LARGE) {
    return FALSE;
  }
  if (!homeworlds_backend_setup_has_required_colors(move)) {
    return FALSE;
  }

  star_size_mask = homeworlds_backend_setup_star_size_mask(move);
  if (star_size_mask == 0 || (star_size_mask & (star_size_mask - 1)) == 0) {
    return FALSE;
  }

  if (side == 1 && star_size_mask == homeworlds_backend_homeworld_star_size_mask(&state->working_position, 0)) {
    return FALSE;
  }

  return TRUE;
}

static gboolean homeworlds_backend_position_is_initial_turn(const HomeworldsPosition *position) {
  g_return_val_if_fail(position != NULL, FALSE);

  if (position->phase != HOMEWORLDS_PHASE_PLAY || position->turn != 0 ||
      homeworlds_system_ship_count_for_side(&position->systems[0], 0) != 1 ||
      homeworlds_system_ship_count_for_side(&position->systems[1], 1) != 1) {
    return FALSE;
  }

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &position->systems[system_index];

    if (system_index != 0 && homeworlds_system_has_ships_for_side(system, 0)) {
      return FALSE;
    }
    if (system_index != 1 && homeworlds_system_has_ships_for_side(system, 1)) {
      return FALSE;
    }
  }

  return TRUE;
}

static guint homeworlds_backend_system_ship_pips_for_color(const HomeworldsSystem *system,
                                                           HomeworldsColor color,
                                                           guint side) {
  guint pips = 0;

  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);
  g_return_val_if_fail(side < 2, 0);

  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    HomeworldsPyramid ship = system->ships[side][slot];

    if (!homeworlds_pyramid_is_valid(ship)) {
      break;
    }
    if (homeworlds_pyramid_color(ship) != color) {
      continue;
    }
    pips += homeworlds_pyramid_size(ship);
  }

  return pips;
}

static gboolean homeworlds_backend_system_has_catastrophe(const HomeworldsSystem *system) {
  g_return_val_if_fail(system != NULL, FALSE);

  for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
    if (homeworlds_system_color_count(system, (HomeworldsColor) color) >= 4) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_backend_position_has_catastrophe(const HomeworldsPosition *position) {
  g_return_val_if_fail(position != NULL, FALSE);

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    if (homeworlds_backend_system_has_catastrophe(&position->systems[system_index])) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_backend_system_has_unfavorable_catastrophe(const HomeworldsSystem *system, guint side) {
  guint opponent = 0;

  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);

  opponent = side == 0 ? 1 : 0;
  for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
    guint own_pips = 0;
    guint opponent_pips = 0;

    if (homeworlds_system_color_count(system, (HomeworldsColor) color) < 4) {
      continue;
    }

    own_pips = homeworlds_backend_system_ship_pips_for_color(system, (HomeworldsColor) color, side);
    opponent_pips = homeworlds_backend_system_ship_pips_for_color(system, (HomeworldsColor) color, opponent);
    if (own_pips > opponent_pips) {
      return TRUE;
    }
  }

  return FALSE;
}

static const HomeworldsTurnStep *homeworlds_backend_appended_step(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state) {
  g_return_val_if_fail(state != NULL, NULL);
  g_return_val_if_fail(child_state != NULL, NULL);

  if (state->move.kind != HOMEWORLDS_MOVE_KIND_TURN ||
      child_state->move.kind != HOMEWORLDS_MOVE_KIND_TURN ||
      child_state->move.step_count != state->move.step_count + 1) {
    return NULL;
  }

  return &child_state->move.steps[child_state->move.step_count - 1];
}

static gboolean homeworlds_backend_resolve_actor_system(const HomeworldsMoveBuilderState *state,
                                                        const HomeworldsTurnStep *step,
                                                        guint *out_system_index) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(out_system_index != NULL, FALSE);

  return homeworlds_position_resolve_system_ref(&state->working_position, &step->actor.system, out_system_index);
}

static gboolean homeworlds_backend_step_removes_last_homeworld_ship(const HomeworldsMoveBuilderState *state,
                                                                    const HomeworldsTurnStep *step) {
  guint side = 0;
  guint system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_MOVE &&
      step->kind != HOMEWORLDS_STEP_DISCOVER &&
      step->kind != HOMEWORLDS_STEP_SACRIFICE) {
    return FALSE;
  }
  if (!homeworlds_backend_resolve_actor_system(state, step, &system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  return system_index == side &&
         homeworlds_system_ship_count_for_side(&state->working_position.systems[side], side) == 1;
}

static gboolean homeworlds_backend_step_is_redundant_small_sacrifice(const HomeworldsMoveBuilderState *state,
                                                                     const HomeworldsTurnStep *step) {
  guint side = 0;
  guint system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_SACRIFICE ||
      !homeworlds_pyramid_is_valid(step->actor.ship) ||
      homeworlds_pyramid_size(step->actor.ship) != HOMEWORLDS_SIZE_SMALL ||
      !homeworlds_backend_resolve_actor_system(state, step, &system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  return homeworlds_system_has_access_to_color(&state->working_position.systems[system_index],
                                               side,
                                               homeworlds_pyramid_color(step->actor.ship));
}

static gboolean homeworlds_backend_step_creates_unfavorable_build_catastrophe(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *step) {
  guint side = 0;
  guint system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_BUILD ||
      !homeworlds_backend_resolve_actor_system(state, step, &system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  if (homeworlds_backend_system_has_unfavorable_catastrophe(&state->working_position.systems[system_index], side)) {
    return FALSE;
  }

  return homeworlds_backend_system_has_unfavorable_catastrophe(&child_state->working_position.systems[system_index],
                                                               side);
}

static gboolean homeworlds_backend_step_creates_unfavorable_trade_catastrophe(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *step) {
  guint side = 0;
  guint system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_TRADE ||
      !homeworlds_backend_resolve_actor_system(state, step, &system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  if (homeworlds_backend_system_has_unfavorable_catastrophe(&state->working_position.systems[system_index], side)) {
    return FALSE;
  }

  return homeworlds_backend_system_has_unfavorable_catastrophe(&child_state->working_position.systems[system_index],
                                                               side);
}

static gboolean homeworlds_backend_step_enters_unfavorable_catastrophe(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *step) {
  guint side = 0;
  guint target_system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_MOVE && step->kind != HOMEWORLDS_STEP_DISCOVER) {
    return FALSE;
  }
  if (!homeworlds_position_resolve_system_ref(&child_state->working_position,
                                              &step->target_system,
                                              &target_system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  return homeworlds_backend_system_has_unfavorable_catastrophe(
      &child_state->working_position.systems[target_system_index],
      side);
}

static gboolean homeworlds_backend_step_is_ship_move(const HomeworldsTurnStep *step) {
  g_return_val_if_fail(step != NULL, FALSE);

  return (step->kind == HOMEWORLDS_STEP_MOVE || step->kind == HOMEWORLDS_STEP_DISCOVER) &&
         homeworlds_pyramid_is_valid(step->actor.ship);
}

static gboolean homeworlds_backend_find_yellow_sacrifice_move_origin(const HomeworldsMoveBuilderState *state,
                                                                     const HomeworldsTurnStep *step,
                                                                     HomeworldsSystemRef *out_origin_ref) {
  HomeworldsSystemRef chain_system = {0};
  gboolean found_origin = FALSE;
  gboolean found_sacrifice = FALSE;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(out_origin_ref != NULL, FALSE);

  if (state->pending_actions_remaining == 0 ||
      state->forced_action_color != HOMEWORLDS_COLOR_YELLOW ||
      !homeworlds_backend_step_is_ship_move(step)) {
    return FALSE;
  }

  chain_system = step->actor.system;
  for (guint i = state->move.step_count; i > 0; --i) {
    const guint step_index = i - 1;
    const HomeworldsTurnStep *prior_step = &state->move.steps[step_index];

    if (prior_step->kind == HOMEWORLDS_STEP_SACRIFICE &&
        homeworlds_pyramid_is_valid(prior_step->actor.ship) &&
        homeworlds_pyramid_color(prior_step->actor.ship) == HOMEWORLDS_COLOR_YELLOW) {
      found_sacrifice = TRUE;
      break;
    }

    if (!homeworlds_backend_step_is_ship_move(prior_step) ||
        prior_step->actor.ship != step->actor.ship ||
        !homeworlds_backend_system_refs_equal(&prior_step->target_system, &chain_system)) {
      continue;
    }

    chain_system = prior_step->actor.system;
    found_origin = TRUE;
  }

  if (!found_sacrifice || !found_origin) {
    return FALSE;
  }

  *out_origin_ref = chain_system;
  return TRUE;
}

static gboolean homeworlds_backend_system_contains_star(const HomeworldsSystem *system, HomeworldsPyramid star) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(star), FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    if (system->stars[i] == star) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_backend_system_for_ref_or_discovery(const HomeworldsPosition *position,
                                                               const HomeworldsSystemRef *ref,
                                                               HomeworldsSystem *out_system,
                                                               guint *out_system_index) {
  guint system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(ref != NULL, FALSE);
  g_return_val_if_fail(out_system != NULL, FALSE);
  g_return_val_if_fail(out_system_index != NULL, FALSE);

  if (homeworlds_position_resolve_system_ref(position, ref, &system_index)) {
    *out_system = position->systems[system_index];
    *out_system_index = system_index;
    return TRUE;
  }

  if (ref->kind != HOMEWORLDS_SYSTEM_REF_SYSTEM ||
      ref->system_index < 2 ||
      ref->system_index >= HOMEWORLDS_SYSTEM_SLOT_COUNT ||
      !homeworlds_pyramid_is_valid(ref->star)) {
    return FALSE;
  }

  if (!homeworlds_system_is_empty(&position->systems[ref->system_index])) {
    if (!homeworlds_backend_system_contains_star(&position->systems[ref->system_index], ref->star)) {
      return FALSE;
    }

    *out_system = position->systems[ref->system_index];
    *out_system_index = ref->system_index;
    return TRUE;
  }

  memset(out_system, 0, sizeof(*out_system));
  out_system->stars[0] = ref->star;
  homeworlds_system_rebuild_color_counts(out_system);
  *out_system_index = ref->system_index;
  return TRUE;
}

static gboolean homeworlds_backend_step_is_redundant_yellow_sacrifice_hop(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *step) {
  HomeworldsSystemRef origin_ref = {0};
  HomeworldsSystem origin_system = {0};
  HomeworldsSystem target_system = {0};
  HomeworldsSystem child_target_system = {0};
  guint origin_system_index = HOMEWORLDS_INVALID_INDEX;
  guint target_system_index = HOMEWORLDS_INVALID_INDEX;
  guint child_target_system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (!homeworlds_backend_find_yellow_sacrifice_move_origin(state, step, &origin_ref)) {
    return FALSE;
  }

  if (!homeworlds_backend_system_for_ref_or_discovery(&state->working_position,
                                                 &step->target_system,
                                                 &target_system,
                                                 &target_system_index) ||
      !homeworlds_backend_system_for_ref_or_discovery(&child_state->working_position,
                                                 &step->target_system,
                                                 &child_target_system,
                                                 &child_target_system_index)) {
    return FALSE;
  }

  if (homeworlds_backend_system_has_catastrophe(&target_system) ||
      homeworlds_backend_system_has_catastrophe(&child_target_system)) {
    return FALSE;
  }

  if (homeworlds_backend_system_refs_equal(&origin_ref, &step->target_system)) {
    return TRUE;
  }

  if (!homeworlds_backend_system_for_ref_or_discovery(&state->working_position,
                                                 &origin_ref,
                                                 &origin_system,
                                                 &origin_system_index)) {
    return FALSE;
  }

  return target_system_index == origin_system_index ||
         homeworlds_system_is_connected(&origin_system, &target_system);
}

static gboolean homeworlds_backend_bank_take(HomeworldsPosition *position, HomeworldsPyramid pyramid) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    if (position->bank[i] != pyramid) {
      continue;
    }

    position->bank[i] = 0;
    return TRUE;
  }

  return FALSE;
}

static gboolean homeworlds_backend_bank_put(HomeworldsPosition *position, HomeworldsPyramid pyramid) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    if (!homeworlds_pyramid_is_unused(position->bank[i])) {
      continue;
    }

    position->bank[i] = pyramid;
    return TRUE;
  }

  return FALSE;
}

static guint homeworlds_backend_bank_pyramid_count(const HomeworldsPosition *position, HomeworldsPyramid pyramid) {
  guint count = 0;

  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), 0);

  for (guint slot = 0; slot < HOMEWORLDS_BANK_SLOT_COUNT; ++slot) {
    count += position->bank[slot] == pyramid;
  }
  return count;
}

static guint homeworlds_backend_system_ship_pyramid_count(const HomeworldsSystem *system,
                                                          guint side,
                                                          HomeworldsPyramid pyramid) {
  guint count = 0;

  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(side < 2, 0);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), 0);

  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    if (!homeworlds_pyramid_is_valid(system->ships[side][slot])) {
      break;
    }
    count += system->ships[side][slot] == pyramid;
  }
  return count;
}

static HomeworldsPyramid homeworlds_backend_trade_target_pyramid(const HomeworldsTurnStep *step) {
  g_return_val_if_fail(step != NULL, 0);
  g_return_val_if_fail(step->kind == HOMEWORLDS_STEP_TRADE, 0);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(step->actor.ship), 0);
  g_return_val_if_fail(step->target_color <= HOMEWORLDS_COLOR_BLUE, 0);

  return homeworlds_pyramid_make((HomeworldsColor) step->target_color, homeworlds_pyramid_size(step->actor.ship));
}

static gint homeworlds_backend_count_before_reversed_trade(guint current_count,
                                                           HomeworldsPyramid counted_pyramid,
                                                           const HomeworldsTurnStep *reversed_step) {
  gint count = (gint) current_count;
  HomeworldsPyramid reversed_source = 0;
  HomeworldsPyramid reversed_target = 0;

  g_return_val_if_fail(homeworlds_pyramid_is_valid(counted_pyramid), -1);
  g_return_val_if_fail(reversed_step != NULL, -1);
  g_return_val_if_fail(reversed_step->kind == HOMEWORLDS_STEP_TRADE, -1);

  reversed_source = reversed_step->actor.ship;
  reversed_target = homeworlds_backend_trade_target_pyramid(reversed_step);
  if (!homeworlds_pyramid_is_valid(reversed_source) || !homeworlds_pyramid_is_valid(reversed_target)) {
    return -1;
  }

  if (counted_pyramid == reversed_source) {
    count++;
  }
  if (counted_pyramid == reversed_target) {
    count--;
  }
  return count;
}

static gint homeworlds_backend_bank_count_before_reversed_trade(const HomeworldsPosition *position,
                                                                HomeworldsPyramid pyramid,
                                                                const HomeworldsTurnStep *reversed_step) {
  gint count = 0;
  HomeworldsPyramid reversed_source = 0;
  HomeworldsPyramid reversed_target = 0;

  g_return_val_if_fail(position != NULL, -1);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), -1);
  g_return_val_if_fail(reversed_step != NULL, -1);
  g_return_val_if_fail(reversed_step->kind == HOMEWORLDS_STEP_TRADE, -1);

  reversed_source = reversed_step->actor.ship;
  reversed_target = homeworlds_backend_trade_target_pyramid(reversed_step);
  if (!homeworlds_pyramid_is_valid(reversed_source) || !homeworlds_pyramid_is_valid(reversed_target)) {
    return -1;
  }

  count = (gint) homeworlds_backend_bank_pyramid_count(position, pyramid);
  if (pyramid == reversed_source) {
    count--;
  }
  if (pyramid == reversed_target) {
    count++;
  }
  return count;
}

static gboolean homeworlds_backend_reverse_trade_step(HomeworldsPosition *position,
                                                      const HomeworldsTurnStep *step) {
  guint system_index = HOMEWORLDS_INVALID_INDEX;
  HomeworldsPyramid traded = 0;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(step->kind == HOMEWORLDS_STEP_TRADE, FALSE);

  if (!homeworlds_pyramid_is_valid(step->actor.ship) ||
      step->target_color > HOMEWORLDS_COLOR_BLUE ||
      !homeworlds_position_resolve_system_ref(position, &step->actor.system, &system_index)) {
    return FALSE;
  }

  traded = homeworlds_pyramid_make((HomeworldsColor) step->target_color,
                                   homeworlds_pyramid_size(step->actor.ship));
  for (guint ship_slot = 0; ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++ship_slot) {
    HomeworldsSystem *system = &position->systems[system_index];
    HomeworldsPyramid ship = system->ships[position->turn][ship_slot];

    if (!homeworlds_pyramid_is_valid(ship)) {
      break;
    }
    if (ship != traded) {
      continue;
    }
    if (!homeworlds_backend_bank_take(position, step->actor.ship) ||
        !homeworlds_backend_bank_put(position, traded)) {
      return FALSE;
    }

    homeworlds_backend_set_ship_slot(system, position->turn, ship_slot, step->actor.ship);
    return TRUE;
  }

  return FALSE;
}

static gboolean homeworlds_backend_reverse_build_step_at_slot(const HomeworldsPosition *position,
                                                              const HomeworldsTurnStep *step,
                                                              guint system_index,
                                                              guint ship_slot,
                                                              HomeworldsPosition *out_before) {
  HomeworldsPosition before = {0};
  HomeworldsPosition reapplied = {0};
  HomeworldsSystem *system = NULL;
  HomeworldsPyramid ship = 0;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(step->kind == HOMEWORLDS_STEP_BUILD, FALSE);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT, FALSE);
  g_return_val_if_fail(out_before != NULL, FALSE);

  before = *position;
  system = &before.systems[system_index];
  ship = system->ships[before.turn][ship_slot];
  if (!homeworlds_pyramid_is_valid(ship) ||
      homeworlds_pyramid_color(ship) != (HomeworldsColor) step->target_color) {
    return FALSE;
  }

  HomeworldsPyramid built = ship;
  homeworlds_backend_set_ship_slot(system, before.turn, ship_slot, 0);
  if (!homeworlds_backend_bank_put(&before, built)) {
    return FALSE;
  }

  reapplied = before;
  if (!homeworlds_position_apply_forced_action_step(&reapplied, step) ||
      !homeworlds_backend_positions_equal(&reapplied, position)) {
    return FALSE;
  }

  *out_before = before;
  return TRUE;
}

static gboolean homeworlds_backend_reverse_forced_action_step(HomeworldsPosition *position,
                                                              const HomeworldsTurnStep *step) {
  guint system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  switch ((HomeworldsStepKind) step->kind) {
    case HOMEWORLDS_STEP_BUILD:
      if (step->target_color > HOMEWORLDS_COLOR_BLUE ||
          !homeworlds_position_resolve_system_ref(position, &step->actor.system, &system_index)) {
        return FALSE;
      }
      for (guint ship_slot = 0; ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++ship_slot) {
        HomeworldsPosition before = {0};
        if (!homeworlds_pyramid_is_valid(position->systems[system_index].ships[position->turn][ship_slot])) {
          break;
        }

        if (!homeworlds_backend_reverse_build_step_at_slot(position, step, system_index, ship_slot, &before)) {
          continue;
        }

        *position = before;
        return TRUE;
      }
      return FALSE;
    case HOMEWORLDS_STEP_TRADE:
      return homeworlds_backend_reverse_trade_step(position, step);
    default:
      return FALSE;
  }
}

static gboolean homeworlds_backend_forced_steps_commute_from_before_previous(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *previous_step,
    const HomeworldsTurnStep *new_step,
    const HomeworldsPosition *before_previous) {
  HomeworldsPosition swapped = {0};

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(previous_step != NULL, FALSE);
  g_return_val_if_fail(new_step != NULL, FALSE);
  g_return_val_if_fail(before_previous != NULL, FALSE);

  if (homeworlds_backend_position_has_catastrophe(before_previous) ||
      homeworlds_backend_position_has_catastrophe(&state->working_position) ||
      homeworlds_backend_position_has_catastrophe(&child_state->working_position)) {
    return FALSE;
  }

  swapped = *before_previous;
  if (!homeworlds_position_apply_forced_action_step(&swapped, new_step) ||
      homeworlds_backend_position_has_catastrophe(&swapped) ||
      !homeworlds_position_apply_forced_action_step(&swapped, previous_step)) {
    return FALSE;
  }

  return homeworlds_backend_positions_equal(&swapped, &child_state->working_position);
}

static gboolean homeworlds_backend_forced_steps_commute_without_catastrophe(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *previous_step,
    const HomeworldsTurnStep *new_step) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(previous_step != NULL, FALSE);
  g_return_val_if_fail(new_step != NULL, FALSE);

  if (previous_step->kind == HOMEWORLDS_STEP_BUILD) {
    guint system_index = HOMEWORLDS_INVALID_INDEX;

    if (previous_step->target_color > HOMEWORLDS_COLOR_BLUE ||
        !homeworlds_position_resolve_system_ref(&state->working_position,
                                                &previous_step->actor.system,
                                                &system_index)) {
      return FALSE;
    }

    for (guint ship_slot = 0; ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++ship_slot) {
      HomeworldsPosition before_previous = {0};
      if (!homeworlds_pyramid_is_valid(state->working_position.systems[system_index]
                                           .ships[state->working_position.turn][ship_slot])) {
        break;
      }

      if (!homeworlds_backend_reverse_build_step_at_slot(&state->working_position,
                                                         previous_step,
                                                         system_index,
                                                         ship_slot,
                                                         &before_previous)) {
        continue;
      }
      if (homeworlds_backend_forced_steps_commute_from_before_previous(state,
                                                                       child_state,
                                                                       previous_step,
                                                                       new_step,
                                                                       &before_previous)) {
        return TRUE;
      }
    }
    return FALSE;
  }

  HomeworldsPosition before_previous = state->working_position;
  if (!homeworlds_backend_reverse_forced_action_step(&before_previous, previous_step)) {
    return FALSE;
  }

  return homeworlds_backend_forced_steps_commute_from_before_previous(state,
                                                                      child_state,
                                                                      previous_step,
                                                                      new_step,
                                                                      &before_previous);
}

static gboolean homeworlds_backend_reverse_build_step(const HomeworldsPosition *position,
                                                      const HomeworldsTurnStep *step,
                                                      HomeworldsPyramid preferred_built,
                                                      HomeworldsPosition *out_before,
                                                      HomeworldsPyramid *out_built) {
  guint system_index = HOMEWORLDS_INVALID_INDEX;
  gboolean found_fallback = FALSE;
  HomeworldsPosition fallback_before = {0};
  HomeworldsPyramid fallback_built = 0;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(step->kind == HOMEWORLDS_STEP_BUILD, FALSE);
  g_return_val_if_fail(preferred_built == 0 || homeworlds_pyramid_is_valid(preferred_built), FALSE);
  g_return_val_if_fail(out_before != NULL, FALSE);
  g_return_val_if_fail(out_built != NULL, FALSE);

  if (step->target_color > HOMEWORLDS_COLOR_BLUE ||
      !homeworlds_position_resolve_system_ref(position, &step->actor.system, &system_index)) {
    return FALSE;
  }

  for (guint ship_slot = 0; ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++ship_slot) {
    HomeworldsPosition before = {0};
    HomeworldsPyramid built = position->systems[system_index].ships[position->turn][ship_slot];

    if (!homeworlds_pyramid_is_valid(built)) {
      break;
    }
    if (!homeworlds_backend_reverse_build_step_at_slot(position, step, system_index, ship_slot, &before)) {
      continue;
    }
    if (preferred_built != 0 && built == preferred_built) {
      *out_before = before;
      *out_built = built;
      return TRUE;
    }
    if (!found_fallback) {
      fallback_before = before;
      fallback_built = built;
      found_fallback = TRUE;
    }
  }

  if (!found_fallback) {
    return FALSE;
  }

  *out_before = fallback_before;
  *out_built = fallback_built;
  return TRUE;
}

static gboolean homeworlds_backend_reverse_sacrifice_step(const HomeworldsPosition *position,
                                                          const HomeworldsTurnStep *step,
                                                          HomeworldsPosition *out_before) {
  guint system_index = HOMEWORLDS_INVALID_INDEX;
  guint side = 0;
  guint ship_count = 0;
  HomeworldsPosition before = {0};
  HomeworldsPosition reapplied = {0};

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(step->kind == HOMEWORLDS_STEP_SACRIFICE, FALSE);
  g_return_val_if_fail(out_before != NULL, FALSE);

  if (!homeworlds_pyramid_is_valid(step->actor.ship) ||
      !homeworlds_position_resolve_system_ref(position, &step->actor.system, &system_index)) {
    return FALSE;
  }

  before = *position;
  if (!homeworlds_backend_bank_take(&before, step->actor.ship)) {
    return FALSE;
  }

  side = before.turn;
  ship_count = homeworlds_system_ship_count_for_side(&before.systems[system_index], side);
  if (ship_count >= HOMEWORLDS_SHIP_SLOT_COUNT ||
      before.systems[system_index].ships[side][ship_count] != 0) {
    return FALSE;
  }
  homeworlds_backend_set_ship_slot(&before.systems[system_index], side, ship_count, step->actor.ship);

  reapplied = before;
  if (!homeworlds_position_apply_turn_step(&reapplied, step) ||
      !homeworlds_backend_positions_equal(&reapplied, position)) {
    return FALSE;
  }

  *out_before = before;
  return TRUE;
}

static gboolean homeworlds_backend_build_step_rebuilds_sacrificed_color_at_source(
    const HomeworldsPosition *position_after_step,
    const HomeworldsTurnStep *step,
    guint sacrifice_system_index,
    HomeworldsPyramid sacrificed_ship,
    HomeworldsPosition *out_before) {
  guint step_system_index = HOMEWORLDS_INVALID_INDEX;
  HomeworldsPosition before = {0};
  HomeworldsPyramid built = 0;

  g_return_val_if_fail(position_after_step != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(step->kind == HOMEWORLDS_STEP_BUILD, FALSE);
  g_return_val_if_fail(sacrifice_system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(sacrificed_ship), FALSE);

  if (step->target_color != homeworlds_pyramid_color(sacrificed_ship) ||
      !homeworlds_position_resolve_system_ref(position_after_step, &step->actor.system, &step_system_index) ||
      step_system_index != sacrifice_system_index ||
      !homeworlds_backend_reverse_build_step(position_after_step, step, sacrificed_ship, &before, &built)) {
    return FALSE;
  }

  if (out_before != NULL) {
    *out_before = before;
  }
  return TRUE;
}

static gboolean homeworlds_backend_build_step_was_available_before_sacrifice(
    const HomeworldsPosition *before_sacrifice,
    const HomeworldsTurnStep *step) {
  HomeworldsPosition alternative = {0};

  g_return_val_if_fail(before_sacrifice != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(step->kind == HOMEWORLDS_STEP_BUILD, FALSE);

  if (step->target_color > HOMEWORLDS_COLOR_BLUE) {
    return FALSE;
  }

  alternative = *before_sacrifice;
  return homeworlds_position_apply_turn_step(&alternative, step);
}

static gboolean homeworlds_backend_step_is_redundant_green_medium_sacrifice_rebuild(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *step) {
  const HomeworldsMove *move = NULL;
  const HomeworldsTurnStep *sacrifice_step = NULL;
  const HomeworldsTurnStep *previous_build = NULL;
  const HomeworldsTurnStep *current_build = NULL;
  guint sacrifice_step_index = HOMEWORLDS_INVALID_INDEX;
  guint sacrifice_system_index = HOMEWORLDS_INVALID_INDEX;
  guint build_count = 0;
  HomeworldsPosition after_sacrifice = {0};
  HomeworldsPosition before_sacrifice = {0};
  HomeworldsPyramid previous_built = 0;
  gboolean previous_rebuild = FALSE;
  gboolean current_rebuild = FALSE;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_BUILD ||
      state->pending_actions_remaining != 1 ||
      child_state->pending_actions_remaining != 0 ||
      state->forced_action_color != HOMEWORLDS_COLOR_GREEN ||
      child_state->move.step_count < 3) {
    return FALSE;
  }

  move = &child_state->move;
  for (guint i = 0; i < move->step_count; ++i) {
    const HomeworldsTurnStep *candidate = &move->steps[i];

    if (candidate->kind == HOMEWORLDS_STEP_SACRIFICE &&
        homeworlds_pyramid_is_valid(candidate->actor.ship) &&
        homeworlds_pyramid_color(candidate->actor.ship) == HOMEWORLDS_COLOR_GREEN &&
        homeworlds_pyramid_size(candidate->actor.ship) == HOMEWORLDS_SIZE_MEDIUM) {
      sacrifice_step_index = i;
      sacrifice_step = candidate;
    }
  }
  if (sacrifice_step == NULL) {
    return FALSE;
  }

  for (guint i = sacrifice_step_index + 1; i < move->step_count; ++i) {
    if (move->steps[i].kind != HOMEWORLDS_STEP_BUILD || build_count >= 2) {
      return FALSE;
    }
    if (build_count == 0) {
      previous_build = &move->steps[i];
    } else {
      current_build = &move->steps[i];
    }
    build_count++;
  }
  if (build_count != 2 || current_build != step || previous_build == NULL) {
    return FALSE;
  }

  if (!homeworlds_position_resolve_system_ref(&state->working_position,
                                              &sacrifice_step->actor.system,
                                              &sacrifice_system_index)) {
    return FALSE;
  }

  previous_rebuild =
      homeworlds_backend_build_step_rebuilds_sacrificed_color_at_source(&state->working_position,
                                                                        previous_build,
                                                                        sacrifice_system_index,
                                                                        sacrifice_step->actor.ship,
                                                                        &after_sacrifice);
  if (!previous_rebuild &&
      !homeworlds_backend_reverse_build_step(&state->working_position,
                                             previous_build,
                                             0,
                                             &after_sacrifice,
                                             &previous_built)) {
    return FALSE;
  }
  if (!homeworlds_backend_reverse_sacrifice_step(&after_sacrifice, sacrifice_step, &before_sacrifice)) {
    return FALSE;
  }

  current_rebuild =
      homeworlds_backend_build_step_rebuilds_sacrificed_color_at_source(&child_state->working_position,
                                                                        current_build,
                                                                        sacrifice_system_index,
                                                                        sacrifice_step->actor.ship,
                                                                        NULL);

  return (previous_rebuild &&
          homeworlds_backend_build_step_was_available_before_sacrifice(&before_sacrifice, current_build)) ||
         (current_rebuild &&
          homeworlds_backend_build_step_was_available_before_sacrifice(&before_sacrifice, previous_build));
}

static gint homeworlds_backend_system_color_count_before_trade(const HomeworldsPosition *position,
                                                               guint system_index,
                                                               HomeworldsColor color,
                                                               guint reversed_system_index,
                                                               const HomeworldsTurnStep *reversed_step) {
  gint count = 0;

  g_return_val_if_fail(position != NULL, -1);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, -1);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, -1);
  g_return_val_if_fail(reversed_system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, -1);
  g_return_val_if_fail(reversed_step != NULL, -1);
  g_return_val_if_fail(reversed_step->kind == HOMEWORLDS_STEP_TRADE, -1);

  count = (gint) homeworlds_system_color_count(&position->systems[system_index], color);
  if (system_index != reversed_system_index) {
    return count;
  }

  if (homeworlds_pyramid_color(reversed_step->actor.ship) == color) {
    count++;
  }
  if ((HomeworldsColor) reversed_step->target_color == color) {
    count--;
  }
  return count;
}

static gint homeworlds_backend_system_ship_count_before_trade(const HomeworldsPosition *position,
                                                              guint system_index,
                                                              guint side,
                                                              HomeworldsPyramid pyramid,
                                                              guint reversed_system_index,
                                                              const HomeworldsTurnStep *reversed_step) {
  gint count = 0;

  g_return_val_if_fail(position != NULL, -1);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, -1);
  g_return_val_if_fail(side < 2, -1);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), -1);
  g_return_val_if_fail(reversed_system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, -1);
  g_return_val_if_fail(reversed_step != NULL, -1);
  g_return_val_if_fail(reversed_step->kind == HOMEWORLDS_STEP_TRADE, -1);

  count = (gint) homeworlds_backend_system_ship_pyramid_count(&position->systems[system_index], side, pyramid);
  if (system_index != reversed_system_index) {
    return count;
  }

  return homeworlds_backend_count_before_reversed_trade((guint) count, pyramid, reversed_step);
}

static gboolean homeworlds_backend_trade_step_is_well_formed(const HomeworldsTurnStep *step) {
  g_return_val_if_fail(step != NULL, FALSE);

  return step->kind == HOMEWORLDS_STEP_TRADE &&
         homeworlds_pyramid_is_valid(step->actor.ship) &&
         step->target_color <= HOMEWORLDS_COLOR_BLUE &&
         homeworlds_pyramid_color(step->actor.ship) != (HomeworldsColor) step->target_color;
}

static gboolean homeworlds_backend_trade_pair_has_no_fast_catastrophe(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *previous_step,
    const HomeworldsTurnStep *new_step,
    guint previous_system_index,
    guint new_system_index) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(previous_step != NULL, FALSE);
  g_return_val_if_fail(new_step != NULL, FALSE);
  g_return_val_if_fail(previous_system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(new_system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);

  if (homeworlds_backend_position_has_catastrophe(&state->working_position) ||
      homeworlds_backend_position_has_catastrophe(&child_state->working_position)) {
    return FALSE;
  }

  for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
    gint before_count = homeworlds_backend_system_color_count_before_trade(&state->working_position,
                                                                           previous_system_index,
                                                                           (HomeworldsColor) color,
                                                                           previous_system_index,
                                                                           previous_step);
    if (before_count < 0 || before_count >= 4) {
      return FALSE;
    }
  }

  for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
    gint swapped_count = homeworlds_backend_system_color_count_before_trade(&state->working_position,
                                                                            new_system_index,
                                                                            (HomeworldsColor) color,
                                                                            previous_system_index,
                                                                            previous_step);

    if (homeworlds_pyramid_color(new_step->actor.ship) == (HomeworldsColor) color) {
      swapped_count--;
    }
    if ((HomeworldsColor) new_step->target_color == (HomeworldsColor) color) {
      swapped_count++;
    }
    if (swapped_count < 0 || swapped_count >= 4) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean homeworlds_backend_trade_steps_commute_locally(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *previous_step,
    const HomeworldsTurnStep *new_step) {
  guint side = 0;
  guint previous_system_index = HOMEWORLDS_INVALID_INDEX;
  guint new_system_index = HOMEWORLDS_INVALID_INDEX;
  HomeworldsPyramid previous_source = 0;
  HomeworldsPyramid previous_target = 0;
  HomeworldsPyramid new_source = 0;
  HomeworldsPyramid new_target = 0;
  gint new_source_count_before_previous = 0;
  gint new_target_bank_count_before_previous = 0;
  gint previous_source_count_after_new_first = 0;
  gint previous_target_bank_count_after_new_first = 0;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(previous_step != NULL, FALSE);
  g_return_val_if_fail(new_step != NULL, FALSE);

  if (!homeworlds_backend_trade_step_is_well_formed(previous_step) ||
      !homeworlds_backend_trade_step_is_well_formed(new_step) ||
      !homeworlds_position_resolve_system_ref(&state->working_position,
                                              &previous_step->actor.system,
                                              &previous_system_index) ||
      !homeworlds_position_resolve_system_ref(&state->working_position,
                                              &new_step->actor.system,
                                              &new_system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  previous_source = previous_step->actor.ship;
  previous_target = homeworlds_backend_trade_target_pyramid(previous_step);
  new_source = new_step->actor.ship;
  new_target = homeworlds_backend_trade_target_pyramid(new_step);
  if (!homeworlds_pyramid_is_valid(previous_target) || !homeworlds_pyramid_is_valid(new_target)) {
    return FALSE;
  }

  if (!homeworlds_backend_trade_pair_has_no_fast_catastrophe(state,
                                                             child_state,
                                                             previous_step,
                                                             new_step,
                                                             previous_system_index,
                                                             new_system_index)) {
    return FALSE;
  }

  new_source_count_before_previous =
      homeworlds_backend_system_ship_count_before_trade(&state->working_position,
                                                        new_system_index,
                                                        side,
                                                        new_source,
                                                        previous_system_index,
                                                        previous_step);
  if (new_source_count_before_previous < 1) {
    return FALSE;
  }

  new_target_bank_count_before_previous =
      homeworlds_backend_bank_count_before_reversed_trade(&state->working_position, new_target, previous_step);
  if (new_target_bank_count_before_previous < 1) {
    return FALSE;
  }

  previous_source_count_after_new_first =
      homeworlds_backend_system_ship_count_before_trade(&state->working_position,
                                                        previous_system_index,
                                                        side,
                                                        previous_source,
                                                        previous_system_index,
                                                        previous_step);
  if (new_system_index == previous_system_index) {
    if (new_source == previous_source) {
      previous_source_count_after_new_first--;
    }
    if (new_target == previous_source) {
      previous_source_count_after_new_first++;
    }
  }
  if (previous_source_count_after_new_first < 1) {
    return FALSE;
  }

  previous_target_bank_count_after_new_first =
      homeworlds_backend_bank_count_before_reversed_trade(&state->working_position, previous_target, previous_step);
  if (new_target == previous_target) {
    previous_target_bank_count_after_new_first--;
  }
  if (new_source == previous_target) {
    previous_target_bank_count_after_new_first++;
  }
  return previous_target_bank_count_after_new_first >= 1;
}

static gboolean homeworlds_backend_step_is_redundant_commutative_blue_trade(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *step) {
  const HomeworldsTurnStep *previous_step = NULL;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_TRADE ||
      state->pending_actions_remaining == 0 ||
      state->forced_action_color != HOMEWORLDS_COLOR_BLUE ||
      state->move.step_count == 0) {
    return FALSE;
  }

  previous_step = &state->move.steps[state->move.step_count - 1];
  if (previous_step->kind != HOMEWORLDS_STEP_TRADE ||
      homeworlds_backend_compare_trade_steps(previous_step, step) <= 0) {
    return FALSE;
  }

  return homeworlds_backend_trade_steps_commute_locally(state, child_state, previous_step, step);
}

static gboolean homeworlds_backend_step_is_redundant_commutative_green_build(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *step) {
  const HomeworldsTurnStep *previous_step = NULL;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_BUILD ||
      state->pending_actions_remaining == 0 ||
      state->forced_action_color != HOMEWORLDS_COLOR_GREEN ||
      state->move.step_count == 0) {
    return FALSE;
  }

  previous_step = &state->move.steps[state->move.step_count - 1];
  if (previous_step->kind != HOMEWORLDS_STEP_BUILD ||
      homeworlds_backend_compare_build_steps(previous_step, step) <= 0) {
    return FALSE;
  }

  return homeworlds_backend_forced_steps_commute_without_catastrophe(state, child_state, previous_step, step);
}

static gboolean homeworlds_backend_child_state_is_good_after_step(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state) {
  const HomeworldsTurnStep *step = NULL;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);

  step = homeworlds_backend_appended_step(state, child_state);
  if (step == NULL) {
    return TRUE;
  }

  return !homeworlds_backend_step_removes_last_homeworld_ship(state, step) &&
         !homeworlds_backend_step_is_redundant_small_sacrifice(state, step) &&
         !homeworlds_backend_step_creates_unfavorable_build_catastrophe(state, child_state, step) &&
         !homeworlds_backend_step_creates_unfavorable_trade_catastrophe(state, child_state, step) &&
         !homeworlds_backend_step_enters_unfavorable_catastrophe(state, child_state, step) &&
         !homeworlds_backend_step_is_redundant_yellow_sacrifice_hop(state, child_state, step) &&
         !homeworlds_backend_step_is_redundant_commutative_blue_trade(state, child_state, step) &&
         !homeworlds_backend_step_is_redundant_commutative_green_build(state, child_state, step) &&
         !homeworlds_backend_step_is_redundant_green_medium_sacrifice_rebuild(state, child_state, step);
}

static gboolean homeworlds_backend_move_has_pass(const HomeworldsMove *move) {
  g_return_val_if_fail(move != NULL, FALSE);

  if (move->kind != HOMEWORLDS_MOVE_KIND_TURN) {
    return FALSE;
  }

  for (guint i = 0; i < move->step_count; ++i) {
    if (move->steps[i].kind == HOMEWORLDS_STEP_PASS) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_backend_move_is_good(const HomeworldsMoveBuilderState *state,
                                                const HomeworldsMove *move,
                                                gboolean allow_pass) {
  gboolean move_has_pass = FALSE;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (move->kind == HOMEWORLDS_MOVE_KIND_SETUP) {
    return homeworlds_backend_setup_move_is_good(state, move);
  }

  move_has_pass = homeworlds_backend_move_has_pass(move);
  if (homeworlds_backend_position_is_initial_turn(&state->working_position) &&
      !(allow_pass && move_has_pass) &&
      (move->step_count != 1 || move->steps[0].kind != HOMEWORLDS_STEP_BUILD)) {
    return FALSE;
  }

  if (move_has_pass && !allow_pass) {
    return FALSE;
  }

  return TRUE;
}

static gboolean homeworlds_backend_candidate_is_pass(const HomeworldsMoveCandidate *candidate) {
  g_return_val_if_fail(candidate != NULL, FALSE);

  return candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION &&
         candidate->data.target_color == HOMEWORLDS_STEP_PASS;
}

static gboolean homeworlds_backend_state_can_use_pass_fallback(const HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(state != NULL, FALSE);

  if (state->pending_actions_remaining > 0) {
    return FALSE;
  }

  for (guint i = 0; i < state->move.step_count; ++i) {
    if (state->move.steps[i].kind != HOMEWORLDS_STEP_CATASTROPHE) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean homeworlds_backend_state_is_catastrophe_boundary(const HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(state != NULL, FALSE);

  return state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP ||
         state->stage == HOMEWORLDS_BUILDER_STAGE_COMPLETE;
}

static guint homeworlds_backend_collect_profitable_catastrophes(const HomeworldsMoveBuilderState *state,
                                                                HomeworldsProfitableCatastrophe *out_catastrophes,
                                                                guint max_catastrophes) {
  guint count = 0;
  guint side = 0;
  guint opponent = 0;

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(out_catastrophes != NULL || max_catastrophes == 0, 0);

  if (state->working_position.phase != HOMEWORLDS_PHASE_PLAY ||
      state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP) {
    return 0;
  }

  side = state->working_position.turn;
  opponent = side == 0 ? 1 : 0;
  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];

    for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
      guint own_pips = 0;
      guint opponent_pips = 0;

      if (homeworlds_system_color_count(system, (HomeworldsColor) color) < 4) {
        continue;
      }

      own_pips = homeworlds_backend_system_ship_pips_for_color(system, (HomeworldsColor) color, side);
      opponent_pips = homeworlds_backend_system_ship_pips_for_color(system, (HomeworldsColor) color, opponent);
      if (opponent_pips <= own_pips) {
        continue;
      }

      if (count < max_catastrophes) {
        HomeworldsSystemRef system_ref = {0};

        if (!homeworlds_position_system_ref_for_index(&state->working_position, system_index, &system_ref)) {
          continue;
        }
        out_catastrophes[count] = (HomeworldsProfitableCatastrophe){
          .system_index = system_index,
          .color = (HomeworldsColor) color,
          .system_ref = system_ref,
        };
      }
      count++;
    }
  }

  return MIN(count, max_catastrophes);
}

static gboolean homeworlds_backend_apply_profitable_catastrophe(HomeworldsMoveBuilderState *state,
                                                                const HomeworldsProfitableCatastrophe *catastrophe) {
  GameBackendMoveBuilder builder = {0};
  HomeworldsTurnStep step = {
    .kind = HOMEWORLDS_STEP_CATASTROPHE,
    .target_color = catastrophe->color,
  };

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(catastrophe != NULL, FALSE);
  g_return_val_if_fail(catastrophe->system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);

  builder.builder_state = state;
  builder.builder_state_size = sizeof(*state);
  if (state->stage != HOMEWORLDS_BUILDER_STAGE_COMPLETE) {
    return homeworlds_move_builder_apply_catastrophe(&builder, catastrophe->system_index, catastrophe->color);
  }

  if (state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS ||
      !homeworlds_position_system_ref_for_index(&state->working_position,
                                                catastrophe->system_index,
                                                &step.target_system)) {
    return FALSE;
  }

  state->move.steps[state->move.step_count++] = step;
  if (!homeworlds_position_apply_turn_step(&state->working_position, &step)) {
    state->move.step_count--;
    return FALSE;
  }
  return TRUE;
}

static gboolean homeworlds_backend_collect_good_moves_recursive(const HomeworldsMoveBuilderState *state,
                                                                const HomeworldsGoodMoveContext *context,
                                                                HomeworldsMoveBuffer *buffer,
                                                                gboolean allow_pass_move) {
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  HomeworldsProfitableCatastrophe catastrophes[HOMEWORLDS_SYSTEM_SLOT_COUNT * 4] = {0};
  guint catastrophe_count = 0;
  gboolean forced_catastrophe_seen = FALSE;
  const HomeworldsMoveCandidate *pass_candidate = NULL;
  gsize good_leaves_before_candidates = 0;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);

  builder.builder_state = (gpointer) state;
  builder.builder_state_size = sizeof(*state);

  if (homeworlds_backend_state_is_catastrophe_boundary(state)) {
    catastrophe_count =
        homeworlds_backend_collect_profitable_catastrophes(state, catastrophes, G_N_ELEMENTS(catastrophes));
  }
  if (catastrophe_count > 0) {
    for (guint i = 0; i < catastrophe_count; ++i) {
      HomeworldsMoveBuilderState child_state = *state;

      if (homeworlds_backend_catastrophe_is_root_required(context, &catastrophes[i])) {
        continue;
      }
      forced_catastrophe_seen = TRUE;
      if (!homeworlds_backend_apply_profitable_catastrophe(&child_state, &catastrophes[i])) {
        continue;
      }
      if (!homeworlds_backend_collect_good_moves_recursive(&child_state, context, buffer, allow_pass_move)) {
        return FALSE;
      }
    }

    if (forced_catastrophe_seen) {
      return TRUE;
    }
  }

  for (guint i = 0; i < catastrophe_count; ++i) {
    HomeworldsMoveBuilderState child_state = *state;

    if (!homeworlds_backend_catastrophe_is_root_required(context, &catastrophes[i]) ||
        homeworlds_backend_move_has_profitable_catastrophe(&state->move, &catastrophes[i]) ||
        !homeworlds_backend_apply_profitable_catastrophe(&child_state, &catastrophes[i])) {
      continue;
    }
    if (!homeworlds_backend_collect_good_moves_recursive(&child_state, context, buffer, allow_pass_move)) {
      return FALSE;
    }
  }

  if (homeworlds_move_builder_is_complete(&builder)) {
    HomeworldsMove move = {0};

    if (!homeworlds_move_builder_build_move(&builder, &move)) {
      return FALSE;
    }
    if (!homeworlds_backend_move_is_good(state, &move, allow_pass_move)) {
      return TRUE;
    }
    if (!homeworlds_backend_move_satisfies_root_catastrophe_requirement(&move, context)) {
      return TRUE;
    }

    return homeworlds_backend_move_buffer_append(buffer, &move);
  }

  candidates = homeworlds_move_builder_list_candidates(&builder);
  good_leaves_before_candidates = buffer->leaves_seen;
  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates.moves)[i];
    HomeworldsMoveBuilderState child_state = *state;
    GameBackendMoveBuilder child = {
      .builder_state = &child_state,
      .builder_state_size = sizeof(child_state),
    };

    if (candidate != NULL && homeworlds_backend_candidate_is_pass(candidate)) {
      pass_candidate = candidate;
      continue;
    }

    if (candidate == NULL ||
        !homeworlds_move_builder_step(&child, candidate) ||
        !homeworlds_backend_child_state_is_good_after_step(state, &child_state)) {
      continue;
    }
    if (!homeworlds_backend_collect_good_moves_recursive(&child_state, context, buffer, allow_pass_move)) {
      homeworlds_backend_move_list_free(&candidates);
      return FALSE;
    }
  }

  if (pass_candidate != NULL &&
      buffer->leaves_seen == good_leaves_before_candidates &&
      homeworlds_backend_state_can_use_pass_fallback(state)) {
    HomeworldsMoveBuilderState child_state = *state;
    GameBackendMoveBuilder child = {
      .builder_state = &child_state,
      .builder_state_size = sizeof(child_state),
    };

    if (homeworlds_move_builder_step(&child, pass_candidate) &&
        homeworlds_backend_child_state_is_good_after_step(state, &child_state) &&
        !homeworlds_backend_collect_good_moves_recursive(&child_state, context, buffer, TRUE)) {
      homeworlds_backend_move_list_free(&candidates);
      return FALSE;
    }
  }

  homeworlds_backend_move_list_free(&candidates);
  return TRUE;
}

static gboolean homeworlds_backend_score_after_move(const HomeworldsPosition *position,
                                                    const HomeworldsMove *move,
                                                    gint *out_score) {
  HomeworldsPosition child = {0};
  GameBackendOutcome outcome = GAME_BACKEND_OUTCOME_ONGOING;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);

  homeworlds_position_copy(&child, position);
  if (!homeworlds_position_apply_move(&child, move)) {
    g_debug("Skipping invalid Homeworlds move while static-pruning good_moves()");
    homeworlds_position_clear(&child);
    return FALSE;
  }

  outcome = homeworlds_position_outcome(&child);
  *out_score = outcome == GAME_BACKEND_OUTCOME_ONGOING
      ? homeworlds_position_evaluate_static(&child)
      : homeworlds_position_terminal_score(outcome, 1);
  homeworlds_position_clear(&child);
  return TRUE;
}

static gboolean homeworlds_backend_score_is_inside_prune_window(guint side, gint score, gint best_score) {
  g_return_val_if_fail(side < 2, FALSE);

  if (side == 0) {
    return score >= best_score - HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_WINDOW;
  }
  return score <= best_score + HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_WINDOW;
}

static void homeworlds_backend_trace_good_moves(guint depth_hint,
                                                guint side,
                                                gsize generated_leaves,
                                                gsize scored_moves,
                                                gsize kept_moves) {
  if (homeworlds_backend_good_move_trace_func == NULL) {
    return;
  }

  HomeworldsGoodMoveTrace trace = {
    .depth_hint = depth_hint,
    .side = side,
    .generated_leaves = generated_leaves,
    .scored_moves = scored_moves,
    .kept_moves = kept_moves,
  };

  homeworlds_backend_good_move_trace_func(&trace, homeworlds_backend_good_move_trace_user_data);
}

static GameBackendMoveList homeworlds_backend_list_good_moves(gconstpointer position, guint depth_hint) {
  const HomeworldsPosition *homeworlds_position = position;
  GameBackendMoveBuilder builder = {0};
  HomeworldsGoodMoveContext context = {0};
  HomeworldsMoveBuffer buffer = {0};
  HomeworldsMove *moves = NULL;
  gsize count = 0;

  g_return_val_if_fail(homeworlds_position != NULL, (GameBackendMoveList){0});

  homeworlds_backend_move_buffer_init(&buffer, homeworlds_position);
  if (!homeworlds_move_builder_init(homeworlds_position, &builder)) {
    homeworlds_backend_move_buffer_clear(&buffer);
    return (GameBackendMoveList){0};
  }
  context.root_catastrophe_count = homeworlds_backend_collect_profitable_catastrophes(
      builder.builder_state,
      context.root_catastrophes,
      G_N_ELEMENTS(context.root_catastrophes));
  if (!homeworlds_backend_collect_good_moves_recursive(builder.builder_state, &context, &buffer, FALSE)) {
    homeworlds_move_builder_clear(&builder);
    homeworlds_backend_move_buffer_clear(&buffer);
    return (GameBackendMoveList){0};
  }

  count = buffer.count;
  moves = homeworlds_backend_move_buffer_copy_moves(&buffer);
  homeworlds_backend_trace_good_moves(depth_hint,
                                      homeworlds_position->turn,
                                      buffer.leaves_seen,
                                      buffer.scored_moves,
                                      count);
  homeworlds_backend_maybe_dump_large_move_position(homeworlds_position, depth_hint, buffer.leaves_seen);

  homeworlds_move_builder_clear(&builder);
  homeworlds_backend_move_buffer_clear(&buffer);
  return (GameBackendMoveList){
    .moves = moves,
    .count = count,
  };
}

static gboolean homeworlds_backend_apply_move(gpointer position, gconstpointer move) {
  HomeworldsPosition *homeworlds_position = position;
  const HomeworldsMove *homeworlds_move = move;

  g_return_val_if_fail(homeworlds_position != NULL, FALSE);
  g_return_val_if_fail(homeworlds_move != NULL, FALSE);

  return homeworlds_position_apply_move(homeworlds_position, homeworlds_move);
}

static gint homeworlds_backend_evaluate_static(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, 0);

  return homeworlds_position_evaluate_static(homeworlds_position);
}

static gint homeworlds_backend_terminal_score(gconstpointer position, GameBackendOutcome outcome, guint ply_depth) {
  g_return_val_if_fail(position != NULL, 0);

  return homeworlds_position_terminal_score(outcome, ply_depth);
}

static guint64 homeworlds_backend_hash_position(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, 0);

  return homeworlds_position_hash(homeworlds_position);
}

static gboolean homeworlds_backend_format_move(gconstpointer move, char *buffer, gsize size) {
  const HomeworldsMove *homeworlds_move = move;

  g_return_val_if_fail(homeworlds_move != NULL, FALSE);

  return homeworlds_move_format(homeworlds_move, buffer, size);
}

static gboolean homeworlds_backend_parse_move(const char *notation, gpointer out_move) {
  HomeworldsMove *homeworlds_move = out_move;

  g_return_val_if_fail(notation != NULL, FALSE);
  g_return_val_if_fail(homeworlds_move != NULL, FALSE);

  return homeworlds_move_parse(notation, homeworlds_move);
}

const GameBackend homeworlds_game_backend = {
  .id = "homeworlds",
  .display_name = "Homeworlds",
  .variant_count = 0,
  .position_size = sizeof(HomeworldsPosition),
  .move_size = sizeof(HomeworldsMove),
  .supports_move_list = FALSE,
  .supports_move_builder = TRUE,
  .supports_ai_search = TRUE,
  .side_label = homeworlds_backend_side_label,
  .sgf_color_for_side = homeworlds_backend_sgf_color_for_side,
  .outcome_banner_text = homeworlds_backend_outcome_banner_text,
  .position_init = homeworlds_backend_position_init,
  .position_clear = homeworlds_backend_position_clear,
  .position_copy = homeworlds_backend_position_copy,
  .position_outcome = homeworlds_backend_position_outcome,
  .position_turn = homeworlds_backend_position_turn,
  .list_good_moves = homeworlds_backend_list_good_moves,
  .move_list_free = homeworlds_backend_move_list_free,
  .move_list_get = homeworlds_backend_move_list_get,
  .moves_equal = homeworlds_backend_moves_equal,
  .move_builder_init = (gboolean (*)(gconstpointer, GameBackendMoveBuilder *)) homeworlds_move_builder_init,
  .move_builder_clear = homeworlds_move_builder_clear,
  .move_builder_list_candidates = (GameBackendMoveList (*)(const GameBackendMoveBuilder *))
      homeworlds_move_builder_list_candidates,
  .move_builder_step = (gboolean (*)(GameBackendMoveBuilder *, gconstpointer)) homeworlds_move_builder_step,
  .move_builder_is_complete = (gboolean (*)(const GameBackendMoveBuilder *)) homeworlds_move_builder_is_complete,
  .move_builder_build_move = (gboolean (*)(const GameBackendMoveBuilder *, gpointer))
      homeworlds_move_builder_build_move,
  .apply_move = homeworlds_backend_apply_move,
  .evaluate_static = homeworlds_backend_evaluate_static,
  .terminal_score = homeworlds_backend_terminal_score,
  .hash_position = homeworlds_backend_hash_position,
  .format_move = homeworlds_backend_format_move,
  .parse_move = homeworlds_backend_parse_move,
  .sgf_apply_setup_node = homeworlds_sgf_position_apply_setup_node,
  .sgf_write_position_node = homeworlds_sgf_position_write_position_node,
  .supports_square_grid_board = FALSE,
};
