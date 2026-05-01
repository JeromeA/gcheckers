#ifndef CHECKERS_SGF_POSITION_H
#define CHECKERS_SGF_POSITION_H

#include "../../game_backend.h"
#include "../../sgf_tree.h"

#include <glib.h>

G_BEGIN_DECLS

gboolean checkers_sgf_position_apply_setup_node(gpointer position, const SgfNode *node, GError **error);
gboolean checkers_sgf_position_write_position_node(gconstpointer position, SgfNode *node, GError **error);

G_END_DECLS

#endif
