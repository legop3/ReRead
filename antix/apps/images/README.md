# Images

A standalone Qt/X11 image viewer and normal MIME handler, packaged as
`reread-app-images`. It uses Qt image decoders, ReRead's optional UI helpers and
ordinary X windows. No background service, custom file-opening protocol or image
codec is added. The image window closes back to whichever app was underneath it.

- Zoom buttons magnify; OK switches between fit and decoded actual size.
- Arrows pan when zoomed. Left/Right browse adjacent images at fit size.
- View rotates the displayed image 90° without editing the file.
- Options shows filename, decoded dimensions, zoom and position; Guide explains controls.
- Back closes the image (or returns from information). Favorites closes the viewer.

Qt applies EXIF orientation. Images above 4096 pixels on either axis are decoded
at reduced size; images above 64 megapixels are rejected on this 512 MiB device.
GIF shows a still frame. JPEG, PNG, BMP, GIF, SVG, WebP, TIFF and Qt's supported
portable bitmap/icon formats use distro codecs. Unsupported/corrupt files show an
error. No image or metadata is written back. A single file opens with its folder's
supported image siblings; an explicit list preserves its order. HTTP URLs must
first be downloaded by the calling application.

`reread-images.desktop` declares `MimeType` and `Exec=... -- %U`, accepts file URIs
or local paths, and supplies an SVG icon. `NoDisplay=true` keeps the file handler
out of Home's launcher; select an image in Files instead. It still appears in
Software as an installed app. System defaults are in
`/etc/xdg/reread-mimeapps.list`; normal user MIME preferences override them.

Build after `antix/ui/build.sh build/antix/ui`, inside the builder:

```sh
python3 antix/apps/images/package.py
```

The normal release and fresh-image builders include it too.

## Choice of viewer

[feh](https://man.finalrewind.org/1/feh/) is configurable and lightweight;
[nsxiv](https://nsxiv.codeberg.page/man/) is another small X viewer with build-time
key customization. This front end retains the Reader's safe display inset,
physical SVG control hints and readable UI without modifying an upstream viewer.
Decoding, window ownership, file associations and app launching stay with Qt,
Xorg and the freedesktop/GIO interfaces. Another viewer can replace it through
normal MIME defaults.
