#include <assert.h>

#include "../src/games/boop/boop_game.h"

int main(void) {
  BoopPosition position = {0};
  BoopMove move = {.square = 0};
  char notation[8] = {0};

  boop_position_init(&position);
  assert(boop_position_turn(&position) == 0);
  assert(boop_position_outcome(&position) == GAME_BACKEND_OUTCOME_ONGOING);
  assert(boop_position_apply_move(&position, &move));
  assert(boop_position_turn(&position) == 1);
  assert(boop_move_format(&move, notation, sizeof(notation)));
  assert(notation[0] != '\0');

  return 0;
}
