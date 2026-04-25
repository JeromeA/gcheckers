#ifndef GGAME_PUZZLE_DIALOG_H
#define GGAME_PUZZLE_DIALOG_H

#include "game_backend.h"
#include "puzzle_progress.h"

#include <gtk/gtk.h>

typedef void (*GGamePuzzleDialogDoneFunc)(gboolean selected,
                                          const GameBackendVariant *variant,
                                          const char *path,
                                          gpointer user_data);

void ggame_puzzle_dialog_present(GtkWindow *parent,
                                     const GameBackendVariant *initial_variant,
                                     GGamePuzzleProgressStore *store,
                                     GGamePuzzleDialogDoneFunc done_func,
                                     gpointer user_data,
                                     GDestroyNotify user_data_destroy);

#endif
