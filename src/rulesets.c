#include "rulesets.h"

typedef struct {
  const char *name;
  const char *short_name;
  const char *summary;
  CheckersRules rules;
} CheckersRulesetInfo;

static const CheckersRulesetInfo checkers_rulesets[] = {
  [PLAYER_RULESET_AMERICAN] =
      {.name = "American (8x8)",
       .short_name = "american",
       .summary = "8x8 board, mandatory captures, short kings, and no backward captures for men.",
       .rules =
           {
             .board_size = 8,
             .men_can_jump_backwards = FALSE,
             .capture_mandatory = TRUE,
             .longest_capture_mandatory = FALSE,
             .kings_can_fly = FALSE,
           }},
  [PLAYER_RULESET_INTERNATIONAL] =
      {.name = "International (10x10)",
       .short_name = "international",
       .summary = "10x10 board, mandatory longest captures, flying kings, and backward captures for men.",
       .rules =
           {
             .board_size = 10,
             .men_can_jump_backwards = TRUE,
             .capture_mandatory = TRUE,
             .longest_capture_mandatory = TRUE,
             .kings_can_fly = TRUE,
           }},
  [PLAYER_RULESET_RUSSIAN] =
      {.name = "Russian (8x8)",
       .short_name = "russian",
       .summary = "8x8 board, mandatory longest captures, flying kings, and backward captures for men.",
       .rules =
           {
             .board_size = 8,
             .men_can_jump_backwards = TRUE,
             .capture_mandatory = TRUE,
             .longest_capture_mandatory = TRUE,
             .kings_can_fly = TRUE,
           }},
};

static gboolean checkers_ruleset_index_is_valid(PlayerRuleset ruleset) {
  gint ruleset_index = (gint)ruleset;
  return ruleset_index >= 0 && (guint)ruleset_index < G_N_ELEMENTS(checkers_rulesets);
}

static const CheckersRulesetInfo *checkers_ruleset_info(PlayerRuleset ruleset) {
  if (!checkers_ruleset_index_is_valid(ruleset)) {
    return NULL;
  }

  return &checkers_rulesets[(gint)ruleset];
}

guint checkers_ruleset_count(void) {
  return G_N_ELEMENTS(checkers_rulesets);
}

const char *checkers_ruleset_name(PlayerRuleset ruleset) {
  const CheckersRulesetInfo *info = checkers_ruleset_info(ruleset);
  if (info == NULL) {
    return NULL;
  }

  return info->name;
}

const char *checkers_ruleset_short_name(PlayerRuleset ruleset) {
  const CheckersRulesetInfo *info = checkers_ruleset_info(ruleset);
  if (info == NULL) {
    return NULL;
  }

  return info->short_name;
}

const char *checkers_ruleset_summary(PlayerRuleset ruleset) {
  const CheckersRulesetInfo *info = checkers_ruleset_info(ruleset);
  if (info == NULL) {
    return NULL;
  }

  return info->summary;
}

const CheckersRules *checkers_ruleset_get_rules(PlayerRuleset ruleset) {
  const CheckersRulesetInfo *info = checkers_ruleset_info(ruleset);
  if (info == NULL) {
    g_debug("Unexpected ruleset value");
    return NULL;
  }

  return &info->rules;
}

gboolean checkers_ruleset_find_by_short_name(const char *short_name, PlayerRuleset *out_ruleset) {
  g_return_val_if_fail(short_name != NULL, FALSE);

  guint count = checkers_ruleset_count();
  for (guint i = 0; i < count; ++i) {
    PlayerRuleset ruleset = (PlayerRuleset)i;
    const char *candidate = checkers_ruleset_short_name(ruleset);
    if (candidate != NULL && g_strcmp0(candidate, short_name) == 0) {
      if (out_ruleset != NULL) {
        *out_ruleset = ruleset;
      }
      return TRUE;
    }
  }

  return FALSE;
}
