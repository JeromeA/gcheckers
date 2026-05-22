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

`homeworlds_move_report.c` has a separate diagnostic collector for the text panel and profiling tools. It recursively
walks the builder to show the legal moves, then displays a count header for `good_moves()` and all moves before listing
`good_moves()` followed by "all possible moves minus good_moves()". This report is for inspection only. It is
deduplicated and not used by the AI. When
`View` -> `Move report` is disabled, the text panel skips both the `good_moves()` call and the diagnostic collector.

`build/tools/homeworlds_profile_moves` applies `--moves` random `good_moves()` choices from the initial position using
`--seed`, or replays the first `--moves` main-line moves from an existing Homeworlds SGF with `--file`. It prints a
non-GTK ASCII board snapshot, then prints that same shared report. The Makefile's default `callgrind-run` target
profiles this command so move-report generation can be measured without starting GTK.

## Legal Moves Versus AI Moves

The AI-specific rules live in `good_moves()`, not in the builder. For example, redundant small sacrifices and
AI-forced profitable catastrophes are not supposed to disappear from legal move generation. They are filtered from the
AI candidate set. A human can still build those moves through the builder when the rules allow them, and the diagnostic
collector should show them in the "all possible moves minus good_moves()" section unless they are removed by
deduplication. If a move is absent from the second section, the first checks should be whether the move is canonically
equal to another displayed move and whether the move is actually legal under `homeworlds_position_apply_move()`. It
should not be absent merely because `good_moves()` dislikes it.

## Duplicate Control

Move generation avoids duplicates in two places.

The preferred place is the representation or builder candidate itself. Setup choices list each available pyramid kind
once rather than once per physical bank copy. Discovery choices list each reachable new star pyramid once. Ship
selection deduplicates identical same-side ships within a system, because selecting either identical ship has the same
symbolic move. Build steps store only the source system and build color, not the size of the source ship, because
`H1g1+` and `H1g3+` are the same build when both are green ships in `H1`.
The staged builder therefore offers build only from the first selectable source ship of each color in a system; other
same-color ships can still be selected for actions where size matters.
During a blue sacrifice, ship selection also skips any pyramid that was created by an earlier trade in that same
system, so `H1r2=g H1g2=y` is represented canonically as `H1r2=y pass`.
The `good_moves()` policy also canonicalizes adjacent blue-sacrifice trades when they are provably commutative: the
later trade is pruned if swapping it with the previous trade is legal, reaches the same position, and no catastrophe is
available before, between, or after the two trades. Green-sacrifice builds use the same proof: adjacent builds are kept
in canonical system/color order only when the swapped order consumes the same bank pieces and reaches the same
position. Reversing a green build may have several plausible predecessors when the target system already contains
same-color ships, so the proof tries every reversible built-ship candidate. Bank-dependent trade and build chains are
left alone.

The AI collector still guards against duplicates when it finishes a move, but only as a diagnostic safety net.
`homeworlds_backend_move_buffer_append()` records structural move hashes in a `GHashTable`; if the same symbolic move
appears again, the duplicate is still appended and a warning logs both the duplicated move and the root position that
generated it. The normal goal is to make duplicates impossible by construction, so this guard should explain a
generation bug rather than silently hiding it.

## Current Good-Move Policy

The current Homeworlds `good_moves()` traversal handles pass before stepping candidates, applies action safety after a
builder choice appends a step, and checks whole-move obligations when a complete move is ready to append.

Setup moves are filtered to prefer playable starts: three distinct colors across the two stars and starting ship, a
large starting ship, two different homeworld star sizes, green included for player 1, and a different star-size
combination for player 2.

During play, pass is rejected for AI while any non-pass good move remains, but kept as the top-level fallback when
every non-pass branch has been filtered away before a primary action is staged. The first move after setup must be a
build unless pass is the only remaining fallback. After a choice appends an action step, the same policy is applied to
ordinary actions and sacrifice-granted actions. The AI does not move or sacrifice the last ship at its own homeworld,
rejects builds that create an unfavorable catastrophe, and rejects a small sacrifice when the sacrificed color's action
was already available at that system. During yellow sacrifices, it also rejects repeated hops by the same ship when the
ship returns to its original source or reaches a destination the original source could already have reached directly,
provided the hop does not cross a catastrophe boundary.
During blue and green sacrifices, independent adjacent trades or builds are kept in canonical order so equivalent
permutations are not searched more than once. The check is conservative: steps are considered reorderable only if the
swapped order is legal, position-equivalent, and catastrophe-free at every boundary.

The catastrophe policy distinguishes profitable and unfavorable catastrophes from the moving side's perspective. A
profitable catastrophe destroys more opponent ship pips than own ship pips. If such a catastrophe exists at the start
of the turn, the final good move must trigger one of those root catastrophes somewhere in the move, but it does not have
to trigger it first. If a profitable catastrophe becomes available during the move, the traversal forces it at the
earliest step before exploring ordinary continuations. An unfavorable catastrophe is the opposite: the moving side would
lose more ship pips than the opponent. Good moves reject move/discover steps that enter such a system, and reject build
steps that create such a situation.

Some actions do not need explicit policy. For instance, an attack action with no legal target simply has no target
candidates in the builder, so that branch naturally fails to produce a completed move.

## Good Move Policy Shape

The recursive traversal keeps policy in named predicates rather than hiding it in traversal mechanics. The important
decision points are pass handling, child-state checks after an appended step, and completed-move checks:

```c
if (candidate_is_pass(candidate)) {
  defer;
}
if (!child_forces_new_profitable_catastrophe(state, child)) {
  force_catastrophe_only;
}
if (!child_state_is_good_after_step(state, child)) {
  prune;
}
if (!completed_move_satisfies_root_catastrophe_requirement(context, move)) {
  prune;
}
```

The exact helper names are not important. Child-state predicates say why the consequence of a choice is unsafe.
Completed-move predicates say what whole-turn obligation remains.

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
