# Camera

An independent Qt/LinuxFB application, packaged as `reread-app-camera`.
`main.cpp` contains the complete on-device Camera UI and its local service client;
`reread-camera.svg` is its standard hicolor application icon. `VERSION` sets the
app package version. Home launches its ordinary `.desktop` entry.

Build the executable after building the shared helpers with `antix/ui/build.sh`:

```sh
antix/apps/camera/build.sh build/antix/ui
```

This produces `reader-camera-app`. The normal release builder builds and packages
it automatically; its installed path remains
`/usr/lib/reread/apps/camera/reader-camera-app`.

The shared hardware service and native camera driver/processing code remain in
`antix/`, since the web interface and other applications can also use them. The
app uses the same local socket API described in [camera support](../../CAMERA.md).
Removing Camera removes the app, retaining that platform support and user photos.
