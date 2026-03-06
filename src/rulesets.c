#include "rulesets.h"

const CheckersRules checkers_rules_american = {
    .board_size = 8,
    .men_can_jump_backwards = FALSE,
    .capture_mandatory = TRUE,
    .longest_capture_mandatory = FALSE,
    .kings_can_fly = FALSE,
};

const CheckersRules checkers_rules_international = {
    .board_size = 10,
    .men_can_jump_backwards = TRUE,
    .capture_mandatory = TRUE,
    .longest_capture_mandatory = TRUE,
    .kings_can_fly = TRUE,
};

const CheckersRules checkers_rules_russian = {
    .board_size = 8,
    .men_can_jump_backwards = TRUE,
    .capture_mandatory = TRUE,
    .longest_capture_mandatory = TRUE,
    .kings_can_fly = TRUE,
};

typedef struct {
  PlayerRuleset ruleset;
  const char *name;
  const char *summary;
  const CheckersRules *rules;
} CheckersRulesetInfo;

static const CheckersRulesetInfo checkers_rulesets[] = {
    {
        .ruleset = PLAYER_RULESET_AMERICAN,
        .name = "American (8x8)",
        .summary = "8x8 board, mandatory captures, short kings, and no backward captures for men.",
        .rules = &checkers_rules_american,
    },
    {
        .ruleset = PLAYER_RULESET_INTERNATIONAL,
        .name = "International (10x10)",
        .summary = "10x10 board, mandatory longest captures, flying kings, and backward captures for men.",
        .rules = &checkers_rules_international,
    },
    {
        .ruleset = PLAYER_RULESET_RUSSIAN,
        .name = "Russian (8x8)",
        .summary = "8x8 board, mandatory longest captures, flying kings, and backward captures for men.",
        .rules = &checkers_rules_russian,
    },
};

static const CheckersRulesetInfo *checkers_ruleset_info(PlayerRuleset ruleset) {
  for (guint i = 0; i < G_N_ELEMENTS(checkers_rulesets); ++i) {
    if (checkers_rulesets[i].ruleset == ruleset) {
      return &checkers_rulesets[i];
    }
  }

  return NULL;
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

  return info->rules;
}
