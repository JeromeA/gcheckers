#ifndef CHECKERS_RULESETS_H
#define CHECKERS_RULESETS_H

#include "game.h"
#include "ruleset.h"

#include <glib.h>

guint checkers_ruleset_count(void);
const char *checkers_ruleset_name(PlayerRuleset ruleset);
const char *checkers_ruleset_summary(PlayerRuleset ruleset);
const CheckersRules *checkers_ruleset_get_rules(PlayerRuleset ruleset);

extern const CheckersRules checkers_rules_american;
extern const CheckersRules checkers_rules_international;
extern const CheckersRules checkers_rules_russian;

#endif
