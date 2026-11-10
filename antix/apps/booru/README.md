# Booru for ReRead

An i386 Qt5/X11 browser for e621/e926, using the shared ReRead UI 5
`Screen`, list/detail helpers, palettes, physical input mapping and hold-to-show
Guide overlay. Requires ReRead 0.21+. Full-size images use the installed
default image application through GIO.

## The entire app flow

The normal path is **Home → Search editor → Results → Post preview**.
**Back** reverses that path, so repeatedly pressing it always reaches Home
and then exits; it never loops between the editor and the gallery.

Home offers:

- **Search editor** — resume the *current* search, without resetting it.
- **Recent posts** — start a fresh, empty search and open the results grid.
- **My favorites** — open the same editor with an editable `fav:username`
  token. This requires a username in the web Booru settings.
- **Saved searches** — open a saved query in that same editor.
- **Switch server** — only shown when multiple hosts are configured. Changes
  host and clears the old host's results/search without touching bookmarks.

### Search editor

There is one active search string at the top. Its terms are directly
selectable underneath: `cat`, `-dog`, `fav:username`, `rating:s`,
`order:score` and any other accepted term.

- Up/Down selects a term or action.
- OK on a term **removes it**, even `fav:username`.
- Left/Right on an ordinary tag includes/excludes it. On supported filter
  terms they move between values. Holding Left/Right never repeats edits.
- OK on an action adds tags, changes filters, saves the search or chooses
  **View results**. View results just navigates to the grid; it is not Apply.
- Options is **Edit search** everywhere. On the editor itself it simply
  leaves the editor open; Back goes Home.

Every edit immediately changes the one query. Network requests are
coalesced after 500 ms rather than generating requests on each button
repeat. No separate active-filter menu, staging area or Apply action exists.

### Tag pickers

**Find tag by name:** Move over the character grid with the D-pad and press
OK to type. View switches between the character grid and matching tag list;
Up/Down moves through the results, OK adds a selected tag. Zoom − deletes
the last character. The DEL and CLR keys on the grid also work.

**Popular tags** and **Tags from selected post:** Up/Down selects a tag,
OK adds it to the *same* search, Back returns to the editor. The post tags
use e621's section order: Invalid, Artists, Contributors, Copyrights,
Characters, Species, General, Meta, Lore. Empty sections are omitted.
Category colors follow e621's standard hues, adjusted for the shared
ReRead palette. The tag arrays are supplied by the posts API.

Tag pickers are **add-only**. To exclude, change or remove a tag, return
to the editor: it is the only place that edits the query.

### Results grid and post preview

The results grid contains six thumbnails (3 columns × 2 rows); more posts
append near the end. Post previews use Left/Right to select neighboring
posts. Previews and grid use the same bounded, paced thumbnail queue;
post previews prioritize the current post and adjacent posts.

| Button | Results grid | Post preview |
| --- | --- | --- |
| D-pad | Navigate the grid | Left/Right: previous/next post |
| OK | Open selected post preview | Open selected image |
| View | Open selected image directly | Open selected image |
| Play | Toggle account favorite | Toggle account favorite |
| Shoot | Download original | Download original |
| Options | Edit the one search | Edit the one search |
| Back | Search editor | Results grid |
| Favorites | Close Booru | Close Booru |

**Options never starts downloads. OK never favorites.** Shoot saves to
`Pictures/Booru`; after completion Booru returns to the browsing screen
that initiated the download. Back during a download cancels it and returns
to its originating view; Options cancels it and goes to Search editor.
The default Images application handles zoom/pan, rotation and playback
where supported. Booru never reimplements those controls.

### Control behavior on every screen

| Screen | OK | Options | Back |
| --- | --- | --- | --- |
| Home | Open selected destination or switch server | Search editor | Exit |
| Search editor | Remove selected term or activate selected action | Stay in search editor | Home |
| Tag picker | Type/add selected tag | Search editor | Search editor |
| Results | Post preview | Search editor | Search editor |
| Post preview | Open image | Search editor | Results |
| Downloading | — | Cancel, then search editor | Cancel, then origin |

All button actions that open viewers, change screens, toggle favorites or
start downloads require a **press**, not a repeat event. D-pad navigation
may repeat. Guide shows the actual actions for the current screen; hold
it to show and release to hide. Favorites closes the app on all screens.
Power, volume, Hold and sleep remain owned by ReRead.

The old conditional "open images automatically" preference was removed:
entering the post preview no longer unexpectedly launches the image viewer.
Users open it explicitly with OK or View.

## Configuration

Use ReRead's standard web **Apps → Booru → Settings** for HTTPS server
addresses, usernames and API keys. Booru reads the shared conffile
`/etc/reread/apps/booru.json` but does not install a separate web UI.
Bookmarks live in app-private state. The removed legacy preset-search
configuration is not needed.

## Local build and tests

From the ReRead checkout:

```sh
docker run --rm -v "$PWD:/work:z" reread-builder python3 /work/antix/apps/booru/package.py
```

Output: `out/antix/reread-app-booru_0.9.0_i386.deb`. Install through the
Reader's normal web Apps page or Software.

`test-opening.cpp` checks query editing and favorites removal, tag-category
ordering, Home shortcuts and server switching, Guide hints, one-shot button
behavior, the Back hierarchy, download cancellation, thumbnail selection,
image cache and GIO default image opening. These checks must be compiled/run
in the local i386 builder; source inspection alone does not establish that
the hardware behaves correctly.
