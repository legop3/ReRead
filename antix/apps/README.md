# ReRead applications

**[Making ReRead apps — the definitive development guide](../../APP_DEVELOPMENT.md)**

Start there for shared UI, physical buttons and their icons, application icons,
file opening, storage, lifecycle, building, Debian packaging and APT publishing.
It replaces the old contract on this page, including its outdated SDK minimum.

## Examples and applications

| Source | Purpose |
| --- | --- |
| [Shared UI starter](examples/screen-window.cpp) and [packager](examples/package-screen.py) | Recommended starting point; collection/details layout and Guide overlay |
| [Files](files/README.md) | Shared storage browser, collection layout and MIME dispatch |
| [Software](software/README.md) | Package/update management and collection layout |
| [Camera](camera/README.md) | Asynchronous camera-service client and live preview |
| [Images](images/README.md) | Independent MIME image viewer |
| [Text](text/README.md) | Compact read-only MIME document handler |
| [Audio](audio/README.md) | GStreamer/ALSA player and standard sleep/idle integration |
| [Internet Archive](archive/README.md) | Network catalog, thumbnails, caching and default viewers |
| [Booru](booru/README.md) | Network image catalog using the default image viewer |

Apps are independent Linux programs and separately managed Debian packages.
The platform does not require a desktop environment or an application server.
