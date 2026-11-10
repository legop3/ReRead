# booru for ReRead

Independent i386 Qt5 framebuffer app for e926/e621-compatible API hosts. No browser engine, X11, RetroArch, keyboard, platform web changes or global configuration changes.

## Build

From the ReRead checkout with the existing `reread-builder` image:

```sh
docker run --rm -v "$PWD:/work:z" reread-builder python3 /work/antix/apps/booru/package.py
```

Package: `out/antix/reread-app-booru_0.1.0_i386.deb`. Install with the normal ReRead Apps web page or dpkg. Package is not yet published to the APT repo; build and test first.

## Configuration

Default host is https://e926.net with no account. The standalone app reads its optional config from:

```
/var/lib/reader/apps/reread-booru/config.json
```

Copy `config.example.json` to that location over SSH and edit it on the desktop. No text entry or platform web UI changes are needed. It accepts at most eight hosts, each with `name`, HTTPS root `url`, optional `username` and `api_key`, and up to twenty saved tag searches. The configuration is loaded at app startup; restart to apply changes. Use `chmod 600` because the config contains credentials.

Create an API key in the site's account settings; use the username and **API key**, not a password. If accessing e621/e926, register a descriptive user agent for the application. Authentication is sent through HTTPS Basic auth to the configured API hostname only; downloads and thumbnails receive no credentials. HTTPS redirects are not followed, including for authenticated API responses.

The app supports e621-style `/posts.json`, `/favorites.json`, and `/favorites/{post_id}.json` on configured hosts; arbitrary booru URLs with other APIs are not supported yet.

## Controls

- Home: D-pad selects Recent, My favorites, or a configured saved search; OK opens; Options rotates between configured hosts; Back exits.
- Gallery: D-pad moves through 3×2 cards, OK views, Options fetches the next 30-post page, Back returns to Home.
- Viewer: D-pad Left/Right switches posts at 1×, or pans when zoomed. Plus/Minus zoom. OK toggles online account favorite. Play shows post ID, rating, and tags; Options downloads the original file explicitly into `Files/Pictures/Booru`. Back closes viewer.
- Favorites button returns to ReRead Home immediately. Power, volume, Hold, and suspend remain with the platform.

Images are fetched on demand into bounded RAM, not saved unless explicitly downloaded. Originals are capped at 64 MiB on download; sample decode at 9 MiB and 2304 px, thumbnail requests 512 KiB. Video formats are not supported. Favorites require an account.

## Limitations / testing

Initial implementation. Compile and device testing have not yet been completed. Some API servers may return incompatible JSON or rate limit requests. Avoid storing keys on an untrusted device: the web/SSH management environment and all apps run with trusted local privileges.
