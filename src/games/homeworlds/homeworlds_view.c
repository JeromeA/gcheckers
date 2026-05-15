#include "homeworlds_view.h"

#include "homeworlds_game.h"
#include "homeworlds_move_builder.h"
#include "homeworlds_random_ai.h"

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

#define HOMEWORLDS_VIEW_HOMEWORLD_BOX_WIDTH 190.0
#define HOMEWORLDS_VIEW_HOMEWORLD_BOX_HEIGHT 140.0
#define HOMEWORLDS_VIEW_SYSTEM_BOX_WIDTH 150.0
#define HOMEWORLDS_VIEW_SYSTEM_BOX_HEIGHT 118.0
#define HOMEWORLDS_VIEW_HOMEWORLD_STAR_OFFSET 22.0
#define HOMEWORLDS_VIEW_HOMEWORLD_SHIP_OFFSET 72.0
#define HOMEWORLDS_VIEW_HOMEWORLD_PIECE_Y_OFFSET 12.0
#define HOMEWORLDS_VIEW_PIECE_SIZE_RATIO 1.45
#define HOMEWORLDS_VIEW_SMALL_PYRAMID_HEIGHT 36.0
#define HOMEWORLDS_VIEW_SMALL_STAR_SIDE 20.0
#define HOMEWORLDS_VIEW_PYRAMID_HEIGHT_TO_BASE 2.0
#define HOMEWORLDS_VIEW_PIP_REFERENCE_RATIO 0.055

struct _HomeworldsView {
  GGameModel *model;
  GtkWidget *root;
  GtkWidget *board_overlay;
  GtkWidget *drawing_area;
  GtkWidget *board_bank_box;
  GtkWidget *stage_label;
  GtkWidget *candidate_box;
  GtkWidget *catastrophe_box;
  GtkWidget *last_move_label;
  GameBackendMoveBuilder builder;
  gboolean builder_ready;
  HomeworldsViewMoveAppliedFunc move_applied;
  gpointer move_applied_data;
};

static void homeworlds_view_update_from_current_builder(HomeworldsView *view);
static void homeworlds_view_update_board_bank(HomeworldsView *view);

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
      state->stage != HOMEWORLDS_BUILDER_STAGE_COMPLETE) {
    return &state->working_position;
  }

  return ggame_model_peek_position(view->model);
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
      return "pass";
    case HOMEWORLDS_STEP_CONSTRUCT:
      return "construct";
    case HOMEWORLDS_STEP_TRADE:
      return "trade";
    case HOMEWORLDS_STEP_ATTACK:
      return "attack";
    case HOMEWORLDS_STEP_MOVE:
      return "move/discover";
    case HOMEWORLDS_STEP_SACRIFICE:
      return "sacrifice";
    case HOMEWORLDS_STEP_CATASTROPHE:
      return "catastrophe";
    case HOMEWORLDS_STEP_DISCOVER:
      return "discover";
    case HOMEWORLDS_STEP_NONE:
    default:
      return "action";
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
      return g_strdup_printf("Action: %s", homeworlds_view_action_name(candidate->data.target_color));
    case HOMEWORLDS_CANDIDATE_TRADE_COLOR:
      return g_strdup_printf("Trade to %s", homeworlds_view_color_name(candidate->data.target_color));
    case HOMEWORLDS_CANDIDATE_ATTACK_TARGET:
      return g_strdup_printf("Attack player %u ship slot %u",
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

static gboolean homeworlds_view_stage_uses_board_bank(const HomeworldsView *view) {
  const HomeworldsMoveBuilderState *state = homeworlds_view_builder_state(view);

  if (state == NULL) {
    return FALSE;
  }

  switch ((HomeworldsBuilderStage) state->stage) {
    case HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP:
      return TRUE;
    case HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_TRADE_COLOR:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ATTACK_TARGET:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_MOVE_TARGET:
    case HOMEWORLDS_BUILDER_STAGE_COMPLETE:
    default:
      return FALSE;
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
    double offset = ((double) i - (((double) visible_count - 1.0) / 2.0)) * 3.0;
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

static guint homeworlds_view_system_ship_count(const HomeworldsSystem *system, guint side) {
  guint count = 0;

  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(side < 2, 0);

  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    if (homeworlds_pyramid_is_valid(system->ships[side][slot])) {
      count++;
    }
  }

  return count;
}

static void homeworlds_view_system_center(guint system_index,
                                          double width,
                                          double height,
                                          double *out_x,
                                          double *out_y) {
  g_return_if_fail(out_x != NULL);
  g_return_if_fail(out_y != NULL);

  if (system_index == 0) {
    *out_x = width * 0.22;
    *out_y = height * 0.72;
    return;
  }
  if (system_index == 1) {
    *out_x = width * 0.22;
    *out_y = height * 0.28;
    return;
  }

  guint offset = system_index - 2;
  guint col = offset % 4;
  guint row = offset / 4;

  *out_x = width * (0.45 + ((double) col * 0.13));
  *out_y = height * (0.20 + ((double) row * 0.18));
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
  double box_width = system_index < 2 ? HOMEWORLDS_VIEW_HOMEWORLD_BOX_WIDTH : HOMEWORLDS_VIEW_SYSTEM_BOX_WIDTH;
  double box_height = system_index < 2 ? HOMEWORLDS_VIEW_HOMEWORLD_BOX_HEIGHT : HOMEWORLDS_VIEW_SYSTEM_BOX_HEIGHT;
  double x = center_x - (box_width / 2.0);
  double y = center_y - (box_height / 2.0);
  HomeworldsViewHomeworldLayout homeworld_layout = {0};
  gboolean has_homeworld_layout = FALSE;
  guint stars_drawn = 0;

  g_return_if_fail(system != NULL);

  has_homeworld_layout = homeworlds_view_calculate_homeworld_layout(system_index,
                                                                    center_x,
                                                                    center_y,
                                                                    &homeworld_layout);

  cairo_save(cr);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + box_width - 16.0, y + 16.0, 16.0, -G_PI / 2.0, 0.0);
  cairo_arc(cr, x + box_width - 16.0, y + box_height - 16.0, 16.0, 0.0, G_PI / 2.0);
  cairo_arc(cr, x + 16.0, y + box_height - 16.0, 16.0, G_PI / 2.0, G_PI);
  cairo_arc(cr, x + 16.0, y + 16.0, 16.0, G_PI, G_PI * 1.5);
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

  for (guint star_slot = 0; star_slot < HOMEWORLDS_STAR_SLOT_COUNT; ++star_slot) {
    HomeworldsPyramid star = system->stars[star_slot];
    if (!homeworlds_pyramid_is_valid(star)) {
      continue;
    }

    HomeworldsSize size = homeworlds_pyramid_size(star);
    double star_x = has_homeworld_layout ? homeworld_layout.star_x[stars_drawn]
                                         : center_x - 22.0 + ((double) stars_drawn * 44.0);
    double star_y = has_homeworld_layout ? homeworld_layout.star_y : y + 48.0;
    homeworlds_view_draw_star(cr, star_x, star_y, size, homeworlds_pyramid_color(star));
    stars_drawn++;
  }

  for (guint side = 0; side < 2; ++side) {
    guint count = homeworlds_view_system_ship_count(system, side);
    guint drawn = 0;
    gboolean use_homeworld_ship_layout = has_homeworld_layout && side == system_index;
    gboolean ship_points_up = use_homeworld_ship_layout ? homeworld_layout.ship_points_up : side == 0;
    double ship_row_center_x = use_homeworld_ship_layout ? homeworld_layout.ship_x : center_x;
    double ship_y = use_homeworld_ship_layout ? homeworld_layout.ship_y
                                              : (side == 0 ? y + box_height - 42.0 : y + 70.0);

    if (count == 0) {
      continue;
    }

    for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
      HomeworldsPyramid ship = system->ships[side][slot];
      if (!homeworlds_pyramid_is_valid(ship)) {
        continue;
      }

      HomeworldsSize size = homeworlds_pyramid_size(ship);
      double step = count > 1 ? MIN(22.0, (box_width - 42.0) / ((double) count - 1.0)) : 0.0;
      double ship_x = ship_row_center_x - (((double) count - 1.0) * step / 2.0) + ((double) drawn * step);

      homeworlds_view_draw_pyramid(cr, ship_x, ship_y, size, ship_points_up, homeworlds_pyramid_color(ship));
      drawn++;
    }
  }

  cairo_restore(cr);
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

  cairo_set_source_rgba(cr, 0.65, 0.72, 0.86, 0.20);
  cairo_set_line_width(cr, 1.0);
  for (guint left = 0; left < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++left) {
    if (homeworlds_system_is_empty(&position->systems[left])) {
      continue;
    }
    for (guint right = left + 1; right < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++right) {
      if (homeworlds_system_is_empty(&position->systems[right]) ||
          !homeworlds_system_is_connected(&position->systems[left], &position->systems[right])) {
        continue;
      }

      double left_x = 0.0;
      double left_y = 0.0;
      double right_x = 0.0;
      double right_y = 0.0;
      homeworlds_view_system_center(left, width, height, &left_x, &left_y);
      homeworlds_view_system_center(right, width, height, &right_x, &right_y);
      cairo_move_to(cr, left_x, left_y);
      cairo_line_to(cr, right_x, right_y);
      cairo_stroke(cr);
    }
  }

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    if (system_index >= 2 && homeworlds_system_is_empty(&position->systems[system_index])) {
      continue;
    }

    double center_x = 0.0;
    double center_y = 0.0;
    gboolean selected = state != NULL && state->selected_system_index == system_index;

    homeworlds_view_system_center(system_index, width, height, &center_x, &center_y);
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

static gboolean homeworlds_view_complete_move_if_ready(HomeworldsView *view) {
  HomeworldsMove move = {0};
  char formatted[128] = {0};

  g_return_val_if_fail(view != NULL, FALSE);

  if (!view->builder_ready || !homeworlds_move_builder_is_complete(&view->builder)) {
    return TRUE;
  }
  if (!homeworlds_move_builder_build_move(&view->builder, &move)) {
    g_debug("Completed Homeworlds builder did not produce a move");
    return FALSE;
  }
  if (!ggame_model_apply_move(view->model, &move)) {
    g_debug("Homeworlds model rejected the completed move");
    return FALSE;
  }

  if (homeworlds_move_format(&move, formatted, sizeof(formatted))) {
    gtk_label_set_text(GTK_LABEL(view->last_move_label), formatted);
  }
  if (view->move_applied != NULL) {
    view->move_applied(view, view->move_applied_data);
  }
  return TRUE;
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
    homeworlds_view_refresh(view);
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

static void homeworlds_view_catastrophe_clicked(GtkButton *button, gpointer user_data) {
  HomeworldsView *view = user_data;
  const HomeworldsPosition *position = NULL;
  HomeworldsPosition next = {0};
  guint system_index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "homeworlds-system-index"));
  guint color = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "homeworlds-color"));

  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(view != NULL);

  if (homeworlds_view_has_partial_selection(view)) {
    g_debug("Resolve or reset the partial Homeworlds move before applying a catastrophe");
    return;
  }

  position = ggame_model_peek_position(view->model);
  g_return_if_fail(position != NULL);
  next = *position;
  if (!homeworlds_position_apply_catastrophe(&next, system_index, (HomeworldsColor) color)) {
    g_debug("Homeworlds catastrophe button did not match a legal catastrophe");
    return;
  }

  if (!ggame_model_set_position(view->model, &next)) {
    g_debug("Unable to update model after Homeworlds catastrophe");
    return;
  }
  gtk_label_set_text(GTK_LABEL(view->last_move_label), "catastrophe");
  if (view->move_applied != NULL) {
    view->move_applied(view, view->move_applied_data);
  }
  homeworlds_view_refresh(view);
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

  position = ggame_model_peek_position(view->model);
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
      g_object_set_data(G_OBJECT(button), "homeworlds-system-index", GUINT_TO_POINTER(system_index));
      g_object_set_data(G_OBJECT(button), "homeworlds-color", GUINT_TO_POINTER(color));
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
  GameBackendMoveList candidates = {0};
  gboolean found = FALSE;

  g_return_val_if_fail(view != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);
  g_return_val_if_fail(out_candidate != NULL, FALSE);

  if (!homeworlds_view_stage_uses_board_bank(view) || !view->builder_ready) {
    return FALSE;
  }

  candidates = homeworlds_move_builder_list_candidates(&view->builder);
  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates.moves)[i];
    if (candidate->data.pyramid != pyramid) {
      continue;
    }
    if (candidate->data.kind != HOMEWORLDS_CANDIDATE_SETUP_STAR &&
        candidate->data.kind != HOMEWORLDS_CANDIDATE_SETUP_SHIP) {
      continue;
    }

    *out_candidate = *candidate;
    found = TRUE;
    break;
  }

  g_clear_pointer(&candidates.moves, g_free);
  return found;
}

static GtkWidget *homeworlds_view_create_bank_button(HomeworldsView *view, HomeworldsPyramid pyramid, guint count) {
  HomeworldsMoveCandidate candidate = {0};
  HomeworldsBankButtonIcon *icon_data = NULL;
  GtkWidget *button = NULL;
  GtkWidget *icon = NULL;
  GtkWidget *overlay = NULL;
  GtkWidget *count_label = NULL;
  char *tooltip = NULL;
  char *count_text = NULL;
  gboolean selectable = FALSE;

  g_return_val_if_fail(view != NULL, NULL);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), NULL);
  g_return_val_if_fail(count > 0, NULL);

  selectable = homeworlds_view_find_bank_candidate(view, pyramid, &candidate);
  button = gtk_button_new();
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_size_request(button, 68, 92);
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
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(icon), 58);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(icon), 86);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon), homeworlds_view_draw_bank_button_icon, icon_data, g_free);

  overlay = gtk_overlay_new();
  gtk_overlay_set_child(GTK_OVERLAY(overlay), icon);
  count_text = g_strdup_printf("x%u", count);
  count_label = gtk_label_new(count_text);
  g_free(count_text);
  gtk_widget_add_css_class(count_label, "caption");
  gtk_widget_set_halign(count_label, GTK_ALIGN_END);
  gtk_widget_set_valign(count_label, GTK_ALIGN_END);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), count_label);
  gtk_button_set_child(GTK_BUTTON(button), overlay);

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
  GtkWidget *grid = NULL;
  guint counts[4][4] = {{0}};

  g_return_if_fail(view != NULL);

  homeworlds_view_clear_box(view->board_bank_box);
  position = ggame_model_peek_position(view->model);
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

  grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
  gtk_box_append(GTK_BOX(view->board_bank_box), grid);

  for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
    GtkWidget *label = gtk_label_new(homeworlds_view_color_styles[color].short_name);
    gtk_widget_set_size_request(label, 22, -1);
    gtk_grid_attach(GTK_GRID(grid), label, 0, (int) color, 1, 1);

    for (HomeworldsSize size = HOMEWORLDS_SIZE_SMALL; size <= HOMEWORLDS_SIZE_LARGE; size++) {
      HomeworldsPyramid pyramid = homeworlds_pyramid_make((HomeworldsColor) color, size);
      if (counts[color][size] == 0) {
        continue;
      }

      GtkWidget *button = homeworlds_view_create_bank_button(view, pyramid, counts[color][size]);
      gtk_grid_attach(GTK_GRID(grid), button, (int) size, (int) color, 1, 1);
    }
  }
}

static void homeworlds_view_update_candidates(HomeworldsView *view) {
  GameBackendMoveList candidates = {0};

  g_return_if_fail(view != NULL);

  homeworlds_view_clear_box(view->candidate_box);
  gtk_label_set_text(GTK_LABEL(view->stage_label), homeworlds_view_stage_text(view));

  if (!view->builder_ready) {
    gtk_box_append(GTK_BOX(view->candidate_box), gtk_label_new("The game is finished."));
    return;
  }

  candidates = homeworlds_move_builder_list_candidates(&view->builder);
  if (homeworlds_view_stage_uses_board_bank(view)) {
    GtkWidget *label = gtk_label_new("Click a highlighted pyramid in the bank on the board.");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(view->candidate_box), label);
    g_clear_pointer(&candidates.moves, g_free);
    return;
  }

  if (candidates.count == 0) {
    GtkWidget *label = gtk_label_new("No legal choices from this partial selection. Reset selection.");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(view->candidate_box), label);
    g_clear_pointer(&candidates.moves, g_free);
    return;
  }

  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates.moves)[i];
    homeworlds_view_append_candidate_button(view, candidate);
  }
  g_clear_pointer(&candidates.moves, g_free);
}

static void homeworlds_view_update_from_current_builder(HomeworldsView *view) {
  g_return_if_fail(view != NULL);

  homeworlds_view_update_candidates(view);
  homeworlds_view_update_catastrophes(view);
  homeworlds_view_update_board_bank(view);
  gtk_widget_queue_draw(view->drawing_area);
}

HomeworldsView *homeworlds_view_new(GGameModel *model) {
  HomeworldsView *view = NULL;
  GtkWidget *bank_frame = NULL;
  GtkWidget *side_panel = NULL;
  GtkWidget *scroller = NULL;
  GtkWidget *heading = NULL;
  GtkWidget *section = NULL;

  g_return_val_if_fail(GGAME_IS_MODEL(model), NULL);

  view = g_new0(HomeworldsView, 1);
  g_return_val_if_fail(view != NULL, NULL);
  view->model = g_object_ref(model);

  view->root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_widget_set_hexpand(view->root, TRUE);
  gtk_widget_set_vexpand(view->root, TRUE);
  gtk_widget_set_name(view->root, "homeworlds-view");

  view->board_overlay = gtk_overlay_new();
  gtk_widget_set_hexpand(view->board_overlay, TRUE);
  gtk_widget_set_vexpand(view->board_overlay, TRUE);
  gtk_box_append(GTK_BOX(view->root), view->board_overlay);

  view->drawing_area = gtk_drawing_area_new();
  gtk_widget_set_name(view->drawing_area, "homeworlds-board");
  gtk_widget_set_hexpand(view->drawing_area, TRUE);
  gtk_widget_set_vexpand(view->drawing_area, TRUE);
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(view->drawing_area), 880);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(view->drawing_area), 620);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(view->drawing_area), homeworlds_view_draw, view, NULL);
  gtk_overlay_set_child(GTK_OVERLAY(view->board_overlay), view->drawing_area);

  bank_frame = gtk_frame_new("Bank");
  gtk_widget_set_name(bank_frame, "homeworlds-board-bank");
  gtk_widget_set_halign(bank_frame, GTK_ALIGN_END);
  gtk_widget_set_valign(bank_frame, GTK_ALIGN_START);
  gtk_widget_set_margin_top(bank_frame, 18);
  gtk_widget_set_margin_end(bank_frame, 18);
  gtk_overlay_add_overlay(GTK_OVERLAY(view->board_overlay), bank_frame);

  view->board_bank_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_margin_top(view->board_bank_box, 8);
  gtk_widget_set_margin_bottom(view->board_bank_box, 8);
  gtk_widget_set_margin_start(view->board_bank_box, 8);
  gtk_widget_set_margin_end(view->board_bank_box, 8);
  gtk_frame_set_child(GTK_FRAME(bank_frame), view->board_bank_box);

  scroller = gtk_scrolled_window_new();
  gtk_widget_set_size_request(scroller, 280, -1);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_box_append(GTK_BOX(view->root), scroller);

  side_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(side_panel, 12);
  gtk_widget_set_margin_bottom(side_panel, 12);
  gtk_widget_set_margin_start(side_panel, 12);
  gtk_widget_set_margin_end(side_panel, 12);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), side_panel);

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

  homeworlds_view_refresh(view);
  return view;
}

void homeworlds_view_free(HomeworldsView *view) {
  if (view == NULL) {
    return;
  }

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
  homeworlds_view_update_board_bank(view);
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

gboolean homeworlds_view_apply_random_move(HomeworldsView *view) {
  const HomeworldsPosition *position = NULL;
  HomeworldsMove move = {0};
  char formatted[128] = {0};

  g_return_val_if_fail(view != NULL, FALSE);

  if (homeworlds_view_has_partial_selection(view)) {
    g_debug("Random Homeworlds move refused while a partial selection is active");
    return FALSE;
  }

  position = ggame_model_peek_position(view->model);
  g_return_val_if_fail(position != NULL, FALSE);
  if (position->phase == HOMEWORLDS_PHASE_FINISHED) {
    return FALSE;
  }
  if (!homeworlds_random_ai_build_move(position, NULL, &move)) {
    g_debug("Homeworlds random AI could not build a legal move");
    return FALSE;
  }
  if (!ggame_model_apply_move(view->model, &move)) {
    g_debug("Homeworlds model rejected random AI move");
    return FALSE;
  }

  if (homeworlds_move_format(&move, formatted, sizeof(formatted))) {
    gtk_label_set_text(GTK_LABEL(view->last_move_label), formatted);
  }
  if (view->move_applied != NULL) {
    view->move_applied(view, view->move_applied_data);
  }
  homeworlds_view_refresh(view);
  return TRUE;
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
    homeworlds_view_refresh(view);
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

void homeworlds_view_set_move_applied_callback(HomeworldsView *view,
                                               HomeworldsViewMoveAppliedFunc func,
                                               gpointer user_data) {
  g_return_if_fail(view != NULL);

  view->move_applied = func;
  view->move_applied_data = user_data;
}
