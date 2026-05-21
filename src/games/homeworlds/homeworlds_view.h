#ifndef HOMEWORLDS_VIEW_H
#define HOMEWORLDS_VIEW_H

#include "../../game_app_profile.h"
#include "../../game_model.h"
#include "homeworlds_types.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _HomeworldsView HomeworldsView;
typedef void (*HomeworldsViewMoveAppliedFunc)(HomeworldsView *view, gpointer user_data);
typedef gboolean (*HomeworldsViewMoveHandler)(gconstpointer move, gpointer user_data);

typedef struct {
  double star_x[HOMEWORLDS_STAR_SLOT_COUNT];
  double star_y;
  double ship_x;
  double ship_y;
  gboolean ship_points_up;
} HomeworldsViewHomeworldLayout;

typedef struct {
  double base;
  double height;
} HomeworldsViewPyramidMetrics;

HomeworldsView *homeworlds_view_new(GGameModel *model);
void homeworlds_view_free(HomeworldsView *view);
GtkWidget *homeworlds_view_create_board_host(GGameModel *model,
                                             BoardView *board_view,
                                             GGameAppMoveHandler move_handler,
                                             gpointer move_handler_data,
                                             const GGameAppBoardHostOptions *options);
void homeworlds_view_sync_board_host_node(GtkWidget *board_host, const SgfNode *node);

GtkWidget *homeworlds_view_get_widget(HomeworldsView *view);
void homeworlds_view_refresh(HomeworldsView *view);
void homeworlds_view_reset_selection(HomeworldsView *view);
gboolean homeworlds_view_has_partial_selection(const HomeworldsView *view);
void homeworlds_view_set_move_report_enabled(HomeworldsView *view, gboolean enabled);
gboolean homeworlds_view_get_move_report_enabled(const HomeworldsView *view);
void homeworlds_view_set_board_host_move_report_enabled(GtkWidget *board_host, gboolean enabled);
gboolean homeworlds_view_apply_candidate_at(HomeworldsView *view, gsize index);
gsize homeworlds_view_get_candidate_count(const HomeworldsView *view);
const char *homeworlds_view_get_last_move_text(const HomeworldsView *view);

void homeworlds_view_set_move_applied_callback(HomeworldsView *view,
                                               HomeworldsViewMoveAppliedFunc func,
                                               gpointer user_data);
void homeworlds_view_set_move_handler(HomeworldsView *view,
                                      HomeworldsViewMoveHandler handler,
                                      gpointer user_data);
gboolean homeworlds_view_calculate_homeworld_layout(guint system_index,
                                                    double center_x,
                                                    double center_y,
                                                    HomeworldsViewHomeworldLayout *out_layout);
gboolean homeworlds_view_calculate_system_center(const HomeworldsPosition *position,
                                                 guint system_index,
                                                 double width,
                                                 double height,
                                                 double *out_x,
                                                 double *out_y);
gboolean homeworlds_view_calculate_board_content_size(const HomeworldsPosition *position,
                                                      double viewport_width,
                                                      double viewport_height,
                                                      int *out_width,
                                                      int *out_height);
double homeworlds_view_calculate_board_content_width(const HomeworldsPosition *position, double viewport_width);
gboolean homeworlds_view_pyramid_metrics(HomeworldsSize size, HomeworldsViewPyramidMetrics *out_metrics);
double homeworlds_view_pip_radius(void);
double homeworlds_view_star_side(HomeworldsSize size);

G_END_DECLS

#endif
