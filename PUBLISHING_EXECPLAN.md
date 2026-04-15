# Prepare gcheckers for Desktop App Publishing

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

`PLANS.md` is checked into this repository root and this document must be maintained in accordance with it.

## Purpose / Big Picture

After this change, `gcheckers` will stop looking like a source tree with a runnable binary and start looking like a
real desktop application that software centers and packaging systems can recognize. A user should be able to build the
project from source, install it, see a proper launcher with an icon, and view an AppStream/metainfo entry with
screenshots and a real application identity. This is the foundation needed before packaging for Flathub or other Linux
distribution channels.

The visible outcome is not only a successful build. The visible outcome is a coherent app identity and metadata set:
the same app ID appears in the `GtkApplication`, the GSettings schema, the desktop file, the metainfo file, and the
icon filename. Once that exists, validation tools such as `appstreamcli validate` and `desktop-file-validate` should
pass, and the repository should contain all the assets needed for packaging.

## Progress

- [x] (2026-04-11 12:57Z) Audited the current repository state for app identity and desktop metadata.
- [x] (2026-04-11 14:05Z) Chose `io.github.JeromeA.gcheckers` as the initial final app ID and propagated it through
  code,
  schema, metadata, and filenames.
- [x] (2026-04-11 14:15Z) Added desktop integration assets: desktop file, scalable icon, and AppStream/metainfo file.
- [x] (2026-04-11 14:25Z) Added install/build wiring so the metadata can be installed and locally validated from the
  repository.
- [x] (2026-04-11 14:40Z) Validated the build/install/metadata path locally and updated the repository docs.
- [x] (2026-04-15 12:55Z) Renamed the published app identity to lowercase `io.github.jeromea.gcheckers`, removed the
  old local Flatpak install, and reran desktop/Flatpak validation.

## Surprises & Discoveries

- Observation: the repository already has useful screenshots for software-center metadata.
  Evidence: `doc/Analysis.png`, `doc/Play_against_computer.png`, `doc/Puzzle.png`, and `doc/Variations.png` exist and
  are already curated enough to reuse in AppStream/metainfo.

- Observation: the application identity is still placeholder-grade.
  Evidence: the GSettings schema file is
  `data/schemas/com.example.gcheckers.gschema.xml`, its schema ID is `com.example.gcheckers`, and
  `src/application.c` sets `GtkApplication:application-id` to `com.example.gcheckers`.

- Observation: there is currently no desktop launcher, metainfo, or icon asset in the repository.
  Evidence: a repository scan found only the GSettings schema and screenshots; there are no `.desktop`,
  `.metainfo.xml`, `.appdata.xml`, or installed icon files.

- Observation: the repository remote already fixes both the app namespace and screenshot hosting path.
  Evidence: `git remote -v` points to `git@github.com:JeromeA/gcheckers.git`, so the chosen app ID
  `io.github.jeromea.gcheckers` matches the public repository owner/name and the metainfo screenshot URLs can point to
  `https://raw.githubusercontent.com/JeromeA/gcheckers/main/doc/...`.

- Observation: AppStream still warns pedantically when the component ID contains uppercase letters.
  Evidence: `appstreamcli validate --no-net --pedantic --explain` reported `cid-contains-uppercase-letter
  io.github.JeromeA.gcheckers`, which disappears after switching to the lowercase ID.

## Decision Log

- Decision: this plan starts with identity and metadata, not Flatpak manifests or distro packaging files.
  Rationale: packaging formats depend on a stable app ID, desktop launcher, icon, and metainfo. If those are still
  missing or unstable, packaging work will either duplicate effort or require a second disruptive rename.
  Date/Author: 2026-04-11 / Codex

- Decision: the app ID must be chosen first and treated as a cross-cutting identifier.
  Rationale: it appears in the `GtkApplication` ID, GSettings schema ID and path, desktop file name, AppStream
  `<id>`, icon name, and likely future Flatpak application ID. Changing it late would ripple through the whole tree.
  Date/Author: 2026-04-11 / Codex

## Outcomes & Retrospective

The repository now has one consistent desktop application identity: `io.github.jeromea.gcheckers`. The `GtkApplication`
ID, GSettings schema filename and XML contents, desktop file, metainfo file, and icon filename all use that same ID.

Desktop integration assets now live directly in-tree:

- `data/io.github.jeromea.gcheckers.desktop`
- `data/io.github.jeromea.gcheckers.metainfo.xml`
- `data/icons/hicolor/scalable/apps/io.github.jeromea.gcheckers.svg`

`Makefile` now supports `make install PREFIX=...`, installing the binary, desktop file, metainfo, icon, and GSettings
schema under a standard prefix layout and compiling the installed schema directory. A metadata consistency test also
installs into a temporary prefix to verify the final layout.

Validation completed with:

- `make all`
- `./test_file_dialog_history`
- `./test_desktop_metadata`
- `make install PREFIX=/tmp/gcheckers-appdir`
- `make validate-desktop-metadata`

`appstreamcli validate --no-net` now succeeds with pedantic-only output, and `desktop-file-validate` is invoked when
available via the Makefile validation target.

## Context and Orientation

This repository currently builds a GTK desktop application named `gcheckers`, but it is missing the standard files
that make Linux desktop environments and software centers recognize it as an installable app. The core application
binary is built by `Makefile`, and the GTK application object is created in `src/application.c`. The current
`GtkApplication` ID is `com.example.gcheckers`, which is a placeholder and should not be published.

GSettings is already used in the app, so the schema is another part of the public identity. The current schema file is
`data/schemas/com.example.gcheckers.gschema.xml`, and code such as `src/import_dialog.c` depends on that schema ID.
Because GSettings schema IDs and paths are part of the installed interface, they must be renamed consistently when the
real app ID is chosen.

The repository already has screenshots under `doc/`. Those are useful for `README.md`, but more importantly they are
the raw material for the AppStream/metainfo file that software centers read. AppStream is the Linux desktop metadata
format that describes an app’s name, summary, description, screenshots, categories, and launchable desktop entry.
Flathub and GNOME Software both rely on it.

What is missing today:

- a final application ID
- a `.desktop` launcher file
- an application icon under the final icon name
- an AppStream/metainfo XML file
- install rules that place these files where desktop systems expect them

In this repository, “desktop file” means a file such as `<app-id>.desktop` containing launcher metadata like `Exec=`,
`Icon=`, categories, and app name. “Metainfo” means an XML file such as `<app-id>.metainfo.xml` describing the app
for software centers. “App ID” means the reverse-DNS-style identifier such as `io.github.<name>.gcheckers` or another
real namespace you control. This plan assumes a single final app ID will be chosen before implementation starts.

## Plan of Work

First, choose the final app ID and replace `com.example.gcheckers` everywhere. This includes the `GtkApplication`
constructor in `src/application.c`, the GSettings schema filename under `data/schemas/`, the schema `<schema id=...>`
and `<schema path=...>` in the XML, and all hard-coded schema lookups in the source tree. The new ID must be used
consistently in filenames and XML contents so nothing still references the placeholder namespace.

Second, add desktop integration files under `data/` or a similarly conventional directory. Create a desktop launcher
named `<app-id>.desktop` with `Type=Application`, `Name=gcheckers`, `Exec=gcheckers`, `Icon=<app-id>`, and categories
that fit a GTK board game, for example `Game;BoardGame;GTK;`. Create an AppStream/metainfo file named
`<app-id>.metainfo.xml` with the same ID, a short summary, a longer description, and screenshot entries pointing to
the chosen hosted screenshot URLs or packaged screenshots according to the publication target. The screenshots already
in `doc/` are the content source; if remote URLs are needed for AppStream publication, the implementation must note
that and stage the local assets appropriately.

Third, add an application icon. This repo currently has no installable icon asset. Introduce at least one high-quality
source asset and installable sizes under a standard hicolor path such as
`data/icons/hicolor/128x128/apps/<app-id>.png`. If the source starts as SVG, keep the SVG in the repository and add
generated PNG sizes only if needed by the chosen install flow. The important rule is that the installed icon name must
exactly match the app ID used by the desktop file.

Fourth, extend `Makefile` so the repository can install these pieces in a predictable way. The exact packaging system
can come later, but the repo should at least support a conventional install flow under a prefix such as:

  - binary to `$(PREFIX)/bin/gcheckers`
  - desktop file to `$(PREFIX)/share/applications/<app-id>.desktop`
  - metainfo to `$(PREFIX)/share/metainfo/<app-id>.metainfo.xml`
  - icons to `$(PREFIX)/share/icons/hicolor/...`
  - schema to `$(PREFIX)/share/glib-2.0/schemas/`

If schema installation is added, document that `glib-compile-schemas` must be run on the target schema directory as
part of installation or post-install validation.

Fifth, validate the metadata and source-build path. The repository should still build cleanly from source. After
installing into a temporary prefix, validate the desktop file with `desktop-file-validate` if available and validate
the metainfo with `appstreamcli validate`. If these tools are not present in the environment, document the commands
and expected behavior in the repository so the next contributor or packager can run them.

Finally, update user-facing documentation. `README.md` should mention the build/install path briefly if it changes, and
the repository should gain a short note explaining the final app ID and the role of the metadata files so future
contributors do not reintroduce placeholder values.

## Concrete Steps

All commands should be run from `/home/jerome/Data/gcheckers`.

Start by auditing the current identity references:

  rg -n "com\\.example\\.gcheckers|application-id|schema id|schema path" src data Makefile

Expected outcome:

  The search should show the placeholder app ID in `src/application.c`, the schema XML, and any settings-related code.

Build from source before changing anything:

  make all

Expected outcome:

  The repository builds successfully and produces `gcheckers`.

After implementing the metadata and install wiring, validate the install tree using a temporary prefix:

  make install PREFIX=/tmp/gcheckers-appdir

Expected outcome:

  `/tmp/gcheckers-appdir/share/applications/<app-id>.desktop`
  `/tmp/gcheckers-appdir/share/metainfo/<app-id>.metainfo.xml`
  `/tmp/gcheckers-appdir/share/icons/hicolor/.../<app-id>.<ext>`
  `/tmp/gcheckers-appdir/share/glib-2.0/schemas/<app-id>.gschema.xml`

Then validate metadata if the tools are installed:

  desktop-file-validate /tmp/gcheckers-appdir/share/applications/<app-id>.desktop
  appstreamcli validate /tmp/gcheckers-appdir/share/metainfo/<app-id>.metainfo.xml

Expected outcome:

  Both commands exit successfully or emit only non-fatal warnings that are explicitly understood and documented.

## Validation and Acceptance

Acceptance for this work is:

1. The placeholder app ID `com.example.gcheckers` is gone from the app, schema, and metadata.
2. The repository contains a desktop file, metainfo file, and icon asset under the final app ID.
3. The repository still builds cleanly from source with `make all`.
4. A temporary install prefix contains the binary and desktop metadata in standard locations.
5. Desktop/metainfo validation commands pass or produce only documented non-blocking warnings.
6. The screenshots already in `doc/` are reflected in the metainfo plan or implementation.

## Idempotence and Recovery

Most steps in this plan are additive and safe to rerun. Renaming the app ID is the one disruptive step, so it should
be performed as a single coherent change rather than piecemeal commits that leave mixed identities in the tree. Install
validation should always target a disposable prefix such as `/tmp/gcheckers-appdir` so repeated runs do not disturb the
system. If an install step fails partway through, remove the temporary prefix and rerun.

## Artifacts and Notes

The final implementation should leave concise evidence here, for example:

  rg -n "com\\.example\\.gcheckers" src data Makefile
  (no matches)

  appstreamcli validate /tmp/gcheckers-appdir/share/metainfo/<app-id>.metainfo.xml
  /tmp/gcheckers-appdir/share/metainfo/<app-id>.metainfo.xml: OK

  desktop-file-validate /tmp/gcheckers-appdir/share/applications/<app-id>.desktop
  (no output)

## Interfaces and Dependencies

At the end of this work, the following repository-level interfaces must exist:

- a single final app ID used by:
  - `src/application.c`
  - `data/schemas/<app-id>.gschema.xml`
  - the schema XML `id=` and `path=`
  - the desktop file name and icon name
  - the metainfo `<id>`

- installable metadata files:
  - `data/<app-id>.desktop` or an equivalent generated source file
  - `data/<app-id>.metainfo.xml`
  - one or more icon files under `data/icons/.../<app-id>.<ext>`

- `Makefile` install support that can place those files under a configurable prefix

If tool-specific validation dependencies are added to the documented workflow, they should be treated as optional
validation tools rather than hard build dependencies unless the repo explicitly decides to enforce them in CI.

Revision note (2026-04-11): Initial ExecPlan created from the repository audit of app ID, schema, screenshots, and
missing desktop integration files.
