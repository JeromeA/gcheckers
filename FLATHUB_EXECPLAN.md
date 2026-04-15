# Prepare gcheckers for Flathub Submission

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

`PLANS.md` is checked into this repository root and this document must be maintained in accordance with it.

## Purpose / Big Picture

After this change, the repository will not only install cleanly as a desktop app, it will also carry the files and
runtime behavior needed to build `gcheckers` as a Flatpak and prepare a Flathub submission. A contributor should be
able to build the app inside Flatpak, run puzzle mode without relying on the source checkout layout, and validate that
the in-tree Flatpak manifest still matches the app ID, command, metadata, and installed asset layout.

The visible result is that the repository contains an upstream Flatpak manifest, a packaging-safe runtime data path for
puzzles, and clear documentation of the remaining submission blockers. The final Flathub pull request will still need
store-facing review, but the upstream repository should already be doing the right thing for local Flatpak builds.

## Progress

- [x] (2026-04-11 15:05Z) Audited the current repository state after the desktop metadata work.
- [x] (2026-04-11 15:20Z) Identified a real Flatpak blocker: puzzle mode currently depends on a repo-relative
  `puzzles/` directory.
- [x] (2026-04-11 15:40Z) Added an upstream Flatpak manifest, a manifest sanity test, and packaging-safe puzzle asset
  lookup plus installed puzzle assets.
- [x] (2026-04-11 15:55Z) Confirmed the new puzzle data path and Flatpak manifest with local builds/tests and
  documented the remaining submission blocker.
- [x] (2026-04-14 10:40Z) Added the repository license plus metainfo `project_license`, an initial release entry, and
  immutable screenshot URLs pinned to a commit.
- [ ] Run `flatpak-builder-lint` / `appstreamcli` and a real local Flatpak build, then decide whether to keep the
  in-repo manifest as a local-build manifest or mirror it directly into the Flathub packaging repo.

## Surprises & Discoveries

- Observation: the previous desktop-app work did not make puzzle mode install-safe.
  Evidence: `src/window.c` resolved puzzles from `GCHECKERS_PUZZLES_DIR` or the literal relative path `puzzles`.

- Observation: Flatpak readiness is blocked by metadata beyond the manifest itself.
  Evidence: the repository still has no declared project source license, so the metainfo file cannot yet be completed
  with a reliable `<project_license>` value.

- Observation: metadata blockers are now reduced to validation and release policy rather than missing core fields.
  Evidence: the metainfo now contains `project_license`, a `<releases>` section, and screenshot URLs pinned to commit
  `97284d7a8dfb8146ed14e4ffad8e83567eec332b`, and the repository metadata tests pass.

- Observation: the Flatpak-prep install path now carries the bundled puzzle SGFs successfully.
  Evidence: `find /tmp/gcheckers-flatpak-appdir/share/gcheckers/puzzles -maxdepth 1 -type f | head` lists
  `puzzle-0001.sgf`, `puzzle-0002.sgf`, and more.

## Decision Log

- Decision: carry an upstream Flatpak manifest in this repository now, even though Flathub packaging may later live in
  a separate submission repository.
  Rationale: an in-tree manifest gives this repository a concrete, testable packaging contract and keeps future
  Flathub work aligned with upstream install behavior.
  Date/Author: 2026-04-11 / Codex

- Decision: solve the puzzle asset path generically by searching application data directories, not by hardcoding
  `/app/share` or adding a Flatpak special case.
  Rationale: the same lookup order works for standard installs, Flatpak installs, and local development checkouts, and
  it keeps platform packaging details out of UI code.
  Date/Author: 2026-04-11 / Codex

## Outcomes & Retrospective

The first packaging slice is in place: the repo now contains a Flatpak manifest and the puzzle feature is no longer
coupled to the source-tree layout. Local validation now covers the new app-data lookup, the manifest contract, the
desktop metadata install path, the installed puzzle asset directory, and the metainfo fields needed for Flathub review.
Remaining work is narrowed to external validation (`flatpak-builder-lint`, `appstreamcli`, local Flatpak build) and
submission policy rather than basic runtime correctness or missing metadata.

## Context and Orientation

This repository already has a coherent desktop identity under `io.github.jeromea.gcheckers`. `Makefile` can build and
install the app, and `data/` now contains the desktop file, metainfo file, icon, and GSettings schema. However,
Flathub submission needs more than that. It needs a Flatpak manifest, and it needs the installed app to behave
correctly when run from packaged data directories instead of the repository checkout.

In this repository, the main packaging-sensitive feature is puzzle mode, implemented in `src/window.c`. Before this
plan, puzzle mode loaded from a relative `puzzles/` directory. That works when the app is started from the source
checkout, but not when the app is installed into `/app` inside Flatpak or `/usr/local` from `make install`. A
“system data directory” means a directory from GLib such as `/usr/share` or `/app/share`. Those are the correct places
to look for read-only installed game assets.

The Flatpak manifest is the YAML file named after the app ID at the repository root. It tells Flatpak which runtime
and SDK to use, what command to launch, what permissions the app needs, and how to build/install the application into
the Flatpak image. The manifest added by this plan is intended as an upstream local-build manifest; later Flathub
submission work can either reuse it directly or copy it into the Flathub packaging repository with a different source
section if needed.

## Plan of Work

First, add a small runtime path helper in `src/app_paths.c` and `src/app_paths.h`. This helper must resolve a
named application data subdirectory by checking, in order: an explicit environment override, the user data directory,
the system data directories, and finally the local checkout directory. The helper should return the first existing
directory it finds, but if nothing exists it should still return the preferred installed path so error messages point
to the packaging location rather than the repository fallback.

Second, update `src/window.c` to use that helper for puzzle discovery instead of the old hardcoded `puzzles`
directory. This is the behavior change that makes puzzle mode packaging-safe. The app should still honor
`GCHECKERS_PUZZLES_DIR`, so local overrides and tests remain possible.

Third, teach `Makefile` to install the bundled puzzle SGF files under `$(PREFIX)/share/gcheckers/puzzles`, because the
new runtime lookup depends on a standard installed asset location. Add a small unit test for the app path helper and a
shell-based sanity test for the Flatpak manifest. The shell test is intentionally static: it verifies that the
manifest still matches the app ID, runtime, command, and permission decisions even when `flatpak-builder` is not
installed.

Fourth, add `io.github.jeromea.gcheckers.yaml` at the repository root. The initial manifest should use the GNOME
runtime and SDK, launch the `gcheckers` command, allow network access for the BoardGameArena import feature, and use
`make install PREFIX=/app` so the Flatpak build consumes the same install layout as the desktop-app work.

Finally, document the remaining blockers for an actual Flathub submission. The main known blocker is licensing:
without a declared project source license in the repository, the metainfo file cannot be completed with a reliable
project license entry and Flathub review cannot be finished responsibly.

## Concrete Steps

All commands should be run from `/home/jerome/Data/gcheckers`.

Build the app after changing the puzzle data lookup and install rules:

  make all

Expected result:

  The repository still builds successfully and produces `gcheckers`.

Run the new non-GTK tests:

  make test_app_paths test_flatpak_manifest
  ./test_app_paths
  ./test_flatpak_manifest

Expected result:

  `test_app_paths` reports two passing checks and `test_flatpak_manifest` exits successfully with no output.

Re-run the existing desktop metadata install test because the install target now installs additional game assets:

  ./test_desktop_metadata

Expected result:

  The test exits successfully with no output.

Verify the installed asset layout still includes puzzles:

  make install PREFIX=/tmp/gcheckers-flatpak-appdir
  find /tmp/gcheckers-flatpak-appdir/share/gcheckers/puzzles -maxdepth 1 -type f | head

Expected result:

  The directory exists and contains installed `puzzle-*.sgf` files.

If `flatpak-builder` is available locally, the next contributor can build the manifest with:

  flatpak-builder --force-clean flatpak-build io.github.jeromea.gcheckers.yaml

Expected result:

  Flatpak builds the app using the in-tree manifest and install rules. If runtimes are missing, Flatpak will prompt
  to install them first.

## Validation and Acceptance

Acceptance for this slice is:

1. Puzzle mode no longer depends on the repository-relative `puzzles/` path when the app is installed.
2. `make install PREFIX=...` installs puzzle assets under `share/gcheckers/puzzles`.
3. The repository contains a Flatpak manifest named `io.github.jeromea.gcheckers.yaml`.
4. Static tests verify that the manifest still matches the app ID, runtime, command, and basic permission decisions.
5. The remaining blockers for Flathub submission are explicitly documented instead of being left implicit.

## Idempotence and Recovery

All steps in this plan are additive and safe to rerun. `make install PREFIX=/tmp/...` should always target a disposable
directory so installed Flatpak-prep assets can be inspected and then discarded. The Flatpak manifest is purely
upstream metadata; editing it does not affect the normal source build unless a test or local Flatpak build is run.

## Artifacts and Notes

The final implementation should leave concise evidence such as:

  ./test_flatpak_manifest
  (no output)

  ./test_app_paths
  ok 1 /app-paths/env-override-wins
  ok 2 /app-paths/system-data-dir-is-used

  ./test_desktop_metadata
  (no output)

  find /tmp/gcheckers-flatpak-appdir/share/gcheckers/puzzles -maxdepth 1 -type f | head -3
  /tmp/gcheckers-flatpak-appdir/share/gcheckers/puzzles/puzzle-0001.sgf
  /tmp/gcheckers-flatpak-appdir/share/gcheckers/puzzles/puzzle-0002.sgf
  /tmp/gcheckers-flatpak-appdir/share/gcheckers/puzzles/puzzle-0003.sgf

## Interfaces and Dependencies

At the end of this plan, the following repository-level interfaces must exist:

- `src/app_paths.h` declaring:

    char *gcheckers_app_paths_find_data_subdir(const char *env_name, const char *subdir_name);

- `src/app_paths.c` implementing the search order:
  environment override, user data directory, system data directories, local checkout fallback.

- `src/window.c` using that helper for puzzle discovery.

- `Makefile` installing bundled puzzle SGF assets to `$(PREFIX)/share/gcheckers/puzzles`.

- A Flatpak manifest file at `io.github.jeromea.gcheckers.yaml`.

- Test artifacts:
  `tests/test_app_paths.c` and `tests/test_flatpak_manifest.sh`.

Revision note (2026-04-11): Initial Flathub-preparation ExecPlan created during the first packaging milestone. It
captures the upstream manifest work, the puzzle asset path blocker, and the remaining licensing blocker.
