#ifndef PUZZLE_CATALOG_H
#define PUZZLE_CATALOG_H

#include "game_backend.h"

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
  guint puzzle_number;
  char *basename;
  char *path;
  char *puzzle_id;
} GamePuzzleCatalogEntry;

void game_puzzle_catalog_entry_free(GamePuzzleCatalogEntry *entry);
gboolean game_puzzle_catalog_name_is_puzzle_sgf(const char *name);
gboolean game_puzzle_catalog_parse_basename(const char *basename, guint *out_number);
gboolean game_puzzle_catalog_find_next_index(const char *dir_path, guint *out_next_index, GError **error);
char *game_puzzle_catalog_build_indexed_path(const char *dir_path, const char *prefix, guint index);
GPtrArray *game_puzzle_catalog_load_variant(const GameBackend *backend,
                                            const GameBackendVariant *variant,
                                            GError **error);

G_END_DECLS

#endif
