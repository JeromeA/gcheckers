/*
 * This crash reproducer embeds copies of src/game_app_profile.c, src/game_model.c,
 * src/games/homeworlds/homeworlds_backend.c, and src/window.c below. Do not link those original source files again
 * or the reproducer stops proving that it is self-contained. Do not remove this comment when simplifying bug.c.
 */
#include <gtk/gtk.h>

/* Begin copied file: src/game_app_profile.c */
#include "game_app_profile.h"

#include "games/homeworlds/homeworlds_backend.h"
#include "games/homeworlds/homeworlds_view.h"
#include "player_controls_panel.h"

static const GGameAppProfile homeworlds_app_profile = {
  .kind = GGAME_APP_KIND_HOMEWORLDS,
  .id = "homeworlds",
  .app_id = "io.github.jeromea.ghomeworlds",
  .display_name = "Homeworlds",
  .window_title_name = "ghomeworlds",
  .settings_schema_id = NULL,
  .backend = &homeworlds_game_backend,
  .features =
      {
          .supports_ai_players = FALSE,
          .supports_analysis = FALSE,
          .supports_puzzles = FALSE,
          .supports_import = FALSE,
          .supports_settings = FALSE,
          .supports_save_position = FALSE,
          .supports_edit_mode = FALSE,
      },
  .import =
      {
          .board_game_arena_game_id = 1515,
          .show_site_step = FALSE,
      },
  .ui =
      {
          .create_board_host = homeworlds_view_create_board_host,
          .sync_board_host_node = homeworlds_view_sync_board_host_node,
          .set_move_report_enabled = homeworlds_view_set_board_host_move_report_enabled,
      },
  .layout =
      {
          .default_board_panel_width = 960,
          .minimum_board_panel_width = 760,
          .default_navigation_panel_width = 300,
          .default_analysis_panel_width = 300,
          .show_navigation_drawer_by_default = TRUE,
          .show_analysis_drawer_by_default = FALSE,
      },
  .default_computer_depth = PLAYER_COMPUTER_DEPTH_MIN,
};

static const GGameAppProfile *const ggame_app_profiles[] = {
  &homeworlds_app_profile,
};

static const GGameAppProfile *active_app_profile = &homeworlds_app_profile;

const GGameAppProfile *ggame_app_profile_lookup_by_id(const char *id) {
  g_return_val_if_fail(id != NULL, NULL);

  for (guint i = 0; i < G_N_ELEMENTS(ggame_app_profiles); ++i) {
    if (g_strcmp0(ggame_app_profiles[i]->id, id) == 0) {
      return ggame_app_profiles[i];
    }
  }

  g_debug("Unknown app profile id %s", id);
  return NULL;
}

const GGameAppProfile *ggame_active_app_profile(void) {
  return active_app_profile;
}

gboolean ggame_app_profile_supports_puzzle_catalog(const GGameAppProfile *profile) {
  g_return_val_if_fail(profile != NULL, FALSE);

  return profile->features.supports_puzzles &&
         profile->backend != NULL &&
         profile->backend->id != NULL &&
         (profile->backend->variant_count == 0 || profile->backend->variant_at != NULL);
}
/* End copied file: src/game_app_profile.c */

/* Begin copied file: src/game_model.c */
#include "game_model.h"

#include <glib.h>

struct _GGameModel {
  GObject parent_instance;
  const GameBackend *backend;
  gpointer position;
  const GameBackendVariant *variant;
};

G_DEFINE_TYPE(GGameModel, ggame_model, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_BACKEND,
  PROP_LAST,
};

enum {
  SIGNAL_STATE_CHANGED,
  SIGNAL_LAST,
};

static guint model_signals[SIGNAL_LAST] = {0};
static GParamSpec *properties[PROP_LAST] = {0};

static const GameBackendVariant *ggame_model_pick_initial_variant(const GameBackend *backend) {
  g_return_val_if_fail(backend != NULL, NULL);

  if (backend->variant_count == 0) {
    return NULL;
  }

  g_return_val_if_fail(backend->variant_at != NULL, NULL);

  return backend->variant_at(0);
}

static void ggame_model_emit_state_changed(GGameModel *self) {
  g_return_if_fail(GGAME_IS_MODEL(self));

  g_signal_emit(self, model_signals[SIGNAL_STATE_CHANGED], 0);
}

static void ggame_model_finalize(GObject *object) {
  GGameModel *self = GGAME_MODEL(object);

  if (self->position != NULL && self->backend != NULL && self->backend->position_clear != NULL) {
    self->backend->position_clear(self->position);
  }
  g_clear_pointer(&self->position, g_free);

  G_OBJECT_CLASS(ggame_model_parent_class)->finalize(object);
}

static void ggame_model_constructed(GObject *object) {
  GGameModel *self = GGAME_MODEL(object);
  gboolean has_move_list_api = FALSE;
  gboolean has_move_builder_api = FALSE;
  gboolean has_good_move_api = FALSE;

  G_OBJECT_CLASS(ggame_model_parent_class)->constructed(object);

  g_return_if_fail(self->backend != NULL);
  g_return_if_fail(self->backend->position_size > 0);
  g_return_if_fail(self->backend->position_init != NULL);
  g_return_if_fail(self->backend->position_clear != NULL);
  g_return_if_fail(self->backend->apply_move != NULL);

  has_move_list_api = self->backend->supports_move_list &&
                      self->backend->list_moves != NULL &&
                      self->backend->move_list_free != NULL &&
                      self->backend->move_list_get != NULL &&
                      self->backend->moves_equal != NULL;
  has_move_builder_api = self->backend->supports_move_builder &&
                         self->backend->move_list_free != NULL &&
                         self->backend->move_list_get != NULL &&
                         self->backend->moves_equal != NULL &&
                         self->backend->move_builder_init != NULL &&
                         self->backend->move_builder_clear != NULL &&
                         self->backend->move_builder_list_candidates != NULL &&
                         self->backend->move_builder_step != NULL &&
                         self->backend->move_builder_is_complete != NULL &&
                         self->backend->move_builder_build_move != NULL;
  has_good_move_api = self->backend->list_good_moves != NULL &&
                      self->backend->move_list_free != NULL &&
                      self->backend->move_list_get != NULL &&
                      self->backend->moves_equal != NULL;
  g_return_if_fail(has_move_list_api || has_move_builder_api);
  if (self->backend->supports_ai_search) {
    g_return_if_fail(has_good_move_api);
    g_return_if_fail(self->backend->evaluate_static != NULL);
    g_return_if_fail(self->backend->terminal_score != NULL);
    g_return_if_fail(self->backend->hash_position != NULL);
  }

  self->position = g_malloc0(self->backend->position_size);
  g_return_if_fail(self->position != NULL);

  self->variant = ggame_model_pick_initial_variant(self->backend);
  self->backend->position_init(self->position, self->variant);
}

static void ggame_model_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
  GGameModel *self = GGAME_MODEL(object);

  switch (property_id) {
    case PROP_BACKEND:
      self->backend = g_value_get_pointer(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void ggame_model_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
  GGameModel *self = GGAME_MODEL(object);

  switch (property_id) {
    case PROP_BACKEND:
      g_value_set_pointer(value, (gpointer) self->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void ggame_model_class_init(GGameModelClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->constructed = ggame_model_constructed;
  object_class->finalize = ggame_model_finalize;
  object_class->set_property = ggame_model_set_property;
  object_class->get_property = ggame_model_get_property;

  properties[PROP_BACKEND] = g_param_spec_pointer("backend",
                                                  "Backend",
                                                  "Active backend used by this model.",
                                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS);
  g_object_class_install_property(object_class, PROP_BACKEND, properties[PROP_BACKEND]);

  model_signals[SIGNAL_STATE_CHANGED] = g_signal_new("state-changed",
                                                     G_TYPE_FROM_CLASS(klass),
                                                     G_SIGNAL_RUN_LAST,
                                                     0,
                                                     NULL,
                                                     NULL,
                                                     NULL,
                                                     G_TYPE_NONE,
                                                     0);
}

static void ggame_model_init(GGameModel *self) {
  self->backend = NULL;
  self->position = NULL;
  self->variant = NULL;
}

GGameModel *ggame_model_new(const GameBackend *backend) {
  g_return_val_if_fail(backend != NULL, NULL);

  return g_object_new(GGAME_TYPE_MODEL, "backend", backend, NULL);
}

void ggame_model_reset(GGameModel *self, const GameBackendVariant *variant_or_null) {
  g_return_if_fail(GGAME_IS_MODEL(self));
  g_return_if_fail(self->backend != NULL);
  g_return_if_fail(self->position != NULL);

  self->backend->position_clear(self->position);
  self->variant = variant_or_null != NULL ? variant_or_null : ggame_model_pick_initial_variant(self->backend);
  self->backend->position_init(self->position, self->variant);
  ggame_model_emit_state_changed(self);
}

gboolean ggame_model_set_position(GGameModel *self, gconstpointer position) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), FALSE);

  return ggame_model_set_position_variant(self, position, self->variant);
}

gboolean ggame_model_set_position_variant(GGameModel *self,
                                          gconstpointer position,
                                          const GameBackendVariant *variant_or_null) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), FALSE);
  g_return_val_if_fail(self->backend != NULL, FALSE);
  g_return_val_if_fail(self->position != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(self->backend->position_copy != NULL, FALSE);

  self->variant = variant_or_null != NULL ? variant_or_null : ggame_model_pick_initial_variant(self->backend);
  self->backend->position_copy(self->position, position);
  ggame_model_emit_state_changed(self);
  return TRUE;
}

GameBackendMoveList ggame_model_list_moves(GGameModel *self) {
  GameBackendMoveList empty = {0};

  g_return_val_if_fail(GGAME_IS_MODEL(self), empty);
  g_return_val_if_fail(self->backend != NULL, empty);
  g_return_val_if_fail(self->position != NULL, empty);
  if (!self->backend->supports_move_list || self->backend->list_moves == NULL) {
    g_debug("Move listing is not available for this backend");
    return empty;
  }

  return self->backend->list_moves(self->position);
}

gboolean ggame_model_apply_move(GGameModel *self, gconstpointer move) {
  GameBackendMoveList moves = {0};
  gboolean applied = FALSE;

  g_return_val_if_fail(GGAME_IS_MODEL(self), FALSE);
  g_return_val_if_fail(self->backend != NULL, FALSE);
  g_return_val_if_fail(self->position != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (!self->backend->supports_move_list) {
    applied = self->backend->apply_move(self->position, move);
    if (!applied) {
      g_debug("Backend rejected move application without move-list validation");
      return FALSE;
    }

    ggame_model_emit_state_changed(self);
    return TRUE;
  }

  moves = self->backend->list_moves(self->position);
  for (gsize i = 0; i < moves.count; ++i) {
    const void *candidate = self->backend->move_list_get(&moves, i);

    if (candidate == NULL || !self->backend->moves_equal(candidate, move)) {
      continue;
    }

    applied = self->backend->apply_move(self->position, candidate);
    break;
  }

  self->backend->move_list_free(&moves);

  if (!applied) {
    g_debug("Attempted to apply a move that is not in the available move list");
    return FALSE;
  }

  ggame_model_emit_state_changed(self);
  return TRUE;
}

gconstpointer ggame_model_peek_position(GGameModel *self) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);

  return self->position;
}

const GameBackend *ggame_model_peek_backend(GGameModel *self) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);

  return self->backend;
}

const GameBackendVariant *ggame_model_peek_variant(GGameModel *self) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);

  return self->variant;
}

char *ggame_model_format_status(GGameModel *self) {
  GameBackendMoveList moves = {0};
  const char *variant_name = "Default";
  char *status = NULL;

  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);
  g_return_val_if_fail(self->backend != NULL, NULL);

  if (self->variant != NULL && self->variant->name != NULL) {
    variant_name = self->variant->name;
  }

  if (self->backend->supports_move_list) {
    moves = ggame_model_list_moves(self);
    status = g_strdup_printf("Game: %s\nVariant: %s\nMoves available: %" G_GSIZE_FORMAT,
                             self->backend->display_name,
                             variant_name,
                             moves.count);
    self->backend->move_list_free(&moves);
  } else {
    status = g_strdup_printf("Game: %s\nVariant: %s\nMoves available: unavailable",
                             self->backend->display_name,
                             variant_name);
  }

  return status;
}
/* End copied file: src/game_model.c */

/* Begin copied file: src/games/homeworlds/homeworlds_backend.c */
#include "homeworlds_backend.h"

#include "homeworlds_game.h"
#include "homeworlds_move_builder.h"
#include "homeworlds_position_text.h"
#include "homeworlds_sgf_position.h"

#include <stdlib.h>
#include <string.h>

enum {
  HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_LIMIT = 512,
  HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_WINDOW = 50,
};

typedef struct {
  HomeworldsMove *moves;
  GHashTable *seen_moves;
  gsize count;
  gsize capacity;
  gsize leaves_seen;
} HomeworldsMoveBuffer;

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
  buffer->count = 0;
  buffer->capacity = 0;
  buffer->leaves_seen = 0;
}

static void homeworlds_backend_move_buffer_clear_seen_moves(HomeworldsMoveBuffer *buffer) {
  g_return_if_fail(buffer != NULL);

  g_clear_pointer(&buffer->seen_moves, g_hash_table_unref);
}

static gboolean homeworlds_backend_move_buffer_note_seen_move(HomeworldsMoveBuffer *buffer,
                                                              const HomeworldsMove *move,
                                                              gboolean *out_duplicate) {
  HomeworldsMove *key = NULL;

  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(out_duplicate != NULL, FALSE);

  *out_duplicate = FALSE;

  if (buffer->seen_moves == NULL) {
    buffer->seen_moves = g_hash_table_new_full(homeworlds_move_hash,
                                               homeworlds_backend_moves_equal,
                                               g_free,
                                               NULL);
    g_return_val_if_fail(buffer->seen_moves != NULL, FALSE);
  }

  if (g_hash_table_contains(buffer->seen_moves, move)) {
    *out_duplicate = TRUE;
    return TRUE;
  }

  key = g_new(HomeworldsMove, 1);
  *key = *move;
  g_hash_table_add(buffer->seen_moves, key);
  return TRUE;
}

static gboolean homeworlds_backend_move_buffer_append(HomeworldsMoveBuffer *buffer, const HomeworldsMove *move) {
  gboolean duplicate = FALSE;

  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  buffer->leaves_seen++;
  if (!homeworlds_backend_move_buffer_note_seen_move(buffer, move, &duplicate)) {
    return FALSE;
  }
  if (duplicate) {
    return TRUE;
  }

  if (buffer->count == buffer->capacity) {
    gsize next_capacity = buffer->capacity == 0 ? 16 : buffer->capacity * 2;
    HomeworldsMove *next_moves = g_realloc_n(buffer->moves, next_capacity, sizeof(*next_moves));
    g_return_val_if_fail(next_moves != NULL, FALSE);
    buffer->moves = next_moves;
    buffer->capacity = next_capacity;
  }

  buffer->moves[buffer->count++] = *move;
  return TRUE;
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

static int homeworlds_backend_scored_move_compare_desc(const void *left, const void *right) {
  const HomeworldsScoredMove *a = left;
  const HomeworldsScoredMove *b = right;

  if (a->score < b->score) {
    return 1;
  }
  if (a->score > b->score) {
    return -1;
  }
  if (a->original_index > b->original_index) {
    return 1;
  }
  if (a->original_index < b->original_index) {
    return -1;
  }
  return 0;
}

static int homeworlds_backend_scored_move_compare_asc(const void *left, const void *right) {
  const HomeworldsScoredMove *a = left;
  const HomeworldsScoredMove *b = right;

  if (a->score < b->score) {
    return -1;
  }
  if (a->score > b->score) {
    return 1;
  }
  if (a->original_index > b->original_index) {
    return 1;
  }
  if (a->original_index < b->original_index) {
    return -1;
  }
  return 0;
}

static gboolean homeworlds_backend_score_is_inside_prune_window(guint side, gint score, gint best_score) {
  g_return_val_if_fail(side < 2, FALSE);

  if (side == 0) {
    return score >= best_score - HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_WINDOW;
  }
  return score <= best_score + HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_WINDOW;
}

static gboolean homeworlds_backend_static_prune_good_moves(const HomeworldsPosition *position,
                                                           HomeworldsMoveBuffer *buffer) {
  g_autofree HomeworldsScoredMove *scored_moves = NULL;
  guint side = 0;
  gint best_score = 0;
  gsize write = 0;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);

  if (position->phase != HOMEWORLDS_PHASE_PLAY || buffer->count <= 1) {
    return TRUE;
  }

  side = position->turn;
  g_return_val_if_fail(side < 2, FALSE);

  scored_moves = g_new0(HomeworldsScoredMove, buffer->count);
  for (gsize i = 0; i < buffer->count; ++i) {
    gint score = 0;

    if (!homeworlds_backend_score_after_move(position, &buffer->moves[i], &score)) {
      return FALSE;
    }
    scored_moves[i] = (HomeworldsScoredMove){
      .move = buffer->moves[i],
      .score = score,
      .original_index = i,
    };
  }

  if (side == 0) {
    qsort(scored_moves, buffer->count, sizeof(scored_moves[0]), homeworlds_backend_scored_move_compare_desc);
  } else {
    qsort(scored_moves, buffer->count, sizeof(scored_moves[0]), homeworlds_backend_scored_move_compare_asc);
  }

  best_score = scored_moves[0].score;
  for (gsize i = 0; i < buffer->count && i < HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_LIMIT; ++i) {
    if (!homeworlds_backend_score_is_inside_prune_window(side, scored_moves[i].score, best_score)) {
      break;
    }
    buffer->moves[write++] = scored_moves[i].move;
  }

  if (write == 0) {
    g_debug("Static pruning removed every Homeworlds good move");
    return FALSE;
  }
  buffer->count = write;
  return TRUE;
}

static GameBackendMoveList homeworlds_backend_list_good_moves(gconstpointer position, guint /*depth_hint*/) {
  const HomeworldsPosition *homeworlds_position = position;
  GameBackendMoveBuilder builder = {0};
  HomeworldsGoodMoveContext context = {0};
  HomeworldsMoveBuffer buffer = {0};

  g_return_val_if_fail(homeworlds_position != NULL, (GameBackendMoveList){0});

  if (!homeworlds_move_builder_init(homeworlds_position, &builder)) {
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

  if (!homeworlds_backend_static_prune_good_moves(homeworlds_position, &buffer)) {
    homeworlds_move_builder_clear(&builder);
    homeworlds_backend_move_buffer_clear(&buffer);
    return (GameBackendMoveList){0};
  }

  homeworlds_move_builder_clear(&builder);
  homeworlds_backend_move_buffer_clear_seen_moves(&buffer);
  return (GameBackendMoveList){
    .moves = buffer.moves,
    .count = buffer.count,
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
/* End copied file: src/games/homeworlds/homeworlds_backend.c */

/* Begin copied file: src/window.c */
#include "application.h"
#include "active_game_backend.h"
#include "game_app_profile.h"
#include "window.h"

#include "ai_search.h"
#include "app_paths.h"
#include "analysis_graph.h"
#include "board_view.h"
#include "common_settings.h"
#include "puzzle_catalog.h"
#include "games/checkers/rulesets.h"
#include "sgf_file_actions.h"
#include "sgf_controller.h"
#include "sgf_io.h"
#include "sgf_metadata.h"
#include "sgf_move_props.h"
#include "style.h"
#include "player_controls_panel.h"
#include "puzzle_progress.h"
#include "widget_utils.h"

#include <string.h>

typedef enum {
  GGAME_WINDOW_LAYOUT_MODE_NORMAL = 0,
  GGAME_WINDOW_LAYOUT_MODE_PUZZLE
} GGameWindowLayoutMode;

struct _GGameWindow {
  GtkApplicationWindow parent_instance;
  const GGameAppProfile *profile;
  GGameModel *game_model;
  GtkWidget *main_paned;
  GtkWidget *board_panel;
  GtkWidget *board_host_box;
  GtkWidget *board_host;
  GtkWidget *drawer_host;
  GtkWidget *drawer_split;
  GtkWidget *navigation_panel;
  GtkWidget *analysis_panel;
  GtkScale *analysis_depth_scale;
  GtkLabel *analysis_status_label;
  GtkTextBuffer *analysis_buffer;
  BoardView *board_view;
  PlayerControlsPanel *controls_panel;
  GtkDropDown *sgf_mode_control;
  GGameSgfController *sgf_controller;
  AnalysisGraph *analysis_graph;
  char *loaded_source_name;
  PlayerRuleset applied_ruleset;
  gulong state_handler_id;
  guint auto_move_source_id;
  guint paned_tick_id;
  gint analysis_mode;
  gint analysis_generation;
  GMutex analysis_report_mutex;
  GQueue *analysis_report_queue;
  guint analysis_expected_nodes;
  guint analysis_attached_nodes;
  guint analysis_processed_nodes;
  const SgfNode *analysis_last_updated_node;
  gboolean analysis_done_received;
  gboolean analysis_canceled;
  gboolean puzzle_mode;
  gboolean puzzle_finished;
  gboolean puzzle_feedback_locked;
  gboolean edit_mode_enabled;
  gboolean puzzle_saved_show_navigation_drawer;
  gboolean puzzle_saved_show_analysis_drawer;
  gboolean show_navigation_drawer;
  gboolean show_analysis_drawer;
  gboolean show_move_report;
  GGameWindowLayoutMode layout_mode;
  gboolean syncing_layout_default_size;
  gint board_panel_width;
  gint navigation_panel_width;
  gint analysis_panel_width;
  gint extra_width;
  gint puzzle_board_panel_width;
  gint puzzle_navigation_panel_width;
  gint puzzle_analysis_panel_width;
  gint puzzle_extra_width;
  GGameWindowBoardOrientationMode board_orientation_mode;
  CheckersColor board_bottom_color;
  const GameBackendVariant *puzzle_variant;
  char *puzzle_variant_key;
  guint puzzle_attacker_side;
  guint puzzle_number;
  guint puzzle_expected_step;
  guint puzzle_wrong_move_source_id;
  GPtrArray *puzzle_steps;
  GtkWidget *puzzle_panel;
  GtkLabel *puzzle_message_label;
  GtkButton *puzzle_next_button;
  GtkButton *puzzle_analyze_button;
  GGamePuzzleProgressStore *puzzle_progress_store;
  gboolean puzzle_attempt_started;
  gboolean puzzle_attempt_made_player_move;
  GGamePuzzleAttemptRecord puzzle_attempt;
  char *puzzle_path;
};

G_DEFINE_TYPE(GGameWindow, ggame_window, GTK_TYPE_APPLICATION_WINDOW)

typedef struct {
  GGameWindow *self;
  const GameBackend *backend;
  guint8 *position;
  gint generation;
  GameAiTranspositionTable *tt;
  guint current_depth;
  guint target_depth;
  gint64 last_progress_publish_us;
  GameAiSearchStats cumulative_stats;
  const SgfNode *target_node;
} GGameWindowAnalysisTask;

typedef enum {
  GGAME_WINDOW_ANALYSIS_MODE_NONE = 0,
  GGAME_WINDOW_ANALYSIS_MODE_CURRENT,
  GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME
} GGameWindowAnalysisMode;

typedef struct {
  gint generation;
  GGameWindowAnalysisMode mode;
  gboolean done;
  gboolean canceled;
  gboolean is_payload;
  char *status_text;
  SgfNodeAnalysis *analysis;
  const SgfNode *node;
} GGameWindowAnalysisEvent;

typedef struct {
  const SgfNode *node;
} GGameWindowFullNodeJob;

typedef struct {
  guint side;
  guint8 *move;
} GGameWindowPuzzleStep;

typedef struct {
  GGameWindow *self;
  gint generation;
  gboolean use_checkers_replay;
  const CheckersRules *checkers_rules;
  const GameBackend *backend;
  const GameBackendVariant *variant;
  guint depth;
  GameAiTranspositionTable *tt;
  GPtrArray *jobs;
  guint64 explored_nodes;
  guint current_job_index;
  gint64 last_progress_publish_us;
} GGameWindowFullAnalysisTask;

static void ggame_window_analysis_sync_ui(GGameWindow *self);
static void ggame_window_analysis_reset_runtime_state(GGameWindow *self);
static void ggame_window_analysis_finish_session(GGameWindow *self);
static gboolean ggame_window_is_edit_mode(GGameWindow *self);
static void ggame_window_set_action_enabled(GActionMap *map, const char *name, gboolean enabled);
static void ggame_window_sync_mode_ui(GGameWindow *self);
static void ggame_window_sync_drawer_ui(GGameWindow *self);
static void ggame_window_sync_move_report_ui(GGameWindow *self);
static void ggame_window_capture_panel_widths(GGameWindow *self);
static gint ggame_window_current_extra_width(GGameWindow *self);
static void ggame_window_apply_saved_panel_widths(GGameWindow *self);
static gint ggame_window_expected_default_width(GGameWindow *self);
static gboolean ggame_window_format_setup_point(uint8_t index, uint8_t board_size, char out_point[3]);
static gboolean ggame_window_update_node_setup_piece(SgfNode *node, const char *point, CheckersPiece piece);
static gboolean ggame_window_apply_player_move(gconstpointer move, gpointer user_data);
static gboolean ggame_window_on_board_square_action(guint8 index, guint button, gpointer user_data);
static void ggame_window_sync_board_orientation(GGameWindow *self);
static void ggame_window_sync_puzzle_ui(GGameWindow *self);
static void ggame_window_leave_puzzle_mode(GGameWindow *self, gboolean restore_drawers);
static void ggame_window_sync_drawer_ui_with_capture(GGameWindow *self, gboolean capture_current_layout);
static void ggame_window_stop_analysis(GGameWindow *self);
static void ggame_window_sync_title(GGameWindow *self);
static void ggame_window_set_analysis_status(GGameWindow *self, const char *text);
static void ggame_window_show_analysis_for_current_node(GGameWindow *self);
static gboolean ggame_window_puzzle_attempt_ensure_started(GGameWindow *self);
static gboolean ggame_window_puzzle_attempt_finish_success(GGameWindow *self);
static gboolean ggame_window_puzzle_attempt_finish_failure(GGameWindow *self,
                                                               gboolean failure_on_first_move,
                                                               gconstpointer failed_first_move);
static void ggame_window_puzzle_attempt_reset(GGameWindow *self);
static char *ggame_window_analysis_format_complete(const SgfNodeAnalysis *analysis);
static void ggame_window_rebuild_board_host(GGameWindow *self);
static void ggame_window_sync_drawer_action_states(GGameWindow *self);
static void ggame_window_load_default_size(gint *out_width, gint *out_height);
static void ggame_window_save_default_size(GGameWindow *self);
static gboolean ggame_window_on_close_request(GtkWindow *window, gpointer user_data);

enum {
  GGAME_WINDOW_DEFAULT_BOARD_PANEL_WIDTH = 500,
  GGAME_WINDOW_DEFAULT_NAVIGATION_PANEL_WIDTH = 300,
  GGAME_WINDOW_DEFAULT_ANALYSIS_PANEL_WIDTH = 300,
  GGAME_WINDOW_DEFAULT_WIDTH = 1100,
  GGAME_WINDOW_DEFAULT_HEIGHT = 700,
  GGAME_WINDOW_ANALYSIS_PROGRESS_INTERVAL_MS = 100,
  GGAME_WINDOW_ANALYSIS_TT_SIZE_MB = 256,
  GGAME_WINDOW_ANALYSIS_DEPTH_MIN = 1,
  GGAME_WINDOW_ANALYSIS_DEPTH_MAX = 16,
  GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT = 8,
  GGAME_WINDOW_PUZZLE_WRONG_MOVE_DELAY_MS = 1000,
};

static void ggame_window_refresh_analysis_graph(GGameWindow *self);

static const GGameAppProfile *ggame_window_get_profile(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  if (self->profile == NULL) {
    g_debug("Missing active game application profile");
    return NULL;
  }

  return self->profile;
}

static GGameModel *ggame_window_get_game_model(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  if (self->game_model == NULL) {
    g_debug("Missing generic game model");
    return NULL;
  }

  return self->game_model;
}

static const GameBackend *ggame_window_get_game_backend(GGameWindow *self) {
  GGameModel *game_model = ggame_window_get_game_model(self);
  g_return_val_if_fail(game_model != NULL, NULL);

  const GameBackend *backend = ggame_model_peek_backend(game_model);
  if (backend == NULL) {
    g_debug("Missing active game backend");
    return NULL;
  }

  return backend;
}

static gconstpointer ggame_window_get_game_position(GGameWindow *self) {
  GGameModel *game_model = ggame_window_get_game_model(self);
  g_return_val_if_fail(game_model != NULL, NULL);

  gconstpointer position = ggame_model_peek_position(game_model);
  if (position == NULL) {
    g_debug("Missing active game position");
    return NULL;
  }

  return position;
}

static const Game *ggame_window_get_checkers_game(GGameWindow *self) {
  const GGameAppProfile *profile = ggame_window_get_profile(self);
  gconstpointer position = NULL;

  g_return_val_if_fail(profile != NULL, NULL);
  if (profile->kind != GGAME_APP_KIND_CHECKERS) {
    return NULL;
  }

  position = ggame_window_get_game_position(self);
  g_return_val_if_fail(position != NULL, NULL);
  return position;
}

static gboolean ggame_window_uses_square_board(GGameWindow *self) {
  const GameBackend *backend = NULL;

  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  backend = ggame_window_get_game_backend(self);
  return backend != NULL && backend->supports_square_grid_board;
}

static void ggame_window_clear_board_selection(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->board_view != NULL && ggame_window_uses_square_board(self)) {
    board_view_clear_selection(self->board_view);
  }
}

static const GameState *ggame_window_get_checkers_state(GGameWindow *self) {
  const Game *game = ggame_window_get_checkers_game(self);

  if (game == NULL) {
    return NULL;
  }

  return &game->state;
}

static const char *ggame_window_side_name(guint side) {
  const GameBackend *backend = ggame_active_app_profile()->backend;
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(backend->side_label != NULL, NULL);

  return backend->side_label(side);
}

static void ggame_window_sync_side_labels(GGameWindow *self) {
  const GGameAppProfile *profile = ggame_window_get_profile(self);
  const GameBackend *backend = profile != NULL ? profile->backend : NULL;
  const char *side0_label = NULL;
  const char *side1_label = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->controls_panel != NULL);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(backend->side_label != NULL);

  side0_label = backend->side_label(0);
  side1_label = backend->side_label(1);
  player_controls_panel_set_side_labels(self->controls_panel, side0_label, side1_label);
}

static GGameApplication *ggame_window_get_application_instance(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  GtkApplication *app = gtk_window_get_application(GTK_WINDOW(self));
  if (!GGAME_IS_APPLICATION(app)) {
    return NULL;
  }

  return GGAME_APPLICATION(app);
}

static void ggame_window_request_puzzle_progress_flush(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  GGameApplication *app = ggame_window_get_application_instance(self);
  if (app == NULL) {
    return;
  }

  ggame_application_request_puzzle_progress_flush(app);
}

static gboolean ggame_window_puzzle_attempt_is_terminal(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  if (!self->puzzle_attempt_started) {
    return FALSE;
  }

  return ggame_puzzle_attempt_record_is_resolved(&self->puzzle_attempt);
}

static gboolean ggame_window_puzzle_attempt_store_update(GGameWindow *self, gboolean append_record) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  if (self->puzzle_progress_store == NULL) {
    return FALSE;
  }

  g_autoptr(GError) error = NULL;
  gboolean ok = append_record
                    ? ggame_puzzle_progress_store_append_attempt(self->puzzle_progress_store,
                                                                    &self->puzzle_attempt,
                                                                    &error)
                    : ggame_puzzle_progress_store_replace_attempt(self->puzzle_progress_store,
                                                                     &self->puzzle_attempt,
                                                                     &error);
  if (!ok) {
    g_debug("Failed to persist puzzle attempt: %s", error != NULL ? error->message : "unknown error");
  }
  return ok;
}

static gboolean ggame_window_puzzle_attempt_ensure_started(GGameWindow *self) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(backend->id != NULL, FALSE);

  if (self->puzzle_attempt_started) {
    return TRUE;
  }
  if (self->puzzle_progress_store == NULL) {
    g_debug("Puzzle progress storage unavailable; attempt will not be persisted");
    return FALSE;
  }
  if (self->puzzle_path == NULL || self->puzzle_path[0] == '\0') {
    g_debug("Cannot start puzzle attempt without a puzzle path");
    return FALSE;
  }

  const char *variant_key = self->puzzle_variant_key != NULL ? self->puzzle_variant_key : "default";
  self->puzzle_attempt = (GGamePuzzleAttemptRecord){
      .attempt_id = g_uuid_string_random(),
      .puzzle_number = self->puzzle_number,
      .puzzle_source_name = g_path_get_basename(self->puzzle_path),
      .puzzle_variant = g_strdup(variant_key),
      .attacker_side = self->puzzle_attacker_side,
      .started_unix_ms = g_get_real_time() / 1000,
      .finished_unix_ms = 0,
      .result = GGAME_PUZZLE_ATTEMPT_RESULT_UNRESOLVED,
      .failure_on_first_move = FALSE,
      .has_failed_first_move = FALSE,
      .first_reported_unix_ms = 0,
      .report_count = 0,
  };

  g_autofree char *basename = g_path_get_basename(self->puzzle_path);
  if (basename == NULL || variant_key == NULL) {
    ggame_window_puzzle_attempt_reset(self);
    return FALSE;
  }
  self->puzzle_attempt.puzzle_id = self->puzzle_variant != NULL
                                       ? g_strdup_printf("%s/%s/%s", backend->id, variant_key, basename)
                                       : g_strdup_printf("%s/%s", backend->id, basename);

  if (!ggame_window_puzzle_attempt_store_update(self, TRUE)) {
    ggame_window_puzzle_attempt_reset(self);
    return FALSE;
  }

  self->puzzle_attempt_started = TRUE;
  self->puzzle_attempt_made_player_move = FALSE;
  return TRUE;
}

static gboolean ggame_window_puzzle_attempt_finish_success(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  if (!self->puzzle_attempt_started || ggame_window_puzzle_attempt_is_terminal(self)) {
    return FALSE;
  }

  self->puzzle_attempt.finished_unix_ms = g_get_real_time() / 1000;
  self->puzzle_attempt.result = GGAME_PUZZLE_ATTEMPT_RESULT_SUCCESS;
  if (!ggame_window_puzzle_attempt_store_update(self, FALSE)) {
    return FALSE;
  }

  ggame_window_request_puzzle_progress_flush(self);
  return TRUE;
}

static gboolean ggame_window_puzzle_attempt_finish_failure(GGameWindow *self,
                                                               gboolean failure_on_first_move,
                                                               gconstpointer failed_first_move) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  if (!self->puzzle_attempt_started || ggame_window_puzzle_attempt_is_terminal(self)) {
    return FALSE;
  }

  self->puzzle_attempt.finished_unix_ms = g_get_real_time() / 1000;
  self->puzzle_attempt.result = GGAME_PUZZLE_ATTEMPT_RESULT_FAILURE;
  self->puzzle_attempt.failure_on_first_move = failure_on_first_move;
  self->puzzle_attempt.has_failed_first_move = failure_on_first_move && failed_first_move != NULL;
  if (self->puzzle_attempt.has_failed_first_move) {
    char notation[128] = {0};
    if (!sgf_move_props_format_notation(failed_first_move, notation, sizeof(notation), NULL)) {
      g_debug("Failed to format failed first move for puzzle progress");
      self->puzzle_attempt.has_failed_first_move = FALSE;
    } else {
      g_free(self->puzzle_attempt.failed_first_move_text);
      self->puzzle_attempt.failed_first_move_text = g_strdup(notation);
    }
  }
  if (!ggame_window_puzzle_attempt_store_update(self, FALSE)) {
    return FALSE;
  }

  ggame_window_request_puzzle_progress_flush(self);
  return TRUE;
}

static void ggame_window_puzzle_attempt_reset(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (!self->puzzle_attempt_started && self->puzzle_attempt.attempt_id == NULL &&
      self->puzzle_attempt.puzzle_id == NULL && self->puzzle_attempt.puzzle_source_name == NULL) {
    return;
  }

  self->puzzle_attempt_started = FALSE;
  self->puzzle_attempt_made_player_move = FALSE;
  ggame_puzzle_attempt_record_clear(&self->puzzle_attempt);
}

static gboolean ggame_window_analysis_depth_valid(guint depth) {
  return depth >= GGAME_WINDOW_ANALYSIS_DEPTH_MIN && depth <= GGAME_WINDOW_ANALYSIS_DEPTH_MAX;
}

static gboolean ggame_window_layout_mode_valid(GGameWindowLayoutMode mode) {
  return mode == GGAME_WINDOW_LAYOUT_MODE_NORMAL || mode == GGAME_WINDOW_LAYOUT_MODE_PUZZLE;
}

static gint *ggame_window_saved_board_panel_width_ptr(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);
  g_return_val_if_fail(ggame_window_layout_mode_valid(self->layout_mode), NULL);

  return self->layout_mode == GGAME_WINDOW_LAYOUT_MODE_PUZZLE ? &self->puzzle_board_panel_width
                                                                  : &self->board_panel_width;
}

static gint *ggame_window_saved_navigation_panel_width_ptr(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);
  g_return_val_if_fail(ggame_window_layout_mode_valid(self->layout_mode), NULL);

  return self->layout_mode == GGAME_WINDOW_LAYOUT_MODE_PUZZLE ? &self->puzzle_navigation_panel_width
                                                                  : &self->navigation_panel_width;
}

static gint *ggame_window_saved_analysis_panel_width_ptr(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);
  g_return_val_if_fail(ggame_window_layout_mode_valid(self->layout_mode), NULL);

  return self->layout_mode == GGAME_WINDOW_LAYOUT_MODE_PUZZLE ? &self->puzzle_analysis_panel_width
                                                                  : &self->analysis_panel_width;
}

static gint *ggame_window_saved_extra_width_ptr(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);
  g_return_val_if_fail(ggame_window_layout_mode_valid(self->layout_mode), NULL);

  return self->layout_mode == GGAME_WINDOW_LAYOUT_MODE_PUZZLE ? &self->puzzle_extra_width : &self->extra_width;
}

static gboolean ggame_window_board_orientation_mode_valid(GGameWindowBoardOrientationMode mode) {
  return mode == GGAME_WINDOW_BOARD_ORIENTATION_FIXED ||
         mode == GGAME_WINDOW_BOARD_ORIENTATION_FOLLOW_PLAYER ||
         mode == GGAME_WINDOW_BOARD_ORIENTATION_FOLLOW_TURN;
}

static gboolean ggame_window_try_resolve_follow_player_bottom_color(GGameWindow *self,
                                                                        CheckersColor *out_color) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(out_color != NULL, FALSE);
  g_return_val_if_fail(self->controls_panel != NULL, FALSE);

  gboolean white_is_user = player_controls_panel_is_user_control(self->controls_panel, 0);
  gboolean black_is_user = player_controls_panel_is_user_control(self->controls_panel, 1);
  if (white_is_user == black_is_user) {
    return FALSE;
  }

  *out_color = white_is_user ? CHECKERS_COLOR_WHITE : CHECKERS_COLOR_BLACK;
  return TRUE;
}

static CheckersColor ggame_window_resolve_board_bottom_color(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), CHECKERS_COLOR_WHITE);

  switch (self->board_orientation_mode) {
    case GGAME_WINDOW_BOARD_ORIENTATION_FIXED:
      return self->board_bottom_color;
    case GGAME_WINDOW_BOARD_ORIENTATION_FOLLOW_PLAYER: {
      CheckersColor bottom_color = self->board_bottom_color;
      if (ggame_window_try_resolve_follow_player_bottom_color(self, &bottom_color)) {
        return bottom_color;
      }
      return self->board_bottom_color;
    }
    case GGAME_WINDOW_BOARD_ORIENTATION_FOLLOW_TURN: {
      const GameBackend *backend = ggame_window_get_game_backend(self);
      gconstpointer position = ggame_window_get_game_position(self);

      if (backend != NULL && position != NULL && backend->position_turn != NULL) {
        guint side = backend->position_turn(position);
        return side == 0 ? CHECKERS_COLOR_WHITE : CHECKERS_COLOR_BLACK;
      }
      return self->board_bottom_color;
    }
    default:
      g_debug("Unexpected board orientation mode %d", self->board_orientation_mode);
      return self->board_bottom_color;
  }
}

static void ggame_window_sync_board_orientation(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->board_view != NULL);

  if (!ggame_window_uses_square_board(self)) {
    return;
  }

  CheckersColor bottom_color = ggame_window_resolve_board_bottom_color(self);
  self->board_bottom_color = bottom_color;
  board_view_set_bottom_side(self->board_view, bottom_color);
}

static gboolean ggame_window_puzzle_name_matches(const char *name) {
  g_return_val_if_fail(name != NULL, FALSE);

  return (g_str_has_prefix(name, "puzzle-") || g_str_has_prefix(name, "puzzles-")) &&
         g_str_has_suffix(name, ".sgf");
}

static gboolean ggame_window_parse_puzzle_number_from_path(const char *path, guint *out_number) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(out_number != NULL, FALSE);

  g_autofree char *name = g_path_get_basename(path);
  g_return_val_if_fail(name != NULL, FALSE);

  if (!ggame_window_puzzle_name_matches(name)) {
    return FALSE;
  }

  const char *dash = strrchr(name, '-');
  const char *dot = strrchr(name, '.');
  if (dash == NULL || dot == NULL || dot <= dash + 1) {
    return FALSE;
  }

  g_autofree char *number_text = g_strndup(dash + 1, (gsize)(dot - dash - 1));
  char *end_ptr = NULL;
  guint64 number = g_ascii_strtoull(number_text, &end_ptr, 10);
  if (end_ptr == number_text || (end_ptr != NULL && *end_ptr != '\0') || number > G_MAXUINT) {
    return FALSE;
  }

  *out_number = (guint)number;
  return TRUE;
}

static void ggame_window_puzzle_step_free(GGameWindowPuzzleStep *step) {
  if (step == NULL) {
    return;
  }

  g_clear_pointer(&step->move, g_free);
  g_free(step);
}

static GGameWindowPuzzleStep *ggame_window_puzzle_step_new(const GameBackend *backend,
                                                           guint side,
                                                           gconstpointer move) {
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(backend->move_size > 0, NULL);
  g_return_val_if_fail(move != NULL, NULL);

  GGameWindowPuzzleStep *step = g_new0(GGameWindowPuzzleStep, 1);
  step->side = side;
  step->move = g_memdup2(move, backend->move_size);
  return step;
}

static gboolean ggame_window_load_puzzle_steps_from_tree(GGameWindow *self, SgfTree *tree, GPtrArray *out_steps) {
  const GameBackend *backend = NULL;
  const GameBackendVariant *variant = NULL;
  gconstpointer root_position = NULL;
  guint8 *position = NULL;
  gboolean ok = FALSE;

  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(tree), FALSE);
  g_return_val_if_fail(out_steps != NULL, FALSE);

  backend = ggame_window_get_game_backend(self);
  variant = ggame_window_get_variant(self);
  root_position = ggame_window_get_game_position(self);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(root_position != NULL, FALSE);
  g_return_val_if_fail(backend->position_size > 0, FALSE);
  g_return_val_if_fail(backend->move_size > 0, FALSE);
  g_return_val_if_fail(backend->position_init != NULL, FALSE);
  g_return_val_if_fail(backend->position_copy != NULL, FALSE);
  g_return_val_if_fail(backend->position_clear != NULL, FALSE);
  g_return_val_if_fail(backend->position_turn != NULL, FALSE);
  g_return_val_if_fail(backend->apply_move != NULL, FALSE);
  g_return_val_if_fail(backend->sgf_color_for_side != NULL, FALSE);

  const SgfNode *node = sgf_tree_get_root(tree);
  g_return_val_if_fail(node != NULL, FALSE);

  position = g_malloc0(backend->position_size);
  backend->position_init(position, variant);
  backend->position_copy(position, root_position);

  while (TRUE) {
    const GPtrArray *children = sgf_node_get_children(node);
    if (children == NULL || children->len == 0) {
      ok = out_steps->len > 0;
      break;
    }

    node = g_ptr_array_index((GPtrArray *)children, 0);
    if (node == NULL) {
      g_debug("Puzzle main line child node was missing");
      break;
    }

    SgfColor sgf_color = SGF_COLOR_NONE;
    g_autofree guint8 *move = g_malloc0(backend->move_size);
    gboolean has_move = FALSE;
    g_autoptr(GError) error = NULL;
    if (!sgf_move_props_try_parse_node(node, &sgf_color, move, &has_move, &error)) {
      g_debug("Failed to parse puzzle node move: %s", error != NULL ? error->message : "unknown error");
      break;
    }
    if (!has_move) {
      g_debug("Puzzle main line node was missing a move");
      break;
    }

    if (sgf_color != SGF_COLOR_BLACK && sgf_color != SGF_COLOR_WHITE) {
      g_debug("Puzzle main line node had unexpected color");
      break;
    }

    guint side = backend->position_turn(position);
    SgfColor expected_color = backend->sgf_color_for_side(side);
    if (expected_color == SGF_COLOR_NONE || sgf_color != expected_color) {
      g_debug("Puzzle main line move color did not match side to move");
      break;
    }

    GGameWindowPuzzleStep *step = ggame_window_puzzle_step_new(backend, side, move);
    if (step == NULL) {
      g_debug("Failed to allocate puzzle step");
      break;
    }
    g_ptr_array_add(out_steps, step);

    if (!backend->apply_move(position, move)) {
      g_debug("Puzzle main line move could not be replayed");
      break;
    }
  }

  backend->position_clear(position);
  g_free(position);
  return ok;
}

static void ggame_window_set_puzzle_message(GGameWindow *self, const char *message) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(message != NULL);

  if (self->puzzle_message_label != NULL) {
    gtk_label_set_text(self->puzzle_message_label, message);
  }
}

static void ggame_window_set_default_puzzle_message(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  g_autofree char *message =
      g_strdup_printf("Puzzle %04u. Find the best sequence for %s.",
                      self->puzzle_number,
                      ggame_window_side_name(self->puzzle_attacker_side));
  ggame_window_set_puzzle_message(self, message);
}

static void ggame_window_clear_board_banner(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->board_view != NULL);

  board_view_set_banner_text(self->board_view, NULL);
}

static gboolean ggame_window_constrain_main_split_cb(GtkWidget * /*widget*/,
                                                         GdkFrameClock * /*frame_clock*/,
                                                         gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_val_if_fail(GGAME_IS_WINDOW(self), G_SOURCE_CONTINUE);

  if (!self->main_paned || !GTK_IS_PANED(self->main_paned)) {
    return G_SOURCE_CONTINUE;
  }
  if (self->profile == NULL ||
      self->profile->backend == NULL ||
      !self->profile->backend->supports_square_grid_board) {
    return G_SOURCE_CONTINUE;
  }

  int height = gtk_widget_get_height(self->main_paned);
  int position = gtk_paned_get_position(GTK_PANED(self->main_paned));
  if (height > 0 && position > height) {
    gtk_paned_set_position(GTK_PANED(self->main_paned), height);
  }

  return G_SOURCE_CONTINUE;
}

static void ggame_window_analysis_sync_ui(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  gboolean full_game_active = self->analysis_mode == GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME;
  if (!full_game_active && self->analysis_graph != NULL) {
    analysis_graph_clear_progress_node(self->analysis_graph);
  }
}

static void ggame_window_sync_drawer_action_states(GGameWindow *self) {
  GAction *action = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));

  action = g_action_map_lookup_action(G_ACTION_MAP(self), "view-show-navigation-drawer");
  if (G_IS_SIMPLE_ACTION(action)) {
    g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(self->show_navigation_drawer));
  }

  action = g_action_map_lookup_action(G_ACTION_MAP(self), "view-show-analysis-drawer");
  if (G_IS_SIMPLE_ACTION(action)) {
    g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(self->show_analysis_drawer));
  }

  action = g_action_map_lookup_action(G_ACTION_MAP(self), "view-show-move-report");
  if (G_IS_SIMPLE_ACTION(action)) {
    g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(self->show_move_report));
  }
}

static void ggame_window_sync_title(GGameWindow *self) {
  const GGameAppProfile *profile = ggame_window_get_profile(self);
  const char *window_title_name = profile != NULL ? profile->window_title_name : "ggame";

  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->loaded_source_name == NULL || *self->loaded_source_name == '\0') {
    gtk_window_set_title(GTK_WINDOW(self), window_title_name);
    return;
  }

  g_autofree char *title = g_strdup_printf("%s - %s", window_title_name, self->loaded_source_name);
  gtk_window_set_title(GTK_WINDOW(self), title);
}

static void ggame_window_capture_panel_widths(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  gint *board_panel_width = ggame_window_saved_board_panel_width_ptr(self);
  gint *navigation_panel_width = ggame_window_saved_navigation_panel_width_ptr(self);
  gint *analysis_panel_width = ggame_window_saved_analysis_panel_width_ptr(self);
  g_return_if_fail(board_panel_width != NULL);
  g_return_if_fail(navigation_panel_width != NULL);
  g_return_if_fail(analysis_panel_width != NULL);

  gboolean drawer_attached = self->drawer_host != NULL && gtk_widget_get_parent(self->drawer_host) != NULL;
  gboolean drawer_split_attached = self->drawer_split != NULL && gtk_widget_get_parent(self->drawer_split) != NULL;

  if (drawer_attached && self->main_paned != NULL) {
    gint position = gtk_paned_get_position(GTK_PANED(self->main_paned));
    if (position > 0) {
      *board_panel_width = position;
    }
  } else if (drawer_attached && self->board_panel != NULL && gtk_widget_get_visible(self->board_panel)) {
    gint width = gtk_widget_get_width(self->board_panel);
    if (width > 0) {
      *board_panel_width = width;
    }
  }

  if (drawer_split_attached && self->drawer_split != NULL) {
    gint position = gtk_paned_get_position(GTK_PANED(self->drawer_split));
    if (position > 0) {
      *navigation_panel_width = position;
    }
  } else if (self->navigation_panel != NULL && gtk_widget_get_parent(self->navigation_panel) != NULL) {
    gint width = gtk_widget_get_width(self->navigation_panel);
    if (width > 0) {
      *navigation_panel_width = width;
    }
  }

  if (self->analysis_panel != NULL && gtk_widget_get_parent(self->analysis_panel) != NULL) {
    gint width = gtk_widget_get_width(self->analysis_panel);
    if (width > 0) {
      *analysis_panel_width = width;
    }
  }

  gint *extra_width = ggame_window_saved_extra_width_ptr(self);
  g_return_if_fail(extra_width != NULL);
  *extra_width = ggame_window_current_extra_width(self);
}

static gint ggame_window_current_extra_width(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), 0);

  gint window_width = gtk_widget_get_width(GTK_WIDGET(self));
  if (window_width <= 0) {
    return 0;
  }

  gint panel_width = 0;
  if (self->board_panel != NULL) {
    panel_width += MAX(0, gtk_widget_get_width(self->board_panel));
  }
  if (self->navigation_panel != NULL && gtk_widget_get_parent(self->navigation_panel) != NULL) {
    panel_width += MAX(0, gtk_widget_get_width(self->navigation_panel));
  }
  if (self->analysis_panel != NULL && gtk_widget_get_parent(self->analysis_panel) != NULL) {
    panel_width += MAX(0, gtk_widget_get_width(self->analysis_panel));
  }

  return MAX(0, window_width - panel_width);
}

static gint ggame_window_expected_default_width(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), GGAME_WINDOW_DEFAULT_BOARD_PANEL_WIDTH);

  gint *board_panel_width = ggame_window_saved_board_panel_width_ptr(self);
  gint *navigation_panel_width = ggame_window_saved_navigation_panel_width_ptr(self);
  gint *analysis_panel_width = ggame_window_saved_analysis_panel_width_ptr(self);
  gint *extra_width = ggame_window_saved_extra_width_ptr(self);
  g_return_val_if_fail(board_panel_width != NULL, GGAME_WINDOW_DEFAULT_BOARD_PANEL_WIDTH);
  g_return_val_if_fail(navigation_panel_width != NULL, GGAME_WINDOW_DEFAULT_NAVIGATION_PANEL_WIDTH);
  g_return_val_if_fail(analysis_panel_width != NULL, GGAME_WINDOW_DEFAULT_ANALYSIS_PANEL_WIDTH);
  g_return_val_if_fail(extra_width != NULL, 0);

  gint drawer_width = 0;
  if (self->show_navigation_drawer) {
    drawer_width += MAX(1, *navigation_panel_width);
  }
  if (self->show_analysis_drawer) {
    drawer_width += MAX(1, *analysis_panel_width);
  }

  return MAX(1, *board_panel_width) + drawer_width + MAX(0, *extra_width);
}

static void ggame_window_apply_saved_panel_widths(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  gint *board_panel_width = ggame_window_saved_board_panel_width_ptr(self);
  gint *navigation_panel_width = ggame_window_saved_navigation_panel_width_ptr(self);
  gint *analysis_panel_width = ggame_window_saved_analysis_panel_width_ptr(self);
  gint *extra_width = ggame_window_saved_extra_width_ptr(self);
  g_return_if_fail(board_panel_width != NULL);
  g_return_if_fail(navigation_panel_width != NULL);
  g_return_if_fail(analysis_panel_width != NULL);
  g_return_if_fail(extra_width != NULL);

  gint board_width = MAX(1, *board_panel_width);
  gint board_min_width = board_width;
  gint navigation_width = MAX(1, *navigation_panel_width);
  gint analysis_width = MAX(1, *analysis_panel_width);
  gint drawer_width = 0;

  if (self->profile != NULL && self->profile->layout.minimum_board_panel_width > 0) {
    board_min_width = MIN(board_width, self->profile->layout.minimum_board_panel_width);
  }

  if (self->show_navigation_drawer) {
    drawer_width += navigation_width;
  }
  if (self->show_analysis_drawer) {
    drawer_width += analysis_width;
  }

  gint current_height = gtk_widget_get_height(GTK_WIDGET(self));
  if (current_height <= 0) {
    gint default_width = 0;
    gint default_height = 0;
    gtk_window_get_default_size(GTK_WINDOW(self), &default_width, &default_height);
    current_height = default_height > 0 ? default_height : GGAME_WINDOW_DEFAULT_HEIGHT;
  }

  self->syncing_layout_default_size = TRUE;
  gtk_window_set_default_size(GTK_WINDOW(self), board_width + drawer_width + MAX(0, *extra_width), current_height);
  self->syncing_layout_default_size = FALSE;

  if (self->main_paned != NULL && (self->show_navigation_drawer || self->show_analysis_drawer)) {
    gtk_paned_set_position(GTK_PANED(self->main_paned), board_width);
  }
  if (self->drawer_split != NULL && self->show_navigation_drawer && self->show_analysis_drawer) {
    gtk_paned_set_position(GTK_PANED(self->drawer_split), navigation_width);
  }
  if (self->board_panel != NULL) {
    gtk_widget_set_size_request(self->board_panel, board_min_width, -1);
  }
  if (self->drawer_host != NULL) {
    gtk_widget_set_size_request(self->drawer_host, -1, -1);
  }
  if (self->drawer_split != NULL) {
    gtk_widget_set_size_request(self->drawer_split, -1, -1);
  }
}

static void ggame_window_sync_drawer_ui(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  ggame_window_sync_drawer_ui_with_capture(self, TRUE);
}

static void ggame_window_sync_move_report_ui(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->profile == NULL || self->profile->ui.set_move_report_enabled == NULL || self->board_host == NULL) {
    return;
  }

  self->profile->ui.set_move_report_enabled(self->board_host, self->show_move_report);
}

static void ggame_window_sync_drawer_ui_with_capture(GGameWindow *self, gboolean capture_current_layout) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (capture_current_layout) {
    ggame_window_capture_panel_widths(self);
  }

  if (self->navigation_panel != NULL) {
    ggame_widget_remove_from_parent(self->navigation_panel);
  }
  if (self->analysis_panel != NULL) {
    ggame_widget_remove_from_parent(self->analysis_panel);
  }
  if (self->drawer_host != NULL) {
    ggame_widget_remove_from_parent(self->drawer_host);
  }
  if (self->drawer_split != NULL) {
    ggame_widget_remove_from_parent(self->drawer_split);
  }

  if (self->show_navigation_drawer && self->show_analysis_drawer) {
    g_return_if_fail(self->drawer_host != NULL);
    g_return_if_fail(self->drawer_split != NULL);
    g_return_if_fail(self->navigation_panel != NULL);
    g_return_if_fail(self->analysis_panel != NULL);
    gtk_paned_set_start_child(GTK_PANED(self->drawer_split), self->navigation_panel);
    gtk_paned_set_end_child(GTK_PANED(self->drawer_split), self->analysis_panel);
    gtk_box_append(GTK_BOX(self->drawer_host), self->drawer_split);
    gtk_widget_set_visible(self->navigation_panel, TRUE);
    gtk_widget_set_visible(self->analysis_panel, TRUE);
    gtk_widget_set_visible(self->drawer_split, TRUE);
    gtk_widget_set_visible(self->drawer_host, TRUE);
    gtk_paned_set_end_child(GTK_PANED(self->main_paned), self->drawer_host);
  } else if (self->show_navigation_drawer) {
    g_return_if_fail(self->drawer_host != NULL);
    g_return_if_fail(self->navigation_panel != NULL);
    gtk_box_append(GTK_BOX(self->drawer_host), self->navigation_panel);
    gtk_widget_set_visible(self->navigation_panel, TRUE);
    gtk_widget_set_visible(self->drawer_host, TRUE);
    gtk_paned_set_end_child(GTK_PANED(self->main_paned), self->drawer_host);
  } else if (self->show_analysis_drawer) {
    g_return_if_fail(self->drawer_host != NULL);
    g_return_if_fail(self->analysis_panel != NULL);
    gtk_box_append(GTK_BOX(self->drawer_host), self->analysis_panel);
    gtk_widget_set_visible(self->analysis_panel, TRUE);
    gtk_widget_set_visible(self->drawer_host, TRUE);
    gtk_paned_set_end_child(GTK_PANED(self->main_paned), self->drawer_host);
  } else {
    gtk_paned_set_end_child(GTK_PANED(self->main_paned), NULL);
  }

  ggame_window_apply_saved_panel_widths(self);
  gtk_widget_queue_allocate(GTK_WIDGET(self));
}

static void ggame_window_sync_puzzle_ui(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->controls_panel != NULL) {
    gtk_widget_set_visible(GTK_WIDGET(self->controls_panel), !self->puzzle_mode);
  }
  if (self->puzzle_panel != NULL) {
    gtk_widget_set_visible(self->puzzle_panel, self->puzzle_mode);
  }
  if (self->puzzle_next_button != NULL) {
    gtk_widget_set_sensitive(GTK_WIDGET(self->puzzle_next_button), self->puzzle_mode && self->puzzle_finished);
  }
  if (self->puzzle_analyze_button != NULL) {
    gtk_widget_set_sensitive(GTK_WIDGET(self->puzzle_analyze_button), self->puzzle_mode);
  }
}

static void ggame_window_on_show_navigation_drawer_change_state(GSimpleAction *action,
                                                                    GVariant *value,
                                                                    gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(G_IS_SIMPLE_ACTION(action));
  g_return_if_fail(value != NULL);

  self->show_navigation_drawer = g_variant_get_boolean(value);
  g_simple_action_set_state(action, value);
  ggame_window_sync_drawer_ui(self);
}

static void ggame_window_on_show_analysis_drawer_change_state(GSimpleAction *action,
                                                                  GVariant *value,
                                                                  gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(G_IS_SIMPLE_ACTION(action));
  g_return_if_fail(value != NULL);

  self->show_analysis_drawer = g_variant_get_boolean(value);
  g_simple_action_set_state(action, value);
  ggame_window_sync_drawer_ui(self);
}

static void ggame_window_on_show_move_report_change_state(GSimpleAction *action,
                                                          GVariant *value,
                                                          gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(G_IS_SIMPLE_ACTION(action));
  g_return_if_fail(value != NULL);

  self->show_move_report = g_variant_get_boolean(value);
  g_simple_action_set_state(action, value);
  ggame_window_sync_move_report_ui(self);
}

static void ggame_window_analysis_reset_runtime_state(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  self->analysis_expected_nodes = 0;
  self->analysis_attached_nodes = 0;
  self->analysis_processed_nodes = 0;
  self->analysis_last_updated_node = NULL;
  self->analysis_done_received = FALSE;
  self->analysis_canceled = FALSE;
}

static void ggame_window_analysis_finish_session(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  self->analysis_mode = GGAME_WINDOW_ANALYSIS_MODE_NONE;
  ggame_window_analysis_reset_runtime_state(self);
  ggame_window_set_analysis_status(self, "");
  ggame_window_analysis_sync_ui(self);
}

static gboolean ggame_window_is_edit_mode(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  return self->edit_mode_enabled;
}

static void ggame_window_set_action_enabled(GActionMap *map, const char *name, gboolean enabled) {
  g_return_if_fail(map != NULL);
  g_return_if_fail(name != NULL);

  GAction *action = g_action_map_lookup_action(map, name);
  if (action == NULL) {
    g_debug("Missing action while toggling enabled state: %s", name);
    return;
  }

  if (!G_IS_SIMPLE_ACTION(action)) {
    g_debug("Unsupported non-simple action while toggling enabled state: %s", name);
    return;
  }

  g_simple_action_set_enabled(G_SIMPLE_ACTION(action), enabled);
}

static void ggame_window_sync_mode_ui(GGameWindow *self) {
  const GGameAppProfile *profile = ggame_window_get_profile(self);
  gboolean supports_save_position = FALSE;
  gboolean supports_analysis = FALSE;
  gboolean supports_edit_mode = FALSE;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(profile != NULL);

  supports_save_position = profile->features.supports_save_position;
  supports_analysis = profile->features.supports_analysis;
  supports_edit_mode = profile->features.supports_edit_mode;

  gboolean allow_navigation = !self->edit_mode_enabled && !self->puzzle_mode;
  gboolean allow_sgf_file_actions = !self->puzzle_mode;
  gboolean allow_view_actions = !self->puzzle_mode;
  gboolean allow_edit_mode_selection = supports_edit_mode && !self->puzzle_mode;

  ggame_window_set_action_enabled(G_ACTION_MAP(self), "game-force-move", allow_navigation);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "navigation-rewind", allow_navigation);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "navigation-step-backward", allow_navigation);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "navigation-step-forward", allow_navigation);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "navigation-step-forward-to-branch", allow_navigation);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "navigation-step-forward-to-end", allow_navigation);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "sgf-delete-node", allow_navigation);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "sgf-load", allow_sgf_file_actions);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "sgf-save-as", allow_sgf_file_actions);
  ggame_window_set_action_enabled(G_ACTION_MAP(self),
                                  "sgf-save-position",
                                  allow_sgf_file_actions && supports_save_position);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "view-show-navigation-drawer", allow_view_actions);
  ggame_window_set_action_enabled(G_ACTION_MAP(self),
                                  "view-show-analysis-drawer",
                                  allow_view_actions && supports_analysis);
  ggame_window_set_action_enabled(G_ACTION_MAP(self),
                                  "view-show-move-report",
                                  allow_view_actions &&
                                  self->profile != NULL &&
                                  self->profile->ui.set_move_report_enabled != NULL);

  if (self->analysis_graph != NULL) {
    GtkWidget *graph_widget = analysis_graph_get_widget(self->analysis_graph);
    if (graph_widget != NULL) {
      gtk_widget_set_sensitive(graph_widget, allow_navigation && supports_analysis);
    }
  }

  if (self->sgf_controller != NULL) {
    GtkWidget *sgf_widget = ggame_sgf_controller_get_widget(self->sgf_controller);
    if (sgf_widget != NULL) {
      gtk_widget_set_sensitive(sgf_widget, allow_navigation);
    }
  }

  if (self->sgf_mode_control != NULL) {
    gtk_widget_set_sensitive(GTK_WIDGET(self->sgf_mode_control), allow_edit_mode_selection);
  }
}

static gboolean ggame_window_format_setup_point(uint8_t index, uint8_t board_size, char out_point[3]) {
  g_return_val_if_fail(out_point != NULL, FALSE);
  g_return_val_if_fail(board_size > 0, FALSE);

  gint row = 0;
  gint col = 0;
  board_coord_from_index(index, &row, &col, board_size);
  if (row < 0 || col < 0 || row >= 26 || col >= 26) {
    g_debug("Unsupported SGF setup coordinate for board size %u", board_size);
    return FALSE;
  }

  out_point[0] = (char)('a' + col);
  out_point[1] = (char)('a' + row);
  out_point[2] = '\0';
  return TRUE;
}

static const char *ggame_window_piece_label(CheckersPiece piece) {
  switch (piece) {
    case CHECKERS_PIECE_EMPTY:
      return "empty";
    case CHECKERS_PIECE_BLACK_MAN:
      return "black-man";
    case CHECKERS_PIECE_BLACK_KING:
      return "black-king";
    case CHECKERS_PIECE_WHITE_MAN:
      return "white-man";
    case CHECKERS_PIECE_WHITE_KING:
      return "white-king";
    default:
      return "unknown";
  }
}

static gboolean ggame_window_node_set_prop_has_point(SgfNode *node,
                                                         const char *ident,
                                                         const char *point,
                                                         gboolean has_point) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(ident != NULL, FALSE);
  g_return_val_if_fail(point != NULL, FALSE);

  g_autoptr(GPtrArray) next_values = g_ptr_array_new_with_free_func(g_free);
  const GPtrArray *existing = sgf_node_get_property_values(node, ident);
  if (existing != NULL) {
    for (guint i = 0; i < existing->len; ++i) {
      const char *value = g_ptr_array_index((GPtrArray *)existing, i);
      g_return_val_if_fail(value != NULL, FALSE);
      if (g_strcmp0(value, point) == 0) {
        continue;
      }
      g_ptr_array_add(next_values, g_strdup(value));
    }
  }
  if (has_point) {
    g_ptr_array_add(next_values, g_strdup(point));
  }

  sgf_node_clear_property(node, ident);
  for (guint i = 0; i < next_values->len; ++i) {
    const char *value = g_ptr_array_index(next_values, i);
    g_return_val_if_fail(value != NULL, FALSE);
    if (!sgf_node_add_property(node, ident, value)) {
      g_debug("Failed to add SGF setup property value");
      return FALSE;
    }
  }

  return TRUE;
}

static const GameBackendVariant *ggame_window_variant_for_ruleset(PlayerRuleset ruleset) {
  const char *short_name = checkers_ruleset_short_name(ruleset);

  if (short_name == NULL) {
    g_debug("Missing short name for ruleset %d", (gint) ruleset);
    return NULL;
  }

  return GGAME_ACTIVE_GAME_BACKEND->variant_by_short_name(short_name);
}

static gboolean ggame_window_ruleset_from_variant(const GameBackendVariant *variant, PlayerRuleset *out_ruleset) {
  g_return_val_if_fail(variant != NULL, FALSE);
  g_return_val_if_fail(out_ruleset != NULL, FALSE);
  g_return_val_if_fail(variant->short_name != NULL, FALSE);

  if (!checkers_ruleset_find_by_short_name(variant->short_name, out_ruleset)) {
    g_debug("Unable to map variant %s to a checkers ruleset", variant->short_name);
    return FALSE;
  }

  return TRUE;
}

static char *ggame_window_build_puzzle_variant_dir(GGameWindow *self, const GameBackendVariant *variant) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(backend->id != NULL, NULL);
  g_return_val_if_fail((variant == NULL) == (backend->variant_count == 0), NULL);
  g_return_val_if_fail(variant == NULL || variant->short_name != NULL, NULL);

  g_autofree char *puzzle_root = ggame_app_paths_find_data_subdir("GCHECKERS_PUZZLES_DIR", "puzzles");
  if (puzzle_root == NULL) {
    g_debug("Failed to resolve puzzle root directory");
    return NULL;
  }

  return variant != NULL ? g_build_filename(puzzle_root, backend->id, variant->short_name, NULL)
                         : g_build_filename(puzzle_root, backend->id, NULL);
}

static gboolean ggame_window_update_node_setup_piece(SgfNode *node, const char *point, CheckersPiece piece) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(point != NULL, FALSE);

  gboolean is_empty = piece == CHECKERS_PIECE_EMPTY;
  gboolean is_black = piece == CHECKERS_PIECE_BLACK_MAN || piece == CHECKERS_PIECE_BLACK_KING;
  gboolean is_white = piece == CHECKERS_PIECE_WHITE_MAN || piece == CHECKERS_PIECE_WHITE_KING;
  gboolean is_black_king = piece == CHECKERS_PIECE_BLACK_KING;
  gboolean is_white_king = piece == CHECKERS_PIECE_WHITE_KING;

  if (!ggame_window_node_set_prop_has_point(node, "AE", point, is_empty) ||
      !ggame_window_node_set_prop_has_point(node, "AB", point, is_black) ||
      !ggame_window_node_set_prop_has_point(node, "AW", point, is_white) ||
      !ggame_window_node_set_prop_has_point(node, "ABK", point, is_black_king) ||
      !ggame_window_node_set_prop_has_point(node, "AWK", point, is_white_king)) {
    g_debug("Edit update failed while setting SGF setup properties at point=%s target=%s",
            point,
            ggame_window_piece_label(piece));
    return FALSE;
  }

  sgf_node_clear_analysis(node);
  g_debug("Edit update wrote SGF setup properties at point=%s target=%s",
          point,
          ggame_window_piece_label(piece));
  return TRUE;
}

static gboolean ggame_window_on_board_square_action(guint8 index, guint button, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  const GameState *state = NULL;

  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  if (button != GDK_BUTTON_PRIMARY && button != GDK_BUTTON_SECONDARY) {
    return FALSE;
  }

  if (!ggame_window_is_edit_mode(self)) {
    return FALSE;
  }

  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller), FALSE);

  state = ggame_window_get_checkers_state(self);
  if (state == NULL) {
    g_debug("Missing game state for edit-mode square action");
    return TRUE;
  }

  guint8 max_square = board_playable_squares(state->board.board_size);
  if (index >= max_square) {
    g_debug("Edit-mode square index out of range");
    return TRUE;
  }

  CheckersPiece current = board_get(&state->board, index);
  CheckersPiece next = CHECKERS_PIECE_EMPTY;
  if (button == GDK_BUTTON_PRIMARY) {
    if (current == CHECKERS_PIECE_EMPTY) {
      next = CHECKERS_PIECE_WHITE_MAN;
    } else if (current == CHECKERS_PIECE_WHITE_MAN) {
      next = CHECKERS_PIECE_WHITE_KING;
    }
  } else {
    if (current == CHECKERS_PIECE_EMPTY) {
      next = CHECKERS_PIECE_BLACK_MAN;
    } else if (current == CHECKERS_PIECE_BLACK_MAN) {
      next = CHECKERS_PIECE_BLACK_KING;
    }
  }

  SgfTree *tree = ggame_sgf_controller_get_tree(self->sgf_controller);
  if (tree == NULL) {
    g_debug("Missing SGF tree for edit-mode square action");
    return TRUE;
  }
  SgfNode *current_node = (SgfNode *)sgf_tree_get_current(tree);
  if (current_node == NULL) {
    g_debug("Missing SGF current node for edit-mode square action");
    return TRUE;
  }

  char point[3] = {0};
  if (!ggame_window_format_setup_point(index, state->board.board_size, point)) {
    g_debug("Edit click failed formatting setup point: index=%u board_size=%u", index, state->board.board_size);
    return TRUE;
  }
  if (!ggame_window_update_node_setup_piece(current_node, point, next)) {
    g_debug("Edit click failed SGF setup update: index=%u point=%s", index, point);
    return TRUE;
  }
  if (!ggame_sgf_controller_refresh_current_node(self->sgf_controller)) {
    g_debug("Failed to refresh model from edited SGF current node");
    return TRUE;
  }

  const GameState *after = ggame_window_get_checkers_state(self);
  if (after == NULL) {
    g_debug("Edit click missing post-refresh game state: index=%u point=%s", index, point);
    return TRUE;
  }
  (void)after;

  return TRUE;
}

static void ggame_window_start_new_game(GGameWindow *self) {
  const GameBackendVariant *variant = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_MODEL(self->game_model));

  ggame_window_leave_puzzle_mode(self, TRUE);
  variant = ggame_window_get_variant(self);
  ggame_model_reset(self->game_model, variant);
  ggame_window_clear_board_selection(self);
  ggame_sgf_controller_new_game(self->sgf_controller);
  g_clear_pointer(&self->loaded_source_name, g_free);
  ggame_window_sync_title(self);
}

static gboolean ggame_window_revert_wrong_puzzle_move_cb(gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_val_if_fail(GGAME_IS_WINDOW(self), G_SOURCE_REMOVE);

  self->puzzle_wrong_move_source_id = 0;
  self->puzzle_feedback_locked = FALSE;
  if (!ggame_sgf_controller_refresh_current_node(self->sgf_controller)) {
    g_debug("Failed to restore puzzle position after wrong move");
  }
  ggame_window_clear_board_selection(self);
  ggame_window_clear_board_banner(self);
  ggame_window_set_default_puzzle_message(self);
  ggame_window_sync_puzzle_ui(self);
  g_object_unref(self);
  return G_SOURCE_REMOVE;
}

static gboolean ggame_window_play_next_puzzle_step_if_needed(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  if (!self->puzzle_mode || self->puzzle_steps == NULL) {
    return FALSE;
  }

  while (self->puzzle_expected_step < self->puzzle_steps->len) {
    GGameWindowPuzzleStep *step =
        g_ptr_array_index(self->puzzle_steps, self->puzzle_expected_step);
    g_return_val_if_fail(step != NULL, FALSE);
    if (step->side == self->puzzle_attacker_side) {
      break;
    }
    if (!ggame_sgf_controller_apply_move(self->sgf_controller, step->move)) {
      g_debug("Failed to apply puzzle defense move at step %u", self->puzzle_expected_step);
      return FALSE;
    }
    self->puzzle_expected_step++;
  }

  if (self->puzzle_expected_step >= self->puzzle_steps->len) {
    self->puzzle_finished = TRUE;
    (void)ggame_window_puzzle_attempt_finish_success(self);
    g_autofree char *message = g_strdup_printf("Puzzle %04u.", self->puzzle_number);
    ggame_window_set_puzzle_message(self, message);
    board_view_set_banner_text(self->board_view, "Puzzle solved");
  } else {
    ggame_window_clear_board_banner(self);
    ggame_window_set_default_puzzle_message(self);
  }
  ggame_window_sync_puzzle_ui(self);
  return TRUE;
}

static void ggame_window_leave_puzzle_mode(GGameWindow *self, gboolean restore_drawers) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (!self->puzzle_mode) {
    return;
  }

  if (self->puzzle_attempt_started && !ggame_window_puzzle_attempt_is_terminal(self)) {
    (void)ggame_window_puzzle_attempt_finish_failure(self, FALSE, NULL);
  }

  ggame_window_capture_panel_widths(self);
  g_clear_handle_id(&self->puzzle_wrong_move_source_id, g_source_remove);
  ggame_window_clear_board_banner(self);
  self->puzzle_feedback_locked = FALSE;
  self->puzzle_mode = FALSE;
  self->layout_mode = GGAME_WINDOW_LAYOUT_MODE_NORMAL;
  self->puzzle_finished = FALSE;
  self->puzzle_expected_step = 0;
  self->puzzle_variant = NULL;
  self->puzzle_attacker_side = 0;
  self->puzzle_number = 0;
  g_clear_pointer(&self->puzzle_variant_key, g_free);
  g_clear_pointer(&self->puzzle_path, g_free);
  if (self->puzzle_steps != NULL) {
    g_ptr_array_unref(self->puzzle_steps);
    self->puzzle_steps = NULL;
  }
  ggame_window_puzzle_attempt_reset(self);

  if (restore_drawers) {
    self->show_navigation_drawer = self->puzzle_saved_show_navigation_drawer;
    self->show_analysis_drawer = self->puzzle_saved_show_analysis_drawer;
  }
  ggame_window_sync_drawer_ui_with_capture(self, FALSE);
  ggame_window_sync_puzzle_ui(self);
  ggame_window_sync_mode_ui(self);
  ggame_window_analysis_sync_ui(self);
}

static gboolean ggame_window_enter_puzzle_mode_with_path(GGameWindow *self, const char *path) {
  const GameBackend *backend = NULL;
  gconstpointer position = NULL;

  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  ggame_window_stop_analysis(self);

  g_autoptr(GError) error = NULL;
  if (!ggame_sgf_controller_load_file(self->sgf_controller, path, &error)) {
    g_debug("Failed to load puzzle file %s: %s", path, error != NULL ? error->message : "unknown error");
    return FALSE;
  }
  ggame_window_set_loaded_source_path(self, path);

  SgfTree *tree = ggame_sgf_controller_get_tree(self->sgf_controller);
  if (tree == NULL) {
    g_debug("Puzzle file load did not produce an SGF tree");
    return FALSE;
  }
  const GameBackendVariant *loaded_variant = NULL;
  if (sgf_io_tree_get_variant(tree, &loaded_variant, NULL) && loaded_variant != NULL) {
    ggame_window_set_loaded_variant(self, loaded_variant);
  }

  backend = ggame_window_get_game_backend(self);
  position = ggame_window_get_game_position(self);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(backend->position_turn != NULL, FALSE);

  g_autoptr(GPtrArray) steps = g_ptr_array_new_with_free_func((GDestroyNotify)ggame_window_puzzle_step_free);
  if (!ggame_window_load_puzzle_steps_from_tree(self, tree, steps)) {
    g_debug("Puzzle file %s did not contain a valid main-line solution", path);
    return FALSE;
  }

  GGameWindowPuzzleStep *first_step = g_ptr_array_index(steps, 0);
  g_return_val_if_fail(first_step != NULL, FALSE);
  if (first_step->side != backend->position_turn(position)) {
    g_debug("Puzzle file %s first move does not match side to move", path);
    return FALSE;
  }

  if (!self->puzzle_mode) {
    ggame_window_capture_panel_widths(self);
    self->puzzle_saved_show_navigation_drawer = self->show_navigation_drawer;
    self->puzzle_saved_show_analysis_drawer = self->show_analysis_drawer;
    self->puzzle_board_panel_width = self->board_panel_width;
    self->puzzle_navigation_panel_width = self->navigation_panel_width;
    self->puzzle_analysis_panel_width = self->analysis_panel_width;
    self->puzzle_extra_width = self->extra_width;
  } else if (self->puzzle_steps != NULL) {
    if (self->puzzle_attempt_started && !ggame_window_puzzle_attempt_is_terminal(self)) {
      (void)ggame_window_puzzle_attempt_finish_failure(self, FALSE, NULL);
    }
    ggame_window_capture_panel_widths(self);
    g_ptr_array_unref(self->puzzle_steps);
    self->puzzle_steps = NULL;
    g_clear_pointer(&self->puzzle_path, g_free);
    g_clear_pointer(&self->puzzle_variant_key, g_free);
    ggame_window_puzzle_attempt_reset(self);
  }

  g_clear_handle_id(&self->puzzle_wrong_move_source_id, g_source_remove);
  self->puzzle_feedback_locked = FALSE;
  self->puzzle_mode = TRUE;
  self->layout_mode = GGAME_WINDOW_LAYOUT_MODE_PUZZLE;
  self->puzzle_finished = FALSE;
  self->puzzle_variant = ggame_window_get_variant(self);
  self->puzzle_variant_key = g_strdup(self->puzzle_variant != NULL ? self->puzzle_variant->short_name : "default");
  self->puzzle_attacker_side = first_step->side;
  if (!ggame_window_parse_puzzle_number_from_path(path, &self->puzzle_number)) {
    self->puzzle_number = 0;
  }
  self->puzzle_path = g_strdup(path);
  self->puzzle_attempt_made_player_move = FALSE;
  self->puzzle_expected_step = 0;
  self->puzzle_steps = g_steal_pointer(&steps);
  self->show_navigation_drawer = FALSE;
  self->show_analysis_drawer = FALSE;
  self->edit_mode_enabled = FALSE;
  if (self->sgf_mode_control != NULL) {
    gtk_drop_down_set_selected(self->sgf_mode_control, 0);
  }
  ggame_window_clear_board_selection(self);
  ggame_window_clear_board_banner(self);
  ggame_window_set_board_orientation_mode(self, GGAME_WINDOW_BOARD_ORIENTATION_FIXED);
  ggame_window_set_board_bottom_color(self,
                                      self->puzzle_attacker_side == 0 ? CHECKERS_COLOR_WHITE
                                                                      : CHECKERS_COLOR_BLACK);
  ggame_window_set_default_puzzle_message(self);
  ggame_window_sync_drawer_ui_with_capture(self, FALSE);
  ggame_window_sync_puzzle_ui(self);
  ggame_window_sync_mode_ui(self);
  ggame_window_analysis_sync_ui(self);
  return TRUE;
}

gboolean ggame_window_start_puzzle_mode_for_path(GGameWindow *self,
                                                 const GameBackendVariant *variant,
                                                 const char *path) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;

  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail((variant == NULL) == (backend->variant_count == 0), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  g_autofree char *variant_dir = ggame_window_build_puzzle_variant_dir(self, variant);
  g_return_val_if_fail(variant_dir != NULL, FALSE);
  if (!g_str_has_prefix(path, variant_dir)) {
    g_debug("Puzzle path %s does not match variant directory %s", path, variant_dir);
    return FALSE;
  }
  if (!ggame_window_enter_puzzle_mode_with_path(self, path)) {
    return FALSE;
  }

  if (variant != NULL) {
    g_clear_pointer(&self->puzzle_variant_key, g_free);
    self->puzzle_variant = variant;
    self->puzzle_variant_key = g_strdup(variant->short_name);
  }
  return TRUE;
}

static void ggame_window_sync_board_host_node(GGameWindow *self, const SgfNode *node) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(node != NULL);

  if (self->profile != NULL && self->profile->ui.sync_board_host_node != NULL && self->board_host != NULL) {
    self->profile->ui.sync_board_host_node(self->board_host, node);
  }
}

static const char *ggame_window_player_name_property_for_side(GGameWindow *self, guint side) {
  const GameBackend *backend = NULL;
  SgfColor color = SGF_COLOR_NONE;

  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);
  g_return_val_if_fail(side < 2, NULL);

  backend = self->profile != NULL ? self->profile->backend : NULL;
  if (backend != NULL && backend->sgf_color_for_side != NULL) {
    color = backend->sgf_color_for_side(side);
  }

  if (color == SGF_COLOR_BLACK) {
    return "PB";
  }
  if (color == SGF_COLOR_WHITE) {
    return "PW";
  }

  g_debug("Falling back to player name SGF property for side %u", side);
  return side == 0 ? "PB" : "PW";
}

static void ggame_window_set_root_text_property(SgfNode *root, const char *property, const char *value) {
  g_return_if_fail(root != NULL);
  g_return_if_fail(property != NULL);
  g_return_if_fail(value != NULL);

  sgf_node_clear_property(root, property);
  if (value[0] != '\0' && !sgf_node_add_property(root, property, value)) {
    g_debug("Failed to set SGF root property %s", property);
  }
}

static void ggame_window_set_new_game_computer_player_names(GGameWindow *self,
                                                            PlayerControlMode side0_mode,
                                                            PlayerControlMode side1_mode) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller));

  SgfTree *tree = ggame_sgf_controller_get_tree(self->sgf_controller);
  if (tree == NULL) {
    g_debug("Unable to set computer player names without an SGF tree");
    return;
  }

  SgfNode *root = (SgfNode *)sgf_tree_get_root(tree);
  if (root == NULL) {
    g_debug("Unable to set computer player names without an SGF root node");
    return;
  }

  PlayerControlMode modes[2] = {side0_mode, side1_mode};
  for (guint side = 0; side < 2; side++) {
    if (modes[side] != PLAYER_CONTROL_MODE_COMPUTER) {
      continue;
    }

    const char *property = ggame_window_player_name_property_for_side(self, side);
    if (property == NULL) {
      g_debug("Unable to set computer player name for side %u", side);
      continue;
    }
    ggame_window_set_root_text_property(root, property, "Computer");
  }
}

static void ggame_window_set_current_computer_player_names(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->controls_panel != NULL);

  ggame_window_set_new_game_computer_player_names(self,
                                                  player_controls_panel_get_mode(self->controls_panel, 0),
                                                  player_controls_panel_get_mode(self->controls_panel, 1));
}

static gboolean ggame_window_apply_player_move(gconstpointer move, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;

  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(backend->moves_equal != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller), FALSE);

  if (self->puzzle_mode) {
    if (self->puzzle_feedback_locked || self->puzzle_finished || self->puzzle_steps == NULL) {
      return FALSE;
    }
    if (self->puzzle_expected_step >= self->puzzle_steps->len) {
      g_debug("Puzzle move attempted after the solution was already complete");
      return FALSE;
    }

    GGameWindowPuzzleStep *expected =
        g_ptr_array_index(self->puzzle_steps, self->puzzle_expected_step);
    g_return_val_if_fail(expected != NULL, FALSE);
    (void)ggame_window_puzzle_attempt_ensure_started(self);
    gboolean failure_on_first_move = !self->puzzle_attempt_made_player_move;
    if (!backend->moves_equal(move, expected->move)) {
      (void)ggame_window_puzzle_attempt_finish_failure(self, failure_on_first_move, move);
      self->puzzle_feedback_locked = TRUE;
      ggame_window_set_puzzle_message(self, "");
      board_view_set_banner_text_red(self->board_view, "Wrong move");
      if (!ggame_model_apply_move(self->game_model, move)) {
        self->puzzle_feedback_locked = FALSE;
        ggame_window_clear_board_banner(self);
        ggame_window_set_default_puzzle_message(self);
        return FALSE;
      }
      ggame_window_clear_board_selection(self);
      self->puzzle_wrong_move_source_id = g_timeout_add_full(G_PRIORITY_DEFAULT,
                                                             GGAME_WINDOW_PUZZLE_WRONG_MOVE_DELAY_MS,
                                                             ggame_window_revert_wrong_puzzle_move_cb,
                                                             g_object_ref(self),
                                                             NULL);
      return TRUE;
    }

    if (!ggame_sgf_controller_apply_move(self->sgf_controller, move)) {
      return FALSE;
    }

    self->puzzle_attempt_made_player_move = TRUE;
    self->puzzle_expected_step++;
    return ggame_window_play_next_puzzle_step_if_needed(self);
  }

  if (!ggame_sgf_controller_apply_move(self->sgf_controller, move)) {
    return FALSE;
  }

  return TRUE;
}

guint ggame_window_get_analysis_depth(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT);
  g_return_val_if_fail(self->analysis_depth_scale != NULL, GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT);

  guint depth = (guint)gtk_range_get_value(GTK_RANGE(self->analysis_depth_scale));
  if (!ggame_window_analysis_depth_valid(depth)) {
    g_debug("Unexpected analysis depth value");
    return GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT;
  }

  return depth;
}

void ggame_window_set_analysis_depth(GGameWindow *self, guint depth) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(ggame_window_analysis_depth_valid(depth));
  g_return_if_fail(self->analysis_depth_scale != NULL);

  gtk_range_set_value(GTK_RANGE(self->analysis_depth_scale), (gdouble)depth);
}

static void ggame_window_set_ruleset(GGameWindow *self, PlayerRuleset ruleset) {
  const GameBackendVariant *variant = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_MODEL(self->game_model));
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller));

  variant = ggame_window_variant_for_ruleset(ruleset);
  if (variant == NULL) {
    return;
  }

  if (ggame_model_peek_variant(self->game_model) == variant) {
    self->applied_ruleset = ruleset;
    return;
  }

  ggame_model_reset(self->game_model, variant);
  ggame_window_clear_board_selection(self);
  ggame_sgf_controller_new_game(self->sgf_controller);
  self->applied_ruleset = ruleset;
}

void ggame_window_set_loaded_variant(GGameWindow *self, const GameBackendVariant *variant) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(variant != NULL);

  if (ggame_window_get_profile(self)->kind == GGAME_APP_KIND_CHECKERS) {
    if (!ggame_window_ruleset_from_variant(variant, &self->applied_ruleset)) {
      return;
    }
  }
}

static void ggame_window_set_analysis_text(GGameWindow *self, const char *text) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(text != NULL);

  if (!self->analysis_buffer) {
    g_debug("Missing analysis buffer");
    return;
  }

  gtk_text_buffer_set_text(self->analysis_buffer, text, -1);
}

static void ggame_window_set_analysis_status(GGameWindow *self, const char *text) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->analysis_status_label == NULL) {
    g_debug("Missing analysis status label");
    return;
  }
  if (!GTK_IS_LABEL(self->analysis_status_label)) {
    g_debug("Analysis status label is no longer a live GtkLabel");
    return;
  }

  gtk_label_set_text(self->analysis_status_label, text != NULL ? text : "");
}

static void ggame_window_show_analysis_for_current_node(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (!ggame_window_get_profile(self)->features.supports_analysis) {
    ggame_window_set_analysis_text(self, "");
    return;
  }

  SgfTree *tree = ggame_sgf_controller_get_tree(self->sgf_controller);
  const SgfNode *node = tree != NULL ? sgf_tree_get_current(tree) : NULL;
  if (node == NULL) {
    return;
  }

  g_autoptr(SgfNodeAnalysis) analysis = sgf_node_get_analysis(node);
  if (analysis != NULL) {
    g_autofree char *text = ggame_window_analysis_format_complete(analysis);
    if (text != NULL) {
      ggame_window_set_analysis_text(self, text);
    }
    return;
  }

  ggame_window_set_analysis_text(self, "");
}

static gboolean ggame_window_node_first_score(const SgfNode *node, gint *out_score) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);

  g_autoptr(SgfNodeAnalysis) analysis = sgf_node_get_analysis(node);
  if (analysis == NULL || analysis->moves == NULL || analysis->moves->len == 0) {
    return FALSE;
  }

  const SgfNodeScoredMove *entry = g_ptr_array_index(analysis->moves, 0);
  if (entry == NULL) {
    return FALSE;
  }

  *out_score = entry->score;
  return TRUE;
}

static void ggame_window_analysis_event_free(gpointer data) {
  GGameWindowAnalysisEvent *event = data;
  if (event == NULL) {
    return;
  }

  g_clear_pointer(&event->status_text, g_free);
  sgf_node_analysis_free(event->analysis);
  g_free(event);
}

static void ggame_window_refresh_analysis_graph(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(ANALYSIS_IS_GRAPH(self->analysis_graph));

  if (!ggame_window_get_profile(self)->features.supports_analysis) {
    analysis_graph_set_nodes(self->analysis_graph, NULL, 0);
    analysis_graph_clear_progress_node(self->analysis_graph);
    return;
  }

  SgfTree *tree = ggame_sgf_controller_get_tree(self->sgf_controller);
  if (tree == NULL) {
    analysis_graph_set_nodes(self->analysis_graph, NULL, 0);
    analysis_graph_clear_progress_node(self->analysis_graph);
    return;
  }

  g_autoptr(GPtrArray) branch = sgf_tree_build_current_branch(tree);
  if (branch == NULL) {
    analysis_graph_set_nodes(self->analysis_graph, NULL, 0);
    analysis_graph_clear_progress_node(self->analysis_graph);
    return;
  }

  const SgfNode *current = sgf_tree_get_current(tree);
  guint selected_index = 0;
  for (guint i = 0; i < branch->len; ++i) {
    if (g_ptr_array_index(branch, i) == current) {
      selected_index = i;
      break;
    }
  }

  guint analyzed_count = 0;
  for (guint i = 0; i < branch->len; ++i) {
    const SgfNode *node = g_ptr_array_index(branch, i);
    if (node == NULL) {
      continue;
    }
    gint score = 0;
    gboolean has_score = ggame_window_node_first_score(node, &score);
    (void)score;
    (void)current;
    if (has_score) {
      analyzed_count++;
    }
  }
  (void)analyzed_count;

  analysis_graph_set_nodes(self->analysis_graph, branch, selected_index);
  if (self->analysis_mode != GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME || self->analysis_last_updated_node == NULL) {
    analysis_graph_clear_progress_node(self->analysis_graph);
    return;
  }

  for (guint i = 0; i < branch->len; ++i) {
    const SgfNode *branch_node = g_ptr_array_index(branch, i);
    if (branch_node == self->analysis_last_updated_node) {
      analysis_graph_set_progress_node(self->analysis_graph, branch_node);
      return;
    }
  }
  analysis_graph_clear_progress_node(self->analysis_graph);
}

typedef struct {
  gint score;
  gint max_distance;
} GGameWindowAnalysisWinScore;

static const GGameWindowAnalysisWinScore ggame_window_analysis_win_scores[] = {
  {1000, 100},
  {3000, 100},
  {10000, 100},
  {100000, 1000}
};

char *ggame_window_format_analysis_score(gint score) {
  gint abs_score = ABS(score);

  for (guint i = 0; i < G_N_ELEMENTS(ggame_window_analysis_win_scores); ++i) {
    gint win_score = ggame_window_analysis_win_scores[i].score;
    gint min_score = win_score - ggame_window_analysis_win_scores[i].max_distance;

    if (abs_score >= min_score && abs_score <= win_score) {
      gint distance = win_score - abs_score;
      return g_strdup_printf("%c#%d", score > 0 ? 'W' : 'B', distance);
    }
  }

  return g_strdup_printf("%+d", score);
}

static void ggame_window_analysis_append_scored_moves(GString *text, const SgfNodeAnalysis *analysis) {
  g_return_if_fail(text != NULL);
  g_return_if_fail(analysis != NULL);
  g_return_if_fail(analysis->moves != NULL);

  gsize score_width = 0;
  for (guint i = 0; i < analysis->moves->len; ++i) {
    const SgfNodeScoredMove *entry = g_ptr_array_index(analysis->moves, i);
    if (entry == NULL) {
      continue;
    }

    g_autofree char *score_text = ggame_window_format_analysis_score(entry->score);
    if (score_text == NULL) {
      g_debug("Failed to format analysis score");
      continue;
    }

    score_width = MAX(score_width, strlen(score_text));
  }

  for (guint i = 0; i < analysis->moves->len; ++i) {
    const SgfNodeScoredMove *entry = g_ptr_array_index(analysis->moves, i);
    if (entry == NULL) {
      continue;
    }
    const char *notation = entry->move_text != NULL ? entry->move_text : "?";

    g_autofree char *score_text = ggame_window_format_analysis_score(entry->score);
    if (score_text == NULL) {
      g_debug("Failed to format analysis score");
      continue;
    }
    g_string_append_printf(text, "%*s  %s\n", (gint)score_width, score_text, notation);
  }
}

char *ggame_window_format_analysis_report(const SgfNodeAnalysis *analysis) {
  g_return_val_if_fail(analysis != NULL, NULL);
  g_return_val_if_fail(analysis->moves != NULL, NULL);

  GString *text = g_string_new(NULL);
  g_string_append_printf(text, "Analysis depth: %u\n", analysis->depth);
  ggame_window_analysis_append_scored_moves(text, analysis);
  return g_string_free(text, FALSE);
}

char *ggame_window_format_analysis_status(const SgfNodeAnalysis *analysis) {
  g_return_val_if_fail(analysis != NULL, NULL);

  return g_strdup_printf("Analysis depth: %u\nNodes: %" G_GUINT64_FORMAT, analysis->depth, analysis->nodes);
}

static char *ggame_window_analysis_format_complete(const SgfNodeAnalysis *analysis) {
  return ggame_window_format_analysis_report(analysis);
}

static void ggame_window_stop_analysis(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->analysis_mode == GGAME_WINDOW_ANALYSIS_MODE_NONE) {
    ggame_window_analysis_finish_session(self);
    return;
  }

  g_atomic_int_inc(&self->analysis_generation);

  g_mutex_lock(&self->analysis_report_mutex);
  if (self->analysis_report_queue != NULL) {
    g_queue_clear_full(self->analysis_report_queue, ggame_window_analysis_event_free);
  }
  g_mutex_unlock(&self->analysis_report_mutex);
  ggame_window_analysis_finish_session(self);
}

static void ggame_window_update_control_state(GGameWindow *self) {
  const GameBackend *backend = NULL;
  gconstpointer position = NULL;
  GameBackendOutcome outcome = GAME_BACKEND_OUTCOME_ONGOING;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->board_view != NULL);

  backend = ggame_window_get_game_backend(self);
  position = ggame_window_get_game_position(self);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(position != NULL);
  g_return_if_fail(backend->position_outcome != NULL);

  outcome = backend->position_outcome(position);
  gboolean input_enabled = outcome == GAME_BACKEND_OUTCOME_ONGOING;
  if (ggame_window_uses_square_board(self)) {
    board_view_set_input_enabled(self->board_view, input_enabled);
  }
}

static void ggame_window_update_status(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_MODEL(self->game_model));

  if (ggame_window_uses_square_board(self)) {
    board_view_update(self->board_view);
  }
}

static void ggame_window_on_state_changed(GGameModel *model, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_if_fail(GGAME_IS_MODEL(model));
  g_return_if_fail(GGAME_IS_WINDOW(self));

  ggame_window_sync_board_orientation(self);
  ggame_window_update_status(self);
  ggame_window_update_control_state(self);
  ggame_window_refresh_analysis_graph(self);
}

static void ggame_window_on_control_changed(PlayerControlsPanel * /*panel*/, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_if_fail(GGAME_IS_WINDOW(self));

  ggame_window_sync_board_orientation(self);
  ggame_window_update_control_state(self);
}

static void ggame_window_on_mode_selected_notify(GObject * /*object*/,
                                                     GParamSpec * /*pspec*/,
                                                     gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GTK_IS_DROP_DOWN(self->sgf_mode_control));

  if (self->puzzle_mode) {
    gtk_drop_down_set_selected(self->sgf_mode_control, 0);
    return;
  }

  self->edit_mode_enabled = gtk_drop_down_get_selected(self->sgf_mode_control) == 1;
  ggame_window_clear_board_selection(self);
  ggame_window_sync_mode_ui(self);
}

static void ggame_window_load_default_size(gint *out_width, gint *out_height) {
  g_return_if_fail(out_width != NULL);
  g_return_if_fail(out_height != NULL);

  *out_width = GGAME_WINDOW_DEFAULT_WIDTH;
  *out_height = GGAME_WINDOW_DEFAULT_HEIGHT;

  g_autoptr(GSettings) settings = ggame_common_settings_create();
  if (!G_IS_SETTINGS(settings)) {
    return;
  }

  gint saved_width = g_settings_get_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_WIDTH);
  gint saved_height = g_settings_get_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_HEIGHT);
  if (saved_width > 0 && saved_height > 0) {
    *out_width = saved_width;
    *out_height = saved_height;
  }
}

static void ggame_window_load_saved_layout(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  g_autoptr(GSettings) settings = ggame_common_settings_create();
  if (!G_IS_SETTINGS(settings)) {
    return;
  }

  gint board_panel_width = g_settings_get_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_BOARD_PANEL_WIDTH);
  if (board_panel_width > 0) {
    self->board_panel_width = board_panel_width;
  }

  gint navigation_panel_width = g_settings_get_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_NAVIGATION_PANEL_WIDTH);
  if (navigation_panel_width > 0) {
    self->navigation_panel_width = navigation_panel_width;
  }

  gint analysis_panel_width = g_settings_get_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_ANALYSIS_PANEL_WIDTH);
  if (analysis_panel_width > 0) {
    self->analysis_panel_width = analysis_panel_width;
  }

  if (!g_settings_get_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_LAYOUT_SAVED)) {
    return;
  }

  self->show_navigation_drawer =
      g_settings_get_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_NAVIGATION_DRAWER);
  self->show_analysis_drawer =
      g_settings_get_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_ANALYSIS_DRAWER);
  self->show_move_report = g_settings_get_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_MOVE_REPORT);
}

static void ggame_window_save_default_size(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->layout_mode == GGAME_WINDOW_LAYOUT_MODE_NORMAL &&
      (self->show_navigation_drawer ||
       self->show_analysis_drawer ||
       (self->drawer_host != NULL && gtk_widget_get_parent(self->drawer_host) != NULL))) {
    ggame_window_capture_panel_widths(self);
  }

  gint width = gtk_widget_get_width(GTK_WIDGET(self));
  gint height = gtk_widget_get_height(GTK_WIDGET(self));
  if (width <= 0 || height <= 0) {
    gtk_window_get_default_size(GTK_WINDOW(self), &width, &height);
  }
  if (width <= 0 || height <= 0) {
    return;
  }

  g_autoptr(GSettings) settings = ggame_common_settings_create();
  if (!G_IS_SETTINGS(settings)) {
    return;
  }

  g_settings_set_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_WIDTH, width);
  g_settings_set_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_HEIGHT, height);
  g_settings_set_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_LAYOUT_SAVED, TRUE);
  g_settings_set_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_BOARD_PANEL_WIDTH, self->board_panel_width);
  g_settings_set_int(settings,
                     GGAME_COMMON_SETTINGS_KEY_WINDOW_NAVIGATION_PANEL_WIDTH,
                     self->navigation_panel_width);
  g_settings_set_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_ANALYSIS_PANEL_WIDTH, self->analysis_panel_width);

  gboolean show_navigation_drawer = self->show_navigation_drawer;
  gboolean show_analysis_drawer = self->show_analysis_drawer;
  if (self->puzzle_mode) {
    show_navigation_drawer = self->puzzle_saved_show_navigation_drawer;
    show_analysis_drawer = self->puzzle_saved_show_analysis_drawer;
  }

  g_settings_set_boolean(settings,
                         GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_NAVIGATION_DRAWER,
                         show_navigation_drawer);
  g_settings_set_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_ANALYSIS_DRAWER, show_analysis_drawer);
  g_settings_set_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_MOVE_REPORT, self->show_move_report);
}

static gboolean ggame_window_on_close_request(GtkWindow *window, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(GTK_IS_WINDOW(window), FALSE);

  ggame_window_save_default_size(self);
  return FALSE;
}

static void ggame_window_on_default_size_notify(GObject *object,
                                                    GParamSpec *pspec,
                                                    gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GTK_IS_WINDOW(object));
  g_return_if_fail(pspec != NULL);

  if (self->syncing_layout_default_size) {
    return;
  }
  if (!self->puzzle_mode || self->layout_mode != GGAME_WINDOW_LAYOUT_MODE_PUZZLE) {
    return;
  }
  if (g_strcmp0(g_param_spec_get_name(pspec), "default-width") != 0) {
    return;
  }

  gint default_width = -1;
  gtk_window_get_default_size(GTK_WINDOW(self), &default_width, NULL);
  gint expected_width = ggame_window_expected_default_width(self);
  if (default_width == expected_width || default_width <= 0) {
    return;
  }

  ggame_window_apply_saved_panel_widths(self);
  gtk_widget_queue_allocate(GTK_WIDGET(self));
}

static void ggame_window_on_manual_requested(GGameSgfController * /*controller*/,
                                                 gpointer /*node*/,
                                                 gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->controls_panel != NULL);

  g_clear_handle_id(&self->auto_move_source_id, g_source_remove);
  ggame_window_set_board_orientation_mode(self, GGAME_WINDOW_BOARD_ORIENTATION_FIXED);
  ggame_window_update_control_state(self);
  ggame_window_show_analysis_for_current_node(self);
  ggame_window_refresh_analysis_graph(self);
}

static void ggame_window_on_sgf_node_changed(GGameSgfController * /*controller*/,
                                                 const SgfNode *node,
                                                 gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(node != NULL);

  ggame_window_sync_board_host_node(self, node);
  ggame_window_show_analysis_for_current_node(self);
  ggame_window_refresh_analysis_graph(self);
}

static void ggame_window_on_analysis_graph_node_activated(AnalysisGraph * /*graph*/,
                                                              const SgfNode *node,
                                                              gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(node != NULL);
}

static void ggame_window_on_force_move_action(GSimpleAction * /*action*/,
                                                  GVariant * /*parameter*/,
                                                  gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
}

static void ggame_window_on_game_information_action(GSimpleAction * /*action*/,
                                                    GVariant * /*parameter*/,
                                                    gpointer /*user_data*/) {
}

static void ggame_window_on_library_action(GSimpleAction * /*action*/,
                                           GVariant * /*parameter*/,
                                           gpointer /*user_data*/) {
}

static void ggame_window_on_puzzle_next_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
}

static void ggame_window_on_puzzle_analyze_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
}

static void ggame_window_on_sgf_rewind(GSimpleAction * /*action*/,
                                           GVariant * /*parameter*/,
                                           gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
}

static void ggame_window_on_sgf_step_backward(GSimpleAction * /*action*/,
                                                  GVariant * /*parameter*/,
                                                  gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
}

static void ggame_window_on_sgf_step_forward(GSimpleAction * /*action*/,
                                                 GVariant * /*parameter*/,
                                                 gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
}

static void ggame_window_on_sgf_step_forward_to_branch(GSimpleAction * /*action*/,
                                                           GVariant * /*parameter*/,
                                                           gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
}

static void ggame_window_on_sgf_step_forward_to_end(GSimpleAction * /*action*/,
                                                        GVariant * /*parameter*/,
                                                        gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
}

static void ggame_window_on_sgf_delete_node(GSimpleAction * /*action*/,
                                            GVariant * /*parameter*/,
                                            gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
}

static GtkWidget *ggame_window_new_toolbar_action_button(const char *icon_name,
                                                             const char *tooltip_text,
                                                             const char *action_name) {
  g_return_val_if_fail(icon_name != NULL, NULL);
  g_return_val_if_fail(tooltip_text != NULL, NULL);
  g_return_val_if_fail(action_name != NULL, NULL);

  GtkWidget *button = gtk_button_new_from_icon_name(icon_name);
  gtk_widget_set_tooltip_text(button, tooltip_text);
  gtk_actionable_set_action_name(GTK_ACTIONABLE(button), action_name);
  return button;
}

const GameBackendVariant *ggame_window_get_variant(GGameWindow *self) {
  const GameBackendVariant *variant = NULL;

  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  variant = self->game_model != NULL ? ggame_model_peek_variant(self->game_model) : NULL;
  if (variant != NULL) {
    return variant;
  }

  if (ggame_window_get_profile(self)->kind == GGAME_APP_KIND_CHECKERS) {
    return ggame_window_variant_for_ruleset(self->applied_ruleset);
  }

  return NULL;
}

void ggame_window_apply_new_game_settings(GGameWindow *self,
                                          const GameBackendVariant *variant,
                                              PlayerControlMode white_mode,
                                              PlayerControlMode black_mode,
                                              guint computer_depth) {
  const GameBackend *backend = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->controls_panel != NULL);
  backend = ggame_window_get_game_backend(self);
  g_return_if_fail(backend != NULL);
  if (backend->variant_count > 0) {
    g_return_if_fail(variant != NULL);
  }

  player_controls_panel_set_mode(self->controls_panel, 0, white_mode);
  player_controls_panel_set_mode(self->controls_panel, 1, black_mode);
  player_controls_panel_set_computer_depth(self->controls_panel, computer_depth);

  ggame_window_set_board_bottom_color(self, CHECKERS_COLOR_WHITE);
  if (white_mode == PLAYER_CONTROL_MODE_USER && black_mode == PLAYER_CONTROL_MODE_USER) {
    ggame_window_set_board_orientation_mode(self, GGAME_WINDOW_BOARD_ORIENTATION_FOLLOW_TURN);
  } else if (white_mode != black_mode) {
    ggame_window_set_board_orientation_mode(self, GGAME_WINDOW_BOARD_ORIENTATION_FOLLOW_PLAYER);
  } else {
    ggame_window_set_board_orientation_mode(self, GGAME_WINDOW_BOARD_ORIENTATION_FIXED);
  }

  if (backend->variant_count > 0 && ggame_window_get_profile(self)->kind == GGAME_APP_KIND_CHECKERS) {
    PlayerRuleset ruleset = PLAYER_RULESET_INTERNATIONAL;
    if (!ggame_window_ruleset_from_variant(variant, &ruleset)) {
      return;
    }
    ggame_window_set_ruleset(self, ruleset);
  }
  ggame_window_start_new_game(self);
  ggame_window_set_current_computer_player_names(self);
}

void ggame_window_set_board_orientation_mode(GGameWindow *self,
                                                 GGameWindowBoardOrientationMode mode) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(ggame_window_board_orientation_mode_valid(mode));

  if (self->board_orientation_mode == mode) {
    return;
  }

  self->board_orientation_mode = mode;
  ggame_window_sync_board_orientation(self);
}

void ggame_window_set_board_bottom_color(GGameWindow *self, CheckersColor bottom_color) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(bottom_color == CHECKERS_COLOR_WHITE || bottom_color == CHECKERS_COLOR_BLACK);

  self->board_bottom_color = bottom_color;
  ggame_window_sync_board_orientation(self);
}

CheckersColor ggame_window_get_board_bottom_color(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), CHECKERS_COLOR_WHITE);

  return self->board_bottom_color;
}

static void ggame_window_set_model(GGameWindow *self, GGameModel *model) {
  const GameBackendVariant *variant = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_MODEL(model));

  g_clear_handle_id(&self->auto_move_source_id, g_source_remove);

  if (self->game_model != NULL && self->state_handler_id != 0) {
    g_signal_handler_disconnect(self->game_model, self->state_handler_id);
    self->state_handler_id = 0;
  }
  g_clear_object(&self->game_model);

  self->game_model = g_object_ref(model);
  self->state_handler_id = g_signal_connect(self->game_model,
                                            "state-changed",
                                            G_CALLBACK(ggame_window_on_state_changed),
                                            self);
  if (ggame_window_uses_square_board(self)) {
    board_view_set_model(self->board_view, self->game_model);
  }
  ggame_sgf_controller_set_game_model(self->sgf_controller, self->game_model);
  ggame_window_rebuild_board_host(self);
  variant = ggame_model_peek_variant(self->game_model);
  if (variant != NULL && self->profile != NULL && self->profile->kind == GGAME_APP_KIND_CHECKERS) {
    (void)ggame_window_ruleset_from_variant(variant, &self->applied_ruleset);
  }
  ggame_window_sync_board_orientation(self);
  ggame_window_update_status(self);
  ggame_window_update_control_state(self);
  ggame_window_set_current_computer_player_names(self);
  ggame_window_refresh_analysis_graph(self);
}

static void ggame_window_rebuild_board_host(GGameWindow *self) {
  GtkWidget *host = NULL;
  GtkWidget *board_widget = NULL;
  GGameAppBoardHostOptions host_options = {
    .move_report_enabled = self->show_move_report,
  };

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->board_view != NULL);
  g_return_if_fail(self->board_host_box != NULL);
  g_return_if_fail(GGAME_IS_MODEL(self->game_model));

  if (self->board_host != NULL) {
    ggame_widget_remove_from_parent(self->board_host);
    self->board_host = NULL;
  }

  if (self->profile != NULL && self->profile->ui.create_board_host != NULL) {
    host = self->profile->ui.create_board_host(self->game_model,
                                               self->board_view,
                                               ggame_window_apply_player_move,
                                               self,
                                               &host_options);
    g_return_if_fail(GTK_IS_WIDGET(host));
  } else {
    g_return_if_fail(ggame_window_uses_square_board(self));
    board_widget = board_view_get_widget(self->board_view);
    g_return_if_fail(GTK_IS_WIDGET(board_widget));
    if (gtk_widget_get_parent(board_widget) != NULL) {
      ggame_widget_remove_from_parent(board_widget);
    }

    host = gtk_aspect_frame_new(0.5f, 0.5f, 1.0f, FALSE);
    gtk_widget_set_hexpand(host, TRUE);
    gtk_widget_set_vexpand(host, TRUE);
    gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(host), board_widget);
  }

  self->board_host = host;
  gtk_box_append(GTK_BOX(self->board_host_box), host);
  ggame_window_sync_move_report_ui(self);
}

static gboolean ggame_window_unparent_controls_panel(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  if (!self->controls_panel) {
    return TRUE;
  }

  GtkWidget *panel_widget = GTK_WIDGET(self->controls_panel);
  gboolean removed = ggame_widget_remove_from_parent(panel_widget);
  if (!removed && gtk_widget_get_parent(panel_widget)) {
    g_debug("Failed to remove controls panel from parent during dispose\n");
    return FALSE;
  }

  return TRUE;
}

static void ggame_window_dispose(GObject *object) {
  GGameWindow *self = GGAME_WINDOW(object);

  self->edit_mode_enabled = FALSE;
  ggame_window_sync_mode_ui(self);

  if (self->game_model != NULL && self->state_handler_id != 0) {
    g_signal_handler_disconnect(self->game_model, self->state_handler_id);
    self->state_handler_id = 0;
  }

  gboolean panel_removed = ggame_window_unparent_controls_panel(self);
  g_clear_handle_id(&self->auto_move_source_id, g_source_remove);
  g_clear_handle_id(&self->puzzle_wrong_move_source_id, g_source_remove);
  if (self->puzzle_attempt_started && !ggame_window_puzzle_attempt_is_terminal(self)) {
    (void)ggame_window_puzzle_attempt_finish_failure(self, FALSE, NULL);
  }

  ggame_window_unparent_controls_panel(self);
  self->analysis_status_label = NULL;
  self->analysis_buffer = NULL;

  ggame_window_stop_analysis(self);
  if (self->paned_tick_id != 0 && self->main_paned) {
    gtk_widget_remove_tick_callback(self->main_paned, self->paned_tick_id);
    self->paned_tick_id = 0;
  }
  g_clear_object(&self->sgf_controller);
  g_clear_object(&self->analysis_graph);
  if (panel_removed) {
    g_clear_object(&self->controls_panel);
  } else {
    self->controls_panel = NULL;
  }
  if (self->navigation_panel != NULL) {
    ggame_widget_remove_from_parent(self->navigation_panel);
    g_clear_object(&self->navigation_panel);
  }
  if (self->analysis_panel != NULL) {
    ggame_widget_remove_from_parent(self->analysis_panel);
    g_clear_object(&self->analysis_panel);
  }
  g_clear_pointer(&self->loaded_source_name, g_free);
  if (self->puzzle_steps != NULL) {
    g_ptr_array_unref(self->puzzle_steps);
    self->puzzle_steps = NULL;
  }
  if (self->drawer_split != NULL) {
    ggame_widget_remove_from_parent(self->drawer_split);
    g_clear_object(&self->drawer_split);
  }
  if (self->drawer_host != NULL) {
    ggame_widget_remove_from_parent(self->drawer_host);
    g_clear_object(&self->drawer_host);
  }
  if (self->board_host != NULL) {
    ggame_widget_remove_from_parent(self->board_host);
    self->board_host = NULL;
  }
  g_clear_object(&self->board_view);
  g_clear_object(&self->game_model);
  ggame_window_puzzle_attempt_reset(self);
  g_clear_pointer(&self->puzzle_variant_key, g_free);
  g_clear_pointer(&self->puzzle_path, g_free);
  self->puzzle_progress_store = NULL;
  self->main_paned = NULL;
  self->board_panel = NULL;
  self->board_host_box = NULL;
  self->puzzle_panel = NULL;
  self->puzzle_message_label = NULL;
  self->puzzle_next_button = NULL;
  self->puzzle_analyze_button = NULL;
  self->analysis_depth_scale = NULL;
  self->analysis_buffer = NULL;
  self->sgf_mode_control = NULL;
  G_OBJECT_CLASS(ggame_window_parent_class)->dispose(object);
}

static void ggame_window_finalize(GObject *object) {
  GGameWindow *self = GGAME_WINDOW(object);

  if (self->analysis_report_queue != NULL) {
    g_queue_free_full(self->analysis_report_queue, ggame_window_analysis_event_free);
    self->analysis_report_queue = NULL;
  }
  g_mutex_clear(&self->analysis_report_mutex);

  G_OBJECT_CLASS(ggame_window_parent_class)->finalize(object);
}

static void ggame_window_class_init(GGameWindowClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = ggame_window_dispose;
  object_class->finalize = ggame_window_finalize;
}

static void ggame_window_init(GGameWindow *self) {
  const GGameAppLayout *layout = NULL;

  self->profile = ggame_active_app_profile();
  layout = self->profile != NULL ? &self->profile->layout : NULL;
  self->auto_move_source_id = 0;
  self->paned_tick_id = 0;
  self->analysis_mode = GGAME_WINDOW_ANALYSIS_MODE_NONE;
  self->analysis_generation = 1;
  self->puzzle_wrong_move_source_id = 0;
  self->syncing_layout_default_size = FALSE;
  g_mutex_init(&self->analysis_report_mutex);
  self->analysis_report_queue = g_queue_new();
  ggame_window_analysis_reset_runtime_state(self);
  self->applied_ruleset = PLAYER_RULESET_INTERNATIONAL;

  static const GActionEntry window_actions[] = {
      {
          .name = "game-force-move",
          .activate = ggame_window_on_force_move_action,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "game-information",
          .activate = ggame_window_on_game_information_action,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "library",
          .activate = ggame_window_on_library_action,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-rewind",
          .activate = ggame_window_on_sgf_rewind,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-step-backward",
          .activate = ggame_window_on_sgf_step_backward,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-step-forward",
          .activate = ggame_window_on_sgf_step_forward,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-step-forward-to-branch",
          .activate = ggame_window_on_sgf_step_forward_to_branch,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-step-forward-to-end",
          .activate = ggame_window_on_sgf_step_forward_to_end,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "sgf-delete-node",
          .activate = ggame_window_on_sgf_delete_node,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "view-show-navigation-drawer",
          .activate = NULL,
          .parameter_type = NULL,
          .state = "true",
          .change_state = ggame_window_on_show_navigation_drawer_change_state,
          .padding = {0},
      },
      {
          .name = "view-show-analysis-drawer",
          .activate = NULL,
          .parameter_type = NULL,
          .state = "true",
          .change_state = ggame_window_on_show_analysis_drawer_change_state,
          .padding = {0},
      },
      {
          .name = "view-show-move-report",
          .activate = NULL,
          .parameter_type = NULL,
          .state = "true",
          .change_state = ggame_window_on_show_move_report_change_state,
          .padding = {0},
      },
  };
  g_action_map_add_action_entries(G_ACTION_MAP(self),
                                  window_actions,
                                  G_N_ELEMENTS(window_actions),
                                  self);
  ggame_window_install_sgf_file_actions(self);
  ggame_window_sync_title(self);
  gint default_width = 0;
  gint default_height = 0;
  ggame_window_load_default_size(&default_width, &default_height);
  gtk_window_set_default_size(GTK_WINDOW(self), default_width, default_height);
  g_signal_connect(self, "close-request", G_CALLBACK(ggame_window_on_close_request), self);

  ggame_style_init();

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(GTK_WINDOW(self), content);

  GApplication *app = g_application_get_default();
  if (GTK_IS_APPLICATION(app)) {
    GMenuModel *menubar = gtk_application_get_menubar(GTK_APPLICATION(app));
    if (menubar != NULL) {
      GtkWidget *menu_bar = gtk_popover_menu_bar_new_from_model(menubar);
      gtk_box_append(GTK_BOX(content), menu_bar);
    }
  }

  GtkWidget *toolbar = gtk_action_bar_new();
  GtkWidget *new_game_button =
      ggame_window_new_toolbar_action_button("document-new-symbolic", "New game...", "app.new-game");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), new_game_button);

  GtkWidget *force_move_button =
      ggame_window_new_toolbar_action_button("media-playback-start-symbolic",
                                                 "Force move",
                                                 "win.game-force-move");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), force_move_button);

  GtkWidget *toolbar_separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), toolbar_separator);

  GtkWidget *rewind_button = ggame_window_new_toolbar_action_button("media-skip-backward-symbolic",
                                                                         "Rewind to start",
                                                                         "win.navigation-rewind");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), rewind_button);

  GtkWidget *step_backward_button =
      ggame_window_new_toolbar_action_button("go-previous-symbolic",
                                                 "Back one move",
                                                 "win.navigation-step-backward");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_backward_button);

  GtkWidget *step_forward_button = ggame_window_new_toolbar_action_button("go-next-symbolic",
                                                                               "Forward one move",
                                                                               "win.navigation-step-forward");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_forward_button);

  GtkWidget *step_to_branch_button =
      ggame_window_new_toolbar_action_button("media-seek-forward-symbolic",
                                                 "Forward to next branch point",
                                                 "win.navigation-step-forward-to-branch");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_to_branch_button);

  GtkWidget *step_to_end_button = ggame_window_new_toolbar_action_button("media-skip-forward-symbolic",
                                                                              "Forward to main line end",
                                                                              "win.navigation-step-forward-to-end");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_to_end_button);

  gtk_box_append(GTK_BOX(content), toolbar);

  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand(paned, TRUE);
  gtk_widget_set_vexpand(paned, TRUE);
  gtk_box_append(GTK_BOX(content), paned);
  self->main_paned = paned;
  g_object_set_data(G_OBJECT(self), "main-paned", paned);
  self->paned_tick_id = gtk_widget_add_tick_callback(paned,
                                                      ggame_window_constrain_main_split_cb,
                                                      self,
                                                      NULL);

  GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(left_panel, TRUE);
  gtk_widget_set_vexpand(left_panel, TRUE);
  gtk_widget_set_margin_top(left_panel, 8);
  gtk_widget_set_margin_bottom(left_panel, 8);
  gtk_widget_set_margin_start(left_panel, 8);
  gtk_widget_set_margin_end(left_panel, 8);
  gtk_paned_set_start_child(GTK_PANED(paned), left_panel);
  gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
  self->board_panel = left_panel;
  g_object_set_data(G_OBJECT(self), "board-panel", left_panel);

  self->controls_panel = g_object_ref_sink(player_controls_panel_new());
  if (self->profile != NULL) {
    player_controls_panel_set_computer_depth(self->controls_panel, self->profile->default_computer_depth);
  }
  ggame_window_sync_side_labels(self);
  gtk_box_append(GTK_BOX(left_panel), GTK_WIDGET(self->controls_panel));
  g_signal_connect(self->controls_panel,
                   "control-changed",
                   G_CALLBACK(ggame_window_on_control_changed),
                   self);

  GtkWidget *puzzle_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_bottom(puzzle_panel, 8);
  gtk_widget_set_visible(puzzle_panel, FALSE);
  gtk_box_append(GTK_BOX(left_panel), puzzle_panel);
  self->puzzle_panel = puzzle_panel;
  g_object_set_data(G_OBJECT(self), "puzzle-panel", puzzle_panel);

  GtkWidget *puzzle_title = gtk_label_new("Puzzle mode");
  gtk_widget_set_halign(puzzle_title, GTK_ALIGN_START);
  gtk_widget_add_css_class(puzzle_title, "title-4");
  gtk_box_append(GTK_BOX(puzzle_panel), puzzle_title);

  GtkWidget *puzzle_message = gtk_label_new("");
  gtk_label_set_wrap(GTK_LABEL(puzzle_message), TRUE);
  gtk_widget_set_halign(puzzle_message, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(puzzle_panel), puzzle_message);
  self->puzzle_message_label = GTK_LABEL(puzzle_message);
  g_object_set_data(G_OBJECT(self), "puzzle-message-label", puzzle_message);

  GtkWidget *puzzle_button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append(GTK_BOX(puzzle_panel), puzzle_button_row);

  GtkWidget *puzzle_next_button = gtk_button_new_with_label("Next puzzle");
  g_signal_connect(puzzle_next_button,
                   "clicked",
                   G_CALLBACK(ggame_window_on_puzzle_next_clicked),
                   self);
  gtk_box_append(GTK_BOX(puzzle_button_row), puzzle_next_button);
  self->puzzle_next_button = GTK_BUTTON(puzzle_next_button);
  g_object_set_data(G_OBJECT(self), "puzzle-next-button", puzzle_next_button);

  GtkWidget *puzzle_analyze_button = gtk_button_new_with_label("Analyze");
  g_signal_connect(puzzle_analyze_button,
                   "clicked",
                   G_CALLBACK(ggame_window_on_puzzle_analyze_clicked),
                   self);
  gtk_box_append(GTK_BOX(puzzle_button_row), puzzle_analyze_button);
  self->puzzle_analyze_button = GTK_BUTTON(puzzle_analyze_button);
  g_object_set_data(G_OBJECT(self), "puzzle-analyze-button", puzzle_analyze_button);

  self->board_view = board_view_new();
  GtkWidget *board_host_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(board_host_box, TRUE);
  gtk_widget_set_vexpand(board_host_box, TRUE);
  gtk_box_append(GTK_BOX(left_panel), board_host_box);
  self->board_host_box = board_host_box;
  g_object_set_data(G_OBJECT(self), "board-host-box", board_host_box);

  GtkWidget *right_split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  g_object_ref_sink(right_split);
  gtk_widget_set_hexpand(right_split, TRUE);
  gtk_widget_set_vexpand(right_split, TRUE);
  self->drawer_split = right_split;
  g_object_set_data(G_OBJECT(self), "drawer-split", right_split);

  GtkWidget *drawer_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_object_ref_sink(drawer_host);
  gtk_widget_set_hexpand(drawer_host, TRUE);
  gtk_widget_set_vexpand(drawer_host, TRUE);
  gtk_paned_set_end_child(GTK_PANED(paned), drawer_host);
  gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
  self->drawer_host = drawer_host;
  g_object_set_data(G_OBJECT(self), "drawer-host", drawer_host);

  GtkWidget *middle_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_object_ref_sink(middle_panel);
  gtk_widget_set_hexpand(middle_panel, TRUE);
  gtk_widget_set_vexpand(middle_panel, TRUE);
  gtk_widget_set_margin_top(middle_panel, 8);
  gtk_widget_set_margin_bottom(middle_panel, 8);
  gtk_widget_set_margin_start(middle_panel, 8);
  gtk_widget_set_margin_end(middle_panel, 8);
  gtk_paned_set_start_child(GTK_PANED(right_split), middle_panel);
  gtk_paned_set_shrink_start_child(GTK_PANED(right_split), FALSE);
  self->navigation_panel = middle_panel;
  g_object_set_data(G_OBJECT(self), "navigation-panel", middle_panel);

  GtkWidget *analysis_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_object_ref_sink(analysis_panel);
  gtk_widget_set_hexpand(analysis_panel, TRUE);
  gtk_widget_set_vexpand(analysis_panel, TRUE);
  gtk_widget_set_margin_top(analysis_panel, 8);
  gtk_widget_set_margin_bottom(analysis_panel, 8);
  gtk_widget_set_margin_start(analysis_panel, 8);
  gtk_widget_set_margin_end(analysis_panel, 8);
  gtk_paned_set_end_child(GTK_PANED(right_split), analysis_panel);
  gtk_paned_set_shrink_end_child(GTK_PANED(right_split), FALSE);
  self->analysis_panel = analysis_panel;
  g_object_set_data(G_OBJECT(self), "analysis-panel", analysis_panel);
  gtk_paned_set_position(GTK_PANED(paned), 500);
  gtk_paned_set_position(GTK_PANED(right_split), 300);

  GtkWidget *sgf_mode_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *sgf_mode_label = gtk_label_new("Mode");
  gtk_widget_set_halign(sgf_mode_label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(sgf_mode_row), sgf_mode_label);
  self->sgf_mode_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(
      (const char *[]){"Play", "Edit", NULL}));
  gtk_drop_down_set_selected(self->sgf_mode_control, 0);
  g_signal_connect(self->sgf_mode_control,
                   "notify::selected",
                   G_CALLBACK(ggame_window_on_mode_selected_notify),
                   self);
  g_signal_connect(self,
                   "notify::default-width",
                   G_CALLBACK(ggame_window_on_default_size_notify),
                   self);
  gtk_box_append(GTK_BOX(sgf_mode_row), GTK_WIDGET(self->sgf_mode_control));
  gtk_box_append(GTK_BOX(middle_panel), sgf_mode_row);

  gboolean square_board = self->profile != NULL && self->profile->backend != NULL &&
                          self->profile->backend->supports_square_grid_board;
  self->sgf_controller = ggame_sgf_controller_new(square_board ? self->board_view : NULL);
  if (square_board) {
    board_view_set_sgf_controller(self->board_view, self->sgf_controller);
    board_view_set_move_handler(self->board_view, ggame_window_apply_player_move, self);
    board_view_set_square_handler(self->board_view, ggame_window_on_board_square_action, self);
  }
  self->analysis_graph = analysis_graph_new();
  GtkWidget *sgf_widget = ggame_sgf_controller_get_widget(self->sgf_controller);
  g_return_if_fail(sgf_widget != NULL);
  g_signal_connect(self->sgf_controller,
                   "manual-requested",
                   G_CALLBACK(ggame_window_on_manual_requested),
                   self);
  g_signal_connect(self->sgf_controller,
                   "node-changed",
                   G_CALLBACK(ggame_window_on_sgf_node_changed),
                   self);
  gtk_widget_add_css_class(sgf_widget, "sgf-panel");
  gtk_box_append(GTK_BOX(middle_panel), sgf_widget);

  GtkWidget *analysis_depth_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_box_append(GTK_BOX(analysis_panel), analysis_depth_box);

  GtkWidget *analysis_depth_label = gtk_label_new("Analysis depth");
  gtk_widget_set_halign(analysis_depth_label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(analysis_depth_box), analysis_depth_label);

  self->analysis_depth_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                                  GGAME_WINDOW_ANALYSIS_DEPTH_MIN,
                                                                  GGAME_WINDOW_ANALYSIS_DEPTH_MAX,
                                                                  1));
  gtk_scale_set_digits(self->analysis_depth_scale, 0);
  gtk_scale_set_draw_value(self->analysis_depth_scale, TRUE);
  gtk_widget_set_hexpand(GTK_WIDGET(self->analysis_depth_scale), TRUE);
  gtk_widget_set_size_request(GTK_WIDGET(self->analysis_depth_scale), 100, -1);
  gtk_box_append(GTK_BOX(analysis_depth_box), GTK_WIDGET(self->analysis_depth_scale));
  g_object_set_data(G_OBJECT(self), "analysis-depth-scale", self->analysis_depth_scale);
  ggame_window_set_analysis_depth(self, GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT);

  GtkWidget *graph_widget = analysis_graph_get_widget(self->analysis_graph);
  g_return_if_fail(graph_widget != NULL);
  g_signal_connect(self->analysis_graph,
                   "node-activated",
                   G_CALLBACK(ggame_window_on_analysis_graph_node_activated),
                   self);
  g_object_set_data(G_OBJECT(self), "analysis-graph", self->analysis_graph);
  gtk_box_append(GTK_BOX(analysis_panel), graph_widget);

  GtkWidget *analysis_status = gtk_label_new("");
  gtk_widget_add_css_class(analysis_status, "analysis-status");
  gtk_widget_set_halign(analysis_status, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(analysis_status), TRUE);
  gtk_label_set_xalign(GTK_LABEL(analysis_status), 0.0f);
  gtk_box_append(GTK_BOX(analysis_panel), analysis_status);
  self->analysis_status_label = GTK_LABEL(analysis_status);
  g_object_set_data(G_OBJECT(self), "analysis-status-label", analysis_status);

  GtkWidget *analysis_scroller = gtk_scrolled_window_new();
  gtk_widget_set_hexpand(analysis_scroller, TRUE);
  gtk_widget_set_vexpand(analysis_scroller, TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(analysis_scroller),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_box_append(GTK_BOX(analysis_panel), analysis_scroller);

  GtkWidget *analysis_view = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(analysis_view), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(analysis_view), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(analysis_view), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(analysis_view), TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(analysis_scroller), analysis_view);
  self->analysis_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(analysis_view));
  self->edit_mode_enabled = FALSE;
  self->show_navigation_drawer =
      layout != NULL ? layout->show_navigation_drawer_by_default : TRUE;
  self->show_analysis_drawer =
      layout != NULL ? layout->show_analysis_drawer_by_default : TRUE;
  self->show_move_report = TRUE;
  self->layout_mode = GGAME_WINDOW_LAYOUT_MODE_NORMAL;
  self->board_panel_width =
      layout != NULL && layout->default_board_panel_width > 0 ? layout->default_board_panel_width
                                                              : GGAME_WINDOW_DEFAULT_BOARD_PANEL_WIDTH;
  self->navigation_panel_width =
      layout != NULL && layout->default_navigation_panel_width > 0 ? layout->default_navigation_panel_width
                                                                   : GGAME_WINDOW_DEFAULT_NAVIGATION_PANEL_WIDTH;
  self->analysis_panel_width =
      layout != NULL && layout->default_analysis_panel_width > 0 ? layout->default_analysis_panel_width
                                                                 : GGAME_WINDOW_DEFAULT_ANALYSIS_PANEL_WIDTH;
  ggame_window_load_saved_layout(self);
  self->puzzle_saved_show_navigation_drawer = self->show_navigation_drawer;
  self->puzzle_saved_show_analysis_drawer = self->show_analysis_drawer;
  self->extra_width = MAX(0, default_width - ggame_window_expected_default_width(self));
  self->puzzle_board_panel_width = self->board_panel_width;
  self->puzzle_navigation_panel_width = self->navigation_panel_width;
  self->puzzle_analysis_panel_width = self->analysis_panel_width;
  self->puzzle_extra_width = 0;
  self->board_orientation_mode = GGAME_WINDOW_BOARD_ORIENTATION_FIXED;
  self->board_bottom_color = CHECKERS_COLOR_WHITE;
  self->puzzle_variant = NULL;
  self->puzzle_variant_key = NULL;
  self->puzzle_attacker_side = 0;
  self->puzzle_number = 0;
  self->puzzle_attempt_started = FALSE;
  self->puzzle_attempt_made_player_move = FALSE;
  ggame_window_sync_drawer_action_states(self);
  ggame_window_sync_drawer_ui_with_capture(self, FALSE);
  ggame_window_sync_puzzle_ui(self);
  ggame_window_sync_mode_ui(self);
  ggame_window_analysis_sync_ui(self);
  ggame_window_show_analysis_for_current_node(self);
  ggame_window_refresh_analysis_graph(self);
}

PlayerControlsPanel *ggame_window_get_controls_panel(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  if (!self->controls_panel) {
    g_debug("Missing controls panel\n");
    return NULL;
  }

  return self->controls_panel;
}

GGameSgfController *ggame_window_get_sgf_controller(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  if (!self->sgf_controller) {
    g_debug("Missing SGF controller\n");
    return NULL;
  }

  return self->sgf_controller;
}

void ggame_window_set_loaded_source_path(GGameWindow *self, const char *path) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  g_clear_pointer(&self->loaded_source_name, g_free);
  if (path != NULL && *path != '\0') {
    self->loaded_source_name = g_path_get_basename(path);
  }
  ggame_window_sync_title(self);
}

GGameWindow *ggame_window_new(GtkApplication *app, GGameModel *model) {
  g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);
  g_return_val_if_fail(GGAME_IS_MODEL(model), NULL);

  GGameWindow *window = g_object_new(GGAME_TYPE_WINDOW, "application", app, NULL);
  if (GGAME_IS_APPLICATION(app) && window->profile != NULL && window->profile->features.supports_puzzles) {
    window->puzzle_progress_store =
        ggame_application_get_puzzle_progress_store(GGAME_APPLICATION(app));
  }
  ggame_window_set_model(window, model);
  return window;
}
/* End copied file: src/window.c */

static GtkApplication *test_homeworlds_app = NULL;

static void drain_main_context(void) {
  for (guint i = 0; i < 256; ++i) {
    g_main_context_iteration(NULL, FALSE);
  }
}

static GGameWindow *create_window(GtkApplication **out_app, GGameModel **out_model) {
  if (test_homeworlds_app == NULL) {
    test_homeworlds_app = gtk_application_new("io.github.jeromea.ghomeworlds.test",
                                              G_APPLICATION_DEFAULT_FLAGS | G_APPLICATION_NON_UNIQUE);
    g_application_register(G_APPLICATION(test_homeworlds_app), NULL, NULL);
  }

  GtkApplication *app = g_object_ref(test_homeworlds_app);
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  GGameWindow *window = ggame_window_new(app, model);

  *out_app = app;
  *out_model = model;
  return window;
}

int main(void) {
  GtkApplication *app = NULL;
  GGameModel *model = NULL;
  GGameWindow *window = NULL;

  g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);

  gtk_init();

  window = create_window(&app, &model);
  gtk_window_present(GTK_WINDOW(window));
  drain_main_context();
  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);

  window = create_window(&app, &model);
  gtk_window_set_default_size(GTK_WINDOW(window), 2000, 700);
  gtk_window_present(GTK_WINDOW(window));
  drain_main_context();
  gtk_paned_set_position(GTK_PANED(g_object_get_data(G_OBJECT(window), "main-paned")), 1400);
  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}
