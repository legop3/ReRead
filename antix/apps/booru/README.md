# booru for ReRead

Independent i386 Qt5 X11 app for e926/e621-compatible API hosts. Its UI uses ReRead's black/white palette, native selection colors, standard list and button-icon controls, without a top header or a custom top strip. Uses ordinary X11 windows without a browser engine or desktop environment.

## Build

From the ReRead checkout with the existing `reread-builder` image:

```sh
docker run --rm -v "$PWD:/work:z" reread-builder python3 /work/antix/apps/booru/package.py
```

Package: `out/antix/reread-app-booru_0.6.0_i386.deb`. Install with the normal ReRead Apps web page or dpkg. Published packages are available from the ReRead APT repository.

## Configuration

Use **web Apps → Booru → Settings** to edit servers, account names, API keys and
saved search text. The app reads `/etc/reread/apps/booru.json` at startup; reopen
it after saving. The packaged draft-7 JSON Schema drives the platform's offline
form editor. It accepts at most eight HTTPS hosts and twenty searches per host.
Existing `Files/Booru/config.json` settings migrate once on upgrade; the original
file is retained as a backup and is no longer read by the app.

Use **Options on Booru's home screen** for settings that work with Reader buttons:
server selection and **Open images automatically** (off by default). That choice
persists in the app's private `preferences.json`, separate from web text entry.

Create an API key in the site's account settings; use the username and **API key**, not a password. If accessing e621/e926, register a descriptive user agent for the application. Authentication is sent through HTTPS Basic auth to the configured API hostname only; downloads and thumbnails receive no credentials. HTTPS redirects are not followed, including for authenticated API responses.

The app supports e621-style `/posts.json`, `/favorites.json`, and `/favorites/{post_id}.json` on configured hosts; arbitrary booru URLs with other APIs are not supported yet.

## Controls

- Home: D-pad selects Recent, My favorites, or a configured saved search; OK opens; Options opens server selection and automatic-opening preferences; Back exits.
- Gallery: D-pad moves through 3×2 cards, OK opens the post card, the next 30 posts append automatically near the end, **Options** opens Search & Filters, and Back returns to Home.
- Search & Filters (Reader buttons only): Select **Popular tags** to browse tags returned by the site's `/tags.json`, or **Tags from selected post** to browse a post's actual tags. OK cycles each tag through **Include → Exclude → Off**. Set sort (newest, oldest, score, favorites, hot, random, updated), rating, media type and minimum score, then choose Apply or press Options. Back cancels without reloading. Filters combine with the initial saved search and can be changed repeatedly.
- Post details (Play on the post card): browse that post's tags with Up/Down, OK adds/excludes/removes a tag and Options searches with the updated filters. Back returns to the post.
- The app does **not** require typing for these filters; use web Apps → Booru → Settings only to enter new arbitrary tag strings, saved searches, host URLs and account credentials.
- Opening a gallery post shows its post card. Press View to download its image
  sample into the app cache and open the system's default image viewer. Zoom, pan and rotation belong to that viewer.
- Back from the viewer returns to Booru's post card: View reopens the image, OK
  toggles the online favorite, Play shows details, Options saves the original,
  and Back returns to the same gallery selection. Left/Right selects adjacent post cards. Enable automatic opening in the Reader
  settings if you prefer to launch the viewer on entering/changing a post.
- Favorites button returns to ReRead Home immediately. Power, volume, Hold, and suspend remain with the platform.

Thumbnails are fetched at most one every 650 ms with up to two concurrent downloads, rather than in unchecked bursts. Transient HTTP 429/503 and timeout failures use bounded, delayed retries (including `Retry-After`), and the decoded thumbnail cache holds at most 60 images. Thumbnail requests are cancelled when Booru becomes inactive/dimmed and resume on return; ongoing explicit downloads are not interrupted. Post-search API requests also back off on 429/503. This helps prevent spikes but cannot prove the cause of every slowdown without checking actual HTTP responses.

Thumbnails stay in bounded RAM. Full viewing uses ordinary GIO/MIME dispatch and
local files under `$XDG_CACHE_HOME/reread-booru/images` (normally
`~/.cache/reread-booru/images`). The 64 MiB LRU cache survives app exit so another
process can read it; it is separate from explicitly saved originals in shared
storage. Samples are limited to 9 MiB; explicit original downloads to 64 MiB;
thumbnail requests to 512 KiB. Each cached post has its own directory so a viewer
cannot navigate into unrelated cached posts. Non-image samples are rejected;
video playback remains a later step. Favorites require an account.

## Limitations / testing

The MIME-opening test uses disposable cache/configuration, a local image and an
isolated desktop handler; it sends no requests or account changes to a live site. Some API servers may return incompatible JSON or rate limit requests. Avoid storing keys on an untrusted device: the web/SSH management environment and all apps run with trusted local privileges.

UI 5 supplies the shared physical-button hints: hold Guide to show them over
the current screen, release to hide. No footer space is reserved. The shared
SVGs match the Reader buttons; apps do not provide private glyphs or Guide handlers.
