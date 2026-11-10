# ReRead project direction

- Prefer standard Linux interfaces, formats, packages and existing system services
  throughout this project. Investigate suitable existing software before building
  custom infrastructure.
- Keep custom code focused on Reader-specific hardware integration and the ReRead
  user experience. Applications remain independent Linux programs; shared ReRead
  device and UI helpers are optional.
- Choose solutions proportionate to this handheld's resources. Favor simple,
  maintained Linux components without requiring a desktop environment.
- When custom infrastructure is necessary, explain the concrete gap that existing
  Linux facilities cannot reasonably cover.

- Applications intentionally run as root. Keep permission separation and per-app
  permission gates out of scope unless the owner requests them.

- For application work, read [APP_DEVELOPMENT.md](APP_DEVELOPMENT.md), the main
  app development and design contract. Use shared UI helpers for new ReRead
  screens by default; keep apps independent Linux executables. Its documented
  UI requirements include intended behavior that is not yet globally enforced.
