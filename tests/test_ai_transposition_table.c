#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include <glib.h>

#include "../src/ai_transposition_table.h"
#include "../src/ai_zobrist.h"
#include "../src/game.h"

static void test_zobrist_distinguishes_turn_and_position(void) {
  Game game = {0};
  game_init(&game);

  guint64 key_start = checkers_ai_zobrist_key(&game);

  Game same = game;
  guint64 key_same = checkers_ai_zobrist_key(&same);
  assert(key_start == key_same);

  Game black_turn = game;
  black_turn.state.turn = CHECKERS_COLOR_BLACK;
  guint64 key_black_turn = checkers_ai_zobrist_key(&black_turn);
  assert(key_start != key_black_turn);

  MoveList moves = game.available_moves(&game);
  assert(moves.count > 0);
  bool applied = game_apply_move(&game, &moves.moves[0]) == 0;
  assert(applied);
  movelist_free(&moves);

  guint64 key_after_move = checkers_ai_zobrist_key(&game);
  assert(key_start != key_after_move);

  game_destroy(&game);
}

static void test_tt_store_lookup_roundtrip(void) {
  CheckersAiTranspositionTable *tt = checkers_ai_tt_new(1);
  assert(tt != NULL);

  guint64 key = 0x1122334455667788ULL;
  CheckersMove move = {0};
  move.length = 2;
  move.path[0] = 12;
  move.path[1] = 16;

  checkers_ai_tt_store(tt, key, 7, 42, CHECKERS_AI_TT_BOUND_EXACT, &move);

  CheckersAiTtEntry entry = {0};
  gboolean found = checkers_ai_tt_lookup(tt, key, &entry);
  assert(found);
  assert(entry.valid);
  assert(entry.key == key);
  assert(entry.depth == 7);
  assert(entry.score == 42);
  assert(entry.bound == CHECKERS_AI_TT_BOUND_EXACT);
  assert(entry.best_move.length == move.length);
  assert(memcmp(entry.best_move.path, move.path, move.length * sizeof(move.path[0])) == 0);

  checkers_ai_tt_free(tt);
}

static void test_tt_replacement_uses_age_and_depth(void) {
  CheckersAiTranspositionTable *tt = checkers_ai_tt_new(1);
  assert(tt != NULL);

  guint64 stride = checkers_ai_tt_entry_count(tt);
  assert(stride > 0);
  guint64 key_a = 0x1234ULL;
  guint64 key_b = key_a + stride;
  guint64 key_c = key_b + stride;

  checkers_ai_tt_store(tt, key_a, 10, 100, CHECKERS_AI_TT_BOUND_EXACT, NULL);

  CheckersAiTtEntry entry = {0};
  assert(checkers_ai_tt_lookup(tt, key_a, &entry));
  assert(entry.depth == 10);

  checkers_ai_tt_store(tt, key_b, 3, 30, CHECKERS_AI_TT_BOUND_EXACT, NULL);
  assert(!checkers_ai_tt_lookup(tt, key_b, &entry));
  assert(checkers_ai_tt_lookup(tt, key_a, &entry));

  checkers_ai_tt_new_generation(tt);
  checkers_ai_tt_store(tt, key_b, 3, 30, CHECKERS_AI_TT_BOUND_EXACT, NULL);
  assert(checkers_ai_tt_lookup(tt, key_b, &entry));
  assert(!checkers_ai_tt_lookup(tt, key_a, &entry));

  checkers_ai_tt_store(tt, key_c, 12, 55, CHECKERS_AI_TT_BOUND_LOWER, NULL);
  assert(checkers_ai_tt_lookup(tt, key_c, &entry));
  assert(entry.depth == 12);
  assert(entry.score == 55);
  assert(entry.bound == CHECKERS_AI_TT_BOUND_LOWER);

  checkers_ai_tt_free(tt);
}

int main(void) {
  test_zobrist_distinguishes_turn_and_position();
  test_tt_store_lookup_roundtrip();
  test_tt_replacement_uses_age_and_depth();
  return 0;
}
