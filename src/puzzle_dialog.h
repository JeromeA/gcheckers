#ifndef GCHECKERS_PUZZLE_DIALOG_H
#define GCHECKERS_PUZZLE_DIALOG_H

#include "puzzle_progress.h"
#include "ruleset.h"

#include <gtk/gtk.h>

typedef void (*GCheckersPuzzleDialogDoneFunc)(gboolean selected,
                                              PlayerRuleset ruleset,
                                              const char *path,
                                              gpointer user_data);

void gcheckers_puzzle_dialog_present(GtkWindow *parent,
                                     PlayerRuleset initial_ruleset,
                                     CheckersPuzzleProgressStore *store,
                                     GCheckersPuzzleDialogDoneFunc done_func,
                                     gpointer user_data,
                                     GDestroyNotify user_data_destroy);

#endif
