#ifndef SGF_IO_H
#define SGF_IO_H

#include "game_backend.h"
#include "sgf_tree.h"

#include <glib.h>

G_BEGIN_DECLS

gboolean sgf_io_load_file(const char *path, SgfTree **out_tree, GError **error);
gboolean sgf_io_save_file(const char *path, SgfTree *tree, GError **error);
gboolean sgf_io_load_data(const char *content, SgfTree **out_tree, GError **error);
char *sgf_io_save_data(SgfTree *tree, GError **error);
gboolean sgf_io_tree_get_variant(const SgfTree *tree, const GameBackendVariant **out_variant, GError **error);
gboolean sgf_io_tree_set_variant(SgfTree *tree, const GameBackendVariant *variant);

G_END_DECLS

#endif
