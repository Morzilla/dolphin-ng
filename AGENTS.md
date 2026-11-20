# AGENTS.md

Guidance for coding agents (and people) working on Dolphin NG.

## What this is

Dolphin NG is a fork of KDE's Dolphin (Qt 6, KDE Frameworks 6) that changes how files are shown and clicked:
cards in three looks (Tessera, Scheda, Dossier), drawn rows, a Content column (pages, duration, pixels, read from the
files), one bar for sorting, filters and groups, groups that follow the sorting, pinned items, quick actions, work in
progress shown on the files, a glass sidebar, tabs in the title bar, a folder's own view. File operations stay
Dolphin's own. It is part of the NG desktop, with
[Breeze NG](https://github.com/Morzilla/breeze-ng) (style, decoration, Aurora Glass), Dialog NG (the Open/Save
window) and the NeoLine icons. What the user sees is described in `NG.md` and `FEATURES.md`.

The development branch is `ng`. Upstream is Dolphin from invent.kde.org; this tree is based on Dolphin 26.08.

## Layout

```
src/                        Dolphin's sources, with the NG code inside them
├── kitemviews/             the file views: cards and rows are painted in kstandarditemlistwidget.cpp,
│                           the NG helpers (ngSetting, ngTr, accents, card looks) live in kitemlistwidget.cpp/.h,
│                           groups, pinned items and the Content column's sorting in kfileitemmodel.cpp
│   └── private/ngfacts.cpp what a file holds (Dossier facts, the Content column), read with KFileMetaData, no Baloo
├── views/                  DolphinView, view properties (per folder), the item list view
├── dolphinviewcontainer.cpp  the bar above the files (NgSortBar), the quick view, the lightbox
├── dolphintabwidget.cpp    the tabs; the title row's tab bar (ngCreateTopBar), its close buttons and icons
├── dolphinmainwindow.cpp   the window: tabs in the title bar as pills (ngStyleTitleRow), client-side window buttons,
│                           the moved tool bar buttons, the window's own state group, the check for the compositor's blur
├── panels/information/nginfoview.cpp  the Information panel drawn again (NgInfoView; INFO=classic: Dolphin's own)
├── panels/places/          the sidebar (places, pinned, filters, disks): its pills and section titles are painted
│                           by the panel, glass or plain, never left to the widget style
├── settings/ng/            the NG settings pages (Configure › NG, NG quick actions)
ng/                         build.sh, install.sh, the dolphin-ng launcher, ng-job, ng-transcribe, screenshots
packaging/dolphin-ng.spec   the Fedora package (COPR morzilla/desktop-ng)
NG.md, FEATURES.md          what Dolphin NG adds, with pictures
```

NG code inside Dolphin's files is marked with a `dolphin-ng:` comment, so that it can be found and carried over to a
new Dolphin release.

## Build and run

```sh
ng/build.sh                 # configure, build, install into ~/.local/dolphin-ng (no root)
ng/dolphin-ng               # start the installed build
```

Build dependencies are Dolphin's own (Fedora: `sudo dnf builddep dolphin`). `ng/install.sh` does everything for a
user, dependencies included; it resets its copy of the sources to the last commit of `ng` and prints which commit it
builds. Building installs over the previous build: say so before building experimental code on a machine someone is
using.

Dolphin NG keeps Dolphin's component name (`dolphin`), so it shares `dolphinrc` and `dolphinstaterc` with a stock
Dolphin installed next to it. NG settings live in their own group (`[DolphinNG]`) and the window state in
`[NG State]` of `dolphinstaterc` (stock Dolphin's `[State]` is copied there once): never write NG data into groups
or keys stock Dolphin uses, or whichever closes last wins.

## Conventions

- **Keep upstream recognisable.** Change Dolphin's code only where needed, mark NG code with `dolphin-ng:`, and do
  not reformat or reorder code you do not change. New NG classes can live in new files.
- **Comments say why**, in plain English sentences: what the code is for and what went wrong without it. A guard
  that looks removable says what it prevents; read that before changing it.
- **Strings the user sees** in NG code go through `ngTr("English", "Italiano")`. Dolphin's own strings keep `i18n`.
- **Settings** live in `dolphinrc`, group `[DolphinNG]`. Read them with `ngSetting("KEY")`; every key can be
  overridden for one run with the environment variable `DOLPHIN_NG_KEY`. A new setting gets a default in
  `ngDefault()` (if it differs from "not set"), a control on the NG settings page and a line in `NG.md`.
- **Colours come from the palette.** The accent is the palette's Highlight; never hard-code a blue. A nearly
  colourless accent (saturation under 0.15) must stay neutral: do not lift its saturation. What is selected or on
  wears the accent, with text and icons white or black by its lightness (`ngOnAccent`); Aurora has its own selection
  colours (`ngAuroraSelection`, `ngAuroraSelectionText`).
- **The same look with any widget style.** The bar, the tabs of the title row and the sidebar's pills are drawn by
  Dolphin NG (style sheets and painting of its own), so that they match with stock Breeze too. Do not hand them back
  to the style.
- **Glass needs the compositor's blur.** KWin announces its blur only once the application's event loop has turned
  (asked at start, `KWindowEffects` says no): check it a moment later, never while the first window is built. Without
  blur the application property `_breeze_ng_bare_glass` is set (by Dolphin NG or by Breeze NG) and the live sidebar
  shows the blurred wallpaper picture instead of a hole.
- **Text on glass gets a shadow** (Breeze NG's `TextShadow`, read with `ngTextShadowMode()`): the sidebar and the
  location bar's buttons carry a `QGraphicsDropShadowEffect` (`ngApplyTextHalo`), the text Dolphin NG paints itself
  goes through `ngPaintTextHalo()` (lighter under the grey details). Inside a panel with that effect, wipe areas by
  clearing them (`PlacesPanel::ngClearForHalo`), not by painting the backdrop again: the shadow outlines it.
- **Check every look**: dark and light colour schemes; Breeze NG's Soft, Glass and Aurora Glass; stock Breeze; and
  for glass, with KWin's blur effect on and off (`qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect
  blur`, then wait a couple of seconds).
- **Narrow changes.** Fix what was asked, where the cause is. Do not bundle unrelated clean-ups.

## Checking a change

There are no unit tests for the NG code: a change is checked by building it and looking at it. Never send
synthetic input (fake clicks and keys) to someone's desktop; use a test machine or session. To reach a state without
input, Dolphin NG reads these environment variables:

| Variable                   | Effect                                                                        |
|----------------------------|-------------------------------------------------------------------------------|
| `DOLPHIN_NG_OPEN_SETTINGS` | `ng` or `actions`: the settings dialog opens on that page                     |
| `DOLPHIN_NG_SETTINGS_TAB`  | the tab of the NG page shown first                                            |
| `DOLPHIN_NG_DEMO_SELECT`   | `name\|name\|…`: these items are selected once the folder is shown            |
| `DOLPHIN_NG_DEMO_HOVER`    | an item drawn as if the pointer rested on it                                  |
| `DOLPHIN_NG_DEMO_MENU`     | `Button\|Item\|/path.png[\|trigger]`: opens a bar menu, saves a picture of it |
| `DOLPHIN_NG_ACTIVITY_DEMO` | work in progress drawn on the given files                                     |
| `DOLPHIN_NG_DEMO_DISKS`    | fake disks in the sidebar (`Name\|fs\|free GiB\|total GiB;…`)                 |
| `DOLPHIN_NG_DEMO_MENU_HOLD`| ms the demo menu stays open                                                   |
| `DOLPHIN_NG_TEST_DOCK_WIDTH`| the sidebar widened to (or narrowed step by step down to) that width          |
| `DOLPHIN_NG_TEST_HIDE_PLACES`| the sidebar button pressed once the window is up                             |
| `DOLPHIN_NG_TEST_INFO`     | the Information panel shown once the window is up (a number: also its width) |
| `DOLPHIN_NG_TEST_ZOOM`     | the Information panel's preview zoomed in by that factor once it has a picture, its −/+ buttons shown |
| `DOLPHIN_NG_TEST_PAGE`     | a PDF in the Information panel turned to that page once it shows |
| `DOLPHIN_NG_DEMO_CRUMB`    | that location bar button drawn as under the pointer                           |
| `DOLPHIN_NG_<KEY>`         | any NG setting, for one run                                                   |

Window actions can be triggered over D-Bus:
`dbus-send --session --dest=org.kde.dolphin-PID /dolphin/Dolphin_1/actions/NAME org.qtproject.Qt.QAction.trigger`
(for example `cards`, `details`, `sort_by_size`, `group_by_same_as_sort`, `show_ngContent`, `new_tab`,
`view_zoom_out`, `options_configure`, `file_quit`; quitting through `file_quit` saves the window state as a user's
close does).

Report what you built and looked at, and what you could not check.

## Commits and packages

- Commit on `ng`. The subject says what changed for the user; the body says why, and what was wrong before.
- `packaging/dolphin-ng.spec` builds a snapshot: after the code is pushed, set `commit` to the pushed commit and
  `snapdate` to its date and time (`YYYYMMDDHHMM`), and add a changelog line. Publishing packages (COPR builds) and
  releases is the maintainer's decision: ask first.
- Keep secrets and personal details (names, e-mail addresses, home paths of a machine) out of code, commits and logs.
