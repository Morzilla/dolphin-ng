# What Dolphin NG can do

A tour of Dolphin NG, one feature at a time, with how to reach it. For building and installing see
[NG.md](NG.md). All screenshots are of a demo folder.

- [Views: cards and drawn rows](#views-cards-and-drawn-rows)
- [Files drawn, not iconised](#files-drawn-not-iconised)
- [Quick actions](#quick-actions)
- [Work in progress, on the files themselves](#work-in-progress-on-the-files-themselves)
- [The pipeline: every operation on your files in one view](#the-pipeline-every-operation-on-your-files-in-one-view)
- [Selection](#selection)
- [Pinned to the top](#pinned-to-the-top)
- [Sorting, filters and groups](#sorting-filters-and-groups)
- [Sidebar](#sidebar)
- [Settings](#settings)

## Views: cards and drawn rows

**Cards** are a fourth view mode (Ctrl+4, or the Cards button in the bar above the files). Every item is a
card with its details, in a grid that follows the width of the window. Three looks, picked from the same button:

| Tessera – the picture across the top | Scheda – the picture on the left, compact |
|---|---|
| ![Tessera cards](ng/screenshots/features/tessera.png) | ![Scheda cards](ng/screenshots/features/scheda.png) |

**Dossier** is the file's identity card: a header in the colour of its kind, then what is inside the file, three
facts large with their word (pages, words and reading time; size in pixels, when a photo was taken and with what
aperture; duration, resolution and frame rate; files and unpacked size of a ZIP; lines and encoding of a text), where
it came from ("Downloaded from …", the camera, the artist, the author), its tags and rating, and the dates on one
line at the foot. A folder shows a bar of the kinds it holds and the entry changed last. The facts are read in the
background with KFileMetaData (no Baloo needed), only for the cards on screen:

![Dossier cards](ng/screenshots/features/dossier.png)

The **details view** becomes two-line **rows**, drawn like the cards: the drawing of the file at the start, the
name and its kind in words, the created and modified dates at the end. There is no column header; the bar above
the files sorts and filters instead.

![Drawn rows](ng/screenshots/features/home-rows.png)

A **Content** column (the Columns button in the bar, or the column header's menu) tells what is inside each file in
one place, with its unit: "24 pages", "123 words", "2:47", "1600×900", and for a folder what it holds ("2 audio ·
1 image"). It is read from the files themselves, like the Dossier, so it is filled in folders the search index
does not cover. Sorting by it keeps pages with pages and durations with durations, and grouped it gives a section
per unit (Folders, Pages, Words, Duration, Pixels, Other). In the two-line rows it joins the second line.

## Files drawn, not iconised

- **Type and size are written into a drawing of the file** instead of a badge: as the title of a page (an archive
  is a page with a zip down its side), on the front of a folder.
- **Real previews become objects:** a picture is a print with its white margin (type and size on it), a PDF a
  page with a footer, a video a frame on a strip of film.
- **A folder with its own cover** (folder.jpg, cover.jpg, front.jpg, albumart.jpg, also .png or .webp) shows that
  picture whole inside the folder on its Tessera card: an album looks like its record.
- **Folders** wear the accent colour, so they stand out from files; they are flat folders with their item count, and their size when Dolphin measures folders
  (NG settings → Rows; it keeps the disk busy for a while, and the option says so).
- **The well-known folders of the home** (Desktop, Downloads, Documents, Pictures, Music, Videos, Templates,
  Public, from xdg-user-dirs) are recognised by where they are, with their own colour and sign.

![The home as cards](ng/screenshots/features/home-cards.png)

## The Information panel

The Information panel (F11) is drawn again: one place for what the cards show about a file and for what can be
done with it. At the top a large preview: the picture (its eye opens the lightbox), the first lines of a text or
of code, a folder's own pictures; audio and video play right there. Over it the kind as a badge and the facts as
pills (size, pixels, pages, lines, duration… read from the file, without the search index). The wheel or the −/+
buttons zoom the picture (drag it to move around, a double click shows it whole); the card's bottom edge drags it
taller or shorter. Then the name with buttons to copy it and to pin the item; the quick actions of its kind in one
pill of icons, and the work in progress on the file; what a folder holds, by kind; its dates on one line; where it
came from and who made it; its rating and tags, changed on the spot. With several items
selected: their pictures, how many and how large, and a bar of the kinds they are. The panel is about the item
selected; "Show item on hover" in its right-click menu makes it follow the pointer too (off by default), and a file
seen again is shown at once, its preview not loaded anew. NG setting `INFO=classic` (in `dolphinrc`, group
`[DolphinNG]`) brings back Dolphin's own panel.

## Quick actions

Hovering a card or a row brings a pill of actions for that kind of file. They run in the background and show
their progress on the file (see below).

| Kind | Actions |
|---|---|
| Images | Resize (75, 50 or 25 %, or to a width of 800, 1280 or 1920 px); an animated GIF to MP4 |
| PDF | To PNG, extract the text |
| Audio | To MP3, to FLAC, transcribe (optional, with a local Whisper model) |
| Video | To MP4, extract the audio, separate audio and video, a frame to PNG, transcribe |
| Archives | Extract here |
| Everything | The next applications for the type, Open with…, cut, copy, pin |

While Ctrl or Shift is held (making a selection) the actions stay hidden and the click selects.

**Your own actions.** Settings → Configure Dolphin → **NG quick actions** lists the kinds of files and their actions:
drag to change the order (the first one shows its name in the pill), untick to switch one off, mark another one as
the main action. *Add* makes an action of your own from a command, from one of the service menus already installed
(the right-click ones) or from an application. A command runs with `sh` in the file's folder, with `{file}`,
`{name}`, `{ext}`, `{dir}` and `{choice}` (an optional small menu, e.g. `75%, 50%, 25%`) quoted for you, inside
quotes or not; what it makes next to the file shows up beside it like the built-in ones. An action can ask before it
runs. Kinds of your own go by extension or type (`*.md`, `text/markdown`). The actions are `.desktop` files in
`~/.config/dolphin-ng/actions`, easy to copy and share.

![The quick actions editor](ng/screenshots/features/actions-editor.png)

## Work in progress, on the files themselves

A copy, a conversion or a transcription shows on the file it works on: a capsule with the percentage, what is going
on and a stop button. The file it makes appears next to it at once ("On its way…") and the two share a bubble in
the colour of the source until 30 seconds after the work ends. Grouped by date, the new file stays in its
source's group while they are tied. The work also shows in Plasma's job view, and keeps going if Dolphin is
closed.

| Cards | Rows |
|---|---|
| ![Work in progress on cards](ng/screenshots/features/work-cards.png) | ![Work in progress on rows](ng/screenshots/features/work-rows.png) |

## The pipeline: every operation on your files in one view

Work on files is usually scattered: a copy in a notification, a conversion in a terminal, a download in the
browser. Dolphin NG gathers it in one pipeline, with one view of what is going on and of what has been done.

**The Jobs button** sits in the title bar (with the system's title bar: after the location bar, between the search
and the menu). It shows a clock when nothing runs and "2 jobs" while something does; a click lists the work going
on and today's last operations, and opens the Jobs tab (also Ctrl+Alt+J).

**The Jobs tab** is a tab like any other:

- **At work**, at the top: a card for every quick action running or waiting in the queue, with the file and its
  path, what it is making, a bar with the percentage and the time left when the program tells them, a button to
  show the file in Dolphin NG and one to stop the work (or to take it off the queue).
- **History**, under it: a line of time with every operation that ended, the last first. Quick actions, copies,
  moves, deletions, items sent to the trash and downloads seen arriving in a folder. Each line names the action
  and the file with its path, then where it went or what it made, how long it took and whether it was stopped.
  One click shows the result in its folder.
- **By day or by action**: the same history grouped by what was done, still the last first.
- **Search**: the field next to the location bar looks through the jobs while this tab shows.
- Back leads to the tab you came from.

**One pipeline for all windows.** Every Dolphin NG window and process shows the same list: an action started in
one window is at work in all of them, and is written to the history once. Quick actions run on their own: close
Dolphin NG and they go on; open it again and they are back in the list.

**Where a file comes from.** A file made by a quick action remembers its source and the action that made it: the
card says "from talk.ogg · Transcribe", the Information panel too.

**In parallel or in a queue.** Quick actions start at once by default; with "one at a time" (Configure › NG ›
Quick actions) they wait for each other. A second action on a file that is being worked on always waits.

| The Jobs tab | In the folder, meanwhile |
|---|---|
| ![The Jobs tab: work going on and the history](ng/screenshots/features/pipeline-jobs.png) | ![Two conversions at work, each next to the file it is making](ng/screenshots/features/pipeline-files.png) |

## Selection

A selected item gets a ring set off from it, a check at its corner and a light wash of the accent colour. Items
selected next to each other share one outline. Selecting with Ctrl or Shift next to a selection grows the outline
over the new item; a plain click that moves the selection fades instead. Cut files are greyed out and see-through.

![Selection](ng/screenshots/features/selection.png)

## Pinned to the top

Right-click → **Pin to the Top**, the pushpin button among the quick actions, or Ctrl+Alt+P keeps files and folders
at the top of their folder, whatever the sorting. A small pushpin marks them and the sidebar lists them in a
**Pinned** section, visible in the cards screenshots above. The pin is an extended attribute on the file
(`user.ng.pinned`), so it follows the file when it is moved or renamed.

## Sorting, filters and groups

The bar above the files has, from left to right:

- **Sort** – one button per criterion (click the active one to reverse); a single "Sort: …" button when the window
  is narrow. Sorted by date modified, a folder counts as modified when something inside it was, not only when
  files were added or removed.
- **Groups** – right beside the sorting: like the sorting (dynamic), none, date modified, date created, type or
  size; grouping by a date sorts by it too.
- **Cards / view** – switches between views and card looks.
- **Filters** – only files, only folders, or one kind (images, documents, spreadsheets, archives, audio and video,
  code…), each with its count. The same list is in the sidebar.
- **Search in folder** – narrows the list as you type.

![Grouped by date modified](ng/screenshots/features/groups.png)

## Sidebar

The places sit on a card of glass that shows the desktop behind it, blurred by the compositor (or a blurred copy
of the wallpaper, or a plain panel). Below them come the **Pinned** items and the **Filters**; at the foot, the
disks: the home one first, then every other disk and partition as a box with its usage (an unmounted one is dashed:
a click mounts and opens it). NG settings → Sidebar chooses between these boxes, the classic **Devices** section or
none, and lists the disks to hide any one of them; phones through KDE Connect get a box too. One setting puts the
sections right after the places instead of at the foot. With the opaque looks of Breeze NG (Soft, Grey) the card is
opaque too. Without the compositor's blur, the card shows the blurred picture of the wallpaper instead of the desktop.

On glass the text of the sidebar, of the cards and rows (in Aurora), of the location bar and the search field gets a
soft shadow of the opposite tone, as on macOS, so that it stays readable over a white or black window behind: Breeze
NG's settings → Transparency → Text shadow (*When the contrast may be low*, *Always*, *Never*).

The sidebar takes the whole left side of the window: by default the window has no title bar of its own, and the
sidebar button, the tabs and the window's buttons make its top row (NG settings → Sidebar → Window to change it).

![The glass sidebar](ng/screenshots/features/sidebar.png)

## Settings

Settings → Configure Dolphin → **NG** has everything above as switches and tiles, with a sketch of the window
that follows the choices: three starting looks (Classic, Soft, Glass), then pages for the look, the sidebar, the
rows, the cards and the previews.

![The NG settings page](ng/screenshots/features/settings.png)
