#include "ai_zobrist.h"

typedef struct {
  guint64 piece[CHECKERS_MAX_SQUARES][5];
  guint64 board_size[CHECKERS_MAX_BOARD_SIZE + 1];
  guint64 turn[2];
  guint64 winner[4];
} CheckersAiZobristState;

static CheckersAiZobristState checkers_ai_zobrist_state = {0};

static guint64 checkers_ai_splitmix64(guint64 value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

static guint checkers_ai_piece_index(CheckersPiece piece) {
  switch (piece) {
    case CHECKERS_PIECE_EMPTY:
      return 0;
    case CHECKERS_PIECE_WHITE_MAN:
      return 1;
    case CHECKERS_PIECE_BLACK_MAN:
      return 2;
    case CHECKERS_PIECE_WHITE_KING:
      return 3;
    case CHECKERS_PIECE_BLACK_KING:
      return 4;
    default:
      return 0;
  }
}

static void checkers_ai_zobrist_init_once(void) {
  static gsize initialized = 0;
  if (g_once_init_enter(&initialized)) {
    guint64 seed = 0xa4dc9f31d42f56b7ULL;
    for (guint i = 0; i < G_N_ELEMENTS(checkers_ai_zobrist_state.piece); ++i) {
      for (guint p = 0; p < G_N_ELEMENTS(checkers_ai_zobrist_state.piece[i]); ++p) {
        seed = checkers_ai_splitmix64(seed);
        checkers_ai_zobrist_state.piece[i][p] = seed;
      }
    }

    for (guint i = 0; i < G_N_ELEMENTS(checkers_ai_zobrist_state.board_size); ++i) {
      seed = checkers_ai_splitmix64(seed);
      checkers_ai_zobrist_state.board_size[i] = seed;
    }

    for (guint i = 0; i < G_N_ELEMENTS(checkers_ai_zobrist_state.turn); ++i) {
      seed = checkers_ai_splitmix64(seed);
      checkers_ai_zobrist_state.turn[i] = seed;
    }

    for (guint i = 0; i < G_N_ELEMENTS(checkers_ai_zobrist_state.winner); ++i) {
      seed = checkers_ai_splitmix64(seed);
      checkers_ai_zobrist_state.winner[i] = seed;
    }

    g_once_init_leave(&initialized, 1);
  }
}

guint64 checkers_ai_zobrist_key(const Game *game) {
  g_return_val_if_fail(game != NULL, 0);

  checkers_ai_zobrist_init_once();

  const CheckersBoard *board = &game->state.board;
  guint8 squares = board_playable_squares(board->board_size);
  guint64 key = 0;

  if (board->board_size <= CHECKERS_MAX_BOARD_SIZE) {
    key ^= checkers_ai_zobrist_state.board_size[board->board_size];
  }

  for (guint8 i = 0; i < squares; ++i) {
    CheckersPiece piece = board_get(board, i);
    guint piece_index = checkers_ai_piece_index(piece);
    key ^= checkers_ai_zobrist_state.piece[i][piece_index];
  }

  if (game->state.turn <= CHECKERS_COLOR_BLACK) {
    key ^= checkers_ai_zobrist_state.turn[game->state.turn];
  }
  if (game->state.winner <= CHECKERS_WINNER_DRAW) {
    key ^= checkers_ai_zobrist_state.winner[game->state.winner];
  }

  return key;
}
