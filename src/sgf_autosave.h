#ifndef SGF_AUTOSAVE_H
#define SGF_AUTOSAVE_H

#include "sgf_tree.h"

#include <glib.h>

G_BEGIN_DECLS

#define SGF_AUTOSAVE_ENV "GCHECKERS_AUTOSAVE_DIR"

char *sgf_autosave_format_current_timestamp(void);
char *sgf_autosave_build_available_path(const char *game_id,
                                        const char *game_started_at,
                                        const char *move_at,
                                        GError **error);
gboolean sgf_autosave_save_tree(const char *game_id,
                                const char *game_started_at,
                                SgfTree *tree,
                                GError **error);

G_END_DECLS

#endif
