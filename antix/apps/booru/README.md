# Booru for ReRead

An independent i386 Qt5/X11 browser for the e621/e926 API, built against the
shared ReRead UI 5. Requires ReRead 0.21 or later. No custom UI framework,
permanent top bar or duplicate image viewer: images open through the system's
default MIME viewer.

## One search editor

**There is one active search and one place to edit it.** Choose Search editor on
Home, or Options from gallery results. The exact active query is shown at the
top, like a booru website's search bar. Underneath, every word in that query is
a selectable row, including special terms such as `fav:username`,
`rating:s`, `order:score`, `cat` and `-dog`.

On a selected term:

- **OK removes it immediately**—including `fav:username` when opening My favorites.
- **Left/Right** includes or excludes an ordinary tag. On a supported filter
  term, Left/Right cycles its value.
- Moving to the **Add...** actions lets you look up a tag by name, browse
  popular tags, choose tags from the post you were viewing, or insert/change
  favorites, sorting, rating, file type, or score filters.

The **Tags from selected post** picker groups tags in e621's actual sidebar order:
**Invalid, Artists, Contributors, Copyrights, Characters, Species, General,
Meta, Lore**. Empty groups are omitted. Each section has a colored heading and
tags have a category-colored indicator. Colors follow e621's default theme,
adjusted for readable contrast on ReRead's light display. The data comes from
the post's existing `tags` JSON object, not extra API calls; popular and
autocomplete tags also use the API's category IDs for their indicators.

Tag pickers are **add-only**; they return to this editor. They do not contain
a second search, an Apply button or another filter state. The D-pad character
picker allows text entry without a hardware keyboard, retrieving tag matches
from the site's tag API. Reader-only tag choices are never separately entered
into web settings.

All edits immediately change the **same query**. After 500 ms of inactivity,
Booru refreshes results automatically, coalescing rapid button presses to
avoid unnecessary requests. **Options / Results** only navigates to results;
it never commits a separate query. Back returns to the editor and retains it.

Home has Search editor, Recent posts, My favorites and saved searches.
Opening My favorites inserts a regular `fav:username` term that can be
removed with OK like anything else. The Search editor home entry reopens the
existing active query rather than clearing it. Saved searches are snapshots
that can be opened and then freely edited.

## Browsing and images

Gallery navigation uses a 3×2 thumbnail grid. The next 30 posts append as
you near the end. Opening a post displays its preview/details; Left/Right
browses posts without losing your gallery position. Preview thumbnails
continue loading while you browse post cards, prioritizing the selected post
and nearby posts instead of the gallery's old six-card window. The asynchronous
queue is paced and limited to two in-flight requests, with a transfer timeout,
delayed retry for transient responses and bounded RAM cache.

View opens the downloaded sample via GIO and the default Images app; this
uses its existing zoom, pan and rotation controls. OK on a post card
toggles an account favorite and Options saves the original image under the
shared Pictures/Booru files folder. Play returns to the one search editor
and makes that post's tags available through its Add from post action.

Hold Guide for the ReRead physical-button help overlay; release it to hide.
Favorites closes Booru. Core ReRead owns power, volume, Hold and sleep.

## Configuration and build

Use the standard **web Apps → Booru → Settings** only for server URLs,
usernames and API keys. The app reads `/etc/reread/apps/booru.json`
and its packaged JSON schema. Reader-side preferences and search bookmarks
are private app data; no migration of old search configuration is required.

Build from a ReRead source checkout using the existing builder:

```sh
docker run --rm -v "$PWD:/work:z" reread-builder python3 /work/antix/apps/booru/package.py
```

Output: `out/antix/reread-app-booru_0.8.1_i386.deb`. Install through the
Reader's normal web Apps page or Software. This does not rebuild core ReRead.

`test-opening.cpp` includes disposable-data checks for direct removal of
favorites and other query terms, include/exclude, adding from tag discovery,
post-to-editor navigation, thumbnail priorities, caching and default image
MIME dispatch. Tests and source inspection are not a substitute for building
the i386 package and testing navigation on real Reader hardware.
