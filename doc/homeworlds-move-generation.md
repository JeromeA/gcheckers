# Homeworlds Move Generation and Good Moves

This document describes the current Homeworlds move-generation pipeline and how it collaborates with the generic
alpha-beta search. It uses "legal moves" for moves that the rules engine accepts, and "good moves" for the backend-owned
subset that the AI is allowed to search.

## Layers

`homeworlds_move_builder.c` is the interactive and diagnostic legal-move builder. It owns a working copy of the
position, a partially built `HomeworldsMove`, and a stage. Each call to `homeworlds_move_builder_list_candidates()`
returns the currently legal next visible choices: setup pyramids, ship selection, action selection, trade color, attack
target, move target, or pass. Each accepted candidate mutates only the builder state and working position.

The builder is deliberately incremental. It does not try to allocate the full legal move tree up front, because
Homeworlds turns can contain prefix catastrophes, a primary action, sacrifice-granted follow-up actions, and trailing
catastrophes. The UI uses this directly: clicking a ship, a bank pyramid, a target ship, or a button advances the same
builder one step at a time.

`homeworlds_game.c` remains the final rules authority. When a completed `HomeworldsMove` is submitted through the
generic model or SGF controller, `homeworlds_position_apply_move()` replays the symbolic steps against the current
position and rejects invalid moves. For Homeworlds, the generic model cannot validate against `list_moves()`, because
Homeworlds intentionally does not expose a full legal move list.

`homeworlds_backend.c` implements `list_good_moves()`. It also walks the staged builder, but it applies AI policy while
walking. That means `good_moves()` is not the same thing as legal move generation: it is a legal-move traversal plus a
set of pruning and forcing rules that keep alpha-beta focused on sensible moves.

`homeworlds_view.c` has a separate diagnostic collector for the text panel. It recursively walks the builder to show a
bounded list of legal moves, then displays `good_moves()` followed by "all possible moves minus good_moves()". This
report is for inspection only. It is capped, deduplicated, and not used by the AI.

## Legal Moves Versus AI Moves

The AI-specific rules live in `good_moves()`, not in the builder. For example, redundant small sacrifices and
AI-forced profitable catastrophes are not supposed to disappear from legal move generation. They are filtered from the
AI candidate set. A human can still build those moves through the builder when the rules allow them, and the diagnostic
collector should show them in the "all possible moves minus good_moves()" section unless they are removed by
deduplication or by the report caps.

The current UI report is therefore meaningful but not exhaustive in the mathematical sense. It stops after
`HOMEWORLDS_VIEW_MOVE_REPORT_MAX_MOVES` stored unique moves or `HOMEWORLDS_VIEW_MOVE_REPORT_MAX_LEAVES` explored
complete branches. If a move is absent from the second section, the first checks should be whether the report was
truncated, whether the move is canonically equal to another displayed move, and whether the move is actually legal
under `homeworlds_position_apply_move()`. It should not be absent merely because `good_moves()` dislikes it.

## Duplicate Control

Move generation avoids duplicates in two places.

The preferred place is the representation or builder candidate itself. Setup choices list each available pyramid kind
once rather than once per physical bank copy. Discovery choices list each reachable new star pyramid once. Ship
selection deduplicates identical same-side ships within a system, because selecting either identical ship has the same
symbolic move. Build steps store only the source system and build color, not the size of the source ship, because
`H1g1+` and `H1g3+` are the same build when both are green ships in `H1`.

Collectors still guard against duplicates when they finish a move. `homeworlds_backend_move_buffer_append()` and the
diagnostic view buffer compare moves through backend formatting/equality before appending. This is a safety net for
multi-step paths, especially when optional catastrophes or equivalent candidate choices can reach the same final
symbolic move. The goal is to make duplicates impossible by construction where practical, and harmless where that would
make the builder too complicated.

## Current Good-Move Policy

The current Homeworlds `good_moves()` traversal applies policy at three points: before stepping a candidate, after
stepping a child state, and when a complete move is ready to append.

Setup moves are filtered to prefer playable starts: three distinct colors across the two stars and starting ship, a
large starting ship, two different homeworld star sizes, green included for player 1, and a different star-size
combination for player 2.

During play, pass is rejected for AI. The first move after setup must be a build. The AI does not move or sacrifice the
last ship at its own homeworld. It avoids building the fourth pyramid of a color at a system with no opponent ships,
because that creates a catastrophe with no tactical target. It rejects a small sacrifice when the sacrificed color's
action was already available at that system, because a one-action sacrifice would only spend a ship to do something the
ship could already do directly.

The catastrophe policy distinguishes profitable and unfavorable catastrophes from the moving side's perspective. A
profitable catastrophe destroys more opponent ship pips than own ship pips. If such a catastrophe exists at the start
of the turn, the final good move must trigger one of those root catastrophes somewhere in the move, but it does not have
to trigger it first. If a profitable catastrophe becomes available during the move, the traversal forces it at the
earliest step before exploring ordinary continuations. An unfavorable catastrophe is the opposite: the moving side would
lose more ship pips than the opponent. Good moves reject move/discover steps that enter such a system, and reject
pending green-sacrifice builds that create such a situation.

Some actions do not need explicit policy. For instance, an attack action with no legal target simply has no target
candidates in the builder, so that branch naturally fails to produce a completed move.

## How To Make Good Moves Read Like Policy

The current implementation is already split into named helpers, but a cleaner shape would make the recursive traversal
mostly policy-neutral and express the AI rules as a small sequence of predicates. A readable rewrite would keep the same
three decision points and give each rule a direct name:

```c
if (!candidate_keeps_ai_turn_non_pass(state, candidate)) {
  prune;
}
if (!candidate_respects_opening_policy(state, candidate)) {
  prune;
}
if (!candidate_keeps_last_homeworld_ship(state, candidate)) {
  prune;
}
if (!candidate_avoids_redundant_small_sacrifice(state, candidate)) {
  prune;
}
if (!child_forces_new_profitable_catastrophe(state, child)) {
  force_catastrophe_only;
}
if (!child_avoids_unfavorable_catastrophe(state, candidate, child)) {
  prune;
}
if (!completed_move_satisfies_root_catastrophe_requirement(context, move)) {
  prune;
}
```

The important part is not the exact helper names. The important part is to avoid hiding policy inside traversal
mechanics. Candidate predicates should say why a visible choice is not useful for AI. Child-state predicates should say
why the consequence of a choice is unsafe. Completed-move predicates should say what whole-turn obligation remains.

## Alpha-Beta Interaction

The generic search in `src/ai_search.c` asks the backend for candidates through `game_ai_search_list_candidate_moves()`.
If a backend exposes `list_good_moves()`, that list is used. Homeworlds does expose `list_good_moves()`, so alpha-beta
never searches the diagnostic "all possible moves" list.

At the root, `game_ai_search_analyze_moves()` gets the good moves, applies each one to a copied position, evaluates the
child with alpha-beta, then sorts the scored moves best-to-worst for side 0 and worst-to-best for side 1. Force Play and
computer-player replies call `game_ai_search_choose_move()`, which uses the same analysis path and then copies one of
the best moves.

At depth 0, the AI still scores every root good move. For each root move, it applies the move and calls the recursive
search with depth 0. The recursive search first checks for terminal positions, then lists candidate moves from the
child. If the child has more than one move, it returns the static evaluation of that child. If the child has exactly one
move, that forced move is searched without consuming depth, so forced continuations can continue until a branch or
terminal state. After all root moves are scored, `game_ai_search_choose_move()` chooses randomly among moves tied for
the best score. It is not "first good move", and it is not uniformly random among all good moves.

