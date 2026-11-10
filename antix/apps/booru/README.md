# booru for ReRead

Independent i386 Qt5 X11 app for e926/e621-compatible API hosts. Its UI uses ReRead's black/white palette, native selection colors, standard list and button-icon controls, without a top header or a custom top strip. Uses ordinary X11 windows without a browser engine or desktop environment.

## Build

From the ReRead checkout with the existing `reread-builder` image:

```sh
docker run --rm -v "$PWD:/work:z" reread-builder python3 /work/antix/apps/booru/package.py
```

Package: `out/antix/reread-app-booru_0.1.3_i386.deb`. Install with the normal ReRead Apps web page or dpkg. Package is not yet published to the APT repo; build and test first.

## Configuration

On first launch, the app creates an editable config in ReRead's shared Files folder, with anonymous e926 as the default host:

```
/var/lib/reader/files/Booru/config.json
```

Edit `Files/Booru/config.json` from your computer using the existing ReRead file manager. No SSH setup, text entry on the Reader, or changes to the platform web UI are needed. It accepts at most eight hosts, each with `name`, HTTPS root `url`, optional `username` and `api_key`, and up to twenty saved tag searches. The configuration is loaded at app startup; restart to apply changes. **Security:** The shared Files folder is accessible through ReRead's file transfer interface. Anyone who can access and download files there may be able to read this config and its API key. Use a dedicated revocable API key, and only use the web file interface on a trusted connection.

Create an API key in the site's account settings; use the username and **API key**, not a password. If accessing e621/e926, register a descriptive user agent for the application. Authentication is sent through HTTPS Basic auth to the configured API hostname only; downloads and thumbnails receive no credentials. HTTPS redirects are not followed, including for authenticated API responses.

The app supports e621-style `/posts.json`, `/favorites.json`, and `/favorites/{post_id}.json` on configured hosts; arbitrary booru URLs with other APIs are not supported yet.

## Controls

- Home: D-pad selects Recent, My favorites, or a configured saved search; OK opens; Options rotates between configured hosts; Back exits.
- Gallery: D-pad moves through 3×2 cards, OK views, the next 30 posts load automatically near the end, Back returns to Home.
- Opening a gallery post downloads its image sample into the app cache and opens it
  with the system's default image viewer. Zoom, pan and rotation belong to that viewer.
- Back from the viewer returns to Booru's post card: View reopens the image, OK
  toggles the online favorite, Play shows details, Options saves the original,
  and Back returns to the same gallery selection. Left/Right opens adjacent posts.
- Favorites button returns to ReRead Home immediately. Power, volume, Hold, and suspend remain with the platform.

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
