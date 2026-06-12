#ifndef HOMEWORLDS_MOVE_REPORT_H
#define HOMEWORLDS_MOVE_REPORT_H

#include "homeworlds_types.h"

#include <glib.h>
#include <stdio.h>

G_BEGIN_DECLS

char *homeworlds_move_report_format(const HomeworldsPosition *position);
gboolean homeworlds_move_report_write(FILE *file,
                                      const HomeworldsPosition *position,
                                      const GArray *played_moves,
                                      gsize *out_all_move_count);

G_END_DECLS

#endif
