# Homeworlds good-move bound pruning

This ExecPlan is a living document. It must be maintained according to `doc/PLANS.md`. The sections `Progress`,
`Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must stay current while the work proceeds.

## Purpose / Big Picture

Homeworlds positions can generate millions of deduplicated complete moves. The current `good_moves()` implementation
keeps memory bounded by scoring complete moves as they are produced and retaining only the best 512 static candidates,
but it still has to enumerate and score every complete move before it can discard low-scoring ones. This change adds an
optional conservative branch-and-bound layer: once `good_moves()` already has 512 candidates and therefore knows the
current worst kept score, selected large-sacrifice subtrees can be skipped when a proof says no continuation can reach
that score. A verification mode explores those same would-pruned subtrees and records whether the proof was sound, so
the optimization can be tuned on real reports before it becomes a default behavior.

## Progress

- [x] (2026-06-19 22:16Z) Read the plan requirements, checked the clean tracked worktree, and identified the relevant
  Homeworlds backend, trace, and eval-experiment code.
- [x] (2026-06-19 22:16Z) Analyzed `homeworlds_3.3M_moves.txt` enough to see that the example is dominated by large
  yellow sacrifices, not green sacrifices.
- [x] (2026-06-19 22:30Z) Added optional pruning mode parsing, trace counters, and verification state.
- [x] (2026-06-19 22:30Z) Implemented the first conservative proof for active large yellow sacrifices.
- [x] (2026-06-19 22:30Z) Added tests for mode control, trace output, verification failures, and unchanged `verify`
  results.
- [x] (2026-06-19 22:30Z) Updated `doc/OVERVIEW.md`.
- [x] (2026-06-19 22:42Z) Ran focused build/test commands, `git diff --check`, wrap scan, and full `make`.
- [x] (2026-06-20 07:37Z) Strengthened the large-yellow proof to ignore only non-positive catastrophes and to prove
  same-color ship reachability before declining to prune.
- [x] (2026-06-20 09:14Z) Replaced the binary positive-catastrophe guard with a numeric catastrophe-gain ceiling that
  charges future yellow actions for saving doomed own ships.
- [x] (2026-06-20 10:53Z) Added `build/tools/homeworlds_proof_probe` for repeatable per-prefix proof inspection from
  big move reports.
- [x] (2026-06-20 11:55Z) Decided to add local candidate ordering for active large yellow sacrifices and to judge the
  result by the final buffer's score quality rather than by exact move-list identity.
- [x] (2026-06-20 12:20Z) Implemented early branch cutoffs from the existing best-score window before the 512-move
  buffer is full.
- [x] (2026-06-20 12:20Z) Implemented stable local ordering for active large yellow sacrifice candidate lists, limited
  to the pre-512 phase where it can raise the bar.
- [x] (2026-06-20 12:20Z) Added trace/report counters for ordering and score-window pruning, updated the proof probe,
  tests, and overview documentation.
- [x] (2026-06-20 12:20Z) Measured the 3.3M report with the proof probe in pruning `off`, `on`, and `verify` modes.
- [x] (2026-06-21 17:20Z) Added a simple-move pre-pass for normal ship-selection states so one-step attack, move,
  build, and trade continuations are explored before sacrifice branches.
- [x] (2026-06-21) Counted opponent ships orphaned by star destruction and signed homeworld-star effects when scoring
  catastrophes for the proof, safety filters, and profitable-catastrophe expansion.
- [ ] `make test-local` reached the known GTK window failure path and failed because the isolated retry also failed.

## Surprises & Discoveries

- Observation: The provided 3.3M report is not primarily a green-sacrifice case.
  Evidence: A text scan of `homeworlds_3.3M_moves.txt` found about 3,382,932 numbered streamed moves. The largest
  first-step prefixes were `S2y3-` with about 1,698,384 moves and `H1y3-` with about 1,596,583 moves. All `g3-` and
  `g2-` occurrences together were about 1,819.

- Observation: The compact existing test positions do not reliably trigger a nonzero large-yellow pruning counter.
  Evidence: The focused backend test validates that `verify` mode returns the same static-pruned moves and reports zero
  failures, but it does not assert a positive pruned-branch count.

- Observation: A raw static-eval delta is too narrow for deciding whether a catastrophe is positive.
  Evidence: A probe version that subtracted all immediate static side effects reported 137 verification failures on the
  3.3M report, even though the final returned digest was unchanged. The proof now uses active weighted ship material
  plus a conservative opponent-homeworld star margin instead.

- Observation: The numeric catastrophe-gain proof materially changes the 3.3M report.
  Evidence: With pruning off, the 256-ply report scored 2,259,128 leaves. In `verify`, it checked 296,331 branches,
  would prune 26,399, verified 1,509,215 leaves, and found zero failures. In `on`, it scored 809,701 leaves, pruned
  34,238 branches, returned the same 512 moves, and kept the same SHA-256 digest
  `83be6e770f1afbedc40ffd459cfc0d90cd20c3f44f80b31bd44523079d15b7f7`.

- Observation: `good_moves()` already has a score bar before the 512th kept move exists.
  Evidence: `HomeworldsMoveBuffer` updates `best_score` whenever a completed move improves the best score, then drops
  completed moves outside the 50-point static-prune window. The branch-bound proof previously ignored that bar because
  it only requested a cutoff after the buffer held 512 moves.

- Observation: Always-on ordering through the entire large-yellow tree made the 3.3M pruning run slightly worse.
  Evidence: With pruning `on`, ordering through every active large-yellow candidate list scored 814,178 leaves, while
  old ordering scored 809,701 leaves. Limiting ordering to the pre-512 phase restored the 809,701 scored-leaf count.

- Observation: On the 3.3M report, pre-512 ordering is safe but not yet a major improvement.
  Evidence: With pruning `on`, cutoff stayed 490, scored leaves stayed 809,701, window-cutoff branch checks were 47,
  and ordering touched 47 candidate lists, reordered 3 of them, and moved 35 candidates. With pruning `verify`, the
  same report had 296,381 checked branches, 26,399 would-pruned branches, 1,509,215 verified leaves, and zero
  verification failures.

## Decision Log

- Decision: Use environment-variable control first: `GCHECKERS_HOMEWORLDS_GOOD_MOVE_PRUNING=off|on|verify`.
  Rationale: This keeps the generic `GameBackend` interface unchanged and makes tuning easy from tools, tests, and the
  app. Invalid values should log and behave like `off`.
  Date/Author: 2026-06-19 / Codex.

- Decision: Default pruning mode is `off`.
  Rationale: The user wants exploration and verification before changing normal AI behavior.
  Date/Author: 2026-06-19 / Codex.

- Decision: Implement yellow large-sacrifice pruning first.
  Rationale: The real 3.3M report is dominated by large yellow sacrifices. Green was easier to explain but would not
  materially reduce this example.
  Date/Author: 2026-06-19 / Codex.

- Decision: The first proof must be conservative and may decline to prune often.
  Rationale: The optimization must never remove a move that could reach the current kept-score cutoff. Verification
  mode exists to measure how useful the proof is before making it more aggressive.
  Date/Author: 2026-06-19 / Codex.

- Decision: Treat only material-positive catastrophes, plus opponent-homeworld star destruction, as cutoff-relevant
  for the large-yellow proof.
  Rationale: A catastrophe that only destroys the mover's own ships cannot improve the retained static score, but a
  catastrophe that destroys opponent material or can damage the opponent homeworld might. Reachability is then checked
  by finding enough same-color own ships that can be moved to the target system within the remaining yellow actions.
  Date/Author: 2026-06-20 / Codex.

- Decision: For existing catastrophes, count both the immediate value and the best own material that remaining yellow
  actions could still move away first.
  Rationale: This models the important tactic directly: an existing catastrophe that is bad now can become better only
  as yellow actions save doomed own ships. If the first yellow action does not save one, the ceiling drops in the child
  state and the branch can then prune.
  Date/Author: 2026-06-20 / Codex.

- Decision: Star-removing catastrophe estimates always count opponent ships that would be orphaned and the signed
  homeworld-star score change.
  Rationale: Own non-color ships may still be saved before a future catastrophe, but opponent ships are not assumed to
  move away on our turn. A catastrophe that removes a homeworld star can also dominate same-color ship material, so the
  safety filter and profitable-catastrophe expansion need the same scoring as the pruning proof.
  Date/Author: 2026-06-21 / Codex.

- Decision: Candidate ordering may change which equivalent move representative survives deduplication.
  Rationale: The user explicitly only cares about the score quality of the final buffer, not byte-for-byte identity of
  the retained moves. Stable local ordering is therefore acceptable if it keeps the score window behavior sound and is
  traceable. Exact old ordering remains available for comparison with `GCHECKERS_HOMEWORLDS_GOOD_MOVE_ORDERING=off`.
  Date/Author: 2026-06-20 / Codex.

- Decision: Limit local candidate ordering to the period before the retained buffer reaches 512 moves.
  Rationale: That is the phase where ordering can raise the early score-window bar or fill the full cutoff sooner. In
  the 3.3M report, continuing to reorder after the full cutoff existed changed dedupe representatives enough to reduce
  pruning effectiveness. Stopping at 512 keeps the useful part and avoids that regression.
  Date/Author: 2026-06-20 / Codex.

- Decision: Explore complete one-step moves before sacrifice branches without a 512-move guard.
  Rationale: Ordinary attack, move, build, and trade turns are bounded enough to enumerate cheaply, and scoring them
  first can raise the score-window cutoff before sacrifice enumeration reaches large branching factors. The pre-pass is
  still controlled by `GCHECKERS_HOMEWORLDS_GOOD_MOVE_ORDERING=off` for old-order comparisons.
  Date/Author: 2026-06-21 / Codex.

## Outcomes & Retrospective

The implementation is nearly complete. Normal runs are unchanged by default, while setting
`GCHECKERS_HOMEWORLDS_GOOD_MOVE_PRUNING=verify` exposes would-prune and verification-failure counters in Homeworlds
good-move traces. The eval experiment trace rows and big-move reports include the same pruning fields. The strengthened
large-yellow proof now uses a numeric catastrophe-gain ceiling; on the 3.3M report it cuts scored leaves by about 64%
in `on` mode with the same returned move digest and zero verification failures. The `homeworlds_proof_probe` tool can
now replay a big move report and show the same proof status after each step of selected `all_moves` rows.
The current revision also uses the best-score window as an early branch cutoff and orders active large-yellow
candidates before the 512-move buffer is full. The 3.3M report shows this is sound, with zero verify failures, but the
pre-512 ordering is not a large performance win for that specific report.
The collector now also performs a simple-move pre-pass from normal ship-selection states before it explores sacrifice
branches, with trace counters for the number of pre-passes and one-step continuations walked.
Focused validation and the full build pass; the full local test target is blocked by a reproducing GTK window failure
outside this backend change.

## Context and Orientation

The relevant code lives mostly in `src/games/homeworlds/homeworlds_backend.c`. The generic search engine calls
`homeworlds_game_backend.list_good_moves`, which points to `homeworlds_backend_list_good_moves()`. That function
initializes a staged move builder, recursively walks candidate choices in
`homeworlds_backend_collect_good_moves_recursive()`, and appends completed moves to `HomeworldsMoveBuffer`.

`HomeworldsMoveBuffer` scores completed play-position moves with `homeworlds_backend_score_after_move()`. It keeps a
sorted list of at most `HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_LIMIT` moves, currently 512. For Player 1, higher static
scores are better; for Player 2, lower static scores are better. Once the buffer has 512 entries, the last entry is the
current worst kept score. A branch can only be pruned if a conservative proof says that every continuation is worse
than this current worst kept score. Before the buffer is full, the collector still has a weaker but valid cutoff from
the current best score and the 50-point static-prune window: for Player 1 a completed move below `best_score - 50`
would be discarded, and for Player 2 a completed move above `best_score + 50` would be discarded.

The trace type `HomeworldsGoodMoveTrace` is declared in `src/games/homeworlds/homeworlds_backend.h`. The eval
experiment tool in `src/homeworlds_eval_experiment.c` installs a trace callback and writes move-count rows plus
optional big-move reports. Tests in `tests/test_homeworlds_backend.c` already exercise Homeworlds good-move filtering
and sacrifice ordering. Tests in `tests/test_homeworlds_eval_experiment.c` verify trace output and report contents.

## Plan of Work

First, extend the Homeworlds backend trace and configuration. Add a small public enum for pruning mode and add counters
to `HomeworldsGoodMoveTrace`. Parse `GCHECKERS_HOMEWORLDS_GOOD_MOVE_PRUNING` inside the backend so all callers share
the same behavior. Add internal stats to the recursive collector and copy them into the trace.

Second, expose the current good-move cutoff from `HomeworldsMoveBuffer` once it is full. For Player 1 this cutoff is
the current lowest score among the kept moves. For Player 2 it is the current highest score among the kept moves. The
first implementation should use strict comparisons and not prune equal-to-cutoff branches.

Third, implement yellow large-sacrifice proof logic. Detect an active large yellow sacrifice by checking that a staged
state has remaining forced yellow actions and that the staged move contains a large yellow sacrifice step. Only inspect
branches after normal move-builder application, dedupe handling, and existing good-move safety checks. The proof should
decline to prune if the branch is terminal, complete, not a play position, or lacks a cutoff. Otherwise, compute the
current static score plus optimistic buildable-color gains and catastrophe gains. Existing catastrophes count their
immediate value plus own material that remaining yellow actions could still move away first; future catastrophes count
only when same-color ships can make them profitable. If even that optimistic bound cannot beat the cutoff, the branch
is prunable.

Fourth, implement verification mode. When a branch is prunable and mode is `verify`, increment would-prune counters but
continue exploring. Pass the decision-time cutoff down the recursion. Every completed move in that verification branch
is scored normally; if any completed score beats the decision-time cutoff, increment verification failures and emit a
warning. In mode `on`, skip the branch and increment actually-pruned counters.

Fifth, update tooling and docs. The eval experiment trace header and report body should include the new counters.
`doc/OVERVIEW.md` should explain that bound pruning is optional, off by default, and traceable/verifyable.

Sixth, order active large yellow sacrifice candidates locally. A candidate list is local to one staged builder state.
When the state is inside a large yellow sacrifice, compute an optimistic priority for each candidate by applying the
candidate to a temporary child state and looking ahead only until the next real turn step is appended. The priority is
the same proof bound used for pruning: better bounds are explored first. Ties keep the original candidate order, so the
change is deterministic. The ordering has an escape hatch, `GCHECKERS_HOMEWORLDS_GOOD_MOVE_ORDERING=off`, to compare
old and new exploration on reports.

## Concrete Steps

Work from `/home/jerome/Data/gcheckers`.

1. Edit `src/games/homeworlds/homeworlds_backend.h`, `src/games/homeworlds/homeworlds_backend.c`, and if needed
   `src/games/homeworlds/homeworlds_game.[ch]` for active weight access.
2. Edit `src/homeworlds_eval_experiment.c` so trace rows and big-move reports include pruning counters.
3. Edit `tests/test_homeworlds_backend.c` and `tests/test_homeworlds_eval_experiment.c`.
4. Edit `doc/OVERVIEW.md` to describe the behavior.
5. Run focused commands:

       make test_homeworlds_backend test_homeworlds_eval_experiment
       build/tests/test_homeworlds_backend
       build/tests/test_homeworlds_eval_experiment
       git diff --check
       make

6. Before committing, run the required full local test command unsandboxed:

       make test-local

## Validation and Acceptance

With the env var unset, Homeworlds `good_moves()` should return the same move list as before. With
`GCHECKERS_HOMEWORLDS_GOOD_MOVE_PRUNING=verify`, tests should show the same returned moves as `off`, no actually
pruned branches, and zero verification failures. When the proof applies, traces should also record checked,
would-pruned, verified-leaf, and actually-pruned counts according to the selected mode.

After local ordering is enabled, exact move-list identity is no longer required to match the old traversal because the
deduper may keep a different equivalent representative. Acceptance should instead compare score quality: the returned
buffer must stay sorted from the current player's perspective, each returned move must remain within the 50-point
static-prune window, and the best and worst retained scores should be no worse than the old traversal for diagnostic
reports where both modes are compared.

The eval experiment trace header should include the new pruning counter columns, and big-move reports should contain
the same values in a human-readable form.

## Idempotence and Recovery

The work is additive and can be retried safely. Environment variables affect only the current process. Generated
`big_move_report_*.txt` files are diagnostic artifacts and should not be staged unless explicitly requested. If a
verification failure appears, leave pruning `off` by default, keep the failure counter visible, and adjust the proof
before enabling `on` behavior in tests.

## Artifacts and Notes

Useful exploratory evidence from the 3.3M report:

    total numbered streamed moves: about 3,382,932
    largest first-step prefixes:
      S2y3-  about 1,698,384
      H1y3-  about 1,596,583
      S1y2-  about 24,800
      S6y2-  about 24,797
    all g3-/g2- occurrences together: about 1,819

## Interfaces and Dependencies

`HomeworldsGoodMoveTrace` must gain fields for the pruning mode and counters. The eval experiment tool must print
those fields. If the implementation needs access to active evaluation weights, add a simple const getter in
`src/games/homeworlds/homeworlds_game.h` and implement it in `homeworlds_game.c`; do not expose mutable state.

Revision note, 2026-06-19 22:16Z: Initial ExecPlan created from the user-approved plan plus the discovery that the
provided pathological report is dominated by yellow sacrifices.

Revision note, 2026-06-19 22:30Z: Backend, trace/report, tests, and overview documentation are implemented; final
validation is still pending.

Revision note, 2026-06-19 22:42Z: Focused Homeworlds tests, formatting checks, wrap checks, and full build passed.
`make test-local` failed at `/gcheckers-window/library-loads-imported-game`; the harness reran the same isolated path
and it failed again, so the non-reproducible-failure filter correctly did not ignore it.

Revision note, 2026-06-20 07:37Z: The large-yellow proof now ignores only non-positive catastrophes and checks whether
enough same-color ships can reach the target system within the remaining yellow actions before it declines to prune.
The 3.3M probe reports zero verification failures and the same returned move digest in `off`, `verify`, and `on`.

Revision note, 2026-06-20 09:14Z: The proof now uses a numeric catastrophe-gain ceiling. Existing catastrophes can
include avoidable own loss only when the remaining yellow actions could first move those own ships away, so branches
that spend yellow actions elsewhere lose that upside in the child state. The 3.3M probe still reports zero verification
failures and the same returned digest, with `on` mode scoring 809,701 leaves instead of 2,259,128.

Revision note, 2026-06-20 10:53Z: Added `homeworlds_proof_probe` as a maintained build tool. It accepts a move report
plus `all_moves` row numbers or quoted move notations, recomputes the current cutoff, and prints the large-yellow proof
state after each prefix step.

Revision note, 2026-06-20 11:55Z: Expanded the plan to cover score-window branch cutoffs before 512 kept moves and
local ordering for active large yellow sacrifice candidates. The validation standard now allows different retained
move identities as long as the retained scores remain at least as good.

Revision note, 2026-06-20 12:20Z: Implemented early score-window cutoffs, pre-512 candidate ordering, ordering trace
counters, and proof-probe trace output. The initial full-tree ordering attempt was measured and narrowed to pre-512
ordering because it otherwise scored slightly more leaves on the 3.3M report.

Revision note, 2026-06-21: Catastrophe scoring now includes opponent ships orphaned by star destruction and signed
homeworld-star effects. This also feeds the unsafe-catastrophe filter and profitable-catastrophe collector, so
homeworld-killing catastrophes created by yellow sacrifice moves are kept instead of being rejected as local own-ship
losses.
