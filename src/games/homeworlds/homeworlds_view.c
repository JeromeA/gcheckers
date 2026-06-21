#include "homeworlds_view.h"

#include "homeworlds_game.h"
#include "homeworlds_move_builder.h"
#include "homeworlds_move_report.h"
#include "../../sgf_controller.h"
#include "../../sgf_tree.h"

#include <math.h>

typedef struct {
  double red;
  double green;
  double blue;
  const char *name;
  const char *short_name;
} HomeworldsColorStyle;

typedef struct {
  HomeworldsPyramid pyramid;
  guint count;
} HomeworldsBankButtonIcon;

#define HOMEWORLDS_VIEW_HOMEWORLD_STAR_OFFSET 22.0
#define HOMEWORLDS_VIEW_HOMEWORLD_SHIP_OFFSET 72.0
#define HOMEWORLDS_VIEW_HOMEWORLD_PIECE_Y_OFFSET 12.0
#define HOMEWORLDS_VIEW_PIECE_SIZE_RATIO 1.45
#define HOMEWORLDS_VIEW_SMALL_PYRAMID_HEIGHT 36.0
#define HOMEWORLDS_VIEW_SMALL_STAR_SIDE 20.0
#define HOMEWORLDS_VIEW_PYRAMID_HEIGHT_TO_BASE 2.0
#define HOMEWORLDS_VIEW_PIP_REFERENCE_RATIO 0.055
#define HOMEWORLDS_VIEW_ITEM_GAP 7.0
#define HOMEWORLDS_VIEW_SYSTEM_PADDING_X 14.0
#define HOMEWORLDS_VIEW_SYSTEM_PADDING_BOTTOM 30.0
#define HOMEWORLDS_VIEW_SYSTEM_LABEL_HEIGHT 32.0
#define HOMEWORLDS_VIEW_SYSTEM_CORNER_RADIUS 16.0
#define HOMEWORLDS_VIEW_PIECE_BUTTON_PAD 5.0
#define HOMEWORLDS_VIEW_BANK_BUTTON_PAD 3.0
#define HOMEWORLDS_VIEW_BANK_STACK_OFFSET 3.0
#define HOMEWORLDS_VIEW_BANK_GRID_COLUMN_SPACING 7
#define HOMEWORLDS_VIEW_BANK_GRID_ROW_SPACING 4
#define HOMEWORLDS_VIEW_BANK_MARGIN 18
#define HOMEWORLDS_VIEW_BANK_INNER_MARGIN 8
#define HOMEWORLDS_VIEW_FALLBACK_BOARD_WIDTH 880
#define HOMEWORLDS_VIEW_FALLBACK_BOARD_HEIGHT 620
#define HOMEWORLDS_VIEW_ROW_MARGIN 54.0
#define HOMEWORLDS_VIEW_ROW_MIN_EMPTY_GAP 24.0
#define HOMEWORLDS_VIEW_ROW_MIN_VERTICAL_GAP 24.0
#define HOMEWORLDS_VIEW_MIN_BOARD_VIEWPORT_WIDTH 420
#define HOMEWORLDS_VIEW_TEXT_PANEL_WIDTH 350
#define HOMEWORLDS_VIEW_TEXT_PANEL_MARGIN 12
#define HOMEWORLDS_VIEW_TEXT_PANEL_CONTENT_WIDTH \
  (HOMEWORLDS_VIEW_TEXT_PANEL_WIDTH - (2 * HOMEWORLDS_VIEW_TEXT_PANEL_MARGIN))
#define HOMEWORLDS_VIEW_INITIAL_BOARD_WIDTH 1
#define HOMEWORLDS_VIEW_INITIAL_BOARD_HEIGHT 1
#define HOMEWORLDS_VIEW_SYSTEM_PIECE_MAX (HOMEWORLDS_STAR_SLOT_COUNT + (2 * HOMEWORLDS_SHIP_SLOT_COUNT))
#define HOMEWORLDS_VIEW_PREVIOUS_MARKER_OFFSET 16.0
#define HOMEWORLDS_VIEW_PREVIOUS_MARKER_PLUS_HALF_LENGTH 7.2
#define HOMEWORLDS_VIEW_PREVIOUS_MARKER_DISC_RADIUS 5.5
#define HOMEWORLDS_VIEW_PREVIOUS_MARKER_ARROW_HALF_LENGTH 7.0
#define HOMEWORLDS_VIEW_PREVIOUS_MARKER_ARROW_HEAD 5.0
#define HOMEWORLDS_VIEW_PREVIOUS_MARKER_LINE_WIDTH 2.5
#define HOMEWORLDS_VIEW_PREVIOUS_DISC_LINE_WIDTH 1.4
#define HOMEWORLDS_VIEW_PREVIOUS_CATASTROPHE_LINE_WIDTH 1.3
#define HOMEWORLDS_VIEW_ACTION_LEGEND_HEIGHT 46.0
#define HOMEWORLDS_VIEW_ACTION_LEGEND_FONT_SIZE 17.0
#define HOMEWORLDS_VIEW_ACTION_LEGEND_WORD_GAP 28.0
typedef enum {
  HOMEWORLDS_VIEW_SYSTEM_ROW_TOP = 0,
  HOMEWORLDS_VIEW_SYSTEM_ROW_MIDDLE,
  HOMEWORLDS_VIEW_SYSTEM_ROW_BOTTOM,
} HomeworldsViewSystemRow;

typedef enum {
  HOMEWORLDS_VIEW_BOARD_ROW_PLAYER_2 = 0,
  HOMEWORLDS_VIEW_BOARD_ROW_TOP,
  HOMEWORLDS_VIEW_BOARD_ROW_MIDDLE,
  HOMEWORLDS_VIEW_BOARD_ROW_BOTTOM,
  HOMEWORLDS_VIEW_BOARD_ROW_PLAYER_1,
  HOMEWORLDS_VIEW_BOARD_ROW_COUNT,
} HomeworldsViewBoardRow;

typedef struct {
  HomeworldsPyramid pyramid;
  gboolean is_ship;
  guint side;
  guint slot;
  double x;
  double y;
  double width;
  double height;
  gboolean points_up;
} HomeworldsViewPieceLayout;

typedef struct {
  HomeworldsViewPieceLayout pieces[HOMEWORLDS_VIEW_SYSTEM_PIECE_MAX];
  guint piece_count;
  double box_x;
  double box_y;
  double box_width;
  double box_height;
} HomeworldsViewSystemLayout;

typedef struct {
  HomeworldsViewPieceLayout pieces[HOMEWORLDS_VIEW_SYSTEM_PIECE_MAX];
  guint count;
  double height;
} HomeworldsViewPieceRow;

typedef struct {
  double width;
} HomeworldsViewRowSystem;

typedef struct {
  gboolean present;
  double fraction;
  double top;
  double bottom;
} HomeworldsViewBoardRowExtent;

typedef struct {
  guint systems[HOMEWORLDS_SYSTEM_SLOT_COUNT];
  guint count;
} HomeworldsViewBoardRowGroup;

typedef struct {
  guint system_index;
  HomeworldsColor color;
} HomeworldsViewCatastropheChoice;

typedef struct {
  const char *label;
  HomeworldsColor color;
} HomeworldsViewActionLegendItem;

static HomeworldsViewBoardRow homeworlds_view_board_row_for_system(const HomeworldsPosition *position,
                                                                   guint system_index);

struct _HomeworldsView {
  GGameModel *model;
  GtkWidget *root;
  GtkWidget *board_scroller;
  GtkWidget *board_overlay;
  GtkWidget *drawing_area;
  GtkWidget *board_choice_layer;
  GtkWidget *board_bank_box;
  GtkWidget *stage_label;
  GtkWidget *candidate_box;
  GtkWidget *catastrophe_box;
  GtkWidget *last_move_label;
  GtkWidget *move_report_view;
  gboolean move_report_enabled;
  GameBackendMoveBuilder builder;
  gboolean builder_ready;
  HomeworldsViewMoveHandler move_handler;
  gpointer move_handler_data;
  HomeworldsViewMoveAppliedFunc move_applied;
  gpointer move_applied_data;
  gulong model_state_changed_handler_id;
  GtkAdjustment *board_hadjustment;
  gulong board_hadjustment_changed_handler_id;
  GtkAdjustment *board_vadjustment;
  gulong board_vadjustment_changed_handler_id;
  guint board_layout_settle_tick_id;
  gulong root_destroy_handler_id;
  char *player_names[2];
  HomeworldsViewPreviousMoveMarker previous_move_markers[HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_CAPACITY];
  gsize previous_move_marker_count;
};

static void homeworlds_view_update_from_current_builder(HomeworldsView *view);
static gboolean homeworlds_view_update_board_content_size(HomeworldsView *view);
static void homeworlds_view_update_board_bank(HomeworldsView *view);
static void homeworlds_view_update_board_choice_buttons(HomeworldsView *view);
static void homeworlds_view_update_move_report(HomeworldsView *view);
static void homeworlds_view_cancel_board_layout_settle(HomeworldsView *view);
static void homeworlds_view_schedule_board_layout_settle(HomeworldsView *view);
static void homeworlds_view_candidate_clicked(GtkButton *button, gpointer user_data);
static gboolean homeworlds_view_calculate_system_layout(const HomeworldsSystem *system,
                                                        guint system_index,
                                                        double center_x,
                                                        double center_y,
                                                        HomeworldsViewSystemLayout *out_layout);
static gboolean homeworlds_view_apply_catastrophe_choice(HomeworldsMoveBuilderState *state,
                                                         const HomeworldsViewCatastropheChoice *choice);

static const HomeworldsColorStyle homeworlds_view_color_styles[] = {
  [HOMEWORLDS_COLOR_RED] = {0.86, 0.18, 0.16, "red", "R"},
  [HOMEWORLDS_COLOR_YELLOW] = {0.95, 0.76, 0.18, "yellow", "Y"},
  [HOMEWORLDS_COLOR_GREEN] = {0.22, 0.66, 0.35, "green", "G"},
  [HOMEWORLDS_COLOR_BLUE] = {0.18, 0.45, 0.86, "blue", "B"},
};

static const HomeworldsViewActionLegendItem homeworlds_view_action_legend_items[] = {
  {"Attack", HOMEWORLDS_COLOR_RED},
  {"Move", HOMEWORLDS_COLOR_YELLOW},
  {"Build", HOMEWORLDS_COLOR_GREEN},
  {"Trade", HOMEWORLDS_COLOR_BLUE},
};

static double homeworlds_view_size_factor(HomeworldsSize size) {
  switch (size) {
    case HOMEWORLDS_SIZE_SMALL:
      return 1.0;
    case HOMEWORLDS_SIZE_MEDIUM:
      return HOMEWORLDS_VIEW_PIECE_SIZE_RATIO;
    case HOMEWORLDS_SIZE_LARGE:
      return HOMEWORLDS_VIEW_PIECE_SIZE_RATIO * HOMEWORLDS_VIEW_PIECE_SIZE_RATIO;
    default:
      g_debug("Unsupported Homeworlds pyramid size");
      return 1.0;
  }
}

gboolean homeworlds_view_pyramid_metrics(HomeworldsSize size, HomeworldsViewPyramidMetrics *out_metrics) {
  double height = 0.0;

  g_return_val_if_fail(out_metrics != NULL, FALSE);
  g_return_val_if_fail(size >= HOMEWORLDS_SIZE_SMALL && size <= HOMEWORLDS_SIZE_LARGE, FALSE);

  height = HOMEWORLDS_VIEW_SMALL_PYRAMID_HEIGHT * homeworlds_view_size_factor(size);
  *out_metrics = (HomeworldsViewPyramidMetrics){
    .base = height / HOMEWORLDS_VIEW_PYRAMID_HEIGHT_TO_BASE,
    .height = height,
  };
  return TRUE;
}

double homeworlds_view_star_side(HomeworldsSize size) {
  g_return_val_if_fail(size >= HOMEWORLDS_SIZE_SMALL && size <= HOMEWORLDS_SIZE_LARGE, 0.0);

  return HOMEWORLDS_VIEW_SMALL_STAR_SIDE * homeworlds_view_size_factor(size);
}

double homeworlds_view_pip_radius(void) {
  HomeworldsViewPyramidMetrics metrics = {0};

  g_return_val_if_fail(homeworlds_view_pyramid_metrics(HOMEWORLDS_SIZE_MEDIUM, &metrics), 0.0);

  return metrics.height * HOMEWORLDS_VIEW_PIP_REFERENCE_RATIO;
}

double homeworlds_view_action_legend_height(void) {
  return HOMEWORLDS_VIEW_ACTION_LEGEND_HEIGHT;
}

static double homeworlds_view_system_area_height(double board_height) {
  g_return_val_if_fail(board_height > 0.0, 1.0);

  return MAX(1.0, board_height - homeworlds_view_action_legend_height());
}

static double homeworlds_view_bank_button_width(HomeworldsSize size) {
  HomeworldsViewPyramidMetrics metrics = {0};

  g_return_val_if_fail(homeworlds_view_pyramid_metrics(size, &metrics), 0.0);

  return ceil(metrics.base +
              (2.0 * HOMEWORLDS_VIEW_BANK_STACK_OFFSET) +
              (2.0 * HOMEWORLDS_VIEW_BANK_BUTTON_PAD));
}

static double homeworlds_view_bank_button_height(HomeworldsSize size) {
  HomeworldsViewPyramidMetrics metrics = {0};

  g_return_val_if_fail(homeworlds_view_pyramid_metrics(size, &metrics), 0.0);

  return ceil(metrics.height +
              (2.0 * HOMEWORLDS_VIEW_BANK_STACK_OFFSET) +
              (2.0 * HOMEWORLDS_VIEW_BANK_BUTTON_PAD));
}

static const HomeworldsMoveBuilderState *homeworlds_view_builder_state(const HomeworldsView *view) {
  g_return_val_if_fail(view != NULL, NULL);

  if (!view->builder_ready || view->builder.builder_state == NULL) {
    return NULL;
  }

  return view->builder.builder_state;
}

static const HomeworldsPosition *homeworlds_view_position(const HomeworldsView *view) {
  const HomeworldsMoveBuilderState *state = NULL;

  g_return_val_if_fail(view != NULL, NULL);
  g_return_val_if_fail(GGAME_IS_MODEL(view->model), NULL);

  state = homeworlds_view_builder_state(view);
  if (state != NULL && state->stage != HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR &&
      state->stage != HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR &&
      state->stage != HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP &&
      (state->stage != HOMEWORLDS_BUILDER_STAGE_COMPLETE ||
       (state->move.kind == HOMEWORLDS_MOVE_KIND_TURN && state->move.step_count > 0))) {
    return &state->working_position;
  }

  return ggame_model_peek_position(view->model);
}

static gboolean homeworlds_view_builder_has_catastrophe_choices(const HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(state != NULL, FALSE);

  if (state->working_position.phase != HOMEWORLDS_PHASE_PLAY ||
      state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP) {
    return FALSE;
  }
  for (guint side = 0; side < 2; ++side) {
    if (state->initial_homeworld_has_ships[side] &&
        !homeworlds_system_has_ships_for_side(&state->working_position.systems[side], side)) {
      return FALSE;
    }
  }

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];

    for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
      if (homeworlds_system_color_count(system, (HomeworldsColor) color) >= 4) {
        return TRUE;
      }
    }
  }

  return FALSE;
}

static const char *homeworlds_view_color_name(HomeworldsColor color) {
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, "unknown");

  return homeworlds_view_color_styles[color].name;
}

static gboolean homeworlds_view_previous_move_marker_append(HomeworldsViewPreviousMoveMarker *markers,
                                                            gsize max_markers,
                                                            gsize *marker_count,
                                                            const HomeworldsViewPreviousMoveMarker *marker) {
  g_return_val_if_fail(marker_count != NULL, FALSE);
  g_return_val_if_fail(marker != NULL, FALSE);
  g_return_val_if_fail(markers != NULL || max_markers == 0, FALSE);

  if (*marker_count >= max_markers) {
    g_debug("Too many Homeworlds previous-move markers");
    return FALSE;
  }

  markers[*marker_count] = *marker;
  (*marker_count)++;
  return TRUE;
}

static gboolean homeworlds_view_find_ship_marker_slot(const HomeworldsPosition *position,
                                                      guint system_index,
                                                      guint side,
                                                      HomeworldsPyramid pyramid,
                                                      guint *out_slot) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);
  g_return_val_if_fail(out_slot != NULL, FALSE);

  *out_slot = HOMEWORLDS_INVALID_INDEX;
  const HomeworldsSystem *system = &position->systems[system_index];
  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    HomeworldsPyramid ship = system->ships[side][slot];

    if (!homeworlds_pyramid_is_valid(ship)) {
      break;
    }
    if (ship != pyramid) {
      continue;
    }

    *out_slot = slot;
    return TRUE;
  }

  return FALSE;
}

static gboolean homeworlds_view_resolve_step_target_system(const HomeworldsPosition *position,
                                                           const HomeworldsTurnStep *step,
                                                           guint *out_system_index) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(out_system_index != NULL, FALSE);

  if (step->kind == HOMEWORLDS_STEP_DISCOVER &&
      step->target_system.kind == HOMEWORLDS_SYSTEM_REF_SYSTEM &&
      step->target_system.system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT &&
      homeworlds_pyramid_is_valid(step->target_system.star)) {
    *out_system_index = step->target_system.system_index;
    return TRUE;
  }

  return homeworlds_position_resolve_system_ref(position, &step->target_system, out_system_index);
}

static gboolean homeworlds_view_previous_move_step_action_color(const HomeworldsTurnStep *step,
                                                                HomeworldsColor *out_color) {
  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(out_color != NULL, FALSE);

  switch ((HomeworldsStepKind) step->kind) {
    case HOMEWORLDS_STEP_BUILD:
      *out_color = HOMEWORLDS_COLOR_GREEN;
      return TRUE;
    case HOMEWORLDS_STEP_TRADE:
      *out_color = HOMEWORLDS_COLOR_BLUE;
      return TRUE;
    case HOMEWORLDS_STEP_ATTACK:
      *out_color = HOMEWORLDS_COLOR_RED;
      return TRUE;
    case HOMEWORLDS_STEP_MOVE:
    case HOMEWORLDS_STEP_DISCOVER:
      *out_color = HOMEWORLDS_COLOR_YELLOW;
      return TRUE;
    case HOMEWORLDS_STEP_NONE:
    case HOMEWORLDS_STEP_PASS:
    case HOMEWORLDS_STEP_SACRIFICE:
    case HOMEWORLDS_STEP_CATASTROPHE:
    default:
      return FALSE;
  }
}

static gboolean homeworlds_view_previous_move_append_ship_marker(const HomeworldsPosition *after_position,
                                                                 guint system_index,
                                                                 guint side,
                                                                 HomeworldsPyramid pyramid,
                                                                 HomeworldsViewPreviousMoveMarkerKind kind,
                                                                 HomeworldsColor marker_color,
                                                                 HomeworldsViewPreviousMoveMarker *markers,
                                                                 gsize max_markers,
                                                                 gsize *marker_count) {
  guint slot = 0;

  g_return_val_if_fail(after_position != NULL, FALSE);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);

  if (!homeworlds_view_find_ship_marker_slot(after_position, system_index, side, pyramid, &slot)) {
    return TRUE;
  }

  HomeworldsViewPreviousMoveMarker marker = {
    .kind = kind,
    .system_index = (guint8)system_index,
    .side = (guint8)side,
    .slot = (guint8)slot,
    .is_ship = TRUE,
    .pyramid = pyramid,
    .color = marker_color,
  };
  return homeworlds_view_previous_move_marker_append(markers, max_markers, marker_count, &marker);
}

static gboolean homeworlds_view_previous_move_append_catastrophe_markers(
    const HomeworldsPosition *after_position,
    const HomeworldsTurnStep *step,
    guint move_side,
    HomeworldsViewPreviousMoveMarker *markers,
    gsize max_markers,
    gsize *marker_count) {
  guint system_index = 0;

  g_return_val_if_fail(after_position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(move_side < 2, FALSE);

  if (step->target_color > HOMEWORLDS_COLOR_BLUE) {
    g_debug("Ignoring previous-move catastrophe marker with invalid color");
    return FALSE;
  }
  if (!homeworlds_position_resolve_system_ref(after_position, &step->target_system, &system_index)) {
    return TRUE;
  }

  const HomeworldsSystem *system = &after_position->systems[system_index];
  if (!homeworlds_system_has_star(system)) {
    return TRUE;
  }

  HomeworldsViewPreviousMoveMarker marker = {
    .kind = HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_CATASTROPHE,
    .system_index = (guint8)system_index,
    .side = (guint8)move_side,
    .slot = HOMEWORLDS_INVALID_INDEX,
    .is_ship = FALSE,
    .pyramid = 0,
    .color = (HomeworldsColor)step->target_color,
  };
  return homeworlds_view_previous_move_marker_append(markers, max_markers, marker_count, &marker);
}

static gboolean homeworlds_view_previous_move_append_step_marker(
    const HomeworldsPosition *working_position,
    const HomeworldsPosition *after_position,
    const HomeworldsTurnStep *step,
    guint move_side,
    HomeworldsViewPreviousMoveMarker *markers,
    gsize max_markers,
    gsize *marker_count) {
  guint system_index = 0;
  HomeworldsPyramid marker_pyramid = 0;
  HomeworldsColor marker_color = HOMEWORLDS_COLOR_RED;
  HomeworldsViewPreviousMoveMarkerKind marker_kind = HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_BUILD;

  g_return_val_if_fail(working_position != NULL, FALSE);
  g_return_val_if_fail(after_position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(move_side < 2, FALSE);

  switch ((HomeworldsStepKind) step->kind) {
    case HOMEWORLDS_STEP_BUILD:
      if (step->target_color > HOMEWORLDS_COLOR_BLUE ||
          !homeworlds_position_resolve_system_ref(working_position, &step->actor.system, &system_index) ||
          !homeworlds_system_find_smallest_bank_ship(working_position,
                                                     (HomeworldsColor)step->target_color,
                                                     &marker_pyramid)) {
        return FALSE;
      }
      marker_kind = HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_BUILD;
      marker_color = HOMEWORLDS_COLOR_GREEN;
      break;
    case HOMEWORLDS_STEP_TRADE:
      if (step->target_color > HOMEWORLDS_COLOR_BLUE ||
          !homeworlds_pyramid_is_valid(step->actor.ship) ||
          !homeworlds_position_resolve_system_ref(working_position, &step->actor.system, &system_index)) {
        return FALSE;
      }
      marker_pyramid = homeworlds_pyramid_make((HomeworldsColor)step->target_color,
                                               homeworlds_pyramid_size(step->actor.ship));
      marker_kind = HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_TRADE;
      marker_color = homeworlds_pyramid_color(step->actor.ship);
      break;
    case HOMEWORLDS_STEP_ATTACK:
      if (!homeworlds_pyramid_is_valid(step->target_ship.ship) ||
          !homeworlds_position_resolve_system_ref(working_position, &step->actor.system, &system_index)) {
        return FALSE;
      }
      marker_pyramid = step->target_ship.ship;
      marker_kind = HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_CAPTURE;
      marker_color = HOMEWORLDS_COLOR_RED;
      break;
    case HOMEWORLDS_STEP_MOVE:
    case HOMEWORLDS_STEP_DISCOVER:
      if (!homeworlds_pyramid_is_valid(step->actor.ship) ||
          !homeworlds_view_resolve_step_target_system(working_position, step, &system_index)) {
        return FALSE;
      }
      marker_pyramid = step->actor.ship;
      marker_kind = HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_MOVE;
      marker_color = HOMEWORLDS_COLOR_YELLOW;
      break;
    case HOMEWORLDS_STEP_CATASTROPHE:
      return homeworlds_view_previous_move_append_catastrophe_markers(after_position,
                                                                      step,
                                                                      move_side,
                                                                      markers,
                                                                      max_markers,
                                                                      marker_count);
    case HOMEWORLDS_STEP_NONE:
    case HOMEWORLDS_STEP_PASS:
    case HOMEWORLDS_STEP_SACRIFICE:
    default:
      return TRUE;
  }

  return homeworlds_view_previous_move_append_ship_marker(after_position,
                                                          system_index,
                                                          move_side,
                                                          marker_pyramid,
                                                          marker_kind,
                                                          marker_color,
                                                          markers,
                                                          max_markers,
                                                          marker_count);
}

gboolean homeworlds_view_collect_previous_move_markers(const HomeworldsPosition *before_position,
                                                       const HomeworldsPosition *after_position,
                                                       const HomeworldsMove *move,
                                                       guint move_side,
                                                       HomeworldsViewPreviousMoveMarker *markers,
                                                       gsize max_markers,
                                                       gsize *out_marker_count) {
  HomeworldsPosition working = {0};
  guint pending_sacrifice_actions = 0;
  HomeworldsColor sacrifice_color = HOMEWORLDS_COLOR_RED;
  gboolean primary_action_done = FALSE;
  gsize marker_count = 0;

  g_return_val_if_fail(before_position != NULL, FALSE);
  g_return_val_if_fail(after_position != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(move_side < 2, FALSE);
  g_return_val_if_fail(markers != NULL || max_markers == 0, FALSE);
  g_return_val_if_fail(out_marker_count != NULL, FALSE);

  if (max_markers > 0) {
    memset(markers, 0, sizeof(*markers) * max_markers);
  }
  *out_marker_count = 0;

  if (move->kind != HOMEWORLDS_MOVE_KIND_TURN) {
    return TRUE;
  }
  if (move->step_count > HOMEWORLDS_MAX_MOVE_STEPS) {
    return FALSE;
  }

  working = *before_position;
  for (guint i = 0; i < move->step_count; ++i) {
    const HomeworldsTurnStep *step = &move->steps[i];
    gboolean require_access = TRUE;

    if (step->kind == HOMEWORLDS_STEP_CATASTROPHE) {
      if (!homeworlds_view_previous_move_append_step_marker(&working,
                                                            after_position,
                                                            step,
                                                            move_side,
                                                            markers,
                                                            max_markers,
                                                            &marker_count) ||
          !homeworlds_position_apply_turn_step(&working, step)) {
        return FALSE;
      }
      continue;
    }

    if (step->kind == HOMEWORLDS_STEP_PASS) {
      if (pending_sacrifice_actions > 0) {
        pending_sacrifice_actions--;
      } else if (primary_action_done) {
        return FALSE;
      } else {
        primary_action_done = TRUE;
      }
    } else if (step->kind == HOMEWORLDS_STEP_SACRIFICE) {
      guint system_index = 0;
      guint ship_slot = 0;

      if (primary_action_done ||
          pending_sacrifice_actions != 0 ||
          !homeworlds_pyramid_is_valid(step->actor.ship) ||
          !homeworlds_position_resolve_system_ref(&working, &step->actor.system, &system_index) ||
          !homeworlds_view_find_ship_marker_slot(&working, system_index, move_side, step->actor.ship, &ship_slot)) {
        return FALSE;
      }
      (void)ship_slot;
      pending_sacrifice_actions = homeworlds_pyramid_size(step->actor.ship);
      sacrifice_color = homeworlds_pyramid_color(step->actor.ship);
      primary_action_done = TRUE;
    } else if (pending_sacrifice_actions == 0) {
      if (primary_action_done) {
        return FALSE;
      }
      primary_action_done = TRUE;
    } else {
      HomeworldsColor action_color = HOMEWORLDS_COLOR_RED;

      if (!homeworlds_view_previous_move_step_action_color(step, &action_color) ||
          action_color != sacrifice_color) {
        return FALSE;
      }
      pending_sacrifice_actions--;
      require_access = FALSE;
    }

    if (!homeworlds_view_previous_move_append_step_marker(&working,
                                                          after_position,
                                                          step,
                                                          move_side,
                                                          markers,
                                                          max_markers,
                                                          &marker_count)) {
      return FALSE;
    }
    if (require_access ? !homeworlds_position_apply_turn_step(&working, step)
                       : !homeworlds_position_apply_forced_action_step(&working, step)) {
      return FALSE;
    }
  }

  if (pending_sacrifice_actions > 0) {
    return FALSE;
  }

  *out_marker_count = marker_count;
  return TRUE;
}

static void homeworlds_view_clear_previous_move_markers(HomeworldsView *view) {
  g_return_if_fail(view != NULL);

  if (view->previous_move_marker_count > 0) {
    memset(view->previous_move_markers, 0, sizeof(view->previous_move_markers));
    view->previous_move_marker_count = 0;
    if (GTK_IS_WIDGET(view->drawing_area)) {
      gtk_widget_queue_draw(view->drawing_area);
    }
  }
}

static gboolean homeworlds_view_side_from_sgf_color(const GameBackend *backend, SgfColor color, guint *out_side) {
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(backend->sgf_color_for_side != NULL, FALSE);
  g_return_val_if_fail(out_side != NULL, FALSE);

  for (guint side = 0; side < 2; side++) {
    if (backend->sgf_color_for_side(side) == color) {
      *out_side = side;
      return TRUE;
    }
  }

  return FALSE;
}

static char *homeworlds_view_system_label(guint system_index) {
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, NULL);

  if (system_index < 2) {
    return g_strdup_printf("H%u", system_index + 1);
  }

  return g_strdup_printf("S%u", system_index - 2);
}

static const char *homeworlds_view_player_name_property_for_side(guint side) {
  g_return_val_if_fail(side < 2, NULL);

  return side == 0 ? "PB" : "PW";
}

static const SgfNode *homeworlds_view_node_root(const SgfNode *node) {
  g_return_val_if_fail(node != NULL, NULL);

  const SgfNode *root = node;
  while (sgf_node_get_parent(root) != NULL) {
    root = sgf_node_get_parent(root);
  }

  return root;
}

static const char *homeworlds_view_player_name_for_side_from_node(const SgfNode *node, guint side) {
  g_return_val_if_fail(node != NULL, NULL);
  g_return_val_if_fail(side < 2, NULL);

  const SgfNode *root = homeworlds_view_node_root(node);
  const char *property = homeworlds_view_player_name_property_for_side(side);
  if (root == NULL || property == NULL) {
    return NULL;
  }

  return sgf_node_get_property_first(root, property);
}

static char *homeworlds_view_format_system_title_with_player_name(guint system_index, const char *player_name) {
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, NULL);

  if (system_index < 2 && player_name != NULL && player_name[0] != '\0') {
    return g_strdup_printf("H%u (%s)", system_index + 1, player_name);
  }

  return homeworlds_view_system_label(system_index);
}

char *homeworlds_view_format_board_system_title(guint system_index, const SgfNode *node) {
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, NULL);

  if (system_index < 2 && node != NULL) {
    const char *player_name = homeworlds_view_player_name_for_side_from_node(node, system_index);
    return homeworlds_view_format_system_title_with_player_name(system_index, player_name);
  }

  return homeworlds_view_system_label(system_index);
}

static const char *homeworlds_view_size_name(HomeworldsSize size) {
  switch (size) {
    case HOMEWORLDS_SIZE_SMALL:
      return "small";
    case HOMEWORLDS_SIZE_MEDIUM:
      return "medium";
    case HOMEWORLDS_SIZE_LARGE:
      return "large";
    default:
      g_debug("Unsupported Homeworlds pyramid size");
      return "unknown";
  }
}

static const char *homeworlds_view_action_name(HomeworldsStepKind action) {
  switch (action) {
    case HOMEWORLDS_STEP_PASS:
      return "Pass";
    case HOMEWORLDS_STEP_BUILD:
      return "Build";
    case HOMEWORLDS_STEP_TRADE:
      return "Trade";
    case HOMEWORLDS_STEP_ATTACK:
      return "Capture";
    case HOMEWORLDS_STEP_MOVE:
      return "Move";
    case HOMEWORLDS_STEP_SACRIFICE:
      return "Sacrifice";
    case HOMEWORLDS_STEP_CATASTROPHE:
      return "Catastrophe";
    case HOMEWORLDS_STEP_DISCOVER:
      return "Discover";
    case HOMEWORLDS_STEP_NONE:
    default:
      return "Action";
  }
}

static char *homeworlds_view_pyramid_label(HomeworldsPyramid pyramid) {
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), g_strdup("unused"));

  return g_strdup_printf("%s %s",
                         homeworlds_view_color_name(homeworlds_pyramid_color(pyramid)),
                         homeworlds_view_size_name(homeworlds_pyramid_size(pyramid)));
}

static gboolean homeworlds_view_candidate_is_discovery(const HomeworldsMoveCandidate *candidate) {
  g_return_val_if_fail(candidate != NULL, FALSE);

  return candidate->data.kind == HOMEWORLDS_CANDIDATE_MOVE_TARGET &&
         candidate->data.target_system_index == HOMEWORLDS_INVALID_INDEX;
}

static char *homeworlds_view_candidate_label(const HomeworldsMoveCandidate *candidate) {
  char *pyramid_label = NULL;
  char *system_label = NULL;
  char *label = NULL;

  g_return_val_if_fail(candidate != NULL, NULL);

  switch ((HomeworldsCandidateKind) candidate->data.kind) {
    case HOMEWORLDS_CANDIDATE_SETUP_STAR:
      pyramid_label = homeworlds_view_pyramid_label(candidate->data.pyramid);
      label = g_strdup_printf("Setup star: %s", pyramid_label);
      g_free(pyramid_label);
      return label;
    case HOMEWORLDS_CANDIDATE_SETUP_SHIP:
      pyramid_label = homeworlds_view_pyramid_label(candidate->data.pyramid);
      label = g_strdup_printf("Setup ship: %s", pyramid_label);
      g_free(pyramid_label);
      return label;
    case HOMEWORLDS_CANDIDATE_SELECT_SHIP:
      pyramid_label = homeworlds_view_pyramid_label(candidate->data.pyramid);
      system_label = homeworlds_view_system_label(candidate->data.system_index);
      label = g_strdup_printf("Select %s at %s", pyramid_label, system_label);
      g_free(system_label);
      g_free(pyramid_label);
      return label;
    case HOMEWORLDS_CANDIDATE_ACTION:
      return g_strdup(homeworlds_view_action_name(candidate->data.target_color));
    case HOMEWORLDS_CANDIDATE_TRADE_COLOR:
      return g_strdup_printf("Trade to %s", homeworlds_view_color_name(candidate->data.target_color));
    case HOMEWORLDS_CANDIDATE_ATTACK_TARGET:
      return g_strdup_printf("Capture player %u ship slot %u",
                             (guint) candidate->data.target_ship_owner + 1,
                             (guint) candidate->data.target_ship_slot);
    case HOMEWORLDS_CANDIDATE_MOVE_TARGET:
      if (homeworlds_view_candidate_is_discovery(candidate)) {
        pyramid_label = homeworlds_view_pyramid_label(candidate->data.pyramid);
        label = g_strdup_printf("Discover at %s star", pyramid_label);
        g_free(pyramid_label);
        return label;
      }
      system_label = homeworlds_view_system_label(candidate->data.target_system_index);
      label = g_strdup_printf("Move to %s", system_label);
      g_free(system_label);
      return label;
    case HOMEWORLDS_CANDIDATE_NONE:
    default:
      return g_strdup("Unknown choice");
  }
}

static const char *homeworlds_view_stage_text(const HomeworldsView *view) {
  const HomeworldsMoveBuilderState *state = homeworlds_view_builder_state(view);

  if (state == NULL) {
    return "Game finished";
  }

  switch ((HomeworldsBuilderStage) state->stage) {
    case HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR:
      return "Choose the first homeworld star";
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR:
      return "Choose the second homeworld star";
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP:
      return "Choose the starting ship";
    case HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP:
      return state->pending_actions_remaining > 0 ? "Choose a ship for the sacrificed action"
                                                  : "Choose a ship or pass";
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION:
      return "Choose an action";
    case HOMEWORLDS_BUILDER_STAGE_SELECT_TRADE_COLOR:
      return "Choose the trade color";
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ATTACK_TARGET:
      return "Choose an enemy ship to capture";
    case HOMEWORLDS_BUILDER_STAGE_SELECT_MOVE_TARGET:
      return "Choose a destination or discovery star";
    case HOMEWORLDS_BUILDER_STAGE_COMPLETE:
    default:
      return "Move complete";
  }
}

static void homeworlds_view_clear_box(GtkWidget *box) {
  GtkWidget *child = NULL;

  g_return_if_fail(GTK_IS_BOX(box));

  child = gtk_widget_get_first_child(box);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(box), child);
    child = next;
  }
}

static void homeworlds_view_clear_fixed(GtkWidget *fixed) {
  GtkWidget *child = NULL;

  g_return_if_fail(GTK_IS_FIXED(fixed));

  child = gtk_widget_get_first_child(fixed);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_fixed_remove(GTK_FIXED(fixed), child);
    child = next;
  }
}

static void homeworlds_view_constrain_text_panel_label(GtkWidget *label) {
  g_return_if_fail(GTK_IS_LABEL(label));

  gtk_widget_set_halign(label, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_natural_wrap_mode(GTK_LABEL(label), GTK_NATURAL_WRAP_WORD);
}

static GtkWidget *homeworlds_view_new_text_panel_label(const char *text) {
  GtkWidget *label = NULL;

  g_return_val_if_fail(text != NULL, NULL);

  label = gtk_label_new(text);
  homeworlds_view_constrain_text_panel_label(label);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  return label;
}

static void homeworlds_view_constrain_text_panel_button(GtkWidget *button) {
  GtkWidget *child = NULL;

  g_return_if_fail(GTK_IS_BUTTON(button));

  gtk_widget_set_halign(button, GTK_ALIGN_FILL);
  child = gtk_button_get_child(GTK_BUTTON(button));
  if (GTK_IS_LABEL(child)) {
    homeworlds_view_constrain_text_panel_label(child);
  }
}

static GtkWidget *homeworlds_view_new_text_panel_button(const char *label) {
  GtkWidget *button = NULL;

  g_return_val_if_fail(label != NULL, NULL);

  button = gtk_button_new_with_label(label);
  homeworlds_view_constrain_text_panel_button(button);
  return button;
}

static GtkWidget *homeworlds_view_new_move_report_view(void) {
  GtkWidget *text_view = NULL;

  text_view = gtk_text_view_new();
  gtk_widget_set_name(text_view, "homeworlds-move-report");
  gtk_widget_set_halign(text_view, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand(text_view, TRUE);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), TRUE);
  return text_view;
}

static GtkWidget *homeworlds_view_new_move_report_scroller(GtkWidget **out_text_view) {
  GtkWidget *scroller = NULL;
  GtkWidget *text_view = NULL;

  g_return_val_if_fail(out_text_view != NULL, NULL);

  scroller = gtk_scrolled_window_new();
  gtk_widget_set_name(scroller, "homeworlds-move-report-scroller");
  gtk_widget_set_halign(scroller, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand(scroller, TRUE);
  gtk_widget_set_vexpand(scroller, TRUE);
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroller), HOMEWORLDS_VIEW_TEXT_PANEL_CONTENT_WIDTH);
  gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(scroller), HOMEWORLDS_VIEW_TEXT_PANEL_CONTENT_WIDTH);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroller), 220);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  text_view = homeworlds_view_new_move_report_view();
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), text_view);
  *out_text_view = text_view;
  return scroller;
}

static void homeworlds_view_set_move_report_text(HomeworldsView *view, const char *text) {
  GtkTextBuffer *buffer = NULL;

  g_return_if_fail(view != NULL);
  g_return_if_fail(GTK_IS_TEXT_VIEW(view->move_report_view));
  g_return_if_fail(text != NULL);

  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->move_report_view));
  g_return_if_fail(GTK_IS_TEXT_BUFFER(buffer));

  gtk_text_buffer_set_text(buffer, text, -1);
}

static gboolean homeworlds_view_stage_uses_bank_choices(const HomeworldsView *view) {
  const HomeworldsMoveBuilderState *state = homeworlds_view_builder_state(view);

  if (state == NULL) {
    return FALSE;
  }

  switch ((HomeworldsBuilderStage) state->stage) {
    case HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_TRADE_COLOR:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_MOVE_TARGET:
      return TRUE;
    case HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ATTACK_TARGET:
    case HOMEWORLDS_BUILDER_STAGE_COMPLETE:
    default:
      return FALSE;
  }
}

static gboolean homeworlds_view_stage_uses_board_choices(const HomeworldsView *view) {
  const HomeworldsMoveBuilderState *state = homeworlds_view_builder_state(view);

  if (state == NULL) {
    return FALSE;
  }

  switch ((HomeworldsBuilderStage) state->stage) {
    case HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ATTACK_TARGET:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_MOVE_TARGET:
      return TRUE;
    case HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_TRADE_COLOR:
    case HOMEWORLDS_BUILDER_STAGE_COMPLETE:
    default:
      return FALSE;
  }
}

static gboolean homeworlds_view_stage_uses_visual_choices(const HomeworldsView *view) {
  return homeworlds_view_stage_uses_bank_choices(view) || homeworlds_view_stage_uses_board_choices(view);
}

static gboolean homeworlds_view_can_cancel_selection(const HomeworldsView *view) {
  const HomeworldsMoveBuilderState *state = homeworlds_view_builder_state(view);

  if (state == NULL) {
    return FALSE;
  }

  switch ((HomeworldsBuilderStage) state->stage) {
    case HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR:
    case HOMEWORLDS_BUILDER_STAGE_COMPLETE:
      return FALSE;
    case HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP:
      return state->pending_actions_remaining > 0 || state->move.step_count > 0;
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_TRADE_COLOR:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ATTACK_TARGET:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_MOVE_TARGET:
    default:
      return TRUE;
  }
}

static void homeworlds_view_draw_base_pips(cairo_t *cr,
                                           double center_x,
                                           double center_y,
                                           double width,
                                           guint count,
                                           double radius) {
  double spacing = count > 1 ? MIN(width / ((double) count + 1.0), radius * 3.0) : 0.0;
  double start_x = center_x - (((double) count - 1.0) * spacing / 2.0);

  g_return_if_fail(cr != NULL);
  g_return_if_fail(count >= 1 && count <= 3);
  g_return_if_fail(width > 0.0);
  g_return_if_fail(radius > 0.0);

  cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
  for (guint i = 0; i < count; ++i) {
    cairo_arc(cr, start_x + ((double) i * spacing), center_y, radius, 0.0, G_PI * 2.0);
    cairo_fill(cr);
  }
}

static void homeworlds_view_draw_pyramid(cairo_t *cr,
                                         double x,
                                         double y,
                                         HomeworldsSize size,
                                         gboolean points_up,
                                         HomeworldsColor color) {
  const HomeworldsColorStyle *style = &homeworlds_view_color_styles[color];
  HomeworldsViewPyramidMetrics metrics = {0};
  double half_height = 0.0;
  double half_base = 0.0;
  double base_y = 0.0;
  double pip_radius = 0.0;
  double pip_y = 0.0;

  g_return_if_fail(homeworlds_view_pyramid_metrics(size, &metrics));

  half_height = metrics.height / 2.0;
  half_base = metrics.base / 2.0;
  base_y = points_up ? y + half_height : y - half_height;
  pip_radius = homeworlds_view_pip_radius();
  pip_y = points_up ? base_y - (pip_radius * 2.1) : base_y + (pip_radius * 2.1);

  cairo_move_to(cr, x, points_up ? y - half_height : y + half_height);
  cairo_line_to(cr, x - half_base, base_y);
  cairo_line_to(cr, x + half_base, base_y);
  cairo_close_path(cr);
  cairo_set_source_rgb(cr, style->red, style->green, style->blue);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.70);
  cairo_set_line_width(cr, 1.6);
  cairo_stroke(cr);
  homeworlds_view_draw_base_pips(cr, x, pip_y, metrics.base * 0.72, size, pip_radius);
}

static void homeworlds_view_draw_bank_button_icon(GtkDrawingArea * /*drawing_area*/,
                                                  cairo_t *cr,
                                                  int width,
                                                  int height,
                                                  gpointer user_data) {
  const HomeworldsBankButtonIcon *icon = user_data;
  guint visible_count = 0;

  g_return_if_fail(icon != NULL);
  g_return_if_fail(homeworlds_pyramid_is_valid(icon->pyramid));
  g_return_if_fail(icon->count > 0);

  visible_count = MIN(icon->count, 3);
  for (guint i = 0; i < visible_count; ++i) {
    double offset = ((double) i - (((double) visible_count - 1.0) / 2.0)) * HOMEWORLDS_VIEW_BANK_STACK_OFFSET;
    homeworlds_view_draw_pyramid(cr,
                                 ((double) width / 2.0) + offset,
                                 ((double) height / 2.0) + offset,
                                 homeworlds_pyramid_size(icon->pyramid),
                                 TRUE,
                                 homeworlds_pyramid_color(icon->pyramid));
  }
}

static void homeworlds_view_draw_star(cairo_t *cr,
                                      double x,
                                      double y,
                                      HomeworldsSize size,
                                      HomeworldsColor color) {
  const HomeworldsColorStyle *style = &homeworlds_view_color_styles[color];
  double side = homeworlds_view_star_side(size);
  double half = side / 2.0;
  double pip_radius = homeworlds_view_pip_radius();
  double pip_y = y + half - (pip_radius * 2.1);

  cairo_rectangle(cr, x - half, y - half, side, side);
  cairo_set_source_rgb(cr, style->red, style->green, style->blue);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.75);
  cairo_set_line_width(cr, 1.8);
  cairo_stroke(cr);
  homeworlds_view_draw_base_pips(cr, x, pip_y, side * 0.56, size, pip_radius);
}

static void homeworlds_view_set_marker_source_color(cairo_t *cr, HomeworldsColor color, double alpha) {
  g_return_if_fail(cr != NULL);
  g_return_if_fail(color <= HOMEWORLDS_COLOR_BLUE);

  const HomeworldsColorStyle *style = &homeworlds_view_color_styles[color];
  cairo_set_source_rgba(cr, style->red, style->green, style->blue, alpha);
}

static double homeworlds_view_previous_marker_direction(guint side) {
  g_return_val_if_fail(side < 2, 1.0);

  return side == 0 ? 1.0 : -1.0;
}

static void homeworlds_view_previous_marker_piece_point(const HomeworldsViewPieceLayout *piece,
                                                        guint side,
                                                        double *out_x,
                                                        double *out_y) {
  g_return_if_fail(piece != NULL);
  g_return_if_fail(out_x != NULL);
  g_return_if_fail(out_y != NULL);

  double direction = homeworlds_view_previous_marker_direction(side);
  *out_x = piece->x;
  if (piece->is_ship) {
    double base_y = piece->points_up ? piece->y + (piece->height / 2.0) : piece->y - (piece->height / 2.0);
    *out_y = base_y + (direction * HOMEWORLDS_VIEW_PREVIOUS_MARKER_OFFSET);
  } else {
    *out_y = piece->y + (direction * ((piece->height / 2.0) + HOMEWORLDS_VIEW_PREVIOUS_MARKER_OFFSET));
  }
}

static void homeworlds_view_draw_previous_marker_plus(cairo_t *cr, double x, double y) {
  g_return_if_fail(cr != NULL);

  cairo_save(cr);
  homeworlds_view_set_marker_source_color(cr, HOMEWORLDS_COLOR_GREEN, 0.96);
  cairo_set_line_width(cr, HOMEWORLDS_VIEW_PREVIOUS_MARKER_LINE_WIDTH);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, x - HOMEWORLDS_VIEW_PREVIOUS_MARKER_PLUS_HALF_LENGTH, y);
  cairo_line_to(cr, x + HOMEWORLDS_VIEW_PREVIOUS_MARKER_PLUS_HALF_LENGTH, y);
  cairo_move_to(cr, x, y - HOMEWORLDS_VIEW_PREVIOUS_MARKER_PLUS_HALF_LENGTH);
  cairo_line_to(cr, x, y + HOMEWORLDS_VIEW_PREVIOUS_MARKER_PLUS_HALF_LENGTH);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void homeworlds_view_draw_previous_marker_disc(cairo_t *cr,
                                                      double x,
                                                      double y,
                                                      HomeworldsColor color) {
  g_return_if_fail(cr != NULL);
  g_return_if_fail(color <= HOMEWORLDS_COLOR_BLUE);

  cairo_save(cr);
  cairo_set_line_width(cr, HOMEWORLDS_VIEW_PREVIOUS_DISC_LINE_WIDTH);
  cairo_arc(cr, x, y, HOMEWORLDS_VIEW_PREVIOUS_MARKER_DISC_RADIUS, 0.0, G_PI * 2.0);
  homeworlds_view_set_marker_source_color(cr, color, 0.96);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.82);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void homeworlds_view_draw_previous_marker_catastrophe_disc(cairo_t *cr,
                                                                  double x,
                                                                  double y,
                                                                  HomeworldsColor color) {
  const double strike = HOMEWORLDS_VIEW_PREVIOUS_MARKER_DISC_RADIUS * 0.82;

  g_return_if_fail(cr != NULL);
  g_return_if_fail(color <= HOMEWORLDS_COLOR_BLUE);

  cairo_save(cr);
  cairo_set_line_width(cr, HOMEWORLDS_VIEW_PREVIOUS_CATASTROPHE_LINE_WIDTH);
  cairo_arc(cr, x, y, HOMEWORLDS_VIEW_PREVIOUS_MARKER_DISC_RADIUS, 0.0, G_PI * 2.0);
  homeworlds_view_set_marker_source_color(cr, color, 0.96);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0.52, 0.54, 0.56, 0.94);
  cairo_stroke(cr);
  cairo_move_to(cr, x - strike, y - strike);
  cairo_line_to(cr, x + strike, y + strike);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void homeworlds_view_draw_previous_marker_arrow(cairo_t *cr,
                                                       double x,
                                                       double y,
                                                       double direction,
                                                       HomeworldsColor color) {
  double start_y = y - (direction * HOMEWORLDS_VIEW_PREVIOUS_MARKER_ARROW_HALF_LENGTH);
  double end_y = y + (direction * HOMEWORLDS_VIEW_PREVIOUS_MARKER_ARROW_HALF_LENGTH);

  g_return_if_fail(cr != NULL);
  g_return_if_fail(direction == 1.0 || direction == -1.0);
  g_return_if_fail(color <= HOMEWORLDS_COLOR_BLUE);

  cairo_save(cr);
  homeworlds_view_set_marker_source_color(cr, color, 0.96);
  cairo_set_line_width(cr, HOMEWORLDS_VIEW_PREVIOUS_MARKER_LINE_WIDTH);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_move_to(cr, x, start_y);
  cairo_line_to(cr, x, end_y);
  cairo_stroke(cr);
  cairo_move_to(cr, x, end_y);
  cairo_line_to(cr,
                x - HOMEWORLDS_VIEW_PREVIOUS_MARKER_ARROW_HEAD,
                end_y - (direction * HOMEWORLDS_VIEW_PREVIOUS_MARKER_ARROW_HEAD));
  cairo_line_to(cr,
                x + HOMEWORLDS_VIEW_PREVIOUS_MARKER_ARROW_HEAD,
                end_y - (direction * HOMEWORLDS_VIEW_PREVIOUS_MARKER_ARROW_HEAD));
  cairo_close_path(cr);
  cairo_fill(cr);
  cairo_restore(cr);
}

static gboolean homeworlds_view_position_has_non_home_system(const HomeworldsPosition *position) {
  g_return_val_if_fail(position != NULL, FALSE);

  for (guint system_index = 2; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    if (!homeworlds_system_is_empty(&position->systems[system_index])) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_view_homeworld_rows_are_compact(const HomeworldsPosition *position) {
  const HomeworldsSystem *player_1 = NULL;
  const HomeworldsSystem *player_2 = NULL;
  guint player_1_size_mask = 0;
  guint player_2_size_mask = 0;

  g_return_val_if_fail(position != NULL, FALSE);

  player_1 = &position->systems[0];
  player_2 = &position->systems[1];
  for (guint star_slot = 0; star_slot < HOMEWORLDS_STAR_SLOT_COUNT; ++star_slot) {
    HomeworldsPyramid player_1_star = player_1->stars[star_slot];
    HomeworldsPyramid player_2_star = player_2->stars[star_slot];

    if (homeworlds_pyramid_is_valid(player_1_star)) {
      player_1_size_mask |= 1u << (homeworlds_pyramid_size(player_1_star) - 1);
    }
    if (homeworlds_pyramid_is_valid(player_2_star)) {
      player_2_size_mask |= 1u << (homeworlds_pyramid_size(player_2_star) - 1);
    }
  }

  return player_1_size_mask == player_2_size_mask ||
         homeworlds_system_is_connected(player_1, player_2);
}

static HomeworldsViewSystemRow homeworlds_view_system_row(const HomeworldsPosition *position, guint system_index) {
  const HomeworldsSystem *system = NULL;
  gboolean connected_to_player_1 = FALSE;
  gboolean connected_to_player_2 = FALSE;

  g_return_val_if_fail(position != NULL, HOMEWORLDS_VIEW_SYSTEM_ROW_MIDDLE);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, HOMEWORLDS_VIEW_SYSTEM_ROW_MIDDLE);

  if (system_index < 2 || homeworlds_view_homeworld_rows_are_compact(position)) {
    return HOMEWORLDS_VIEW_SYSTEM_ROW_MIDDLE;
  }

  system = &position->systems[system_index];
  connected_to_player_1 = homeworlds_system_is_connected(system, &position->systems[0]);
  connected_to_player_2 = homeworlds_system_is_connected(system, &position->systems[1]);

  if (connected_to_player_2 && !connected_to_player_1) {
    return HOMEWORLDS_VIEW_SYSTEM_ROW_TOP;
  }
  if (connected_to_player_1 && !connected_to_player_2) {
    return HOMEWORLDS_VIEW_SYSTEM_ROW_BOTTOM;
  }

  return HOMEWORLDS_VIEW_SYSTEM_ROW_MIDDLE;
}

static void homeworlds_view_board_row_group_append(HomeworldsViewBoardRowGroup *group, guint system_index) {
  g_return_if_fail(group != NULL);
  g_return_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT);
  g_return_if_fail(group->count < G_N_ELEMENTS(group->systems));

  group->systems[group->count++] = system_index;
}

static void homeworlds_view_board_row_groups_init(const HomeworldsPosition *position,
                                                  HomeworldsViewBoardRowGroup *rows) {
  g_return_if_fail(position != NULL);
  g_return_if_fail(rows != NULL);

  for (guint row_index = 0; row_index < HOMEWORLDS_VIEW_BOARD_ROW_COUNT; ++row_index) {
    rows[row_index].count = 0;
  }

  homeworlds_view_board_row_group_append(&rows[HOMEWORLDS_VIEW_BOARD_ROW_PLAYER_2], 1);
  for (guint system_index = 2; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    HomeworldsViewBoardRow row = HOMEWORLDS_VIEW_BOARD_ROW_MIDDLE;

    if (homeworlds_system_is_empty(&position->systems[system_index])) {
      continue;
    }

    row = homeworlds_view_board_row_for_system(position, system_index);
    homeworlds_view_board_row_group_append(&rows[row], system_index);
  }
  homeworlds_view_board_row_group_append(&rows[HOMEWORLDS_VIEW_BOARD_ROW_PLAYER_1], 0);
}

static gboolean homeworlds_view_board_rows_are_connected(const HomeworldsPosition *position,
                                                         const HomeworldsViewBoardRowGroup *previous,
                                                         const HomeworldsViewBoardRowGroup *current) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(previous != NULL, FALSE);
  g_return_val_if_fail(current != NULL, FALSE);

  for (guint previous_index = 0; previous_index < previous->count; ++previous_index) {
    const HomeworldsSystem *previous_system = &position->systems[previous->systems[previous_index]];

    for (guint current_index = 0; current_index < current->count; ++current_index) {
      const HomeworldsSystem *current_system = &position->systems[current->systems[current_index]];

      if (homeworlds_system_is_connected(previous_system, current_system)) {
        return TRUE;
      }
    }
  }

  return FALSE;
}

static void homeworlds_view_default_row_y_fractions(gboolean has_non_home_system, double *fractions) {
  double player_1_y = has_non_home_system ? 0.84 : 0.72;
  double player_2_y = has_non_home_system ? 0.16 : 0.28;
  double band = player_1_y - player_2_y;

  g_return_if_fail(fractions != NULL);

  fractions[HOMEWORLDS_VIEW_BOARD_ROW_PLAYER_2] = player_2_y;
  fractions[HOMEWORLDS_VIEW_BOARD_ROW_TOP] = player_2_y + (band * 0.25);
  fractions[HOMEWORLDS_VIEW_BOARD_ROW_MIDDLE] = player_2_y + (band * 0.50);
  fractions[HOMEWORLDS_VIEW_BOARD_ROW_BOTTOM] = player_2_y + (band * 0.75);
  fractions[HOMEWORLDS_VIEW_BOARD_ROW_PLAYER_1] = player_1_y;
}

static void homeworlds_view_row_y_fractions(const HomeworldsPosition *position, double *fractions) {
  gboolean has_non_home_system = FALSE;
  double player_1_y = 0.0;
  double player_2_y = 0.0;
  double band = 0.0;
  HomeworldsViewBoardRowGroup rows[HOMEWORLDS_VIEW_BOARD_ROW_COUNT] = {0};
  double row_units[HOMEWORLDS_VIEW_BOARD_ROW_COUNT] = {0.0};
  guint previous_row = HOMEWORLDS_VIEW_BOARD_ROW_COUNT;
  double total_units = 0.0;

  g_return_if_fail(position != NULL);
  g_return_if_fail(fractions != NULL);

  has_non_home_system = homeworlds_view_position_has_non_home_system(position);
  player_1_y = has_non_home_system ? 0.84 : 0.72;
  player_2_y = has_non_home_system ? 0.16 : 0.28;
  band = player_1_y - player_2_y;

  homeworlds_view_default_row_y_fractions(has_non_home_system, fractions);
  homeworlds_view_board_row_groups_init(position, rows);

  for (guint row_index = 0; row_index < HOMEWORLDS_VIEW_BOARD_ROW_COUNT; ++row_index) {
    if (rows[row_index].count == 0) {
      continue;
    }

    if (previous_row != HOMEWORLDS_VIEW_BOARD_ROW_COUNT) {
      HomeworldsViewBoardRow previous = (HomeworldsViewBoardRow)previous_row;

      total_units += homeworlds_view_board_rows_are_connected(position, &rows[previous], &rows[row_index]) ? 1.0 : 2.0;
    }
    row_units[row_index] = total_units;
    previous_row = row_index;
  }

  if (total_units <= 0.0) {
    return;
  }

  for (guint row_index = 0; row_index < HOMEWORLDS_VIEW_BOARD_ROW_COUNT; ++row_index) {
    if (rows[row_index].count == 0) {
      continue;
    }

    fractions[row_index] = player_2_y + (band * row_units[row_index] / total_units);
  }
}

static void homeworlds_view_row_y_positions(const HomeworldsPosition *position, double height, double *positions) {
  g_return_if_fail(height > 0.0);
  g_return_if_fail(position != NULL);
  g_return_if_fail(positions != NULL);

  homeworlds_view_row_y_fractions(position, positions);
  for (guint row_index = 0; row_index < HOMEWORLDS_VIEW_BOARD_ROW_COUNT; ++row_index) {
    positions[row_index] *= height;
  }
}

static double homeworlds_view_bank_reserved_width(void) {
  double button_width = 0.0;

  for (HomeworldsSize size = HOMEWORLDS_SIZE_SMALL; size <= HOMEWORLDS_SIZE_LARGE; size++) {
    button_width += homeworlds_view_bank_button_width(size);
  }

  return HOMEWORLDS_VIEW_BANK_MARGIN +
         (2.0 * HOMEWORLDS_VIEW_BANK_INNER_MARGIN) +
         button_width +
         (2.0 * HOMEWORLDS_VIEW_BANK_GRID_COLUMN_SPACING);
}

static void homeworlds_view_row_x_bounds(double width,
                                         double *out_left,
                                         double *out_right) {
  double bank_reserved_width = homeworlds_view_bank_reserved_width();
  double left = HOMEWORLDS_VIEW_ROW_MARGIN;
  double right = width - bank_reserved_width - HOMEWORLDS_VIEW_ROW_MARGIN;

  g_return_if_fail(width > 0.0);
  g_return_if_fail(out_left != NULL);
  g_return_if_fail(out_right != NULL);

  if (right <= left) {
    right = left;
  }

  *out_left = left;
  *out_right = right;
}

static gboolean homeworlds_view_measure_system_width(const HomeworldsPosition *position,
                                                     guint system_index,
                                                     double *out_width) {
  HomeworldsViewSystemLayout layout = {0};

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(out_width != NULL, FALSE);

  if (system_index >= 2 && homeworlds_system_is_empty(&position->systems[system_index])) {
    return FALSE;
  }
  if (!homeworlds_view_calculate_system_layout(&position->systems[system_index],
                                               system_index,
                                               0.0,
                                               0.0,
                                               &layout)) {
    return FALSE;
  }

  *out_width = layout.box_width;
  return TRUE;
}

static gboolean homeworlds_view_collect_target_row(const HomeworldsPosition *position,
                                                   guint target_system_index,
                                                   HomeworldsViewRowSystem *systems,
                                                   guint max_systems,
                                                   guint *out_count,
                                                   guint *out_target_index,
                                                   double *out_total_width) {
  HomeworldsViewSystemRow row = HOMEWORLDS_VIEW_SYSTEM_ROW_MIDDLE;
  guint count = 0;
  guint target_index = HOMEWORLDS_INVALID_INDEX;
  double total_width = 0.0;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(target_system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(systems != NULL, FALSE);
  g_return_val_if_fail(max_systems > 0, FALSE);
  g_return_val_if_fail(out_count != NULL, FALSE);
  g_return_val_if_fail(out_target_index != NULL, FALSE);
  g_return_val_if_fail(out_total_width != NULL, FALSE);

  if (target_system_index < 2) {
    double width = 0.0;

    if (!homeworlds_view_measure_system_width(position, target_system_index, &width)) {
      return FALSE;
    }

    systems[0] = (HomeworldsViewRowSystem){
      .width = width,
    };
    *out_count = 1;
    *out_target_index = 0;
    *out_total_width = width;
    return TRUE;
  }
  if (homeworlds_system_is_empty(&position->systems[target_system_index])) {
    return FALSE;
  }

  row = homeworlds_view_system_row(position, target_system_index);
  for (guint i = 2; i < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++i) {
    double width = 0.0;

    if (homeworlds_system_is_empty(&position->systems[i]) ||
        homeworlds_view_system_row(position, i) != row) {
      continue;
    }
    if (!homeworlds_view_measure_system_width(position, i, &width)) {
      return FALSE;
    }
    g_return_val_if_fail(count < max_systems, FALSE);

    if (i == target_system_index) {
      target_index = count;
    }
    systems[count++] = (HomeworldsViewRowSystem){
      .width = width,
    };
    total_width += width;
  }

  if (target_index == HOMEWORLDS_INVALID_INDEX) {
    return FALSE;
  }

  *out_count = count;
  *out_target_index = target_index;
  *out_total_width = total_width;
  return TRUE;
}

static double homeworlds_view_row_required_width(double total_system_width, guint system_count) {
  g_return_val_if_fail(system_count > 0, 0.0);

  return total_system_width + (HOMEWORLDS_VIEW_ROW_MIN_EMPTY_GAP * (double)(system_count + 1));
}

static double homeworlds_view_board_width_for_row(double required_row_width) {
  return (2.0 * HOMEWORLDS_VIEW_ROW_MARGIN) + homeworlds_view_bank_reserved_width() + required_row_width;
}

static void homeworlds_view_board_row_extent_add(HomeworldsViewBoardRowExtent *extent,
                                                 double top,
                                                 double bottom) {
  g_return_if_fail(extent != NULL);
  g_return_if_fail(bottom >= top);

  if (!extent->present) {
    extent->present = TRUE;
    extent->top = top;
    extent->bottom = bottom;
    return;
  }

  extent->top = MIN(extent->top, top);
  extent->bottom = MAX(extent->bottom, bottom);
}

static HomeworldsViewBoardRow homeworlds_view_board_row_for_system(const HomeworldsPosition *position,
                                                                   guint system_index) {
  g_return_val_if_fail(position != NULL, HOMEWORLDS_VIEW_BOARD_ROW_MIDDLE);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, HOMEWORLDS_VIEW_BOARD_ROW_MIDDLE);

  if (system_index == 0) {
    return HOMEWORLDS_VIEW_BOARD_ROW_PLAYER_1;
  }
  if (system_index == 1) {
    return HOMEWORLDS_VIEW_BOARD_ROW_PLAYER_2;
  }

  switch (homeworlds_view_system_row(position, system_index)) {
    case HOMEWORLDS_VIEW_SYSTEM_ROW_TOP:
      return HOMEWORLDS_VIEW_BOARD_ROW_TOP;
    case HOMEWORLDS_VIEW_SYSTEM_ROW_BOTTOM:
      return HOMEWORLDS_VIEW_BOARD_ROW_BOTTOM;
    case HOMEWORLDS_VIEW_SYSTEM_ROW_MIDDLE:
    default:
      return HOMEWORLDS_VIEW_BOARD_ROW_MIDDLE;
  }
}

static void homeworlds_view_board_row_extents_init(const HomeworldsPosition *position,
                                                   HomeworldsViewBoardRowExtent *extents) {
  double fractions[HOMEWORLDS_VIEW_BOARD_ROW_COUNT] = {0.0};

  g_return_if_fail(position != NULL);
  g_return_if_fail(extents != NULL);

  homeworlds_view_row_y_fractions(position, fractions);
  for (guint row_index = 0; row_index < HOMEWORLDS_VIEW_BOARD_ROW_COUNT; ++row_index) {
    extents[row_index].fraction = fractions[row_index];
  }
}

static double homeworlds_view_required_height_for_row_extents(const HomeworldsViewBoardRowExtent *extents,
                                                             double viewport_height) {
  double required_height = viewport_height;
  const HomeworldsViewBoardRowExtent *previous = NULL;

  g_return_val_if_fail(extents != NULL, viewport_height);
  g_return_val_if_fail(viewport_height > 0.0, viewport_height);

  for (guint i = 0; i < HOMEWORLDS_VIEW_BOARD_ROW_COUNT; i++) {
    const HomeworldsViewBoardRowExtent *extent = &extents[i];
    double fraction = extent->fraction;

    if (!extent->present) {
      continue;
    }

    if (fraction > 0.0) {
      required_height = MAX(required_height, -extent->top / fraction);
    }
    if (fraction < 1.0) {
      required_height = MAX(required_height, extent->bottom / (1.0 - fraction));
    }

    if (previous != NULL && fraction > previous->fraction) {
      double required_gap_height =
          (previous->bottom + HOMEWORLDS_VIEW_ROW_MIN_VERTICAL_GAP - extent->top) /
          (fraction - previous->fraction);
      required_height = MAX(required_height, required_gap_height);
    }
    previous = extent;
  }

  return ceil(required_height);
}

static double homeworlds_view_row_center_for_index(double left,
                                                   double right,
                                                   const HomeworldsViewRowSystem *systems,
                                                   guint system_count,
                                                   guint target_index,
                                                   double total_system_width) {
  double available_width = right - left;
  double empty_gap = 0.0;
  double cursor = left;

  g_return_val_if_fail(systems != NULL, (left + right) / 2.0);
  g_return_val_if_fail(system_count > 0, (left + right) / 2.0);
  g_return_val_if_fail(target_index < system_count, (left + right) / 2.0);

  empty_gap = (available_width - total_system_width) / (double)(system_count + 1);
  if (empty_gap < 0.0) {
    empty_gap = 0.0;
  }
  cursor += empty_gap;

  for (guint i = 0; i < system_count; ++i) {
    if (i == target_index) {
      return cursor + (systems[i].width / 2.0);
    }
    cursor += systems[i].width + empty_gap;
  }

  return (left + right) / 2.0;
}

gboolean homeworlds_view_calculate_board_content_size(const HomeworldsPosition *position,
                                                      double viewport_width,
                                                      double viewport_height,
                                                      int *out_width,
                                                      int *out_height) {
  double required_width = viewport_width;
  double system_viewport_height = 0.0;
  double required_system_height = 0.0;
  HomeworldsViewBoardRowExtent extents[HOMEWORLDS_VIEW_BOARD_ROW_COUNT] = {0};

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(viewport_width > 0.0, FALSE);
  g_return_val_if_fail(viewport_height > 0.0, FALSE);
  g_return_val_if_fail(out_width != NULL, FALSE);
  g_return_val_if_fail(out_height != NULL, FALSE);

  homeworlds_view_board_row_extents_init(position, extents);
  for (guint system_index = 0; system_index < 2; ++system_index) {
    const HomeworldsSystem *system = &position->systems[system_index];
    HomeworldsViewSystemLayout layout = {0};
    double system_width = 0.0;

    if (!homeworlds_view_measure_system_width(position, system_index, &system_width)) {
      continue;
    }
    required_width = MAX(required_width,
                         homeworlds_view_board_width_for_row(homeworlds_view_row_required_width(system_width, 1)));
    if (homeworlds_view_calculate_system_layout(system, system_index, 0.0, 0.0, &layout)) {
      HomeworldsViewBoardRow row = homeworlds_view_board_row_for_system(position, system_index);
      homeworlds_view_board_row_extent_add(&extents[row], layout.box_y, layout.box_y + layout.box_height);
    }
  }

  for (guint row_value = HOMEWORLDS_VIEW_SYSTEM_ROW_TOP;
       row_value <= HOMEWORLDS_VIEW_SYSTEM_ROW_BOTTOM;
       ++row_value) {
    HomeworldsViewSystemRow row = (HomeworldsViewSystemRow)row_value;
    guint system_count = 0;
    double total_width = 0.0;

    for (guint system_index = 2; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
      const HomeworldsSystem *system = &position->systems[system_index];
      HomeworldsViewSystemLayout layout = {0};
      double system_width = 0.0;

      if (homeworlds_system_is_empty(system) ||
          homeworlds_view_system_row(position, system_index) != row) {
        continue;
      }
      if (!homeworlds_view_measure_system_width(position, system_index, &system_width)) {
        continue;
      }
      total_width += system_width;
      system_count++;
      if (homeworlds_view_calculate_system_layout(system, system_index, 0.0, 0.0, &layout)) {
        HomeworldsViewBoardRow board_row = homeworlds_view_board_row_for_system(position, system_index);
        homeworlds_view_board_row_extent_add(&extents[board_row], layout.box_y, layout.box_y + layout.box_height);
      }
    }

    if (system_count > 0) {
      required_width = MAX(required_width,
                           homeworlds_view_board_width_for_row(
                               homeworlds_view_row_required_width(total_width, system_count)));
    }
  }

  system_viewport_height = homeworlds_view_system_area_height(viewport_height);
  required_system_height = homeworlds_view_required_height_for_row_extents(extents, system_viewport_height);
  *out_width = (int)ceil(required_width);
  *out_height = (int)ceil(required_system_height + homeworlds_view_action_legend_height());
  return TRUE;
}

double homeworlds_view_calculate_board_content_width(const HomeworldsPosition *position, double viewport_width) {
  int content_width = HOMEWORLDS_VIEW_FALLBACK_BOARD_WIDTH;
  int content_height = HOMEWORLDS_VIEW_FALLBACK_BOARD_HEIGHT;

  g_return_val_if_fail(position != NULL, (double)HOMEWORLDS_VIEW_FALLBACK_BOARD_WIDTH);
  g_return_val_if_fail(viewport_width > 0.0, (double)HOMEWORLDS_VIEW_FALLBACK_BOARD_WIDTH);

  if (!homeworlds_view_calculate_board_content_size(position,
                                                    viewport_width,
                                                    (double)HOMEWORLDS_VIEW_FALLBACK_BOARD_HEIGHT,
                                                    &content_width,
                                                    &content_height)) {
    return (double)HOMEWORLDS_VIEW_FALLBACK_BOARD_WIDTH;
  }

  return (double)content_width;
}

gboolean homeworlds_view_calculate_system_center(const HomeworldsPosition *position,
                                                 guint system_index,
                                                 double width,
                                                 double height,
                                                 double *out_x,
                                                 double *out_y) {
  double row_y[HOMEWORLDS_VIEW_BOARD_ROW_COUNT] = {0.0};
  HomeworldsViewBoardRow board_row = HOMEWORLDS_VIEW_BOARD_ROW_MIDDLE;
  HomeworldsViewRowSystem row_systems[HOMEWORLDS_SYSTEM_SLOT_COUNT] = {0};
  guint row_count = 0;
  guint row_index = HOMEWORLDS_INVALID_INDEX;
  double total_row_width = 0.0;
  double row_left = 0.0;
  double row_right = 0.0;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(width > 0.0, FALSE);
  g_return_val_if_fail(height > 0.0, FALSE);
  g_return_val_if_fail(out_x != NULL, FALSE);
  g_return_val_if_fail(out_y != NULL, FALSE);

  if (system_index >= 2 && homeworlds_system_is_empty(&position->systems[system_index])) {
    return FALSE;
  }

  homeworlds_view_row_y_positions(position, homeworlds_view_system_area_height(height), row_y);
  homeworlds_view_row_x_bounds(width, &row_left, &row_right);
  if (!homeworlds_view_collect_target_row(position,
                                          system_index,
                                          row_systems,
                                          G_N_ELEMENTS(row_systems),
                                          &row_count,
                                          &row_index,
                                          &total_row_width)) {
    return FALSE;
  }

  board_row = homeworlds_view_board_row_for_system(position, system_index);
  *out_y = row_y[board_row];
  *out_x = homeworlds_view_row_center_for_index(row_left,
                                                row_right,
                                                row_systems,
                                                row_count,
                                                row_index,
                                                total_row_width);
  return TRUE;
}

static void homeworlds_view_piece_dimensions(HomeworldsPyramid pyramid,
                                             gboolean is_ship,
                                             double *out_width,
                                             double *out_height) {
  g_return_if_fail(homeworlds_pyramid_is_valid(pyramid));
  g_return_if_fail(out_width != NULL);
  g_return_if_fail(out_height != NULL);

  if (is_ship) {
    HomeworldsViewPyramidMetrics metrics = {0};
    g_return_if_fail(homeworlds_view_pyramid_metrics(homeworlds_pyramid_size(pyramid), &metrics));
    *out_width = metrics.base;
    *out_height = metrics.height;
    return;
  }

  *out_width = homeworlds_view_star_side(homeworlds_pyramid_size(pyramid));
  *out_height = *out_width;
}

static gboolean homeworlds_view_piece_layout_bounds(const HomeworldsViewPieceLayout *piece,
                                                    double *out_left,
                                                    double *out_top,
                                                    double *out_right,
                                                    double *out_bottom) {
  g_return_val_if_fail(piece != NULL, FALSE);
  g_return_val_if_fail(out_left != NULL, FALSE);
  g_return_val_if_fail(out_top != NULL, FALSE);
  g_return_val_if_fail(out_right != NULL, FALSE);
  g_return_val_if_fail(out_bottom != NULL, FALSE);

  *out_left = piece->x - (piece->width / 2.0);
  *out_top = piece->y - (piece->height / 2.0);
  *out_right = piece->x + (piece->width / 2.0);
  *out_bottom = piece->y + (piece->height / 2.0);
  return TRUE;
}

static gboolean homeworlds_view_piece_row_append(HomeworldsViewPieceRow *row,
                                                 HomeworldsPyramid pyramid,
                                                 gboolean is_ship,
                                                 guint side,
                                                 guint slot,
                                                 gboolean points_up) {
  HomeworldsViewPieceLayout *piece = NULL;

  g_return_val_if_fail(row != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);
  g_return_val_if_fail(row->count < G_N_ELEMENTS(row->pieces), FALSE);

  piece = &row->pieces[row->count++];
  *piece = (HomeworldsViewPieceLayout){
    .pyramid = pyramid,
    .is_ship = is_ship,
    .side = side,
    .slot = slot,
    .points_up = points_up,
  };
  homeworlds_view_piece_dimensions(pyramid, is_ship, &piece->width, &piece->height);
  row->height = MAX(row->height, piece->height);
  return TRUE;
}

static gboolean homeworlds_view_system_layout_append_piece(HomeworldsViewSystemLayout *layout,
                                                           const HomeworldsViewPieceLayout *piece) {
  g_return_val_if_fail(layout != NULL, FALSE);
  g_return_val_if_fail(piece != NULL, FALSE);
  g_return_val_if_fail(layout->piece_count < G_N_ELEMENTS(layout->pieces), FALSE);

  layout->pieces[layout->piece_count++] = *piece;
  return TRUE;
}

static gboolean homeworlds_view_system_layout_append_row(HomeworldsViewSystemLayout *layout,
                                                         const HomeworldsViewPieceRow *row,
                                                         double center_x,
                                                         double center_y) {
  double total_width = 0.0;
  double cursor = 0.0;

  g_return_val_if_fail(layout != NULL, FALSE);
  g_return_val_if_fail(row != NULL, FALSE);

  if (row->count == 0) {
    return TRUE;
  }

  for (guint i = 0; i < row->count; ++i) {
    total_width += row->pieces[i].width;
  }
  total_width += HOMEWORLDS_VIEW_ITEM_GAP * (double)(row->count - 1);
  cursor = center_x - (total_width / 2.0);

  for (guint i = 0; i < row->count; ++i) {
    HomeworldsViewPieceLayout piece = row->pieces[i];
    piece.x = cursor + (piece.width / 2.0);
    piece.y = center_y;
    cursor += piece.width + HOMEWORLDS_VIEW_ITEM_GAP;
    if (!homeworlds_view_system_layout_append_piece(layout, &piece)) {
      return FALSE;
    }
  }

  return TRUE;
}

static void homeworlds_view_system_layout_finish(HomeworldsViewSystemLayout *layout,
                                                 guint system_index,
                                                 double center_x,
                                                 double center_y) {
  const double min_width = system_index < 2 ? 168.0 : 108.0;
  const double min_height = 56.0;
  double min_x = center_x - (min_width / 2.0);
  double max_x = center_x + (min_width / 2.0);
  double min_y = center_y - 8.0;
  double max_y = center_y + 8.0;

  g_return_if_fail(layout != NULL);

  for (guint i = 0; i < layout->piece_count; ++i) {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

    if (!homeworlds_view_piece_layout_bounds(&layout->pieces[i], &left, &top, &right, &bottom)) {
      continue;
    }

    min_x = MIN(min_x, left);
    max_x = MAX(max_x, right);
    min_y = MIN(min_y, top);
    max_y = MAX(max_y, bottom);
  }

  layout->box_x = min_x - HOMEWORLDS_VIEW_SYSTEM_PADDING_X;
  layout->box_y = min_y - HOMEWORLDS_VIEW_SYSTEM_LABEL_HEIGHT;
  layout->box_width = MAX(min_width, (max_x - min_x) + (2.0 * HOMEWORLDS_VIEW_SYSTEM_PADDING_X));
  layout->box_height = MAX(min_height,
                           (max_y - layout->box_y) + HOMEWORLDS_VIEW_SYSTEM_PADDING_BOTTOM);
}

static gboolean homeworlds_view_collect_star_row(const HomeworldsSystem *system, HomeworldsViewPieceRow *row) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(row != NULL, FALSE);

  for (guint star_slot = 0; star_slot < HOMEWORLDS_STAR_SLOT_COUNT; ++star_slot) {
    HomeworldsPyramid star = system->stars[star_slot];
    if (!homeworlds_pyramid_is_valid(star)) {
      continue;
    }
    if (!homeworlds_view_piece_row_append(row, star, FALSE, 0, star_slot, FALSE)) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean homeworlds_view_collect_ship_row(const HomeworldsSystem *system,
                                                 guint side,
                                                 gboolean points_up,
                                                 HomeworldsViewPieceRow *row) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(row != NULL, FALSE);

  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    HomeworldsPyramid ship = system->ships[side][slot];
    if (!homeworlds_pyramid_is_valid(ship)) {
      break;
    }
    if (!homeworlds_view_piece_row_append(row, ship, TRUE, side, slot, points_up)) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean homeworlds_view_calculate_system_layout(const HomeworldsSystem *system,
                                                        guint system_index,
                                                        double center_x,
                                                        double center_y,
                                                        HomeworldsViewSystemLayout *out_layout) {
  HomeworldsViewPieceRow row = {0};
  double piece_center_y = center_y;

  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(out_layout != NULL, FALSE);

  memset(out_layout, 0, sizeof(*out_layout));
  if (system_index < 2) {
    piece_center_y += HOMEWORLDS_VIEW_HOMEWORLD_PIECE_Y_OFFSET;
  }

  if (!homeworlds_view_collect_ship_row(system, 1, FALSE, &row) ||
      !homeworlds_view_collect_star_row(system, &row) ||
      !homeworlds_view_collect_ship_row(system, 0, TRUE, &row) ||
      !homeworlds_view_system_layout_append_row(out_layout, &row, center_x, piece_center_y)) {
    return FALSE;
  }

  homeworlds_view_system_layout_finish(out_layout, system_index, center_x, center_y);
  return TRUE;
}

gboolean homeworlds_view_calculate_homeworld_layout(guint system_index,
                                                    double center_x,
                                                    double center_y,
                                                    HomeworldsViewHomeworldLayout *out_layout) {
  g_return_val_if_fail(out_layout != NULL, FALSE);

  memset(out_layout, 0, sizeof(*out_layout));
  if (system_index >= 2) {
    return FALSE;
  }

  out_layout->star_x[0] = center_x - HOMEWORLDS_VIEW_HOMEWORLD_STAR_OFFSET;
  out_layout->star_x[1] = center_x + HOMEWORLDS_VIEW_HOMEWORLD_STAR_OFFSET;
  out_layout->star_y = center_y + HOMEWORLDS_VIEW_HOMEWORLD_PIECE_Y_OFFSET;
  out_layout->ship_y = out_layout->star_y;

  if (system_index == 0) {
    out_layout->ship_x = center_x + HOMEWORLDS_VIEW_HOMEWORLD_SHIP_OFFSET;
    out_layout->ship_points_up = TRUE;
  } else {
    out_layout->ship_x = center_x - HOMEWORLDS_VIEW_HOMEWORLD_SHIP_OFFSET;
    out_layout->ship_points_up = FALSE;
  }

  return TRUE;
}

static const HomeworldsViewPieceLayout *homeworlds_view_system_layout_find_marker_piece(
    const HomeworldsViewSystemLayout *layout,
    const HomeworldsViewPreviousMoveMarker *marker) {
  g_return_val_if_fail(layout != NULL, NULL);
  g_return_val_if_fail(marker != NULL, NULL);

  for (guint i = 0; i < layout->piece_count; ++i) {
    const HomeworldsViewPieceLayout *piece = &layout->pieces[i];

    if (piece->is_ship != marker->is_ship ||
        piece->slot != marker->slot ||
        piece->pyramid != marker->pyramid) {
      continue;
    }
    if (marker->is_ship && piece->side != marker->side) {
      continue;
    }

    return piece;
  }

  return NULL;
}

static gboolean homeworlds_view_system_layout_find_catastrophe_marker_point(
    const HomeworldsViewSystemLayout *layout,
    double *out_x,
    double *out_y) {
  double x_sum = 0.0;
  double max_bottom = 0.0;
  guint star_count = 0;

  g_return_val_if_fail(layout != NULL, FALSE);
  g_return_val_if_fail(out_x != NULL, FALSE);
  g_return_val_if_fail(out_y != NULL, FALSE);

  for (guint i = 0; i < layout->piece_count; ++i) {
    const HomeworldsViewPieceLayout *piece = &layout->pieces[i];

    if (piece->is_ship) {
      continue;
    }

    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    if (!homeworlds_view_piece_layout_bounds(piece, &left, &top, &right, &bottom)) {
      continue;
    }

    x_sum += piece->x;
    max_bottom = star_count == 0 ? bottom : MAX(max_bottom, bottom);
    star_count++;
  }

  if (star_count == 0) {
    return FALSE;
  }

  *out_x = x_sum / star_count;
  *out_y = max_bottom + HOMEWORLDS_VIEW_PREVIOUS_MARKER_OFFSET;
  return TRUE;
}

static void homeworlds_view_draw_previous_move_markers(HomeworldsView *view,
                                                       cairo_t *cr,
                                                       guint system_index,
                                                       const HomeworldsViewSystemLayout *layout) {
  g_return_if_fail(view != NULL);
  g_return_if_fail(cr != NULL);
  g_return_if_fail(layout != NULL);
  g_return_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT);

  for (gsize i = 0; i < view->previous_move_marker_count; ++i) {
    const HomeworldsViewPreviousMoveMarker *marker = &view->previous_move_markers[i];
    const HomeworldsViewPieceLayout *piece = NULL;
    double x = 0.0;
    double y = 0.0;
    double direction = 1.0;

    if (marker->system_index != system_index) {
      continue;
    }

    if (marker->kind == HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_CATASTROPHE) {
      if (homeworlds_view_system_layout_find_catastrophe_marker_point(layout, &x, &y)) {
        homeworlds_view_draw_previous_marker_catastrophe_disc(cr, x, y, marker->color);
      }
      continue;
    }

    piece = homeworlds_view_system_layout_find_marker_piece(layout, marker);
    if (piece == NULL) {
      continue;
    }

    direction = homeworlds_view_previous_marker_direction(marker->side);
    homeworlds_view_previous_marker_piece_point(piece, marker->side, &x, &y);
    switch (marker->kind) {
      case HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_BUILD:
        homeworlds_view_draw_previous_marker_plus(cr, x, y);
        break;
      case HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_TRADE:
        homeworlds_view_draw_previous_marker_disc(cr, x, y, marker->color);
        break;
      case HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_MOVE:
        homeworlds_view_draw_previous_marker_arrow(cr, x, y, -direction, HOMEWORLDS_COLOR_YELLOW);
        break;
      case HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_CAPTURE:
        homeworlds_view_draw_previous_marker_arrow(cr, x, y, -direction, HOMEWORLDS_COLOR_RED);
        break;
      case HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_CATASTROPHE:
      default:
        break;
    }
  }
}

static void homeworlds_view_draw_system(HomeworldsView *view,
                                        cairo_t *cr,
                                        const HomeworldsSystem *system,
                                        guint system_index,
                                        double center_x,
                                        double center_y,
                                        gboolean selected) {
  HomeworldsViewSystemLayout layout = {0};
  double x = 0.0;
  double y = 0.0;
  double box_width = 0.0;
  double box_height = 0.0;
  double radius = HOMEWORLDS_VIEW_SYSTEM_CORNER_RADIUS;

  g_return_if_fail(system != NULL);
  g_return_if_fail(view != NULL);

  if (!homeworlds_view_calculate_system_layout(system, system_index, center_x, center_y, &layout)) {
    return;
  }

  x = layout.box_x;
  y = layout.box_y;
  box_width = layout.box_width;
  box_height = layout.box_height;
  radius = MIN(radius, MIN(box_width, box_height) / 2.0);

  cairo_save(cr);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + box_width - radius, y + radius, radius, -G_PI / 2.0, 0.0);
  cairo_arc(cr, x + box_width - radius, y + box_height - radius, radius, 0.0, G_PI / 2.0);
  cairo_arc(cr, x + radius, y + box_height - radius, radius, G_PI / 2.0, G_PI);
  cairo_arc(cr, x + radius, y + radius, radius, G_PI, G_PI * 1.5);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, 0.05, 0.07, 0.12, selected ? 0.78 : 0.58);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, selected ? 0.98 : 0.78, selected ? 0.91 : 0.86, selected ? 0.55 : 0.92, 0.92);
  cairo_set_line_width(cr, selected ? 3.0 : 1.6);
  cairo_stroke(cr);

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 12.0);
  cairo_set_source_rgba(cr, 0.96, 0.96, 0.90, 0.92);
  {
    const char *player_name = system_index < 2 ? view->player_names[system_index] : NULL;
    char *label = homeworlds_view_format_system_title_with_player_name(system_index, player_name);

    cairo_move_to(cr, x + 14.0, y + 22.0);
    if (label != NULL) {
      cairo_show_text(cr, label);
    }
    g_free(label);
  }

  for (guint i = 0; i < layout.piece_count; ++i) {
    const HomeworldsViewPieceLayout *piece = &layout.pieces[i];
    HomeworldsSize size = homeworlds_pyramid_size(piece->pyramid);
    HomeworldsColor color = homeworlds_pyramid_color(piece->pyramid);

    if (piece->is_ship) {
      homeworlds_view_draw_pyramid(cr, piece->x, piece->y, size, piece->points_up, color);
    } else {
      homeworlds_view_draw_star(cr, piece->x, piece->y, size, color);
    }
  }

  homeworlds_view_draw_previous_move_markers(view, cr, system_index, &layout);
  cairo_restore(cr);
}

static void homeworlds_view_board_size(HomeworldsView *view, int *out_width, int *out_height) {
  int width = 0;
  int height = 0;
  int content_width = 0;
  int content_height = 0;

  g_return_if_fail(view != NULL);
  g_return_if_fail(out_width != NULL);
  g_return_if_fail(out_height != NULL);

  width = gtk_widget_get_width(view->drawing_area);
  height = gtk_widget_get_height(view->drawing_area);
  content_width = gtk_drawing_area_get_content_width(GTK_DRAWING_AREA(view->drawing_area));
  content_height = gtk_drawing_area_get_content_height(GTK_DRAWING_AREA(view->drawing_area));
  *out_width = MAX(width, content_width);
  *out_height = MAX(height, content_height);
  if (*out_width <= 1) {
    *out_width = HOMEWORLDS_VIEW_FALLBACK_BOARD_WIDTH;
  }
  if (*out_height <= 1) {
    *out_height = HOMEWORLDS_VIEW_FALLBACK_BOARD_HEIGHT;
  }
}

static void homeworlds_view_board_viewport_size(HomeworldsView *view, int *out_width, int *out_height) {
  int width = 0;
  int height = 0;

  g_return_if_fail(view != NULL);
  g_return_if_fail(out_width != NULL);
  g_return_if_fail(out_height != NULL);

  if (view->board_scroller != NULL) {
    width = gtk_widget_get_width(view->board_scroller);
    height = gtk_widget_get_height(view->board_scroller);
  }
  if (width <= 1 && view->board_overlay != NULL) {
    width = gtk_widget_get_width(view->board_overlay);
  }
  if (height <= 1 && view->board_overlay != NULL) {
    height = gtk_widget_get_height(view->board_overlay);
  }
  if (width <= 1 && view->board_hadjustment != NULL) {
    double page_size = gtk_adjustment_get_page_size(view->board_hadjustment);
    if (page_size > 1.0) {
      width = (int)ceil(page_size);
    }
  }
  if (height <= 1 && view->board_vadjustment != NULL) {
    double page_size = gtk_adjustment_get_page_size(view->board_vadjustment);
    if (page_size > 1.0) {
      height = (int)ceil(page_size);
    }
  }

  *out_width = width > 1 ? width : HOMEWORLDS_VIEW_INITIAL_BOARD_WIDTH;
  *out_height = height > 1 ? height : HOMEWORLDS_VIEW_INITIAL_BOARD_HEIGHT;
}

static gboolean homeworlds_view_update_board_content_size(HomeworldsView *view) {
  const HomeworldsPosition *position = NULL;
  int viewport_width = 0;
  int viewport_height = 0;
  int content_width = 0;
  int content_height = 0;
  int current_content_width = 0;
  int current_content_height = 0;
  gboolean changed = FALSE;

  g_return_val_if_fail(view != NULL, FALSE);
  g_return_val_if_fail(GTK_IS_DRAWING_AREA(view->drawing_area), FALSE);

  position = homeworlds_view_position(view);
  if (position == NULL) {
    return FALSE;
  }

  homeworlds_view_board_viewport_size(view, &viewport_width, &viewport_height);
  if (!homeworlds_view_calculate_board_content_size(position,
                                                    (double)viewport_width,
                                                    (double)viewport_height,
                                                    &content_width,
                                                    &content_height)) {
    return FALSE;
  }
  current_content_width = gtk_drawing_area_get_content_width(GTK_DRAWING_AREA(view->drawing_area));
  current_content_height = gtk_drawing_area_get_content_height(GTK_DRAWING_AREA(view->drawing_area));
  if (viewport_width <= HOMEWORLDS_VIEW_INITIAL_BOARD_WIDTH &&
      content_width <= HOMEWORLDS_VIEW_FALLBACK_BOARD_WIDTH) {
    content_width = current_content_width;
  }
  if (viewport_height <= HOMEWORLDS_VIEW_INITIAL_BOARD_HEIGHT &&
      content_height <= HOMEWORLDS_VIEW_FALLBACK_BOARD_HEIGHT) {
    content_height = current_content_height;
  }
  if (content_width != current_content_width) {
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(view->drawing_area), content_width);
    changed = TRUE;
  }
  if (content_height != current_content_height) {
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(view->drawing_area), content_height);
    changed = TRUE;
  }

  return changed;
}

static gboolean homeworlds_view_board_layout_matches_viewport(HomeworldsView *view) {
  const HomeworldsPosition *position = NULL;
  int viewport_width = 0;
  int viewport_height = 0;
  int expected_content_width = 0;
  int expected_content_height = 0;
  int content_width = 0;
  int content_height = 0;
  int allocated_content_width = 0;
  int allocated_content_height = 0;
  int allocated_overlay_width = 0;
  int allocated_overlay_height = 0;
  int minimum_allocated_width = 0;
  int minimum_allocated_height = 0;

  g_return_val_if_fail(view != NULL, FALSE);

  if (!GTK_IS_SCROLLED_WINDOW(view->board_scroller) ||
      !GTK_IS_WIDGET(view->board_overlay) ||
      !GTK_IS_DRAWING_AREA(view->drawing_area)) {
    return FALSE;
  }

  viewport_width = gtk_widget_get_width(view->board_scroller);
  viewport_height = gtk_widget_get_height(view->board_scroller);
  if (viewport_width <= 1 || viewport_height <= 1) {
    return FALSE;
  }

  position = homeworlds_view_position(view);
  if (position == NULL) {
    return FALSE;
  }

  if (!homeworlds_view_calculate_board_content_size(position,
                                                    (double)viewport_width,
                                                    (double)viewport_height,
                                                    &expected_content_width,
                                                    &expected_content_height)) {
    return FALSE;
  }
  content_width = gtk_drawing_area_get_content_width(GTK_DRAWING_AREA(view->drawing_area));
  content_height = gtk_drawing_area_get_content_height(GTK_DRAWING_AREA(view->drawing_area));
  allocated_content_width = gtk_widget_get_width(view->drawing_area);
  allocated_content_height = gtk_widget_get_height(view->drawing_area);
  allocated_overlay_width = gtk_widget_get_width(view->board_overlay);
  allocated_overlay_height = gtk_widget_get_height(view->board_overlay);
  minimum_allocated_width = MIN(viewport_width, content_width);
  minimum_allocated_height = MIN(viewport_height, content_height);
  if (ABS(content_width - expected_content_width) > 1) {
    return FALSE;
  }
  if (ABS(content_height - expected_content_height) > 1) {
    return FALSE;
  }
  if (allocated_content_width < minimum_allocated_width - 1 ||
      allocated_overlay_width < minimum_allocated_width - 1 ||
      allocated_content_height < minimum_allocated_height - 1 ||
      allocated_overlay_height < minimum_allocated_height - 1) {
    return FALSE;
  }
  return allocated_content_width <= content_width + 1 &&
         allocated_overlay_width <= content_width + 1 &&
         allocated_content_height <= content_height + 1 &&
         allocated_overlay_height <= content_height + 1;
}

static gboolean homeworlds_view_board_layout_settle_tick(GtkWidget * /*widget*/,
                                                         GdkFrameClock * /*frame_clock*/,
                                                         gpointer user_data) {
  HomeworldsView *view = user_data;
  gboolean content_size_changed = FALSE;

  g_return_val_if_fail(view != NULL, G_SOURCE_REMOVE);

  content_size_changed = homeworlds_view_update_board_content_size(view);
  if (content_size_changed) {
    gtk_widget_queue_resize(view->board_overlay);
    gtk_widget_queue_resize(view->drawing_area);
    homeworlds_view_update_board_choice_buttons(view);
  }
  gtk_widget_queue_draw(view->drawing_area);

  if (!homeworlds_view_board_layout_matches_viewport(view)) {
    return G_SOURCE_CONTINUE;
  }

  view->board_layout_settle_tick_id = 0;
  return G_SOURCE_REMOVE;
}

static void homeworlds_view_cancel_board_layout_settle(HomeworldsView *view) {
  g_return_if_fail(view != NULL);

  if (view->board_layout_settle_tick_id == 0) {
    return;
  }

  if (GTK_IS_WIDGET(view->board_scroller)) {
    gtk_widget_remove_tick_callback(view->board_scroller, view->board_layout_settle_tick_id);
  }
  view->board_layout_settle_tick_id = 0;
}

static void homeworlds_view_schedule_board_layout_settle(HomeworldsView *view) {
  g_return_if_fail(view != NULL);
  g_return_if_fail(GTK_IS_WIDGET(view->board_scroller));

  if (view->board_layout_settle_tick_id != 0) {
    return;
  }

  view->board_layout_settle_tick_id =
      gtk_widget_add_tick_callback(view->board_scroller, homeworlds_view_board_layout_settle_tick, view, NULL);
}

static const HomeworldsMoveCandidate *homeworlds_view_find_ship_candidate(const GameBackendMoveList *candidates,
                                                                          guint system_index,
                                                                          const HomeworldsViewPieceLayout *piece) {
  g_return_val_if_fail(candidates != NULL, NULL);
  g_return_val_if_fail(piece != NULL, NULL);

  for (gsize i = 0; i < candidates->count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates->moves)[i];

    if (candidate->data.kind == HOMEWORLDS_CANDIDATE_SELECT_SHIP &&
        candidate->data.system_index == system_index &&
        candidate->data.ship_owner == piece->side &&
        candidate->data.pyramid == piece->pyramid) {
      return candidate;
    }
  }

  return NULL;
}

static const HomeworldsMoveCandidate *homeworlds_view_find_attack_candidate(const GameBackendMoveList *candidates,
                                                                            guint system_index,
                                                                            const HomeworldsViewPieceLayout *piece,
                                                                            const HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(candidates != NULL, NULL);
  g_return_val_if_fail(piece != NULL, NULL);
  g_return_val_if_fail(state != NULL, NULL);

  if (state->selected_system_index != system_index) {
    return NULL;
  }

  for (gsize i = 0; i < candidates->count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates->moves)[i];

    if (candidate->data.kind == HOMEWORLDS_CANDIDATE_ATTACK_TARGET &&
        candidate->data.target_ship_owner == piece->side &&
        candidate->data.target_ship_slot == piece->slot &&
        candidate->data.pyramid == piece->pyramid) {
      return candidate;
    }
  }

  return NULL;
}

static const HomeworldsMoveCandidate *homeworlds_view_find_move_target_candidate(const GameBackendMoveList *candidates,
                                                                                 guint system_index) {
  g_return_val_if_fail(candidates != NULL, NULL);

  for (gsize i = 0; i < candidates->count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates->moves)[i];

    if (candidate->data.kind == HOMEWORLDS_CANDIDATE_MOVE_TARGET &&
        candidate->data.target_system_index == system_index) {
      return candidate;
    }
  }

  return NULL;
}

static gboolean homeworlds_view_stage_highlights_selected_ship(const HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(state != NULL, FALSE);

  switch ((HomeworldsBuilderStage) state->stage) {
    case HOMEWORLDS_BUILDER_STAGE_SELECT_TRADE_COLOR:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ATTACK_TARGET:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_MOVE_TARGET:
      return homeworlds_pyramid_is_valid(state->selected_ship_pyramid) &&
             state->selected_system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT;
    case HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION:
    case HOMEWORLDS_BUILDER_STAGE_COMPLETE:
    default:
      return FALSE;
  }
}

static gboolean homeworlds_view_piece_is_selected_ship(const HomeworldsMoveBuilderState *state,
                                                       const HomeworldsViewPieceLayout *piece) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(piece != NULL, FALSE);

  return piece->is_ship &&
         piece->side == state->working_position.turn &&
         piece->pyramid == state->selected_ship_pyramid;
}

static void homeworlds_view_append_active_ship_highlight(HomeworldsView *view,
                                                         const HomeworldsViewPieceLayout *piece) {
  GtkWidget *highlight = NULL;
  double x = 0.0;
  double y = 0.0;

  g_return_if_fail(view != NULL);
  g_return_if_fail(GTK_IS_FIXED(view->board_choice_layer));
  g_return_if_fail(piece != NULL);

  highlight = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(highlight, "homeworlds-board-active-ship");
  gtk_widget_set_can_focus(highlight, FALSE);
  gtk_widget_set_can_target(highlight, FALSE);
  g_object_set_data(G_OBJECT(highlight), "homeworlds-board-active-ship", GUINT_TO_POINTER(1));

  x = piece->x - (piece->width / 2.0) - HOMEWORLDS_VIEW_PIECE_BUTTON_PAD;
  y = piece->y - (piece->height / 2.0) - HOMEWORLDS_VIEW_PIECE_BUTTON_PAD;
  gtk_widget_set_size_request(highlight,
                              (int)(piece->width + (2.0 * HOMEWORLDS_VIEW_PIECE_BUTTON_PAD) + 1.0),
                              (int)(piece->height + (2.0 * HOMEWORLDS_VIEW_PIECE_BUTTON_PAD) + 1.0));
  gtk_fixed_put(GTK_FIXED(view->board_choice_layer), highlight, x, y);
}

static GtkWidget *homeworlds_view_create_board_choice_button(HomeworldsView *view,
                                                             const HomeworldsMoveCandidate *candidate,
                                                             const char *data_key) {
  GtkWidget *button = NULL;
  char *tooltip = NULL;

  g_return_val_if_fail(view != NULL, NULL);
  g_return_val_if_fail(candidate != NULL, NULL);
  g_return_val_if_fail(data_key != NULL, NULL);

  button = gtk_button_new();
  gtk_widget_add_css_class(button, "homeworlds-board-choice");
  gtk_widget_set_can_focus(button, TRUE);
  g_object_set_data(G_OBJECT(button), data_key, GUINT_TO_POINTER(1));
  g_object_set_data_full(G_OBJECT(button),
                         "homeworlds-candidate",
                         g_memdup2(candidate, sizeof(*candidate)),
                         g_free);
  tooltip = homeworlds_view_candidate_label(candidate);
  gtk_widget_set_tooltip_text(button, tooltip);
  g_free(tooltip);
  g_signal_connect(button, "clicked", G_CALLBACK(homeworlds_view_candidate_clicked), view);
  return button;
}

static void homeworlds_view_update_board_choice_buttons(HomeworldsView *view) {
  const HomeworldsMoveBuilderState *state = NULL;
  const HomeworldsPosition *position = NULL;
  GameBackendMoveList candidates = {0};
  gboolean uses_board_choices = FALSE;
  gboolean highlights_selected_ship = FALSE;
  gboolean highlighted_selected_ship = FALSE;
  int board_width = 0;
  int board_height = 0;

  g_return_if_fail(view != NULL);
  g_return_if_fail(GTK_IS_FIXED(view->board_choice_layer));

  homeworlds_view_clear_fixed(view->board_choice_layer);
  state = homeworlds_view_builder_state(view);
  if (state == NULL) {
    return;
  }

  uses_board_choices = homeworlds_view_stage_uses_board_choices(view);
  highlights_selected_ship = homeworlds_view_stage_highlights_selected_ship(state);
  if (!uses_board_choices && !highlights_selected_ship) {
    return;
  }

  position = homeworlds_view_position(view);
  if (position == NULL) {
    return;
  }

  homeworlds_view_board_size(view, &board_width, &board_height);
  candidates = homeworlds_move_builder_list_candidates(&view->builder);
  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &position->systems[system_index];
    HomeworldsViewSystemLayout layout = {0};
    double center_x = 0.0;
    double center_y = 0.0;

    if (system_index >= 2 && homeworlds_system_is_empty(system)) {
      continue;
    }

    if (!homeworlds_view_calculate_system_center(position,
                                                 system_index,
                                                 board_width,
                                                 board_height,
                                                 &center_x,
                                                 &center_y)) {
      continue;
    }
    if (!homeworlds_view_calculate_system_layout(system, system_index, center_x, center_y, &layout)) {
      continue;
    }

    for (guint piece_index = 0; piece_index < layout.piece_count; ++piece_index) {
      const HomeworldsViewPieceLayout *piece = &layout.pieces[piece_index];
      const HomeworldsMoveCandidate *candidate = NULL;
      GtkWidget *button = NULL;
      double button_x = 0.0;
      double button_y = 0.0;
      int button_width = 0;
      int button_height = 0;

      if (!piece->is_ship) {
        continue;
      }

      if (!highlighted_selected_ship &&
          highlights_selected_ship &&
          state->selected_system_index == system_index &&
          homeworlds_view_piece_is_selected_ship(state, piece)) {
        homeworlds_view_append_active_ship_highlight(view, piece);
        highlighted_selected_ship = TRUE;
      }

      if (state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP) {
        candidate = homeworlds_view_find_ship_candidate(&candidates, system_index, piece);
      } else if (state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_ATTACK_TARGET) {
        candidate = homeworlds_view_find_attack_candidate(&candidates, system_index, piece, state);
      }
      if (candidate == NULL) {
        continue;
      }

      button = homeworlds_view_create_board_choice_button(view, candidate, "homeworlds-board-ship-choice");
      button_x = piece->x - (piece->width / 2.0) - HOMEWORLDS_VIEW_PIECE_BUTTON_PAD;
      button_y = piece->y - (piece->height / 2.0) - HOMEWORLDS_VIEW_PIECE_BUTTON_PAD;
      button_width = (int)(piece->width + (2.0 * HOMEWORLDS_VIEW_PIECE_BUTTON_PAD) + 1.0);
      button_height = (int)(piece->height + (2.0 * HOMEWORLDS_VIEW_PIECE_BUTTON_PAD) + 1.0);
      gtk_widget_set_size_request(button, button_width, button_height);
      gtk_fixed_put(GTK_FIXED(view->board_choice_layer), button, button_x, button_y);
    }

    if (state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_MOVE_TARGET) {
      const HomeworldsMoveCandidate *candidate = homeworlds_view_find_move_target_candidate(&candidates, system_index);
      GtkWidget *button = NULL;
      double button_x = 0.0;
      double button_y = 0.0;

      if (candidate == NULL) {
        continue;
      }

      button = homeworlds_view_create_board_choice_button(view, candidate, "homeworlds-board-system-choice");
      button_x = layout.box_x - HOMEWORLDS_VIEW_PIECE_BUTTON_PAD;
      button_y = layout.box_y - HOMEWORLDS_VIEW_PIECE_BUTTON_PAD;
      gtk_widget_set_size_request(button,
                                  (int)(layout.box_width + (2.0 * HOMEWORLDS_VIEW_PIECE_BUTTON_PAD) + 1.0),
                                  (int)(layout.box_height + (2.0 * HOMEWORLDS_VIEW_PIECE_BUTTON_PAD) + 1.0));
      gtk_fixed_put(GTK_FIXED(view->board_choice_layer), button, button_x, button_y);
    }
  }

  g_clear_pointer(&candidates.moves, g_free);
}

static void homeworlds_view_board_resized(GtkDrawingArea * /*drawing_area*/,
                                          int /*width*/,
                                          int /*height*/,
                                          gpointer user_data) {
  HomeworldsView *view = user_data;

  g_return_if_fail(view != NULL);

  homeworlds_view_update_board_content_size(view);
  homeworlds_view_update_board_choice_buttons(view);
  homeworlds_view_schedule_board_layout_settle(view);
}

static void homeworlds_view_board_viewport_changed(GtkAdjustment * /*adjustment*/, gpointer user_data) {
  HomeworldsView *view = user_data;

  g_return_if_fail(view != NULL);

  homeworlds_view_update_board_content_size(view);
  homeworlds_view_update_board_choice_buttons(view);
  homeworlds_view_schedule_board_layout_settle(view);
  gtk_widget_queue_draw(view->drawing_area);
}

static void homeworlds_view_board_scroller_mapped(GtkWidget *widget, gpointer user_data) {
  HomeworldsView *view = user_data;

  g_return_if_fail(GTK_IS_WIDGET(widget));
  g_return_if_fail(view != NULL);

  homeworlds_view_schedule_board_layout_settle(view);
}

static void homeworlds_view_root_destroyed(GtkWidget * /*widget*/, gpointer user_data) {
  HomeworldsView *view = user_data;

  g_return_if_fail(view != NULL);

  homeworlds_view_cancel_board_layout_settle(view);
  view->root_destroy_handler_id = 0;
}

static void homeworlds_view_draw_starfield(cairo_t *cr, int width, int height) {
  cairo_pattern_t *background = cairo_pattern_create_linear(0.0, 0.0, 0.0, (double) height);
  cairo_pattern_add_color_stop_rgb(background, 0.0, 0.015, 0.025, 0.060);
  cairo_pattern_add_color_stop_rgb(background, 1.0, 0.055, 0.070, 0.115);
  cairo_rectangle(cr, 0.0, 0.0, (double) width, (double) height);
  cairo_set_source(cr, background);
  cairo_fill(cr);
  cairo_pattern_destroy(background);

  for (guint i = 0; i < 90; ++i) {
    double x = (double) ((i * 83u + 37u) % MAX((guint) width, 1u));
    double y = (double) ((i * 47u + 19u) % MAX((guint) height, 1u));
    double alpha = 0.25 + ((double) ((i * 7u) % 40u) / 100.0);

    cairo_arc(cr, x, y, i % 9 == 0 ? 1.6 : 0.9, 0.0, G_PI * 2.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 0.92, alpha);
    cairo_fill(cr);
  }
}

static void homeworlds_view_draw_action_legend(cairo_t *cr, double width, double height) {
  double text_widths[G_N_ELEMENTS(homeworlds_view_action_legend_items)] = {0.0};
  double total_width = 0.0;
  double x = 0.0;
  double y = 0.0;
  cairo_font_extents_t font_extents = {0};

  g_return_if_fail(cr != NULL);
  g_return_if_fail(width > 0.0);
  g_return_if_fail(height > 0.0);

  cairo_save(cr);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, HOMEWORLDS_VIEW_ACTION_LEGEND_FONT_SIZE);
  cairo_font_extents(cr, &font_extents);

  for (guint i = 0; i < G_N_ELEMENTS(homeworlds_view_action_legend_items); ++i) {
    cairo_text_extents_t text_extents = {0};

    cairo_text_extents(cr, homeworlds_view_action_legend_items[i].label, &text_extents);
    text_widths[i] = text_extents.x_advance;
    total_width += text_widths[i];
  }
  total_width += HOMEWORLDS_VIEW_ACTION_LEGEND_WORD_GAP *
                 (double)(G_N_ELEMENTS(homeworlds_view_action_legend_items) - 1);

  x = (width - total_width) / 2.0;
  y = height - (homeworlds_view_action_legend_height() / 2.0) +
      ((font_extents.ascent - font_extents.descent) / 2.0);

  for (guint i = 0; i < G_N_ELEMENTS(homeworlds_view_action_legend_items); ++i) {
    const HomeworldsViewActionLegendItem *item = &homeworlds_view_action_legend_items[i];
    const HomeworldsColorStyle *style = &homeworlds_view_color_styles[item->color];

    cairo_move_to(cr, x + 1.0, y + 1.0);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.62);
    cairo_show_text(cr, item->label);

    cairo_move_to(cr, x, y);
    cairo_set_source_rgba(cr, style->red, style->green, style->blue, 0.98);
    cairo_show_text(cr, item->label);

    x += text_widths[i] + HOMEWORLDS_VIEW_ACTION_LEGEND_WORD_GAP;
  }

  cairo_restore(cr);
}

static void homeworlds_view_draw(GtkDrawingArea * /*drawing_area*/,
                                 cairo_t *cr,
                                 int width,
                                 int height,
                                 gpointer user_data) {
  HomeworldsView *view = user_data;
  const HomeworldsPosition *position = NULL;
  const HomeworldsMoveBuilderState *state = NULL;

  g_return_if_fail(view != NULL);

  position = homeworlds_view_position(view);
  if (position == NULL) {
    return;
  }

  state = homeworlds_view_builder_state(view);
  homeworlds_view_draw_starfield(cr, width, height);

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    if (system_index >= 2 && homeworlds_system_is_empty(&position->systems[system_index])) {
      continue;
    }

    double center_x = 0.0;
    double center_y = 0.0;
    gboolean selected = state != NULL && state->selected_system_index == system_index;

    if (!homeworlds_view_calculate_system_center(position, system_index, width, height, &center_x, &center_y)) {
      continue;
    }
    homeworlds_view_draw_system(view, cr, &position->systems[system_index], system_index, center_x, center_y, selected);
  }

  homeworlds_view_draw_action_legend(cr, (double) width, (double) height);
}

static void homeworlds_view_rebuild_builder(HomeworldsView *view) {
  const HomeworldsPosition *position = NULL;

  g_return_if_fail(view != NULL);
  g_return_if_fail(GGAME_IS_MODEL(view->model));

  if (view->builder_ready) {
    homeworlds_move_builder_clear(&view->builder);
    view->builder_ready = FALSE;
  }

  position = ggame_model_peek_position(view->model);
  if (position == NULL || position->phase == HOMEWORLDS_PHASE_FINISHED) {
    return;
  }

  view->builder_ready = homeworlds_move_builder_init(position, &view->builder);
  if (!view->builder_ready) {
    g_debug("Unable to initialize Homeworlds move builder");
  }
}

static gboolean homeworlds_view_apply_completed_move(HomeworldsView *view, const HomeworldsMove *move) {
  char formatted[128] = {0};

  g_return_val_if_fail(view != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (view->move_handler != NULL) {
    if (!view->move_handler(move, view->move_handler_data)) {
      g_debug("Homeworlds move handler rejected the completed move");
      return FALSE;
    }
  } else if (!ggame_model_apply_move(view->model, move)) {
    g_debug("Homeworlds model rejected the completed move");
    return FALSE;
  }

  if (homeworlds_move_format(move, formatted, sizeof(formatted))) {
    gtk_label_set_text(GTK_LABEL(view->last_move_label), formatted);
  }
  if (view->move_applied != NULL) {
    view->move_applied(view, view->move_applied_data);
  }
  return TRUE;
}

static gboolean homeworlds_view_apply_completed_builder_move(HomeworldsView *view) {
  HomeworldsMove move = {0};

  g_return_val_if_fail(view != NULL, FALSE);
  g_return_val_if_fail(view->builder_ready, FALSE);
  g_return_val_if_fail(homeworlds_move_builder_is_complete(&view->builder), FALSE);

  if (!homeworlds_move_builder_build_move(&view->builder, &move)) {
    g_debug("Completed Homeworlds builder did not produce a move");
    return FALSE;
  }
  return homeworlds_view_apply_completed_move(view, &move);
}

static gboolean homeworlds_view_complete_move_if_ready(HomeworldsView *view) {
  const HomeworldsMoveBuilderState *state = NULL;

  g_return_val_if_fail(view != NULL, FALSE);

  if (!view->builder_ready || !homeworlds_move_builder_is_complete(&view->builder)) {
    return TRUE;
  }
  state = homeworlds_view_builder_state(view);
  if (state != NULL && homeworlds_view_builder_has_catastrophe_choices(state)) {
    return TRUE;
  }
  return homeworlds_view_apply_completed_builder_move(view);
}

static void homeworlds_view_pass_staged_catastrophes_clicked(GtkButton *button, gpointer user_data) {
  HomeworldsView *view = user_data;

  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(view != NULL);

  if (!view->builder_ready ||
      !homeworlds_move_builder_is_complete(&view->builder) ||
      !homeworlds_view_apply_completed_builder_move(view)) {
    homeworlds_view_refresh(view);
    return;
  }
  homeworlds_view_refresh(view);
}

static void homeworlds_view_candidate_clicked(GtkButton *button, gpointer user_data) {
  HomeworldsView *view = user_data;
  const HomeworldsMoveCandidate *candidate = NULL;

  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(view != NULL);

  candidate = g_object_get_data(G_OBJECT(button), "homeworlds-candidate");
  if (candidate == NULL || !view->builder_ready) {
    return;
  }
  if (!homeworlds_move_builder_step(&view->builder, candidate)) {
    g_debug("Homeworlds move builder rejected selected candidate");
    homeworlds_view_refresh(view);
    return;
  }

  if (homeworlds_move_builder_is_complete(&view->builder)) {
    if (!homeworlds_view_complete_move_if_ready(view)) {
      homeworlds_view_refresh(view);
      return;
    }
    if (view->builder_ready &&
        homeworlds_move_builder_is_complete(&view->builder) &&
        homeworlds_view_builder_has_catastrophe_choices(homeworlds_view_builder_state(view))) {
      homeworlds_view_update_from_current_builder(view);
    } else {
      homeworlds_view_refresh(view);
    }
    return;
  }

  homeworlds_view_update_from_current_builder(view);
}

static void homeworlds_view_append_candidate_button(HomeworldsView *view, const HomeworldsMoveCandidate *candidate) {
  GtkWidget *button = NULL;
  char *label = NULL;

  g_return_if_fail(view != NULL);
  g_return_if_fail(candidate != NULL);

  label = homeworlds_view_candidate_label(candidate);
  button = homeworlds_view_new_text_panel_button(label);
  g_object_set_data_full(G_OBJECT(button),
                         "homeworlds-candidate",
                         g_memdup2(candidate, sizeof(*candidate)),
                         g_free);
  g_signal_connect(button, "clicked", G_CALLBACK(homeworlds_view_candidate_clicked), view);
  gtk_box_append(GTK_BOX(view->candidate_box), button);
  g_free(label);
}

static void homeworlds_view_cancel_selection_clicked(GtkButton *button, gpointer user_data) {
  HomeworldsView *view = user_data;

  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(view != NULL);

  homeworlds_view_refresh(view);
}

static void homeworlds_view_append_cancel_button(HomeworldsView *view) {
  GtkWidget *button = NULL;

  g_return_if_fail(view != NULL);

  button = homeworlds_view_new_text_panel_button("Cancel");
  g_object_set_data(G_OBJECT(button), "homeworlds-cancel-choice", GUINT_TO_POINTER(1));
  g_signal_connect(button, "clicked", G_CALLBACK(homeworlds_view_cancel_selection_clicked), view);
  gtk_box_append(GTK_BOX(view->candidate_box), button);
}

static void homeworlds_view_append_cancel_button_if_available(HomeworldsView *view) {
  g_return_if_fail(view != NULL);

  if (homeworlds_view_can_cancel_selection(view)) {
    homeworlds_view_append_cancel_button(view);
  }
}

static const char *homeworlds_view_visual_choice_text(const HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(state != NULL, "Click a highlighted board control.");

  switch ((HomeworldsBuilderStage) state->stage) {
    case HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP:
      return "Click a highlighted pyramid in the bank on the board.";
    case HOMEWORLDS_BUILDER_STAGE_SELECT_TRADE_COLOR:
      return "Click a highlighted same-size pyramid in the bank.";
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ATTACK_TARGET:
      return "Click a highlighted enemy ship on the board.";
    case HOMEWORLDS_BUILDER_STAGE_SELECT_MOVE_TARGET:
      return "Click a highlighted destination system on the board or a highlighted star in the bank.";
    case HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP:
      return "Click a highlighted ship on the board to activate it.";
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION:
    case HOMEWORLDS_BUILDER_STAGE_COMPLETE:
    default:
      return "Click a highlighted board control.";
  }
}

static void homeworlds_view_catastrophe_clicked(GtkButton *button, gpointer user_data) {
  HomeworldsView *view = user_data;
  guint stored_system_index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "homeworlds-system-index"));
  guint stored_color = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "homeworlds-color"));
  guint system_index = stored_system_index - 1;
  guint color = stored_color - 1;
  HomeworldsMoveBuilderState *state = NULL;
  HomeworldsViewCatastropheChoice choice = {
    .system_index = system_index,
    .color = (HomeworldsColor)color,
  };

  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(view != NULL);
  g_return_if_fail(stored_system_index > 0);
  g_return_if_fail(stored_color > 0);

  if (homeworlds_view_has_partial_selection(view)) {
    g_debug("Resolve or reset the partial Homeworlds move before applying a catastrophe");
    return;
  }

  if (!view->builder_ready || view->builder.builder_state == NULL) {
    return;
  }

  state = view->builder.builder_state;
  if (!homeworlds_view_apply_catastrophe_choice(state, &choice)) {
    g_debug("Homeworlds move builder rejected selected catastrophe");
    homeworlds_view_refresh(view);
    return;
  }

  if (homeworlds_move_builder_is_complete(&view->builder) &&
      !homeworlds_view_builder_has_catastrophe_choices(homeworlds_view_builder_state(view))) {
    if (!homeworlds_view_apply_completed_builder_move(view)) {
      homeworlds_view_refresh(view);
      return;
    }
    homeworlds_view_refresh(view);
    return;
  }
  homeworlds_view_update_from_current_builder(view);
}

static void homeworlds_view_update_catastrophes(HomeworldsView *view) {
  const HomeworldsPosition *position = NULL;
  gboolean appended = FALSE;

  g_return_if_fail(view != NULL);

  homeworlds_view_clear_box(view->catastrophe_box);
  if (homeworlds_view_has_partial_selection(view)) {
    GtkWidget *label = homeworlds_view_new_text_panel_label("Reset the partial move before catastrophes.");
    gtk_box_append(GTK_BOX(view->catastrophe_box), label);
    return;
  }

  position = homeworlds_view_position(view);
  if (position == NULL) {
    return;
  }

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &position->systems[system_index];
    for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
      if (homeworlds_system_color_count(system, (HomeworldsColor) color) < 4) {
        continue;
      }

      char *system_label = homeworlds_view_system_label(system_index);
      char *label = g_strdup_printf("Catastrophe %s at %s",
                                    homeworlds_view_color_name((HomeworldsColor) color),
                                    system_label);
      GtkWidget *button = homeworlds_view_new_text_panel_button(label);
      g_object_set_data(G_OBJECT(button), "homeworlds-system-index", GUINT_TO_POINTER(system_index + 1));
      g_object_set_data(G_OBJECT(button), "homeworlds-color", GUINT_TO_POINTER(color + 1));
      g_signal_connect(button, "clicked", G_CALLBACK(homeworlds_view_catastrophe_clicked), view);
      gtk_box_append(GTK_BOX(view->catastrophe_box), button);
      g_free(system_label);
      g_free(label);
      appended = TRUE;
    }
  }

  if (!appended) {
    GtkWidget *label = homeworlds_view_new_text_panel_label("No catastrophes are currently available.");
    gtk_box_append(GTK_BOX(view->catastrophe_box), label);
  }
}

static gboolean homeworlds_view_trade_candidate_matches_pyramid(const HomeworldsMoveCandidate *candidate,
                                                                const HomeworldsMoveBuilderState *state,
                                                                HomeworldsPyramid pyramid) {
  g_return_val_if_fail(candidate != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);

  return candidate->data.kind == HOMEWORLDS_CANDIDATE_TRADE_COLOR &&
         state != NULL &&
         homeworlds_pyramid_is_valid(state->selected_ship_pyramid) &&
         homeworlds_pyramid_size(state->selected_ship_pyramid) == homeworlds_pyramid_size(pyramid) &&
         candidate->data.target_color == homeworlds_pyramid_color(pyramid);
}

static gboolean homeworlds_view_bank_candidate_matches_pyramid(const HomeworldsMoveCandidate *candidate,
                                                               const HomeworldsMoveBuilderState *state,
                                                               HomeworldsPyramid pyramid) {
  g_return_val_if_fail(candidate != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);

  switch ((HomeworldsCandidateKind) candidate->data.kind) {
    case HOMEWORLDS_CANDIDATE_SETUP_STAR:
    case HOMEWORLDS_CANDIDATE_SETUP_SHIP:
      return candidate->data.pyramid == pyramid;
    case HOMEWORLDS_CANDIDATE_TRADE_COLOR:
      return homeworlds_view_trade_candidate_matches_pyramid(candidate, state, pyramid);
    case HOMEWORLDS_CANDIDATE_MOVE_TARGET:
      return homeworlds_view_candidate_is_discovery(candidate) && candidate->data.pyramid == pyramid;
    case HOMEWORLDS_CANDIDATE_NONE:
    case HOMEWORLDS_CANDIDATE_SELECT_SHIP:
    case HOMEWORLDS_CANDIDATE_ACTION:
    case HOMEWORLDS_CANDIDATE_ATTACK_TARGET:
    default:
      return FALSE;
  }
}

static gboolean homeworlds_view_find_bank_candidate(HomeworldsView *view,
                                                    HomeworldsPyramid pyramid,
                                                    HomeworldsMoveCandidate *out_candidate) {
  const HomeworldsMoveBuilderState *state = NULL;
  GameBackendMoveList candidates = {0};
  gboolean found = FALSE;

  g_return_val_if_fail(view != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);
  g_return_val_if_fail(out_candidate != NULL, FALSE);

  if (!homeworlds_view_stage_uses_bank_choices(view) || !view->builder_ready) {
    return FALSE;
  }

  state = homeworlds_view_builder_state(view);
  candidates = homeworlds_move_builder_list_candidates(&view->builder);
  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates.moves)[i];

    if (homeworlds_view_bank_candidate_matches_pyramid(candidate, state, pyramid)) {
      *out_candidate = *candidate;
      found = TRUE;
      break;
    }
  }

  g_clear_pointer(&candidates.moves, g_free);
  return found;
}

static gboolean homeworlds_view_apply_catastrophe_choice(HomeworldsMoveBuilderState *state,
                                                         const HomeworldsViewCatastropheChoice *choice) {
  GameBackendMoveBuilder builder = {0};

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(choice != NULL, FALSE);
  g_return_val_if_fail(choice->system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);

  builder.builder_state = state;
  builder.builder_state_size = sizeof(*state);
  return homeworlds_move_builder_apply_catastrophe_step(&builder, choice->system_index, choice->color);
}

static void homeworlds_view_update_move_report(HomeworldsView *view) {
  const HomeworldsPosition *position = NULL;
  g_autofree char *text = NULL;

  g_return_if_fail(view != NULL);
  g_return_if_fail(GTK_IS_TEXT_VIEW(view->move_report_view));

  if (!view->move_report_enabled) {
    homeworlds_view_set_move_report_text(view, "Move report disabled.");
    return;
  }

  position = ggame_model_peek_position(view->model);
  if (position == NULL) {
    homeworlds_view_set_move_report_text(view, "No moves.");
    return;
  }
  text = homeworlds_move_report_format(position);
  homeworlds_view_set_move_report_text(view, text != NULL ? text : "No moves.");
}

static GtkWidget *homeworlds_view_create_bank_button(HomeworldsView *view, HomeworldsPyramid pyramid, guint count) {
  HomeworldsMoveCandidate candidate = {0};
  HomeworldsBankButtonIcon *icon_data = NULL;
  GtkWidget *button = NULL;
  GtkWidget *icon = NULL;
  char *tooltip = NULL;
  char *count_text = NULL;
  gboolean selectable = FALSE;
  int button_width = 0;
  int button_height = 0;

  g_return_val_if_fail(view != NULL, NULL);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), NULL);
  g_return_val_if_fail(count > 0, NULL);

  button_width = (int)homeworlds_view_bank_button_width(homeworlds_pyramid_size(pyramid));
  button_height = (int)homeworlds_view_bank_button_height(homeworlds_pyramid_size(pyramid));
  selectable = homeworlds_view_find_bank_candidate(view, pyramid, &candidate);
  button = gtk_button_new();
  gtk_widget_add_css_class(button, "homeworlds-bank-pile");
  if (selectable) {
    gtk_widget_add_css_class(button, "homeworlds-bank-choice");
  }
  gtk_widget_set_size_request(button, button_width, button_height);
  gtk_widget_set_can_focus(button, selectable);
  gtk_widget_set_sensitive(button, selectable);
  g_object_set_data(G_OBJECT(button), "homeworlds-board-bank-choice", GUINT_TO_POINTER(1));
  g_object_set_data(G_OBJECT(button), "homeworlds-bank-pyramid", GUINT_TO_POINTER(pyramid));
  g_object_set_data(G_OBJECT(button), "homeworlds-bank-selectable", GUINT_TO_POINTER(selectable ? 1 : 0));

  tooltip = homeworlds_view_pyramid_label(pyramid);
  count_text = g_strdup_printf("%s x%u", tooltip, count);
  gtk_widget_set_tooltip_text(button, count_text);
  g_free(count_text);
  g_free(tooltip);

  icon_data = g_new0(HomeworldsBankButtonIcon, 1);
  g_return_val_if_fail(icon_data != NULL, button);
  icon_data->pyramid = pyramid;
  icon_data->count = count;
  icon = gtk_drawing_area_new();
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(icon), button_width);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(icon), button_height);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon), homeworlds_view_draw_bank_button_icon, icon_data, g_free);

  gtk_button_set_child(GTK_BUTTON(button), icon);

  if (selectable) {
    g_object_set_data_full(G_OBJECT(button),
                           "homeworlds-candidate",
                           g_memdup2(&candidate, sizeof(candidate)),
                           g_free);
    g_signal_connect(button, "clicked", G_CALLBACK(homeworlds_view_candidate_clicked), view);
  }

  return button;
}

static void homeworlds_view_update_board_bank(HomeworldsView *view) {
  const HomeworldsPosition *position = NULL;
  GtkWidget *title = NULL;
  GtkWidget *grid = NULL;
  guint counts[4][4] = {{0}};

  g_return_if_fail(view != NULL);

  homeworlds_view_clear_box(view->board_bank_box);
  position = homeworlds_view_position(view);
  if (position == NULL) {
    return;
  }

  for (guint bank_slot = 0; bank_slot < HOMEWORLDS_BANK_SLOT_COUNT; ++bank_slot) {
    HomeworldsPyramid pyramid = position->bank[bank_slot];
    if (!homeworlds_pyramid_is_valid(pyramid)) {
      continue;
    }

    counts[homeworlds_pyramid_color(pyramid)][homeworlds_pyramid_size(pyramid)]++;
  }

  title = gtk_label_new("Bank");
  gtk_widget_set_name(title, "homeworlds-bank-title");
  gtk_widget_add_css_class(title, "homeworlds-bank-title");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_box_append(GTK_BOX(view->board_bank_box), title);

  grid = gtk_grid_new();
  gtk_widget_set_name(grid, "homeworlds-bank-grid");
  gtk_grid_set_row_spacing(GTK_GRID(grid), HOMEWORLDS_VIEW_BANK_GRID_ROW_SPACING);
  gtk_grid_set_column_spacing(GTK_GRID(grid), HOMEWORLDS_VIEW_BANK_GRID_COLUMN_SPACING);
  gtk_box_append(GTK_BOX(view->board_bank_box), grid);

  for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
    for (HomeworldsSize size = HOMEWORLDS_SIZE_SMALL; size <= HOMEWORLDS_SIZE_LARGE; size++) {
      HomeworldsPyramid pyramid = homeworlds_pyramid_make((HomeworldsColor) color, size);
      if (counts[color][size] == 0) {
        continue;
      }

      GtkWidget *button = homeworlds_view_create_bank_button(view, pyramid, counts[color][size]);
      gtk_grid_attach(GTK_GRID(grid), button, (int) size - 1, (int) color, 1, 1);
    }
  }
}

static void homeworlds_view_update_candidates(HomeworldsView *view) {
  GameBackendMoveList candidates = {0};
  const HomeworldsMoveBuilderState *state = NULL;
  gboolean appended = FALSE;

  g_return_if_fail(view != NULL);

  homeworlds_view_clear_box(view->candidate_box);
  gtk_label_set_text(GTK_LABEL(view->stage_label), homeworlds_view_stage_text(view));
  state = homeworlds_view_builder_state(view);

  if (!view->builder_ready) {
    gtk_box_append(GTK_BOX(view->candidate_box), homeworlds_view_new_text_panel_label("The game is finished."));
    return;
  }

  candidates = homeworlds_move_builder_list_candidates(&view->builder);
  if (state != NULL &&
      state->stage == HOMEWORLDS_BUILDER_STAGE_COMPLETE &&
      homeworlds_view_builder_has_catastrophe_choices(state)) {
    GtkWidget *label = homeworlds_view_new_text_panel_label("Catastrophe available. Trigger one or pass.");
    GtkWidget *button = homeworlds_view_new_text_panel_button("Pass");

    gtk_box_append(GTK_BOX(view->candidate_box), label);
    g_object_set_data(G_OBJECT(button), "homeworlds-finish-catastrophes-pass", GUINT_TO_POINTER(1));
    g_signal_connect(button, "clicked", G_CALLBACK(homeworlds_view_pass_staged_catastrophes_clicked), view);
    gtk_box_append(GTK_BOX(view->candidate_box), button);
    g_clear_pointer(&candidates.moves, g_free);
    return;
  }
  if (state != NULL &&
      state->stage != HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP &&
      homeworlds_view_stage_uses_visual_choices(view)) {
    GtkWidget *label = homeworlds_view_new_text_panel_label(homeworlds_view_visual_choice_text(state));
    gtk_box_append(GTK_BOX(view->candidate_box), label);
    homeworlds_view_append_cancel_button_if_available(view);
    g_clear_pointer(&candidates.moves, g_free);
    return;
  }

  if (candidates.count == 0) {
    GtkWidget *label = homeworlds_view_new_text_panel_label("No legal choices from this partial selection. "
                                                            "Reset selection.");
    gtk_box_append(GTK_BOX(view->candidate_box), label);
    homeworlds_view_append_cancel_button_if_available(view);
    g_clear_pointer(&candidates.moves, g_free);
    return;
  }

  if (state != NULL && state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP) {
    GtkWidget *label = homeworlds_view_new_text_panel_label(homeworlds_view_visual_choice_text(state));
    gtk_box_append(GTK_BOX(view->candidate_box), label);

    for (gsize i = 0; i < candidates.count; ++i) {
      const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates.moves)[i];
      if (candidate->data.kind == HOMEWORLDS_CANDIDATE_SELECT_SHIP) {
        continue;
      }
      homeworlds_view_append_candidate_button(view, candidate);
      appended = TRUE;
    }
    if (!appended) {
      GtkWidget *fallback = homeworlds_view_new_text_panel_label("No side-panel choices are available for this step.");
      gtk_box_append(GTK_BOX(view->candidate_box), fallback);
    }
    homeworlds_view_append_cancel_button_if_available(view);
    g_clear_pointer(&candidates.moves, g_free);
    return;
  }

  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates.moves)[i];
    homeworlds_view_append_candidate_button(view, candidate);
  }
  homeworlds_view_append_cancel_button_if_available(view);
  g_clear_pointer(&candidates.moves, g_free);
}

static void homeworlds_view_update_from_current_builder(HomeworldsView *view) {
  g_return_if_fail(view != NULL);

  homeworlds_view_update_candidates(view);
  homeworlds_view_update_catastrophes(view);
  homeworlds_view_update_board_content_size(view);
  homeworlds_view_update_board_bank(view);
  homeworlds_view_update_board_choice_buttons(view);
  gtk_widget_queue_draw(view->drawing_area);
}

static void homeworlds_view_model_state_changed(GGameModel * /*model*/, gpointer user_data) {
  HomeworldsView *view = user_data;

  g_return_if_fail(view != NULL);

  homeworlds_view_clear_previous_move_markers(view);
  homeworlds_view_refresh(view);
}

static HomeworldsView *homeworlds_view_new_with_move_report(GGameModel *model, gboolean move_report_enabled) {
  HomeworldsView *view = NULL;
  GtkWidget *bank_frame = NULL;
  GtkWidget *side_panel = NULL;
  GtkWidget *side_scroller = NULL;
  GtkWidget *move_report_scroller = NULL;
  GtkWidget *heading = NULL;
  GtkWidget *section = NULL;

  g_return_val_if_fail(GGAME_IS_MODEL(model), NULL);

  view = g_new0(HomeworldsView, 1);
  g_return_val_if_fail(view != NULL, NULL);
  view->model = g_object_ref(model);
  view->move_report_enabled = move_report_enabled;
  view->model_state_changed_handler_id = g_signal_connect(view->model,
                                                          "state-changed",
                                                          G_CALLBACK(homeworlds_view_model_state_changed),
                                                          view);

  view->root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_widget_set_hexpand(view->root, TRUE);
  gtk_widget_set_vexpand(view->root, TRUE);
  gtk_widget_set_name(view->root, "homeworlds-view");
  view->root_destroy_handler_id = g_signal_connect(view->root,
                                                   "destroy",
                                                   G_CALLBACK(homeworlds_view_root_destroyed),
                                                   view);

  view->board_scroller = gtk_scrolled_window_new();
  gtk_widget_set_name(view->board_scroller, "homeworlds-board-scroller");
  gtk_widget_set_hexpand(view->board_scroller, TRUE);
  gtk_widget_set_vexpand(view->board_scroller, TRUE);
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(view->board_scroller),
                                            HOMEWORLDS_VIEW_MIN_BOARD_VIEWPORT_WIDTH);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(view->board_scroller),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  g_signal_connect_after(view->board_scroller, "map", G_CALLBACK(homeworlds_view_board_scroller_mapped), view);
  gtk_box_append(GTK_BOX(view->root), view->board_scroller);

  view->board_overlay = gtk_overlay_new();
  gtk_widget_set_hexpand(view->board_overlay, TRUE);
  gtk_widget_set_vexpand(view->board_overlay, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(view->board_scroller), view->board_overlay);

  view->drawing_area = gtk_drawing_area_new();
  gtk_widget_set_name(view->drawing_area, "homeworlds-board");
  gtk_widget_set_hexpand(view->drawing_area, TRUE);
  gtk_widget_set_vexpand(view->drawing_area, TRUE);
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(view->drawing_area), HOMEWORLDS_VIEW_INITIAL_BOARD_WIDTH);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(view->drawing_area), HOMEWORLDS_VIEW_INITIAL_BOARD_HEIGHT);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(view->drawing_area), homeworlds_view_draw, view, NULL);
  g_signal_connect(view->drawing_area, "resize", G_CALLBACK(homeworlds_view_board_resized), view);
  gtk_overlay_set_child(GTK_OVERLAY(view->board_overlay), view->drawing_area);
  view->board_hadjustment =
      g_object_ref(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(view->board_scroller)));
  view->board_hadjustment_changed_handler_id = g_signal_connect(view->board_hadjustment,
                                                                "changed",
                                                                G_CALLBACK(homeworlds_view_board_viewport_changed),
                                                                view);
  view->board_vadjustment =
      g_object_ref(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(view->board_scroller)));
  view->board_vadjustment_changed_handler_id = g_signal_connect(view->board_vadjustment,
                                                                "changed",
                                                                G_CALLBACK(homeworlds_view_board_viewport_changed),
                                                                view);

  view->board_choice_layer = gtk_fixed_new();
  gtk_widget_set_hexpand(view->board_choice_layer, TRUE);
  gtk_widget_set_vexpand(view->board_choice_layer, TRUE);
  gtk_widget_set_halign(view->board_choice_layer, GTK_ALIGN_FILL);
  gtk_widget_set_valign(view->board_choice_layer, GTK_ALIGN_FILL);
  gtk_overlay_add_overlay(GTK_OVERLAY(view->board_overlay), view->board_choice_layer);

  bank_frame = gtk_frame_new(NULL);
  gtk_widget_set_name(bank_frame, "homeworlds-board-bank");
  gtk_widget_set_halign(bank_frame, GTK_ALIGN_END);
  gtk_widget_set_valign(bank_frame, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_end(bank_frame, HOMEWORLDS_VIEW_BANK_MARGIN);
  gtk_overlay_add_overlay(GTK_OVERLAY(view->board_overlay), bank_frame);

  view->board_bank_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_margin_top(view->board_bank_box, HOMEWORLDS_VIEW_BANK_INNER_MARGIN);
  gtk_widget_set_margin_bottom(view->board_bank_box, HOMEWORLDS_VIEW_BANK_INNER_MARGIN);
  gtk_widget_set_margin_start(view->board_bank_box, HOMEWORLDS_VIEW_BANK_INNER_MARGIN);
  gtk_widget_set_margin_end(view->board_bank_box, HOMEWORLDS_VIEW_BANK_INNER_MARGIN);
  gtk_frame_set_child(GTK_FRAME(bank_frame), view->board_bank_box);

  side_scroller = gtk_scrolled_window_new();
  gtk_widget_set_name(side_scroller, "homeworlds-text-panel");
  gtk_widget_set_hexpand(side_scroller, FALSE);
  gtk_widget_set_halign(side_scroller, GTK_ALIGN_START);
  gtk_widget_set_size_request(side_scroller, HOMEWORLDS_VIEW_TEXT_PANEL_WIDTH, -1);
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(side_scroller), HOMEWORLDS_VIEW_TEXT_PANEL_WIDTH);
  gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(side_scroller), HOMEWORLDS_VIEW_TEXT_PANEL_WIDTH);
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(side_scroller), FALSE);
  gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(side_scroller), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(side_scroller), GTK_POLICY_EXTERNAL, GTK_POLICY_AUTOMATIC);
  gtk_box_append(GTK_BOX(view->root), side_scroller);

  side_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_name(side_panel, "homeworlds-text-panel-content");
  gtk_widget_set_halign(side_panel, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand(side_panel, FALSE);
  gtk_widget_set_size_request(side_panel, HOMEWORLDS_VIEW_TEXT_PANEL_CONTENT_WIDTH, -1);
  gtk_widget_set_margin_top(side_panel, HOMEWORLDS_VIEW_TEXT_PANEL_MARGIN);
  gtk_widget_set_margin_bottom(side_panel, HOMEWORLDS_VIEW_TEXT_PANEL_MARGIN);
  gtk_widget_set_margin_start(side_panel, HOMEWORLDS_VIEW_TEXT_PANEL_MARGIN);
  gtk_widget_set_margin_end(side_panel, HOMEWORLDS_VIEW_TEXT_PANEL_MARGIN);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(side_scroller), side_panel);

  heading = gtk_label_new("Homeworlds");
  gtk_widget_add_css_class(heading, "title-2");
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
  gtk_box_append(GTK_BOX(side_panel), heading);

  view->stage_label = gtk_label_new("");
  homeworlds_view_constrain_text_panel_label(view->stage_label);
  gtk_label_set_xalign(GTK_LABEL(view->stage_label), 0.0f);
  gtk_box_append(GTK_BOX(side_panel), view->stage_label);

  section = gtk_label_new("Choices");
  gtk_widget_add_css_class(section, "heading");
  gtk_label_set_xalign(GTK_LABEL(section), 0.0f);
  gtk_box_append(GTK_BOX(side_panel), section);

  view->candidate_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_append(GTK_BOX(side_panel), view->candidate_box);

  section = gtk_label_new("Catastrophes");
  gtk_widget_add_css_class(section, "heading");
  gtk_label_set_xalign(GTK_LABEL(section), 0.0f);
  gtk_box_append(GTK_BOX(side_panel), section);

  view->catastrophe_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_append(GTK_BOX(side_panel), view->catastrophe_box);

  section = gtk_label_new("Last move");
  gtk_widget_add_css_class(section, "heading");
  gtk_label_set_xalign(GTK_LABEL(section), 0.0f);
  gtk_box_append(GTK_BOX(side_panel), section);

  view->last_move_label = gtk_label_new("None");
  homeworlds_view_constrain_text_panel_label(view->last_move_label);
  gtk_label_set_xalign(GTK_LABEL(view->last_move_label), 0.0f);
  gtk_box_append(GTK_BOX(side_panel), view->last_move_label);

  section = gtk_label_new("Move report");
  gtk_widget_add_css_class(section, "heading");
  gtk_label_set_xalign(GTK_LABEL(section), 0.0f);
  gtk_box_append(GTK_BOX(side_panel), section);

  move_report_scroller = homeworlds_view_new_move_report_scroller(&view->move_report_view);
  gtk_box_append(GTK_BOX(side_panel), move_report_scroller);

  homeworlds_view_refresh(view);
  return view;
}

HomeworldsView *homeworlds_view_new(GGameModel *model) {
  return homeworlds_view_new_with_move_report(model, TRUE);
}

GtkWidget *homeworlds_view_create_board_host(GGameModel *model,
                                             BoardView * /*board_view*/,
                                             GGameAppMoveHandler move_handler,
                                             gpointer move_handler_data,
                                             const GGameAppBoardHostOptions *options) {
  HomeworldsView *view = NULL;
  GtkWidget *widget = NULL;
  gboolean move_report_enabled = TRUE;

  g_return_val_if_fail(GGAME_IS_MODEL(model), NULL);

  if (options != NULL) {
    move_report_enabled = options->move_report_enabled;
  }

  view = homeworlds_view_new_with_move_report(model, move_report_enabled);
  g_return_val_if_fail(view != NULL, NULL);
  homeworlds_view_set_move_handler(view, (HomeworldsViewMoveHandler)move_handler, move_handler_data);

  widget = homeworlds_view_get_widget(view);
  g_return_val_if_fail(GTK_IS_WIDGET(widget), NULL);
  g_object_set_data_full(G_OBJECT(widget), "homeworlds-view-state", view, (GDestroyNotify)homeworlds_view_free);
  return widget;
}

void homeworlds_view_free(HomeworldsView *view) {
  if (view == NULL) {
    return;
  }

  homeworlds_view_cancel_board_layout_settle(view);
  if (view->root_destroy_handler_id != 0 && GTK_IS_WIDGET(view->root)) {
    g_signal_handler_disconnect(view->root, view->root_destroy_handler_id);
    view->root_destroy_handler_id = 0;
  }
  if (view->model != NULL && view->model_state_changed_handler_id != 0) {
    g_signal_handler_disconnect(view->model, view->model_state_changed_handler_id);
    view->model_state_changed_handler_id = 0;
  }
  if (view->board_hadjustment != NULL && view->board_hadjustment_changed_handler_id != 0) {
    g_signal_handler_disconnect(view->board_hadjustment, view->board_hadjustment_changed_handler_id);
    view->board_hadjustment_changed_handler_id = 0;
  }
  if (view->board_vadjustment != NULL && view->board_vadjustment_changed_handler_id != 0) {
    g_signal_handler_disconnect(view->board_vadjustment, view->board_vadjustment_changed_handler_id);
    view->board_vadjustment_changed_handler_id = 0;
  }
  g_clear_object(&view->board_hadjustment);
  g_clear_object(&view->board_vadjustment);
  if (view->builder_ready) {
    homeworlds_move_builder_clear(&view->builder);
  }
  for (guint side = 0; side < 2; side++) {
    g_free(view->player_names[side]);
  }
  g_clear_object(&view->model);
  g_free(view);
}

GtkWidget *homeworlds_view_get_widget(HomeworldsView *view) {
  g_return_val_if_fail(view != NULL, NULL);

  return view->root;
}

void homeworlds_view_refresh(HomeworldsView *view) {
  g_return_if_fail(view != NULL);

  homeworlds_view_rebuild_builder(view);
  homeworlds_view_update_candidates(view);
  homeworlds_view_update_catastrophes(view);
  homeworlds_view_update_move_report(view);
  homeworlds_view_update_board_content_size(view);
  homeworlds_view_update_board_bank(view);
  homeworlds_view_update_board_choice_buttons(view);
  gtk_widget_queue_draw(view->drawing_area);
}

void homeworlds_view_reset_selection(HomeworldsView *view) {
  g_return_if_fail(view != NULL);

  homeworlds_view_refresh(view);
}

gboolean homeworlds_view_has_partial_selection(const HomeworldsView *view) {
  const HomeworldsMoveBuilderState *state = homeworlds_view_builder_state(view);

  if (state == NULL) {
    return FALSE;
  }

  switch ((HomeworldsBuilderStage) state->stage) {
    case HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP:
    case HOMEWORLDS_BUILDER_STAGE_COMPLETE:
      return FALSE;
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_TRADE_COLOR:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ATTACK_TARGET:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_MOVE_TARGET:
    default:
      return TRUE;
  }
}

void homeworlds_view_set_move_report_enabled(HomeworldsView *view, gboolean enabled) {
  g_return_if_fail(view != NULL);

  if (view->move_report_enabled == enabled) {
    return;
  }

  view->move_report_enabled = enabled;
  homeworlds_view_update_move_report(view);
}

gboolean homeworlds_view_get_move_report_enabled(const HomeworldsView *view) {
  g_return_val_if_fail(view != NULL, FALSE);

  return view->move_report_enabled;
}

gboolean homeworlds_view_apply_candidate_at(HomeworldsView *view, gsize index) {
  GameBackendMoveList candidates = {0};
  HomeworldsMoveCandidate candidate = {0};

  g_return_val_if_fail(view != NULL, FALSE);

  if (!view->builder_ready) {
    return FALSE;
  }

  candidates = homeworlds_move_builder_list_candidates(&view->builder);
  if (index >= candidates.count) {
    g_clear_pointer(&candidates.moves, g_free);
    return FALSE;
  }

  candidate = ((const HomeworldsMoveCandidate *) candidates.moves)[index];
  g_clear_pointer(&candidates.moves, g_free);

  if (!homeworlds_move_builder_step(&view->builder, &candidate)) {
    g_debug("Homeworlds move builder rejected indexed candidate");
    homeworlds_view_refresh(view);
    return FALSE;
  }

  if (homeworlds_move_builder_is_complete(&view->builder)) {
    if (!homeworlds_view_complete_move_if_ready(view)) {
      homeworlds_view_refresh(view);
      return FALSE;
    }
    if (view->builder_ready &&
        homeworlds_move_builder_is_complete(&view->builder) &&
        homeworlds_view_builder_has_catastrophe_choices(homeworlds_view_builder_state(view))) {
      homeworlds_view_update_from_current_builder(view);
    } else {
      homeworlds_view_refresh(view);
    }
    return TRUE;
  }

  homeworlds_view_update_from_current_builder(view);
  return TRUE;
}

gsize homeworlds_view_get_candidate_count(const HomeworldsView *view) {
  GameBackendMoveList candidates = {0};
  gsize count = 0;

  g_return_val_if_fail(view != NULL, 0);

  if (!view->builder_ready) {
    return 0;
  }

  candidates = homeworlds_move_builder_list_candidates(&view->builder);
  count = candidates.count;
  g_clear_pointer(&candidates.moves, g_free);
  return count;
}

const char *homeworlds_view_get_last_move_text(const HomeworldsView *view) {
  g_return_val_if_fail(view != NULL, NULL);
  g_return_val_if_fail(GTK_IS_LABEL(view->last_move_label), NULL);

  return gtk_label_get_text(GTK_LABEL(view->last_move_label));
}

static void homeworlds_view_update_player_names_from_node(HomeworldsView *view, const SgfNode *node) {
  gboolean changed = FALSE;

  g_return_if_fail(view != NULL);
  g_return_if_fail(node != NULL);

  for (guint side = 0; side < 2; side++) {
    const char *name = homeworlds_view_player_name_for_side_from_node(node, side);
    if (g_strcmp0(view->player_names[side], name) != 0) {
      g_free(view->player_names[side]);
      view->player_names[side] = g_strdup(name);
      changed = TRUE;
    }
  }

  if (changed && GTK_IS_WIDGET(view->drawing_area)) {
    gtk_widget_queue_draw(view->drawing_area);
  }
}

void homeworlds_view_sync_board_host_node(GtkWidget *board_host, const SgfNode *node) {
  HomeworldsView *view = NULL;
  const GameBackend *backend = NULL;
  const HomeworldsPosition *after_position = NULL;
  HomeworldsPosition before_position = {0};
  HomeworldsMove move = {0};
  SgfColor color = SGF_COLOR_NONE;
  gboolean has_move = FALSE;
  guint move_side = 0;
  gsize marker_count = 0;
  char formatted[128] = {0};

  g_return_if_fail(GTK_IS_WIDGET(board_host));
  g_return_if_fail(node != NULL);

  view = g_object_get_data(G_OBJECT(board_host), "homeworlds-view-state");
  if (view == NULL) {
    return;
  }

  homeworlds_view_update_player_names_from_node(view, node);

  backend = ggame_model_peek_backend(view->model);
  after_position = ggame_model_peek_position(view->model);
  if (backend == NULL || after_position == NULL) {
    homeworlds_view_clear_previous_move_markers(view);
    gtk_label_set_text(GTK_LABEL(view->last_move_label), "None");
    return;
  }

  homeworlds_position_init(&before_position);
  g_autoptr(GError) replay_error = NULL;
  if (!ggame_sgf_controller_replay_parent_node_for_move(node,
                                                        backend,
                                                        &before_position,
                                                        &move,
                                                        &color,
                                                        &has_move,
                                                        &replay_error) ||
      !has_move) {
    if (replay_error != NULL) {
      g_debug("Failed to reconstruct Homeworlds previous move markers: %s", replay_error->message);
    }
    homeworlds_view_clear_previous_move_markers(view);
    gtk_label_set_text(GTK_LABEL(view->last_move_label), "None");
    homeworlds_position_clear(&before_position);
    return;
  }

  if (homeworlds_move_format(&move, formatted, sizeof(formatted))) {
    gtk_label_set_text(GTK_LABEL(view->last_move_label), formatted);
  } else {
    gtk_label_set_text(GTK_LABEL(view->last_move_label), "Unknown");
  }

  if (!homeworlds_view_side_from_sgf_color(backend, color, &move_side) ||
      !homeworlds_view_collect_previous_move_markers(&before_position,
                                                     after_position,
                                                     &move,
                                                     move_side,
                                                     view->previous_move_markers,
                                                     G_N_ELEMENTS(view->previous_move_markers),
                                                     &marker_count)) {
    homeworlds_view_clear_previous_move_markers(view);
    homeworlds_position_clear(&before_position);
    return;
  }

  view->previous_move_marker_count = marker_count;
  gtk_widget_queue_draw(view->drawing_area);
  homeworlds_position_clear(&before_position);
}

void homeworlds_view_set_board_host_move_report_enabled(GtkWidget *board_host, gboolean enabled) {
  HomeworldsView *view = NULL;

  g_return_if_fail(GTK_IS_WIDGET(board_host));

  view = g_object_get_data(G_OBJECT(board_host), "homeworlds-view-state");
  if (view == NULL) {
    return;
  }

  homeworlds_view_set_move_report_enabled(view, enabled);
}

void homeworlds_view_set_move_applied_callback(HomeworldsView *view,
                                               HomeworldsViewMoveAppliedFunc func,
                                               gpointer user_data) {
  g_return_if_fail(view != NULL);

  view->move_applied = func;
  view->move_applied_data = user_data;
}

void homeworlds_view_set_move_handler(HomeworldsView *view,
                                      HomeworldsViewMoveHandler handler,
                                      gpointer user_data) {
  g_return_if_fail(view != NULL);

  view->move_handler = handler;
  view->move_handler_data = user_data;
}
