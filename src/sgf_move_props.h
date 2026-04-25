#ifndef SGF_MOVE_PROPS_H
#define SGF_MOVE_PROPS_H

#include "games/checkers/game.h"
#include "sgf_tree.h"

#include <glib.h>

G_BEGIN_DECLS

gboolean sgf_move_props_parse_notation(const char *notation, CheckersMove *out_move, GError **error);
gboolean sgf_move_props_format_notation(const CheckersMove *move, char *buffer, size_t size, GError **error);
gboolean sgf_move_props_try_parse_node(const SgfNode *node,
                                       SgfColor *out_color,
                                       CheckersMove *out_move,
                                       gboolean *out_has_move,
                                       GError **error);
gboolean sgf_move_props_parse_node(const SgfNode *node, SgfColor *out_color, CheckersMove *out_move, GError **error);
gboolean sgf_move_props_set_move(SgfNode *node, SgfColor color, const CheckersMove *move, GError **error);

G_END_DECLS

#endif
