#include "homeworlds_view.h"

#include "homeworlds_backend.h"
#include "homeworlds_game.h"
#include "homeworlds_move_builder.h"
#include "../../sgf_move_props.h"

#include <math.h>
#include <string.h>

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
#define HOMEWORLDS_VIEW_SYSTEM_PADDING_BOTTOM 14.0
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
#define HOMEWORLDS_VIEW_MIN_BOARD_VIEWPORT_WIDTH 420
#define HOMEWORLDS_VIEW_SYSTEM_PIECE_MAX (HOMEWORLDS_STAR_SLOT_COUNT + (2 * HOMEWORLDS_SHIP_SLOT_COUNT))
#define HOMEWORLDS_VIEW_MOVE_REPORT_MAX_MOVES 512
#define HOMEWORLDS_VIEW_MOVE_REPORT_MAX_LEAVES 4096

typedef enum {
  HOMEWORLDS_VIEW_SYSTEM_ROW_TOP = 0,
  HOMEWORLDS_VIEW_SYSTEM_ROW_MIDDLE,
  HOMEWORLDS_VIEW_SYSTEM_ROW_BOTTOM,
} HomeworldsViewSystemRow;

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
  HomeworldsMove *moves;
  gsize count;
  gsize capacity;
  gsize leaves_seen;
  gboolean truncated;
} HomeworldsViewMoveBuffer;

typedef struct {
  guint system_index;
  HomeworldsColor color;
} HomeworldsViewCatastropheChoice;

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
  GtkWidget *move_report_label;
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
};

static void homeworlds_view_update_from_current_builder(HomeworldsView *view);
static void homeworlds_view_update_board_content_width(HomeworldsView *view);
static void homeworlds_view_update_board_bank(HomeworldsView *view);
static void homeworlds_view_update_board_choice_buttons(HomeworldsView *view);
static void homeworlds_view_update_move_report(HomeworldsView *view);
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

static char *homeworlds_view_candidate_label(const HomeworldsMoveCandidate *candidate) {
  char *pyramid_label = NULL;
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
      label = g_strdup_printf("Select %s at system %u",
                              pyramid_label,
                              (guint) candidate->data.system_index);
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
      if (candidate->data.target_system_index == HOMEWORLDS_INVALID_INDEX) {
        pyramid_label = homeworlds_view_pyramid_label(candidate->data.pyramid);
        label = g_strdup_printf("Discover at %s star", pyramid_label);
        g_free(pyramid_label);
        return label;
      }
      return g_strdup_printf("Move to system %u", (guint) candidate->data.target_system_index);
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

static void homeworlds_view_row_y_positions(double height,
                                            gboolean has_non_home_system,
                                            double *out_top,
                                            double *out_middle,
                                            double *out_bottom,
                                            double *out_player_1,
                                            double *out_player_2) {
  double player_1_y = has_non_home_system ? height * 0.84 : height * 0.72;
  double player_2_y = has_non_home_system ? height * 0.16 : height * 0.28;
  double band = player_1_y - player_2_y;

  g_return_if_fail(out_top != NULL);
  g_return_if_fail(out_middle != NULL);
  g_return_if_fail(out_bottom != NULL);
  g_return_if_fail(out_player_1 != NULL);
  g_return_if_fail(out_player_2 != NULL);

  *out_top = player_2_y + (band * 0.25);
  *out_middle = player_2_y + (band * 0.50);
  *out_bottom = player_2_y + (band * 0.75);
  *out_player_1 = player_1_y;
  *out_player_2 = player_2_y;
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

double homeworlds_view_calculate_board_content_width(const HomeworldsPosition *position, double viewport_width) {
  double required_width = viewport_width;

  g_return_val_if_fail(position != NULL, (double)HOMEWORLDS_VIEW_FALLBACK_BOARD_WIDTH);
  g_return_val_if_fail(viewport_width > 0.0, (double)HOMEWORLDS_VIEW_FALLBACK_BOARD_WIDTH);

  for (guint system_index = 0; system_index < 2; ++system_index) {
    double system_width = 0.0;

    if (!homeworlds_view_measure_system_width(position, system_index, &system_width)) {
      continue;
    }
    required_width = MAX(required_width,
                         homeworlds_view_board_width_for_row(homeworlds_view_row_required_width(system_width, 1)));
  }

  for (guint row_value = HOMEWORLDS_VIEW_SYSTEM_ROW_TOP;
       row_value <= HOMEWORLDS_VIEW_SYSTEM_ROW_BOTTOM;
       ++row_value) {
    HomeworldsViewSystemRow row = (HomeworldsViewSystemRow)row_value;
    guint system_count = 0;
    double total_width = 0.0;

    for (guint system_index = 2; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
      double system_width = 0.0;

      if (homeworlds_system_is_empty(&position->systems[system_index]) ||
          homeworlds_view_system_row(position, system_index) != row) {
        continue;
      }
      if (!homeworlds_view_measure_system_width(position, system_index, &system_width)) {
        continue;
      }
      total_width += system_width;
      system_count++;
    }

    if (system_count > 0) {
      required_width = MAX(required_width,
                           homeworlds_view_board_width_for_row(
                               homeworlds_view_row_required_width(total_width, system_count)));
    }
  }

  return ceil(required_width);
}

gboolean homeworlds_view_calculate_system_center(const HomeworldsPosition *position,
                                                 guint system_index,
                                                 double width,
                                                 double height,
                                                 double *out_x,
                                                 double *out_y) {
  gboolean has_non_home_system = FALSE;
  double top_y = 0.0;
  double middle_y = 0.0;
  double bottom_y = 0.0;
  double player_1_y = 0.0;
  double player_2_y = 0.0;
  HomeworldsViewSystemRow row = HOMEWORLDS_VIEW_SYSTEM_ROW_MIDDLE;
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

  has_non_home_system = homeworlds_view_position_has_non_home_system(position);
  homeworlds_view_row_y_positions(height,
                                  has_non_home_system,
                                  &top_y,
                                  &middle_y,
                                  &bottom_y,
                                  &player_1_y,
                                  &player_2_y);
  homeworlds_view_row_x_bounds(width, &row_left, &row_right);

  if (system_index == 0) {
    if (!homeworlds_view_collect_target_row(position,
                                            system_index,
                                            row_systems,
                                            G_N_ELEMENTS(row_systems),
                                            &row_count,
                                            &row_index,
                                            &total_row_width)) {
      return FALSE;
    }
    *out_x = homeworlds_view_row_center_for_index(row_left,
                                                  row_right,
                                                  row_systems,
                                                  row_count,
                                                  row_index,
                                                  total_row_width);
    *out_y = player_1_y;
    return TRUE;
  }
  if (system_index == 1) {
    if (!homeworlds_view_collect_target_row(position,
                                            system_index,
                                            row_systems,
                                            G_N_ELEMENTS(row_systems),
                                            &row_count,
                                            &row_index,
                                            &total_row_width)) {
      return FALSE;
    }
    *out_x = homeworlds_view_row_center_for_index(row_left,
                                                  row_right,
                                                  row_systems,
                                                  row_count,
                                                  row_index,
                                                  total_row_width);
    *out_y = player_2_y;
    return TRUE;
  }
  if (homeworlds_system_is_empty(&position->systems[system_index])) {
    return FALSE;
  }

  row = homeworlds_view_system_row(position, system_index);
  if (!homeworlds_view_collect_target_row(position,
                                          system_index,
                                          row_systems,
                                          G_N_ELEMENTS(row_systems),
                                          &row_count,
                                          &row_index,
                                          &total_row_width)) {
    return FALSE;
  }

  *out_y = row == HOMEWORLDS_VIEW_SYSTEM_ROW_TOP ? top_y :
           row == HOMEWORLDS_VIEW_SYSTEM_ROW_BOTTOM ? bottom_y : middle_y;
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
      continue;
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

static void homeworlds_view_draw_system(cairo_t *cr,
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
  if (system_index < 2) {
    cairo_move_to(cr, x + 14.0, y + 22.0);
    cairo_show_text(cr, system_index == 0 ? "Player 1 homeworld" : "Player 2 homeworld");
  } else {
    char label[32] = {0};
    g_snprintf(label, sizeof(label), "System %u", system_index);
    cairo_move_to(cr, x + 14.0, y + 22.0);
    cairo_show_text(cr, label);
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

static int homeworlds_view_board_viewport_width(HomeworldsView *view) {
  int width = 0;

  g_return_val_if_fail(view != NULL, HOMEWORLDS_VIEW_FALLBACK_BOARD_WIDTH);

  if (view->board_hadjustment != NULL) {
    double page_size = gtk_adjustment_get_page_size(view->board_hadjustment);
    if (page_size > 1.0) {
      return (int)ceil(page_size);
    }
  }
  if (view->board_scroller != NULL) {
    width = gtk_widget_get_width(view->board_scroller);
  }
  if (width <= 1 && view->board_overlay != NULL) {
    width = gtk_widget_get_width(view->board_overlay);
  }

  return width > 1 ? width : HOMEWORLDS_VIEW_FALLBACK_BOARD_WIDTH;
}

static void homeworlds_view_update_board_content_width(HomeworldsView *view) {
  const HomeworldsPosition *position = NULL;
  int viewport_width = 0;
  int content_width = 0;

  g_return_if_fail(view != NULL);
  g_return_if_fail(GTK_IS_DRAWING_AREA(view->drawing_area));

  position = homeworlds_view_position(view);
  if (position == NULL) {
    return;
  }

  viewport_width = homeworlds_view_board_viewport_width(view);
  content_width = (int)homeworlds_view_calculate_board_content_width(position, (double)viewport_width);
  if (content_width != gtk_drawing_area_get_content_width(GTK_DRAWING_AREA(view->drawing_area))) {
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(view->drawing_area), content_width);
  }
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

  homeworlds_view_update_board_content_width(view);
  homeworlds_view_update_board_choice_buttons(view);
}

static void homeworlds_view_board_viewport_changed(GtkAdjustment * /*adjustment*/, gpointer user_data) {
  HomeworldsView *view = user_data;

  g_return_if_fail(view != NULL);

  homeworlds_view_update_board_content_width(view);
  homeworlds_view_update_board_choice_buttons(view);
  gtk_widget_queue_draw(view->drawing_area);
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
    homeworlds_view_draw_system(cr, &position->systems[system_index], system_index, center_x, center_y, selected);
  }
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
  button = gtk_button_new_with_label(label);
  gtk_widget_set_halign(button, GTK_ALIGN_FILL);
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

  button = gtk_button_new_with_label("Cancel");
  gtk_widget_set_halign(button, GTK_ALIGN_FILL);
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
    gtk_box_append(GTK_BOX(view->catastrophe_box), gtk_label_new("Reset the partial move before catastrophes."));
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

      char *label = g_strdup_printf("Catastrophe %s at system %u",
                                    homeworlds_view_color_name((HomeworldsColor) color),
                                    system_index);
      GtkWidget *button = gtk_button_new_with_label(label);
      g_object_set_data(G_OBJECT(button), "homeworlds-system-index", GUINT_TO_POINTER(system_index + 1));
      g_object_set_data(G_OBJECT(button), "homeworlds-color", GUINT_TO_POINTER(color + 1));
      g_signal_connect(button, "clicked", G_CALLBACK(homeworlds_view_catastrophe_clicked), view);
      gtk_box_append(GTK_BOX(view->catastrophe_box), button);
      g_free(label);
      appended = TRUE;
    }
  }

  if (!appended) {
    GtkWidget *label = gtk_label_new("No catastrophes are currently available.");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(view->catastrophe_box), label);
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

    switch ((HomeworldsCandidateKind) candidate->data.kind) {
      case HOMEWORLDS_CANDIDATE_SETUP_STAR:
      case HOMEWORLDS_CANDIDATE_SETUP_SHIP:
        found = candidate->data.pyramid == pyramid;
        break;
      case HOMEWORLDS_CANDIDATE_TRADE_COLOR:
        found = state != NULL &&
                homeworlds_pyramid_is_valid(state->selected_ship_pyramid) &&
                homeworlds_pyramid_size(state->selected_ship_pyramid) == homeworlds_pyramid_size(pyramid) &&
                candidate->data.target_color == homeworlds_pyramid_color(pyramid);
        break;
      case HOMEWORLDS_CANDIDATE_MOVE_TARGET:
        found = candidate->data.target_system_index == HOMEWORLDS_INVALID_INDEX &&
                candidate->data.pyramid == pyramid;
        break;
      case HOMEWORLDS_CANDIDATE_NONE:
      case HOMEWORLDS_CANDIDATE_SELECT_SHIP:
      case HOMEWORLDS_CANDIDATE_ACTION:
      case HOMEWORLDS_CANDIDATE_ATTACK_TARGET:
      default:
        found = FALSE;
        break;
    }

    if (found) {
      *out_candidate = *candidate;
      break;
    }
  }

  g_clear_pointer(&candidates.moves, g_free);
  return found;
}

static gboolean homeworlds_view_moves_equal(const HomeworldsMove *left, const HomeworldsMove *right) {
  char left_text[128] = {0};
  char right_text[128] = {0};

  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  return homeworlds_move_format(left, left_text, sizeof(left_text)) &&
         homeworlds_move_format(right, right_text, sizeof(right_text)) &&
         strcmp(left_text, right_text) == 0;
}

static gboolean homeworlds_view_move_buffer_append(HomeworldsViewMoveBuffer *buffer, const HomeworldsMove *move) {
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (buffer->leaves_seen >= HOMEWORLDS_VIEW_MOVE_REPORT_MAX_LEAVES) {
    buffer->truncated = TRUE;
    return TRUE;
  }
  buffer->leaves_seen++;

  if (buffer->count >= HOMEWORLDS_VIEW_MOVE_REPORT_MAX_MOVES) {
    buffer->truncated = TRUE;
    return TRUE;
  }

  for (gsize i = 0; i < buffer->count; ++i) {
    if (homeworlds_view_moves_equal(&buffer->moves[i], move)) {
      return TRUE;
    }
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

static guint homeworlds_view_collect_catastrophe_choices(const HomeworldsMoveBuilderState *state,
                                                         HomeworldsViewCatastropheChoice *out_choices,
                                                         guint max_choices) {
  guint count = 0;

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(out_choices != NULL || max_choices == 0, 0);

  if (state->working_position.phase != HOMEWORLDS_PHASE_PLAY ||
      state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP) {
    return 0;
  }

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];

    for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
      if (homeworlds_system_color_count(system, (HomeworldsColor) color) < 4) {
        continue;
      }
      if (count < max_choices) {
        out_choices[count] = (HomeworldsViewCatastropheChoice){
          .system_index = system_index,
          .color = (HomeworldsColor) color,
        };
      }
      count++;
    }
  }

  return MIN(count, max_choices);
}

static gboolean homeworlds_view_apply_catastrophe_choice(HomeworldsMoveBuilderState *state,
                                                         const HomeworldsViewCatastropheChoice *choice) {
  GameBackendMoveBuilder builder = {0};
  HomeworldsTurnStep step = {
    .kind = HOMEWORLDS_STEP_CATASTROPHE,
    .target_color = choice->color,
  };

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(choice != NULL, FALSE);
  g_return_val_if_fail(choice->system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);

  builder.builder_state = state;
  builder.builder_state_size = sizeof(*state);
  if (state->stage != HOMEWORLDS_BUILDER_STAGE_COMPLETE) {
    return homeworlds_move_builder_apply_catastrophe(&builder, choice->system_index, choice->color);
  }

  if (state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS ||
      !homeworlds_position_system_ref_for_index(&state->working_position, choice->system_index, &step.target_system)) {
    return FALSE;
  }

  state->move.steps[state->move.step_count++] = step;
  if (!homeworlds_position_apply_turn_step(&state->working_position, &step)) {
    state->move.step_count--;
    return FALSE;
  }
  return TRUE;
}

static gboolean homeworlds_view_collect_all_moves_recursive(const HomeworldsMoveBuilderState *state,
                                                            HomeworldsViewMoveBuffer *buffer) {
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  HomeworldsViewCatastropheChoice catastrophes[HOMEWORLDS_SYSTEM_SLOT_COUNT * 4] = {0};
  guint catastrophe_count = 0;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);

  if (buffer->truncated) {
    return TRUE;
  }

  builder.builder_state = (gpointer) state;
  builder.builder_state_size = sizeof(*state);
  if (state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS &&
      !homeworlds_move_builder_is_complete(&builder)) {
    buffer->truncated = TRUE;
    return TRUE;
  }

  if (homeworlds_move_builder_is_complete(&builder)) {
    HomeworldsMove move = {0};

    if (!homeworlds_move_builder_build_move(&builder, &move) ||
        !homeworlds_view_move_buffer_append(buffer, &move)) {
      return FALSE;
    }
  }

  catastrophe_count = homeworlds_view_collect_catastrophe_choices(state, catastrophes, G_N_ELEMENTS(catastrophes));
  for (guint i = 0; i < catastrophe_count; ++i) {
    HomeworldsMoveBuilderState child_state = *state;

    if (!homeworlds_view_apply_catastrophe_choice(&child_state, &catastrophes[i])) {
      continue;
    }
    if (!homeworlds_view_collect_all_moves_recursive(&child_state, buffer)) {
      return FALSE;
    }
    if (buffer->truncated) {
      return TRUE;
    }
  }

  if (homeworlds_move_builder_is_complete(&builder)) {
    return TRUE;
  }

  candidates = homeworlds_move_builder_list_candidates(&builder);
  for (guint pass = 0; pass < 2; ++pass) {
    gboolean want_pass = pass == 0;

    for (gsize i = 0; i < candidates.count; ++i) {
      const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates.moves)[i];
      HomeworldsMoveBuilderState child_state = *state;
      GameBackendMoveBuilder child = {
        .builder_state = &child_state,
        .builder_state_size = sizeof(child_state),
      };
      gboolean is_pass = candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION &&
                         candidate->data.target_color == HOMEWORLDS_STEP_PASS;

      if (is_pass != want_pass) {
        continue;
      }

      if (!homeworlds_move_builder_step(&child, candidate)) {
        continue;
      }
      if (!homeworlds_view_collect_all_moves_recursive(&child_state, buffer)) {
        homeworlds_game_backend.move_list_free(&candidates);
        return FALSE;
      }
      if (buffer->truncated) {
        break;
      }
    }
    if (buffer->truncated) {
      break;
    }
  }

  homeworlds_game_backend.move_list_free(&candidates);
  return TRUE;
}

static GameBackendMoveList homeworlds_view_list_all_moves(const HomeworldsPosition *position, gboolean *out_truncated) {
  GameBackendMoveBuilder builder = {0};
  HomeworldsViewMoveBuffer buffer = {0};

  g_return_val_if_fail(position != NULL, (GameBackendMoveList){0});
  g_return_val_if_fail(out_truncated != NULL, (GameBackendMoveList){0});

  *out_truncated = FALSE;
  if (!homeworlds_move_builder_init(position, &builder)) {
    return (GameBackendMoveList){0};
  }
  if (!homeworlds_view_collect_all_moves_recursive(builder.builder_state, &buffer)) {
    homeworlds_move_builder_clear(&builder);
    g_free(buffer.moves);
    return (GameBackendMoveList){0};
  }

  homeworlds_move_builder_clear(&builder);
  *out_truncated = buffer.truncated;
  return (GameBackendMoveList){
    .moves = buffer.moves,
    .count = buffer.count,
  };
}

static gboolean homeworlds_view_move_list_contains(const GameBackendMoveList *moves, const HomeworldsMove *move) {
  g_return_val_if_fail(moves != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  for (gsize i = 0; i < moves->count; ++i) {
    const HomeworldsMove *candidate = homeworlds_game_backend.move_list_get(moves, i);

    if (candidate != NULL && homeworlds_view_moves_equal(candidate, move)) {
      return TRUE;
    }
  }

  return FALSE;
}

static void homeworlds_view_append_move_list_text(GString *text,
                                                  const GameBackendMoveList *moves,
                                                  const char *title,
                                                  const GameBackendMoveList *exclude) {
  guint displayed = 0;

  g_return_if_fail(text != NULL);
  g_return_if_fail(moves != NULL);
  g_return_if_fail(title != NULL);

  g_string_append_printf(text, "%s:\n", title);
  for (gsize i = 0; i < moves->count; ++i) {
    const HomeworldsMove *move = homeworlds_game_backend.move_list_get(moves, i);
    char notation[128] = {0};

    if (move == NULL || (exclude != NULL && homeworlds_view_move_list_contains(exclude, move))) {
      continue;
    }
    if (!homeworlds_move_format(move, notation, sizeof(notation))) {
      continue;
    }

    displayed++;
    g_string_append_printf(text, "%u. %s\n", displayed, notation);
  }

  if (displayed == 0) {
    g_string_append(text, "None\n");
  }
}

static void homeworlds_view_update_move_report(HomeworldsView *view) {
  const HomeworldsPosition *position = NULL;
  GameBackendMoveList good_moves = {0};
  GameBackendMoveList all_moves = {0};
  g_autofree char *good_title = NULL;
  g_autofree char *other_title = NULL;
  g_autoptr(GString) text = NULL;
  gboolean good_moves_truncated = FALSE;
  gboolean all_moves_truncated = FALSE;

  g_return_if_fail(view != NULL);
  g_return_if_fail(GTK_IS_LABEL(view->move_report_label));

  if (!view->move_report_enabled) {
    gtk_label_set_text(GTK_LABEL(view->move_report_label), "Move report disabled.");
    return;
  }

  position = ggame_model_peek_position(view->model);
  if (position == NULL || position->phase == HOMEWORLDS_PHASE_FINISHED) {
    gtk_label_set_text(GTK_LABEL(view->move_report_label), "No moves.");
    return;
  }
  if (position->phase != HOMEWORLDS_PHASE_PLAY) {
    gtk_label_set_text(GTK_LABEL(view->move_report_label), "Move report is available during play.");
    return;
  }

  good_moves = homeworlds_backend_list_good_moves_limited(position,
                                                          HOMEWORLDS_VIEW_MOVE_REPORT_MAX_LEAVES,
                                                          &good_moves_truncated);
  all_moves = homeworlds_view_list_all_moves(position, &all_moves_truncated);
  text = g_string_new(NULL);
  good_title = g_strdup_printf("good_moves() (%zu%s)", good_moves.count, good_moves_truncated ? "+" : "");
  other_title = g_strdup_printf("all possible moves minus good_moves() (%zu%s total before filtering)",
                                all_moves.count,
                                all_moves_truncated ? "+" : "");
  homeworlds_view_append_move_list_text(text, &good_moves, good_title, NULL);
  g_string_append_c(text, '\n');
  homeworlds_view_append_move_list_text(text, &all_moves, other_title, &good_moves);
  gtk_label_set_text(GTK_LABEL(view->move_report_label), text->str);

  homeworlds_game_backend.move_list_free(&good_moves);
  g_free(all_moves.moves);
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
  gtk_widget_add_css_class(button, selectable ? "homeworlds-bank-choice" : "flat");
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
    gtk_box_append(GTK_BOX(view->candidate_box), gtk_label_new("The game is finished."));
    return;
  }

  candidates = homeworlds_move_builder_list_candidates(&view->builder);
  if (state != NULL &&
      state->stage == HOMEWORLDS_BUILDER_STAGE_COMPLETE &&
      homeworlds_view_builder_has_catastrophe_choices(state)) {
    GtkWidget *label = gtk_label_new("Catastrophe available. Trigger one or pass.");
    GtkWidget *button = gtk_button_new_with_label("Pass");

    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
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
    GtkWidget *label = gtk_label_new(homeworlds_view_visual_choice_text(state));
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(view->candidate_box), label);
    homeworlds_view_append_cancel_button_if_available(view);
    g_clear_pointer(&candidates.moves, g_free);
    return;
  }

  if (candidates.count == 0) {
    GtkWidget *label = gtk_label_new("No legal choices from this partial selection. Reset selection.");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(view->candidate_box), label);
    homeworlds_view_append_cancel_button_if_available(view);
    g_clear_pointer(&candidates.moves, g_free);
    return;
  }

  if (state != NULL && state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP) {
    GtkWidget *label = gtk_label_new(homeworlds_view_visual_choice_text(state));
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
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
      GtkWidget *fallback = gtk_label_new("No side-panel choices are available for this step.");
      gtk_label_set_wrap(GTK_LABEL(fallback), TRUE);
      gtk_label_set_xalign(GTK_LABEL(fallback), 0.0f);
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
  homeworlds_view_update_board_content_width(view);
  homeworlds_view_update_board_bank(view);
  homeworlds_view_update_board_choice_buttons(view);
  gtk_widget_queue_draw(view->drawing_area);
}

static void homeworlds_view_model_state_changed(GGameModel * /*model*/, gpointer user_data) {
  HomeworldsView *view = user_data;

  g_return_if_fail(view != NULL);

  homeworlds_view_refresh(view);
}

static HomeworldsView *homeworlds_view_new_with_move_report(GGameModel *model, gboolean move_report_enabled) {
  HomeworldsView *view = NULL;
  GtkWidget *bank_frame = NULL;
  GtkWidget *side_panel = NULL;
  GtkWidget *side_scroller = NULL;
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

  view->board_scroller = gtk_scrolled_window_new();
  gtk_widget_set_name(view->board_scroller, "homeworlds-board-scroller");
  gtk_widget_set_hexpand(view->board_scroller, TRUE);
  gtk_widget_set_vexpand(view->board_scroller, TRUE);
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(view->board_scroller),
                                            HOMEWORLDS_VIEW_MIN_BOARD_VIEWPORT_WIDTH);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(view->board_scroller),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_box_append(GTK_BOX(view->root), view->board_scroller);

  view->board_overlay = gtk_overlay_new();
  gtk_widget_set_hexpand(view->board_overlay, TRUE);
  gtk_widget_set_vexpand(view->board_overlay, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(view->board_scroller), view->board_overlay);

  view->drawing_area = gtk_drawing_area_new();
  gtk_widget_set_name(view->drawing_area, "homeworlds-board");
  gtk_widget_set_hexpand(view->drawing_area, TRUE);
  gtk_widget_set_vexpand(view->drawing_area, TRUE);
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(view->drawing_area), 880);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(view->drawing_area), 620);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(view->drawing_area), homeworlds_view_draw, view, NULL);
  g_signal_connect(view->drawing_area, "resize", G_CALLBACK(homeworlds_view_board_resized), view);
  gtk_overlay_set_child(GTK_OVERLAY(view->board_overlay), view->drawing_area);
  view->board_hadjustment =
      g_object_ref(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(view->board_scroller)));
  view->board_hadjustment_changed_handler_id = g_signal_connect(view->board_hadjustment,
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
  gtk_widget_set_size_request(side_scroller, 280, -1);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(side_scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_box_append(GTK_BOX(view->root), side_scroller);

  side_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(side_panel, 12);
  gtk_widget_set_margin_bottom(side_panel, 12);
  gtk_widget_set_margin_start(side_panel, 12);
  gtk_widget_set_margin_end(side_panel, 12);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(side_scroller), side_panel);

  heading = gtk_label_new("Homeworlds");
  gtk_widget_add_css_class(heading, "title-2");
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
  gtk_box_append(GTK_BOX(side_panel), heading);

  view->stage_label = gtk_label_new("");
  gtk_label_set_wrap(GTK_LABEL(view->stage_label), TRUE);
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
  gtk_label_set_wrap(GTK_LABEL(view->last_move_label), TRUE);
  gtk_label_set_xalign(GTK_LABEL(view->last_move_label), 0.0f);
  gtk_box_append(GTK_BOX(side_panel), view->last_move_label);

  section = gtk_label_new("Move report");
  gtk_widget_add_css_class(section, "heading");
  gtk_label_set_xalign(GTK_LABEL(section), 0.0f);
  gtk_box_append(GTK_BOX(side_panel), section);

  view->move_report_label = gtk_label_new("");
  gtk_widget_set_name(view->move_report_label, "homeworlds-move-report");
  gtk_label_set_selectable(GTK_LABEL(view->move_report_label), TRUE);
  gtk_label_set_wrap(GTK_LABEL(view->move_report_label), TRUE);
  gtk_label_set_xalign(GTK_LABEL(view->move_report_label), 0.0f);
  gtk_box_append(GTK_BOX(side_panel), view->move_report_label);

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

  if (view->model != NULL && view->model_state_changed_handler_id != 0) {
    g_signal_handler_disconnect(view->model, view->model_state_changed_handler_id);
    view->model_state_changed_handler_id = 0;
  }
  if (view->board_hadjustment != NULL && view->board_hadjustment_changed_handler_id != 0) {
    g_signal_handler_disconnect(view->board_hadjustment, view->board_hadjustment_changed_handler_id);
    view->board_hadjustment_changed_handler_id = 0;
  }
  g_clear_object(&view->board_hadjustment);
  if (view->builder_ready) {
    homeworlds_move_builder_clear(&view->builder);
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
  homeworlds_view_update_board_content_width(view);
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

void homeworlds_view_sync_board_host_node(GtkWidget *board_host, const SgfNode *node) {
  HomeworldsView *view = NULL;
  HomeworldsMove move = {0};
  SgfColor color = SGF_COLOR_NONE;
  gboolean has_move = FALSE;
  char formatted[128] = {0};

  g_return_if_fail(GTK_IS_WIDGET(board_host));
  g_return_if_fail(node != NULL);

  view = g_object_get_data(G_OBJECT(board_host), "homeworlds-view-state");
  if (view == NULL) {
    return;
  }

  if (!sgf_move_props_try_parse_node(node, &color, &move, &has_move, NULL) || !has_move) {
    gtk_label_set_text(GTK_LABEL(view->last_move_label), "None");
    return;
  }

  if (homeworlds_move_format(&move, formatted, sizeof(formatted))) {
    gtk_label_set_text(GTK_LABEL(view->last_move_label), formatted);
  } else {
    gtk_label_set_text(GTK_LABEL(view->last_move_label), "Unknown");
  }
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
