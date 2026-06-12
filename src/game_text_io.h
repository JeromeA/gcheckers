#ifndef GAME_TEXT_IO_H
#define GAME_TEXT_IO_H

#include "game_backend.h"
#include "sgf_tree.h"

#include <glib.h>

G_BEGIN_DECLS

gboolean ggame_text_game_io_backend_supports_path(const GameBackend *backend, const char *path);
gboolean ggame_text_game_io_load_data(const GameBackend *backend,
                                      const GameBackendVariant *variant_or_null,
                                      const char *content,
                                      SgfTree **out_tree,
                                      GError **error);
gboolean ggame_text_game_io_load_file(const GameBackend *backend,
                                      const GameBackendVariant *variant_or_null,
                                      const char *path,
                                      SgfTree **out_tree,
                                      GError **error);
char *ggame_text_game_io_save_data(const GameBackend *backend,
                                   const GameBackendVariant *variant_or_null,
                                   SgfTree *tree,
                                   GError **error);
gboolean ggame_text_game_io_save_file(const GameBackend *backend,
                                      const GameBackendVariant *variant_or_null,
                                      const char *path,
                                      SgfTree *tree,
                                      GError **error);

G_END_DECLS

#endif
