#ifndef GGAME_SGF_CONTROLLER_H
#define GGAME_SGF_CONTROLLER_H

#include "board_view.h"
#include "game_model.h"
#include "games/checkers/checkers_model.h"
#include "sgf_tree.h"
#include "sgf_view.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GGAME_TYPE_SGF_CONTROLLER (ggame_sgf_controller_get_type())

G_DECLARE_FINAL_TYPE(GGameSgfController,
                     ggame_sgf_controller,
                     GGAME,
                     SGF_CONTROLLER,
                     GObject)

GGameSgfController *ggame_sgf_controller_new(BoardView *board_view);
void ggame_sgf_controller_set_model(GGameSgfController *self, GCheckersModel *model);
void ggame_sgf_controller_set_game_model(GGameSgfController *self, GGameModel *model);
void ggame_sgf_controller_new_game(GGameSgfController *self);
gboolean ggame_sgf_controller_replay_node_into_game(const SgfNode *node, Game *game, GError **error);
gboolean ggame_sgf_controller_replay_node_into_position(const SgfNode *node,
                                                        const GameBackend *backend,
                                                        gpointer position,
                                                        GError **error);
gboolean ggame_sgf_controller_apply_move(GGameSgfController *self, gconstpointer move);
gboolean ggame_sgf_controller_step_ai_move(GGameSgfController *self, guint depth, gpointer out_move);
gboolean ggame_sgf_controller_rewind_to_start(GGameSgfController *self);
gboolean ggame_sgf_controller_step_backward(GGameSgfController *self);
gboolean ggame_sgf_controller_step_forward(GGameSgfController *self);
gboolean ggame_sgf_controller_step_forward_to_branch(GGameSgfController *self);
gboolean ggame_sgf_controller_step_forward_to_end(GGameSgfController *self);
gboolean ggame_sgf_controller_select_node(GGameSgfController *self, const SgfNode *node);
gboolean ggame_sgf_controller_refresh_current_node(GGameSgfController *self);
gboolean ggame_sgf_controller_get_current_node_move(GGameSgfController *self, gpointer out_move);
gboolean ggame_sgf_controller_load_file(GGameSgfController *self, const char *path, GError **error);
gboolean ggame_sgf_controller_save_file(GGameSgfController *self, const char *path, GError **error);
gboolean ggame_sgf_controller_save_position_file(GGameSgfController *self, const char *path, GError **error);
GtkWidget *ggame_sgf_controller_get_widget(GGameSgfController *self);
SgfTree *ggame_sgf_controller_get_tree(GGameSgfController *self);
SgfView *ggame_sgf_controller_get_view(GGameSgfController *self);
gboolean ggame_sgf_controller_is_replaying(GGameSgfController *self);
void ggame_sgf_controller_force_layout_resync(GGameSgfController *self);

G_END_DECLS

#endif
