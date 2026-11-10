# booru for ReRead

Independent i386 Qt5 framebuffer app for e926/e621-compatible API hosts. Its UI uses ReRead's black/white palette, native selection colors, standard list and button-icon controls, without a top header or a custom top strip. No browser engine, X11, RetroArch, keyboard, platform web changes or global configuration changes.

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
- Viewer: unobstructed fullscreen without header/footer; D-pad Left/Right switches posts at 1× and prefetches following pages, or pans in the expected direction when zoomed (with bounded edges). Plus/Minus zoom through 13 levels from fit-to-screen to 10×. OK toggles online account favorite. Play shows post ID, rating, and tags; Options downloads the original file explicitly into `Files/Pictures/Booru`. Back closes viewer.
- Favorites button returns to ReRead Home immediately. Power, volume, Hold, and suspend remain with the platform.

Images are fetched on demand into bounded RAM, not saved unless explicitly downloaded. Originals are capped at 64 MiB on download; sample decode at 9 MiB and 2304 px, thumbnail requests 512 KiB. Video formats are not supported. Favorites require an account.

## Limitations / testing

Initial implementation. Compile and device testing have not yet been completed. Some API servers may return incompatible JSON or rate limit requests. Avoid storing keys on an untrusted device: the web/SSH management environment and all apps run with trusted local privileges.
