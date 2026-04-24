#ifndef ACTIVE_GAME_BACKEND_H
#define ACTIVE_GAME_BACKEND_H

#include "game_backend.h"

#if defined(GGAME_GAME_CHECKERS)
#include "games/checkers/checkers_backend.h"
#define GGAME_ACTIVE_GAME_BACKEND (&checkers_game_backend)
#else
#error "No game backend selected. Define GGAME_GAME_CHECKERS or another backend define."
#endif

#endif
