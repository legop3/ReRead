# Text

An independent, read-only Qt/X11 text viewer, installed as `reread-app-text`.
Normal MIME associations select it for plain text, Markdown, CSV, JSON and XML;
markup is displayed as source text. `NoDisplay=true` keeps this document handler
out of Home. Open a document from Files or use `gio open FILE`.

- Up/Down scroll a line; Left/Right move a screenful.
- Zoom changes text size, preserving the approximate reading position.
- OK returns to the beginning; Options shows file information.
- Hold Guide for icon controls; release it to hide them. Help overlays the text.
- Back closes information or the document. Favorites closes the viewer.

Qt's QTextDocument handles Unicode layout and word wrapping; QTextCodec decodes
UTF-8 and BOM-marked UTF-16/32. No custom text engine or background service is
added. The original file is never changed. Files over 2 MiB display a clearly
marked bounded preview. Binary data and missing/unreadable files show errors.
HTTP resources must first be downloaded by the caller. The desktop entry uses
`%f`; paths containing spaces, punctuation and Unicode are passed without a shell.

Build after the shared SDK: `python3 antix/apps/text/package.py`. The platform
release builder and fresh installer also include this package. It can be
replaced through ordinary MIME defaults; see [file opening](../../FILE_OPENING.md).

Qt references: [QTextDocument](https://doc.qt.io/archives/qt-5.15/qtextdocument.html),
[QTextCodec](https://doc.qt.io/archives/qt-5.15/qtextcodec.html).
