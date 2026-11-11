# Web redesign — ReRead 0.23.0

Approved after on-Reader previews. Presentation-only change; existing Python
API routes, package operations, app configuration and storage mappings stay intact.

- Pinned Tabler 1.7.4 CSS/JS with local MIT license notices; assembled into the
  existing static management HTML at build time. No CDN, font download, Node
  runtime, new backend route or UI SDK ABI change.
- Compact sidebar and responsive drawer, summary data, full-width workspace,
  application filters, tabular app/network/source listings and adjacent editors.
- Device & SSH names the hostname/root-login/password section accurately.
- No decorative live indicator, forced uppercase headings or collapsible sections.
- Log viewport uses 55vh (minimum 320px), with vertical resizing and readable text.
- Browser checks against live Reader endpoints: page navigation, mobile drawer,
  no horizontal mobile overflow, persistent Copyparty frame, no JS errors.
- Mocked package controls: update/install, busy/failed states, retained cached
  catalog, no automatic checks, Wi-Fi profile editing and repository controls.
- On-Reader Files check: folders, upload, UTF-8 editing/save/reload, tab navigation.
- On-Reader log viewport check: 495px at 900px browser height; resize enabled.

Reader owner approved the final preview for release. The existing USB installer
is 0.21.1 and can update through APT; new source builds include this presentation.
