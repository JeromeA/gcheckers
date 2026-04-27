#ifndef ACTIVE_GAME_BACKEND_H
#define ACTIVE_GAME_BACKEND_H

#include "game_backend.h"

#if defined(GGAME_GAME_CHECKERS)
#include "games/checkers/checkers_backend.h"
#define GGAME_ACTIVE_GAME_BACKEND (&checkers_game_backend)
#elif defined(GGAME_GAME_HOMEWORLDS)
#include "games/homeworlds/homeworlds_backend.h"
#define GGAME_ACTIVE_GAME_BACKEND (&homeworlds_game_backend)
#elif defined(GGAME_GAME_BOOP)
#include "games/boop/boop_backend.h"
#define GGAME_ACTIVE_GAME_BACKEND (&boop_game_backend)
#else
#error "No game backend selected. Define GGAME_GAME_CHECKERS, GGAME_GAME_HOMEWORLDS, GGAME_GAME_BOOP, or another backend define."
#endif

#endif
