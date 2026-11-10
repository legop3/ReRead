# Booru for ReRead

An independent i386 Qt5/X11 browser for e621/e926-compatible APIs. Requires ReRead 0.21+ and uses the shared UI 5 palette, list/detail layouts, physical controls and hold-to-show Guide. There is no permanent title or help bar. Actual images open in the installed default Images app through GIO, so zoom/pan/rotation are not reinvented in Booru.

## Build locally

From the ReRead checkout:

```sh
docker run --rm -v "$PWD:/work:z" reread-builder python3 /work/antix/apps/booru/package.py
```

Output: `out/antix/reread-app-booru_0.7.0_i386.deb`. Install through the Reader's web Apps page or Software. The app build does not rebuild or modify core ReRead.

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

**Changes are staged, not silently sent to the server.** Explore and the tag browsers explicitly show **NOT SEARCHED YET** for a new search or **CHANGES NOT APPLIED** when your filters differ from the loaded results. Choose **SHOW RESULTS**, or press Options from Explore or a tag browser, to execute the full query. The grid reloads with the new results. This allows selecting several tags together without generating one HTTP request per button press.

From results, press Options to refine the current search. Back cancels changes and returns to results. Inside a post, press Play to inspect and select its tags; Options applies the resulting search. Back from the tag browser keeps the changes available in Explore or returns to the post.

The gallery displays six thumbnails per screen, retaining selection while results automatically append in batches of 30. OK opens a post card; View opens the image through the default MIME handler. On the card, OK favorites (with configured credentials), Options downloads the original and Back returns to the same gallery position. Left/Right chooses adjacent cards. You may enable automatic image opening through Home → Options.

Hold Guide for contextual controls; release to hide. Favorites closes Booru. Power, volume, Hold and suspend remain owned by ReRead.

## Configuration

Credentials, host URLs and text-only configuration remain in the platform's generic **web Apps → Booru → Settings**, backed by `/etc/reread/apps/booru.json` and the packaged JSON Schema. No special Booru web UI or new server is required. The old manually configured search presets are not used by this browsing interface; build searches in Explore and save them on the Reader instead. The legacy shared-Files configuration migration was removed, and new web defaults contain only servers and credentials. An older conffile containing extra preset fields remains readable but those fields are ignored.

Per-device choices and Reader-created searches live in `/var/lib/reader/apps/reread-booru/preferences.json`. Downloads go to the shared `Pictures/Booru` folder. Temporary sample files are kept in a bounded, separate image cache.

## Network behavior and limits

An asynchronous Qt network client retrieves posts and tags. Preview downloads are paced (one start every 650 ms, up to two in flight), with bounded cache and retry/backoff for temporary 429/503, timeouts and related transient errors; API post requests also back off. Background/inactive thumbnail loading is cancelled and resumed when visible. These safeguards do not prove rate limiting was the cause of earlier speed fluctuations. Inspect actual HTTP response codes to distinguish server throttling from slow connections and image decoding.

There is no built-in general keyboard input or image decoder/player beyond thumbnail browsing; the ReRead default Images app handles full-image interaction. Some booru sites do not implement e621's API and are unsupported.

## Tests

`test-opening.cpp` covers search composition, tag inclusion/exclusion, D-pad tag-prefix entry and suggestion selection, the Explore navigation path, bookmark persistence, image MIME dispatch, and cache behavior using local fixtures. These are source tests, not proof of real hardware or live-server behavior.
