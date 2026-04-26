#ifndef HOMEWORLDS_TYPES_H
#define HOMEWORLDS_TYPES_H

#include <glib.h>

#define HOMEWORLDS_BANK_SLOT_COUNT 36
#define HOMEWORLDS_SYSTEM_SLOT_COUNT 16
#define HOMEWORLDS_STAR_SLOT_COUNT 2
#define HOMEWORLDS_SHIP_SLOT_COUNT 14
#define HOMEWORLDS_MAX_MOVE_STEPS 8
#define HOMEWORLDS_INVALID_INDEX 255
#define HOMEWORLDS_INVALID_COLOR 255

typedef guint8 HomeworldsPyramid;

typedef enum {
  HOMEWORLDS_COLOR_RED = 0,
  HOMEWORLDS_COLOR_YELLOW = 1,
  HOMEWORLDS_COLOR_GREEN = 2,
  HOMEWORLDS_COLOR_BLUE = 3,
} HomeworldsColor;

typedef enum {
  HOMEWORLDS_SIZE_SMALL = 1,
  HOMEWORLDS_SIZE_MEDIUM = 2,
  HOMEWORLDS_SIZE_LARGE = 3,
} HomeworldsSize;

typedef enum {
  HOMEWORLDS_PHASE_SETUP = 0,
  HOMEWORLDS_PHASE_PLAY,
  HOMEWORLDS_PHASE_FINISHED,
} HomeworldsPhase;

typedef enum {
  HOMEWORLDS_MOVE_KIND_NONE = 0,
  HOMEWORLDS_MOVE_KIND_SETUP,
  HOMEWORLDS_MOVE_KIND_TURN,
} HomeworldsMoveKind;

typedef enum {
  HOMEWORLDS_STEP_NONE = 0,
  HOMEWORLDS_STEP_PASS,
  HOMEWORLDS_STEP_CONSTRUCT,
  HOMEWORLDS_STEP_TRADE,
  HOMEWORLDS_STEP_ATTACK,
  HOMEWORLDS_STEP_MOVE,
  HOMEWORLDS_STEP_DISCOVER,
  HOMEWORLDS_STEP_SACRIFICE,
  HOMEWORLDS_STEP_CATASTROPHE,
} HomeworldsStepKind;

typedef enum {
  HOMEWORLDS_CANDIDATE_NONE = 0,
  HOMEWORLDS_CANDIDATE_SETUP_STAR,
  HOMEWORLDS_CANDIDATE_SETUP_SHIP,
  HOMEWORLDS_CANDIDATE_SELECT_SHIP,
  HOMEWORLDS_CANDIDATE_ACTION,
  HOMEWORLDS_CANDIDATE_TRADE_COLOR,
  HOMEWORLDS_CANDIDATE_ATTACK_TARGET,
  HOMEWORLDS_CANDIDATE_MOVE_TARGET,
} HomeworldsCandidateKind;

typedef struct {
  HomeworldsPyramid stars[HOMEWORLDS_STAR_SLOT_COUNT];
  HomeworldsPyramid ships[2][HOMEWORLDS_SHIP_SLOT_COUNT];
} HomeworldsSystem;

typedef struct {
  guint8 kind;
  guint8 system_index;
  guint8 ship_owner;
  guint8 ship_slot;
  guint8 target_system_index;
  guint8 target_ship_owner;
  guint8 target_ship_slot;
  guint8 target_color;
  HomeworldsPyramid pyramid;
} HomeworldsTurnStep;

typedef struct {
  guint8 kind;
  guint8 acting_side;
  guint8 step_count;
  HomeworldsPyramid setup_stars[HOMEWORLDS_STAR_SLOT_COUNT];
  HomeworldsPyramid setup_ship;
  HomeworldsTurnStep steps[HOMEWORLDS_MAX_MOVE_STEPS];
} HomeworldsMove;

typedef struct {
  HomeworldsPyramid bank[HOMEWORLDS_BANK_SLOT_COUNT];
  HomeworldsSystem systems[HOMEWORLDS_SYSTEM_SLOT_COUNT];
  guint8 phase;
  guint8 turn;
} HomeworldsPosition;

typedef union {
  struct {
    guint8 kind;
    guint8 system_index;
    guint8 ship_owner;
    guint8 ship_slot;
    guint8 target_system_index;
    guint8 target_ship_owner;
    guint8 target_ship_slot;
    guint8 target_color;
    HomeworldsPyramid pyramid;
  } data;
  guint8 storage[sizeof(HomeworldsMove)];
} HomeworldsMoveCandidate;

static inline gboolean homeworlds_pyramid_is_valid(HomeworldsPyramid pyramid) {
  return pyramid >= 1 && pyramid <= 12;
}

static inline gboolean homeworlds_pyramid_is_unused(HomeworldsPyramid pyramid) {
  return pyramid == 0;
}

static inline HomeworldsPyramid homeworlds_pyramid_make(HomeworldsColor color, HomeworldsSize size) {
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);
  g_return_val_if_fail(size >= HOMEWORLDS_SIZE_SMALL && size <= HOMEWORLDS_SIZE_LARGE, 0);

  return (HomeworldsPyramid) ((color * 3) + size);
}

static inline HomeworldsColor homeworlds_pyramid_color(HomeworldsPyramid pyramid) {
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), HOMEWORLDS_COLOR_RED);

  return (HomeworldsColor) ((pyramid - 1) / 3);
}

static inline HomeworldsSize homeworlds_pyramid_size(HomeworldsPyramid pyramid) {
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), HOMEWORLDS_SIZE_SMALL);

  return (HomeworldsSize) (((pyramid - 1) % 3) + 1);
}

static inline gboolean homeworlds_system_has_star(const HomeworldsSystem *system) {
  g_return_val_if_fail(system != NULL, FALSE);

  return system->stars[0] != 0 || system->stars[1] != 0;
}

static inline gboolean homeworlds_system_has_any_ship(const HomeworldsSystem *system) {
  g_return_val_if_fail(system != NULL, FALSE);

  for (guint side = 0; side < 2; ++side) {
    for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
      if (system->ships[side][slot] != 0) {
        return TRUE;
      }
    }
  }

  return FALSE;
}

static inline gboolean homeworlds_system_is_empty(const HomeworldsSystem *system) {
  g_return_val_if_fail(system != NULL, TRUE);

  return !homeworlds_system_has_star(system) && !homeworlds_system_has_any_ship(system);
}

#endif
