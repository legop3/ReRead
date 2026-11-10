# Bundled file manager

ReRead bundles **Copyparty 1.20.26**, unmodified, from
https://github.com/9001/copyparty/releases/tag/v1.20.26 .

- Source archive: `copyparty-1.20.26.tar.gz` from that release.
- SHA-256: `ac7ea05bde8d431b32cc2ff618f2754e46bd967bf10deaa6654dac9a449a6ff9`.
- Upstream source and license notices: https://github.com/9001/copyparty .
- Installed module/resources: `/usr/lib/reread/vendor/copyparty/`.
- Installed upstream license bundle: `/usr/share/doc/reread/copyparty-LICENSE.txt`.

The platform builder downloads and verifies the pinned archive once, caching it
under ignored `downloads/copyparty/`. It packages the Python module, web assets and
license notices for offline operation; no pip installation or runtime download is
needed on the Reader. HTML rendering uses the normal distro `python3-jinja2`
package, declared as a platform dependency. Upstream code is not modified. ReRead's small integration is
`files.sh`, `files.conf`, the supported `files-head.html` branding hook, the
management page embed and USB-session child startup. The branding adds readable
toolbar labels and omits the unrelated server-log message button.
Version/checksum pins live in `third_party.py` and should be updated together with
this document after validation. Copyparty uses MIT and other licenses listed in
its upstream license bundle, which is included in every platform package.

## App configuration forms

ReRead bundles **@json-editor/json-editor 2.17.4** from
https://github.com/json-editor/json-editor (MIT and bundled dependency notices).
The npm archive is pinned in `third_party.py` with SHA-256
`edfa9eca0e5518c10b59c698de78ef398a07096ae84636dbc49bdd27808954c6`.
Only its prebuilt browser script and license files are installed; no npm, Node,
CDN or runtime download is required on the Reader. Installed notices are
`/usr/share/doc/reread/json-editor-LICENSE.txt` and
`json-editor-dependencies-LICENSE.txt`. Server validation uses Debian's
`python3-jsonschema`, rather than a custom schema validator.
