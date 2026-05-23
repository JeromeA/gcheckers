#include "homeworlds_sgf_position.h"

#include "homeworlds_game.h"

#include <string.h>

#define HOMEWORLDS_PYRAMID_KIND_COUNT 12
#define HOMEWORLDS_PYRAMID_SUPPLY_COUNT 3

static GQuark homeworlds_sgf_position_error_quark(void) {
  return g_quark_from_static_string("homeworlds-sgf-position-error");
}

static gboolean homeworlds_sgf_position_parse_uint8(const char *text, guint max_value, guint8 *out_value) {
  guint64 value = 0;
  char *end_ptr = NULL;

  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(out_value != NULL, FALSE);

  if (*text == '\0') {
    return FALSE;
  }
  for (const char *p = text; *p != '\0'; ++p) {
    if (!g_ascii_isdigit(*p)) {
      return FALSE;
    }
  }

  value = g_ascii_strtoull(text, &end_ptr, 10);
  if (end_ptr == text || end_ptr == NULL || *end_ptr != '\0' || value > max_value || value > G_MAXUINT8) {
    return FALSE;
  }

  *out_value = (guint8)value;
  return TRUE;
}

static gboolean homeworlds_sgf_position_parse_uint8_list(const char *text,
                                                         guint expected_count,
                                                         guint max_value,
                                                         guint8 *out_values) {
  g_auto(GStrv) fields = NULL;

  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(expected_count > 0, FALSE);
  g_return_val_if_fail(out_values != NULL, FALSE);

  fields = g_strsplit(text, ",", -1);
  if (fields == NULL) {
    return FALSE;
  }
  for (guint i = 0; i < expected_count; ++i) {
    if (fields[i] == NULL || !homeworlds_sgf_position_parse_uint8(fields[i], max_value, &out_values[i])) {
      return FALSE;
    }
  }

  return fields[expected_count] == NULL;
}

static gboolean homeworlds_sgf_position_system_is_valid_snapshot(const HomeworldsSystem *system) {
  g_return_val_if_fail(system != NULL, FALSE);

  return homeworlds_system_is_empty(system) ||
         (homeworlds_system_has_star(system) && homeworlds_system_has_any_ship(system));
}

static gboolean homeworlds_sgf_position_count_pyramid(HomeworldsPyramid pyramid,
                                                      guint counts[HOMEWORLDS_PYRAMID_KIND_COUNT + 1]) {
  g_return_val_if_fail(counts != NULL, FALSE);

  if (!homeworlds_pyramid_is_valid(pyramid)) {
    return pyramid == 0;
  }

  counts[pyramid]++;
  return counts[pyramid] <= HOMEWORLDS_PYRAMID_SUPPLY_COUNT;
}

static gboolean homeworlds_sgf_position_validate_pyramid_supply(const HomeworldsPosition *position) {
  guint counts[HOMEWORLDS_PYRAMID_KIND_COUNT + 1] = {0};

  g_return_val_if_fail(position != NULL, FALSE);

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    if (!homeworlds_sgf_position_count_pyramid(position->bank[i], counts)) {
      return FALSE;
    }
  }
  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &position->systems[system_index];

    for (guint star_slot = 0; star_slot < HOMEWORLDS_STAR_SLOT_COUNT; ++star_slot) {
      if (!homeworlds_sgf_position_count_pyramid(system->stars[star_slot], counts)) {
        return FALSE;
      }
    }
    for (guint side = 0; side < 2; ++side) {
      for (guint ship_slot = 0; ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++ship_slot) {
        if (!homeworlds_sgf_position_count_pyramid(system->ships[side][ship_slot], counts)) {
          return FALSE;
        }
      }
    }
  }

  for (HomeworldsPyramid pyramid = 1; pyramid <= HOMEWORLDS_PYRAMID_KIND_COUNT; ++pyramid) {
    if (counts[pyramid] != HOMEWORLDS_PYRAMID_SUPPLY_COUNT) {
      return FALSE;
    }
  }

  return TRUE;
}

static char *homeworlds_sgf_position_format_uint8_list(const guint8 *values, guint count) {
  GString *text = NULL;

  g_return_val_if_fail(values != NULL, NULL);
  g_return_val_if_fail(count > 0, NULL);

  text = g_string_new(NULL);
  g_return_val_if_fail(text != NULL, NULL);
  for (guint i = 0; i < count; ++i) {
    if (i > 0) {
      g_string_append_c(text, ',');
    }
    g_string_append_printf(text, "%u", (guint)values[i]);
  }

  return g_string_free(text, FALSE);
}

static gboolean homeworlds_sgf_position_parse_system_value(const char *value,
                                                           HomeworldsPosition *position,
                                                           guint *out_system_index) {
  g_auto(GStrv) fields = NULL;
  guint8 system_index = 0;

  g_return_val_if_fail(value != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_system_index != NULL, FALSE);

  fields = g_strsplit(value, "|", 4);
  if (fields == NULL || fields[0] == NULL || fields[1] == NULL || fields[2] == NULL || fields[3] == NULL ||
      fields[4] != NULL) {
    return FALSE;
  }
  if (!homeworlds_sgf_position_parse_uint8(fields[0], HOMEWORLDS_SYSTEM_SLOT_COUNT - 1, &system_index)) {
    return FALSE;
  }
  *out_system_index = system_index;

  HomeworldsSystem *system = &position->systems[system_index];
  if (!homeworlds_sgf_position_parse_uint8_list(fields[1], HOMEWORLDS_STAR_SLOT_COUNT, 12, system->stars) ||
      !homeworlds_sgf_position_parse_uint8_list(fields[2], HOMEWORLDS_SHIP_SLOT_COUNT, 12, system->ships[0]) ||
      !homeworlds_sgf_position_parse_uint8_list(fields[3], HOMEWORLDS_SHIP_SLOT_COUNT, 12, system->ships[1])) {
    return FALSE;
  }

  return homeworlds_sgf_position_system_is_valid_snapshot(system);
}

gboolean homeworlds_sgf_position_apply_setup_node(gpointer position, const SgfNode *node, GError **error) {
  HomeworldsPosition *homeworlds_position = position;
  const char *phase_text = NULL;
  const char *turn_text = NULL;
  const char *bank_text = NULL;
  const GPtrArray *systems = NULL;
  gboolean seen_systems[HOMEWORLDS_SYSTEM_SLOT_COUNT] = {FALSE};
  HomeworldsPosition parsed = {0};

  g_return_val_if_fail(homeworlds_position != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  phase_text = sgf_node_get_property_first(node, "GHP");
  turn_text = sgf_node_get_property_first(node, "GHT");
  bank_text = sgf_node_get_property_first(node, "GHB");
  systems = sgf_node_get_property_values(node, "GHS");
  if (phase_text == NULL && turn_text == NULL && bank_text == NULL && systems == NULL) {
    return TRUE;
  }
  if (phase_text == NULL || turn_text == NULL || bank_text == NULL) {
    g_set_error_literal(error,
                        homeworlds_sgf_position_error_quark(),
                        1,
                        "Incomplete Homeworlds SGF position snapshot");
    return FALSE;
  }

  memset(&parsed, 0, sizeof(parsed));
  if (!homeworlds_sgf_position_parse_uint8(phase_text, HOMEWORLDS_PHASE_FINISHED, &parsed.phase) ||
      !homeworlds_sgf_position_parse_uint8(turn_text, 1, &parsed.turn) ||
      !homeworlds_sgf_position_parse_uint8_list(bank_text, HOMEWORLDS_BANK_SLOT_COUNT, 12, parsed.bank)) {
    g_set_error_literal(error,
                        homeworlds_sgf_position_error_quark(),
                        2,
                        "Invalid Homeworlds SGF position metadata");
    return FALSE;
  }

  if (systems != NULL) {
    for (guint i = 0; i < systems->len; ++i) {
      const char *system_value = g_ptr_array_index((GPtrArray *)systems, i);
      guint system_index = HOMEWORLDS_INVALID_INDEX;

      if (system_value == NULL ||
          !homeworlds_sgf_position_parse_system_value(system_value, &parsed, &system_index) ||
          seen_systems[system_index]) {
        g_set_error_literal(error,
                            homeworlds_sgf_position_error_quark(),
                            3,
                            "Invalid Homeworlds SGF system snapshot");
        return FALSE;
      }
      seen_systems[system_index] = TRUE;
    }
  }
  if (!homeworlds_sgf_position_validate_pyramid_supply(&parsed)) {
    g_set_error_literal(error,
                        homeworlds_sgf_position_error_quark(),
                        6,
                        "Invalid Homeworlds SGF pyramid supply");
    return FALSE;
  }
  homeworlds_position_rebuild_color_counts(&parsed);

  *homeworlds_position = parsed;
  return TRUE;
}

static gboolean homeworlds_sgf_position_write_system(const HomeworldsSystem *system,
                                                     guint system_index,
                                                     SgfNode *node) {
  g_autofree char *stars = NULL;
  g_autofree char *side0 = NULL;
  g_autofree char *side1 = NULL;
  g_autofree char *value = NULL;

  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  if (homeworlds_system_is_empty(system)) {
    return TRUE;
  }

  stars = homeworlds_sgf_position_format_uint8_list(system->stars, HOMEWORLDS_STAR_SLOT_COUNT);
  side0 = homeworlds_sgf_position_format_uint8_list(system->ships[0], HOMEWORLDS_SHIP_SLOT_COUNT);
  side1 = homeworlds_sgf_position_format_uint8_list(system->ships[1], HOMEWORLDS_SHIP_SLOT_COUNT);
  if (stars == NULL || side0 == NULL || side1 == NULL) {
    return FALSE;
  }

  value = g_strdup_printf("%u|%s|%s|%s", system_index, stars, side0, side1);
  g_return_val_if_fail(value != NULL, FALSE);
  return sgf_node_add_property(node, "GHS", value);
}

gboolean homeworlds_sgf_position_write_position_node(gconstpointer position, SgfNode *node, GError **error) {
  const HomeworldsPosition *homeworlds_position = position;
  char phase_text[8] = {0};
  char turn_text[8] = {0};
  g_autofree char *bank_text = NULL;

  g_return_val_if_fail(homeworlds_position != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  sgf_node_clear_property(node, "GHP");
  sgf_node_clear_property(node, "GHT");
  sgf_node_clear_property(node, "GHB");
  sgf_node_clear_property(node, "GHS");

  g_snprintf(phase_text, sizeof(phase_text), "%u", (guint)homeworlds_position->phase);
  g_snprintf(turn_text, sizeof(turn_text), "%u", (guint)homeworlds_position->turn);
  bank_text = homeworlds_sgf_position_format_uint8_list(homeworlds_position->bank, HOMEWORLDS_BANK_SLOT_COUNT);
  if (bank_text == NULL ||
      !sgf_node_add_property(node, "GHP", phase_text) ||
      !sgf_node_add_property(node, "GHT", turn_text) ||
      !sgf_node_add_property(node, "GHB", bank_text)) {
    g_set_error_literal(error,
                        homeworlds_sgf_position_error_quark(),
                        4,
                        "Failed to write Homeworlds SGF position metadata");
    return FALSE;
  }

  for (guint i = 0; i < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++i) {
    if (!homeworlds_sgf_position_write_system(&homeworlds_position->systems[i], i, node)) {
      g_set_error_literal(error,
                          homeworlds_sgf_position_error_quark(),
                          5,
                          "Failed to write Homeworlds SGF system snapshot");
      return FALSE;
    }
  }

  return TRUE;
}
