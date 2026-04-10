BUGS

This document lists past bugs, their symptoms, and how they were fixed. Add any new bugs you find to the end of this
list. The first entry below shows a template.

## Short description of the bug

[How the code was trying to do things right]

[What was actually happening and why]

[How it was fixed]

## Puzzle continuation targets used searched depth-0 score instead of static material

Puzzle continuation extraction should stop when pure board material reaches the depth-8 target score for the candidate.

The generator used `checkers_ai_alpha_beta_evaluate_position(..., depth=0)` as its "eval0". After forced-move depth
extensions were introduced, depth-0 search was no longer purely static material and could change across non-capturing
moves due to forced tactical continuations.

The fix adds a dedicated public API `checkers_ai_evaluate_static_material()` and updates puzzle continuation building to
use that static evaluator for target matching.
