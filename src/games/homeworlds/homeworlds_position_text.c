#include "homeworlds_position_text.h"

#include "homeworlds_game.h"

typedef enum {
  HOMEWORLDS_POSITION_TEXT_ROW_TOP = 0,
  HOMEWORLDS_POSITION_TEXT_ROW_MIDDLE,
  HOMEWORLDS_POSITION_TEXT_ROW_BOTTOM,
} HomeworldsPositionTextRow;

typedef struct {
  guint systems[HOMEWORLDS_SYSTEM_SLOT_COUNT];
  guint count;
} HomeworldsPositionTextRowGroup;

static char homeworlds_position_text_color_letter(HomeworldsColor color, gboolean uppercase) {
  char letter = '?';

  switch (color) {
    case HOMEWORLDS_COLOR_RED:
      letter = 'r';
      break;
    case HOMEWORLDS_COLOR_YELLOW:
      letter = 'y';
      break;
    case HOMEWORLDS_COLOR_GREEN:
      letter = 'g';
      break;
    case HOMEWORLDS_COLOR_BLUE:
      letter = 'b';
      break;
    default:
      g_debug("Unsupported Homeworlds pyramid color");
      return '?';
  }

  return uppercase ? (char) g_ascii_toupper(letter) : letter;
}

static gboolean homeworlds_position_text_homeworld_rows_are_compact(const HomeworldsPosition *position) {
  const HomeworldsSystem *player_1 = NULL;
  const HomeworldsSystem *player_2 = NULL;
  guint player_1_size_mask = 0;
  guint player_2_size_mask = 0;

  g_return_val_if_fail(position != NULL, FALSE);

  player_1 = &position->systems[0];
  player_2 = &position->systems[1];
  for (guint star_slot = 0; star_slot < HOMEWORLDS_STAR_SLOT_COUNT; ++star_slot) {
    HomeworldsPyramid player_1_star = player_1->stars[star_slot];
    HomeworldsPyramid player_2_star = player_2->stars[star_slot];

    if (homeworlds_pyramid_is_valid(player_1_star)) {
      player_1_size_mask |= 1u << (homeworlds_pyramid_size(player_1_star) - 1);
    }
    if (homeworlds_pyramid_is_valid(player_2_star)) {
      player_2_size_mask |= 1u << (homeworlds_pyramid_size(player_2_star) - 1);
    }
  }

  return player_1_size_mask == player_2_size_mask ||
         homeworlds_system_is_connected(player_1, player_2);
}

static HomeworldsPositionTextRow homeworlds_position_text_system_row(const HomeworldsPosition *position,
                                                                     guint system_index) {
  const HomeworldsSystem *system = NULL;
  gboolean connected_to_player_1 = FALSE;
  gboolean connected_to_player_2 = FALSE;

  g_return_val_if_fail(position != NULL, HOMEWORLDS_POSITION_TEXT_ROW_MIDDLE);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, HOMEWORLDS_POSITION_TEXT_ROW_MIDDLE);

  if (system_index < 2 || homeworlds_position_text_homeworld_rows_are_compact(position)) {
    return HOMEWORLDS_POSITION_TEXT_ROW_MIDDLE;
  }

  system = &position->systems[system_index];
  connected_to_player_1 = homeworlds_system_is_connected(system, &position->systems[0]);
  connected_to_player_2 = homeworlds_system_is_connected(system, &position->systems[1]);

  if (connected_to_player_2 && !connected_to_player_1) {
    return HOMEWORLDS_POSITION_TEXT_ROW_TOP;
  }
  if (connected_to_player_1 && !connected_to_player_2) {
    return HOMEWORLDS_POSITION_TEXT_ROW_BOTTOM;
  }

  return HOMEWORLDS_POSITION_TEXT_ROW_MIDDLE;
}

static void homeworlds_position_text_row_group_append(HomeworldsPositionTextRowGroup *group, guint system_index) {
  g_return_if_fail(group != NULL);
  g_return_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT);
  g_return_if_fail(group->count < G_N_ELEMENTS(group->systems));

  group->systems[group->count++] = system_index;
}

static gboolean homeworlds_position_text_rows_are_connected(const HomeworldsPosition *position,
                                                            const HomeworldsPositionTextRowGroup *previous,
                                                            const HomeworldsPositionTextRowGroup *current) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(previous != NULL, FALSE);
  g_return_val_if_fail(current != NULL, FALSE);

  for (guint previous_index = 0; previous_index < previous->count; ++previous_index) {
    const HomeworldsSystem *previous_system = &position->systems[previous->systems[previous_index]];

    for (guint current_index = 0; current_index < current->count; ++current_index) {
      const HomeworldsSystem *current_system = &position->systems[current->systems[current_index]];

      if (homeworlds_system_is_connected(previous_system, current_system)) {
        return TRUE;
      }
    }
  }

  return FALSE;
}

static void homeworlds_position_text_append_pyramid(GString *text,
                                                    HomeworldsPyramid pyramid,
                                                    gboolean is_star) {
  g_return_if_fail(text != NULL);
  g_return_if_fail(homeworlds_pyramid_is_valid(pyramid));

  g_string_append_printf(text,
                         "%c%u",
                         homeworlds_position_text_color_letter(homeworlds_pyramid_color(pyramid), is_star),
                         (guint) homeworlds_pyramid_size(pyramid));
}

static void homeworlds_position_text_append_stars(GString *text, const HomeworldsSystem *system) {
  gsize start_len = 0;

  g_return_if_fail(text != NULL);
  g_return_if_fail(system != NULL);

  start_len = text->len;
  for (guint slot = 0; slot < HOMEWORLDS_STAR_SLOT_COUNT; ++slot) {
    HomeworldsPyramid star = system->stars[slot];

    if (homeworlds_pyramid_is_valid(star)) {
      homeworlds_position_text_append_pyramid(text, star, TRUE);
    }
  }

  if (text->len == start_len) {
    g_string_append_c(text, '-');
  }
}

static void homeworlds_position_text_append_ships(GString *text, const HomeworldsSystem *system, guint side) {
  gsize start_len = 0;

  g_return_if_fail(text != NULL);
  g_return_if_fail(system != NULL);
  g_return_if_fail(side < 2);

  start_len = text->len;
  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    HomeworldsPyramid ship = system->ships[side][slot];

    if (homeworlds_pyramid_is_valid(ship)) {
      homeworlds_position_text_append_pyramid(text, ship, FALSE);
    }
  }

  if (text->len == start_len) {
    g_string_append_c(text, '-');
  }
}

static void homeworlds_position_text_append_system(GString *text, const HomeworldsSystem *system) {
  g_return_if_fail(text != NULL);
  g_return_if_fail(system != NULL);

  homeworlds_position_text_append_ships(text, system, 1);
  g_string_append_c(text, ' ');
  homeworlds_position_text_append_stars(text, system);
  g_string_append_c(text, ' ');
  homeworlds_position_text_append_ships(text, system, 0);
  g_string_append_c(text, '\n');
}

char *homeworlds_position_format_ascii(const HomeworldsPosition *position) {
  GString *text = NULL;
  HomeworldsPositionTextRowGroup rows[5] = {0};
  const HomeworldsPositionTextRowGroup *previous_row = NULL;
  gboolean appended_any_system = FALSE;

  g_return_val_if_fail(position != NULL, NULL);

  text = g_string_new(NULL);
  g_return_val_if_fail(text != NULL, NULL);

  if (!homeworlds_system_is_empty(&position->systems[1])) {
    homeworlds_position_text_row_group_append(&rows[0], 1);
  }
  for (guint system_index = 2; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    HomeworldsPositionTextRow row = HOMEWORLDS_POSITION_TEXT_ROW_MIDDLE;

    if (homeworlds_system_is_empty(&position->systems[system_index])) {
      continue;
    }

    row = homeworlds_position_text_system_row(position, system_index);
    homeworlds_position_text_row_group_append(&rows[(guint) row + 1], system_index);
  }
  if (!homeworlds_system_is_empty(&position->systems[0])) {
    homeworlds_position_text_row_group_append(&rows[4], 0);
  }

  for (guint row_index = 0; row_index < G_N_ELEMENTS(rows); ++row_index) {
    const HomeworldsPositionTextRowGroup *row = &rows[row_index];

    if (row->count == 0) {
      continue;
    }
    if (previous_row != NULL && !homeworlds_position_text_rows_are_connected(position, previous_row, row)) {
      g_string_append_c(text, '\n');
    }

    for (guint system = 0; system < row->count; ++system) {
      homeworlds_position_text_append_system(text, &position->systems[row->systems[system]]);
      appended_any_system = TRUE;
    }
    previous_row = row;
  }

  if (!appended_any_system) {
    g_string_append(text, "No systems.\n");
  }

  return g_string_free(text, FALSE);
}
