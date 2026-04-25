#ifndef GGAME_FILE_DIALOG_HISTORY_H
#define GGAME_FILE_DIALOG_HISTORY_H

#include <gio/gio.h>

G_BEGIN_DECLS

GSettings *ggame_file_dialog_history_create_settings(void);
GFile *ggame_file_dialog_history_get_initial_folder(GSettings *settings, const char *key);
gboolean ggame_file_dialog_history_remember_parent(GSettings *settings, const char *key, GFile *file);

G_END_DECLS

#endif
