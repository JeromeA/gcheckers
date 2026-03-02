#ifndef POSITION_FORMAT_H
#define POSITION_FORMAT_H

#include "game.h"

#include <glib.h>

char *checkers_position_format_line(const CheckersMove *line, guint line_length);

#endif
