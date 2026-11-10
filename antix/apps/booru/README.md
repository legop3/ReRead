# Booru for ReRead

Booru is a button-operated e621/e926 browser for the Intel Reader. It uses
ReRead's shared UI 5 `Screen`, palette, list/detail helpers and Guide overlay.
Full images are opened in the system's installed Images app through GIO.

## Navigation

Booru has one active search and **one search editor**. The main screens are:

1. **Home**: continue your search, view Recent posts, open My favorites,
   choose a saved search, or switch server (when multiple servers exist).
2. **Results**: browse the six-post thumbnail grid.
3. **Post**: preview the selected post, move through posts, favorite or
   download.
4. **Search editor**: edit every term in the active query, and add tags from
   tag finders or the current post.

**Options toggles Search from every browsing screen.** From Results it opens
the editor; pressing Options again returns to Results. From a Post it opens
the same editor **focused on "Tags from post #…"**, with the post's category
counts visible beside it. Press Options again to return to that same post
if the query is unchanged. If you add, remove, or change any terms, it
returns to the refreshed Results instead; a post from the previous query
might no longer match.

Back from the editor follows the same return behavior. Back from a Post
returns to Results. Back from Results returns Home. Back from Home closes
Booru. There is no Search ↔ Results navigation trap, no separate post
search and no hidden Apply step.

### One editable query

The editor displays the actual query once, across the top, and its
individual terms as selectable rows. Terms include ordinary tags,
exclusions such as `-cat` and metatags such as `fav:username` and
`rating:s`. Select a term and press OK to remove it, including
`fav:username`. Left/Right includes or excludes ordinary tags or changes
a supported filter value. Unrecognized metatags can still be removed but
do not advertise unsupported Left/Right actions.

The action list below the terms allows you to find tags by name using a
D-pad character grid, browse popular tags, change favorites/rating/sort/
file type/minimum-score filters, and save the search. When a post has
been selected, **Tags from post #…** appears first among these actions.
The post tag snapshot remains available even after you change the search
and the old results are replaced.

Tags from a post are organized in e621's sidebar order: Invalid, Artists,
Contributors, Copyrights, Characters, Species, General, Meta, Lore.
Empty sections are skipped; category markers use e621's hues with
readable ReRead text contrast. Post details show a concise category
summary rather than duplicating a giant raw tag string.

A tag picker **adds** terms to the one editor. To exclude or remove a
term, return to that editor. The D-pad tag finder uses View only to
switch between its letter grid and suggestions, and Zoom − to delete
a character. Search changes refresh the results automatically after
a short debounce (500 ms). Rapid repeated keypresses do not launch
duplicate requests.

## Physical controls

| Screen | D-pad | OK | Options | Back |
| --- | --- | --- | --- | --- |
| Home | Choose a destination | Open selection | Edit search | Exit |
| Search editor | Up/Down select; Left/Right edit supported terms | Remove term or choose action | Return to post or results | Same return |
| Tag picker | Navigate tags or letter grid | Type/add tag | Return to editor | Return to editor |
| Results grid | Move among six posts | Post preview | Edit search | Home |
| Post preview | Left/Right browse posts | Open image in Images | Edit search (post tags focused) | Results |
| Download in progress | — | — | Cancel and edit search | Cancel to originating view |

In both Results and Post, **Play** toggles a favorite and **Shoot**
downloads the original to `Pictures/Booru`. This role never changes.
There is **one image-open action**, OK on the Post preview; the View
button no longer duplicates it. Results OK opens the Post preview, not
the full image.

The Guide only lists actions meaningful on the current screen.
It does not add a redundant Home/Close control. The physical Favorites
button still closes the app, as supported by ReRead. System-owned
Power, Volume and Hold inputs remain untouched. One-shot controls ignore
key-repeat events; scrolling and D-pad movement may repeat.

The old "open images automatically" option has been removed, so navigating
into a post never unexpectedly opens another app. The selected post and
gallery position are preserved when returning without changing the search.

## Network and configuration

The results API retrieves 30 posts at a time; further batches are
appended near the end. Preview thumbnails run through a shared bounded,
paced queue. The selected Post and its neighbors are prioritized while
browsing. Temporary server failures retry with backoff; requests have
timeouts, and cache size is bounded.

Servers, usernames and API keys are configured using ReRead's standard
web **Apps → Booru → Settings** form, backed by
`/etc/reread/apps/booru.json`. Reader-created search bookmarks live in
private app data, not the old preset-search configuration.

## Build

From your ReRead checkout:

```sh
docker run --rm -v "$PWD:/work:z" reread-builder python3 /work/antix/apps/booru/package.py
```

Output: `out/antix/reread-app-booru_0.10.0_i386.deb`.
Install it through the Reader's web Apps page or Software.

The source test `test-opening.cpp` covers search editing, returning to
the originating post, post category snapshots after query edits, tag
pickers, bookmarking, Guide controls, navigation, download actions,
thumbnail priorities, caching and GIO image opening. Real device behavior
still requires running the i386 build and testing on the Reader.
