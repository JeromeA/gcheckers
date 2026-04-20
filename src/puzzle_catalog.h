#ifndef GCHECKERS_PUZZLE_CATALOG_H
#define GCHECKERS_PUZZLE_CATALOG_H

#include "ruleset.h"

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
  guint puzzle_number;
  char *basename;
  char *path;
  char *puzzle_id;
} CheckersPuzzleCatalogEntry;

void checkers_puzzle_catalog_entry_free(CheckersPuzzleCatalogEntry *entry);
GPtrArray *checkers_puzzle_catalog_load_for_ruleset(PlayerRuleset ruleset, GError **error);

G_END_DECLS

#endif
