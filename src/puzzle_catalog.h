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
GPtrArray *game_puzzle_catalog_load_variant(const GameBackend *backend,
                                            const GameBackendVariant *variant,
                                            GError **error);

G_END_DECLS

#endif
