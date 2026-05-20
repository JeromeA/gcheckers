#ifndef HOMEWORLDS_MOVE_REPORT_H
#define HOMEWORLDS_MOVE_REPORT_H

#include "homeworlds_types.h"

#include <glib.h>

G_BEGIN_DECLS

char *homeworlds_move_report_format(const HomeworldsPosition *position);

G_END_DECLS

#endif
