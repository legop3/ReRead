# Web management presentation

The web UI uses a compact sidebar and status overview, full-workspace
Files, application/repository/network tables, and adjacent editors. Existing API
endpoints and storage behavior are unchanged.

Use all available horizontal space. Keep controls, tables and form rows compact,
with plain borders and normal-case labels. No decorative live indicators, forced
uppercase labels, oversized headings, shadows or placeholder dashboard widgets.
Device & SSH is the hostname/root-login/password page; app settings stay with apps.

- `../web.html`: page template and existing API client.
- `layout.css`: ReRead layout and component overrides.
- `../web_build.py`: downloads/verifies pinned Tabler Core and assembles a single
  static page with local CSS/JS and system fonts.
- License files: included in the core package alongside the generated page.

Render from the repository root:

```sh
python3 antix/web_build.py build/web-preview/root
```

Output: `build/web-preview/root/usr/share/reader/web.html`. API requests need the
Reader origin or browser test fixtures. `gadget.stage()` uses the same renderer
for normal builds. No Node or new server is required.

Browser tests:

```sh
REREAD_WEB_HTML=build/web-preview/root/usr/share/reader/web.html NODE_PATH=build/web-tests/node_modules node antix/test-web-packages.cjs
NODE_PATH=build/web-tests/node_modules node antix/test-web-files.cjs
```

The package-control test uses mocked endpoints. The Files test talks to the
connected Reader, writes only its unique test directory and removes it.
