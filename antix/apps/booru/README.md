# Booru for ReRead

An independent i386 Qt5/X11 browser for e621/e926-compatible APIs. Requires ReRead 0.21+ and uses the shared UI 5 palette, list/detail layouts, physical controls and hold-to-show Guide. There is no permanent title or help bar. Actual images open in the installed default Images app through GIO, so zoom/pan/rotation are not reinvented in Booru.

## Build locally

From the ReRead checkout:

```sh
docker run --rm -v "$PWD:/work:z" reread-builder python3 /work/antix/apps/booru/package.py
```

Output: `out/antix/reread-app-booru_0.7.1_i386.deb`. Install through the Reader's web Apps page or Software. The app build does not rebuild or modify core ReRead.

## Browse without a keyboard

The home screen has **Explore / build search**, **Recent posts**, **My favorites** (with an account), and searches you save on the Reader. Preset search strings in older web configuration are deliberately not used by this design.

**Explore** is the starting point for finding content. It is a shared ReRead collection/detail layout, with search actions on the left and the current query on the right. It supports:

- **Find tag by name:** an on-device D-pad character picker searches real tag names through the server's tag API. Letters, numbers, underscores, Delete and Clear are available without a keyboard. View switches between the character grid and matching tags; OK on a match adds/excludes/removes it from the search.
- Browsing popular tags, with automatic pagination and category selection (all, general, artist, copyright, character, species).
- Adding tags from a selected post or editing all tags already in the search.
- Cycling any tag through **Include → Exclude → Off** with OK. The tag detail pane shows the effective state, including tags inherited from a query.
- Sorting by newest, oldest, score, favorite count, hot, random, or last updated.
- Filtering by rating, file type and minimum score.
- Saving the completed search to Home, without typing (maximum 12 per server).

**There is exactly one active search.** Every tag Include / Exclude / Off action immediately updates its query and schedules a fresh results request. Changes to sorting, rating, file type or score work the same way. Several rapid button presses are coalesced into a single request after 500 ms; there is no pending search, separate Apply step, or confirmation. The Explore and tag views show the active query and whether its results are updating.

Press Options or choose **VIEW RESULTS** to navigate to the current gallery; this does not submit a different search. From the gallery press Options to edit that same active search, and Back returns to its results without discarding any changes. A tag picker opened from a post keeps that post's tag list available while matching posts update; when the search has changed, Back returns to the refreshed gallery rather than the stale post.

The gallery displays six thumbnails per screen, retaining selection while results automatically append in batches of 30. Post-preview navigation requests the current post's thumbnail first, then neighboring posts, independent of the gallery's old six-tile window. Both views use the same paced queue; stalled transfers time out after 15 seconds and transient failures retry with a cooldown. OK opens a post card; View opens the image through the default MIME handler. On the card, OK favorites (with configured credentials), Options downloads the original and Back returns to the same gallery position. Left/Right chooses adjacent cards. You may enable automatic image opening through Home → Options.

Hold Guide for contextual controls; release to hide. Favorites closes Booru. Power, volume, Hold and suspend remain owned by ReRead.

## Configuration

Credentials, host URLs and text-only configuration remain in the platform's generic **web Apps → Booru → Settings**, backed by `/etc/reread/apps/booru.json` and the packaged JSON Schema. No special Booru web UI or new server is required. The old manually configured search presets are not used by this browsing interface; build searches in Explore and save them on the Reader instead. The legacy shared-Files configuration migration was removed, and new web defaults contain only servers and credentials. An older conffile containing extra preset fields remains readable but those fields are ignored.

Per-device choices and Reader-created searches live in `/var/lib/reader/apps/reread-booru/preferences.json`. Downloads go to the shared `Pictures/Booru` folder. Temporary sample files are kept in a bounded, separate image cache.

## Network behavior and limits

An asynchronous Qt network client retrieves posts and tags. Preview downloads are paced (one start every 550 ms, up to two in flight), with bounded cache and retry/backoff for temporary 429/503, timeouts and related transient errors; API post requests also back off. Background/inactive thumbnail loading is cancelled and resumed when visible. These safeguards do not prove rate limiting was the cause of earlier speed fluctuations. Inspect actual HTTP response codes to distinguish server throttling from slow connections and image decoding.

There is no built-in general keyboard input or image decoder/player beyond thumbnail browsing; the ReRead default Images app handles full-image interaction. Some booru sites do not implement e621's API and are unsupported.

## Tests

`test-opening.cpp` covers live search changes without an Apply action, current-post thumbnail priorities, tag inclusion/exclusion, D-pad tag-prefix entry and suggestion selection, Explore navigation, bookmark persistence, image MIME dispatch, and cache behavior using local fixtures. These are source tests, not proof of real hardware or live-server behavior.
