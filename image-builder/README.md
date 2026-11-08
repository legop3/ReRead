# ReRead builder

The Dockerfile supplies the tools for the single [ReRead antiX build](../antix/README.md).
Run `./make-usb.sh` from the repository root, or `./make-usb.sh --build-only`
to produce release files without writing a USB drive.

The antiX Core i386 ISO is the only image input. The installer uses antiX's
kernel, BusyBox, libc and modules; no stock firmware extraction is required.
Optional stock research extraction is in `analysis/extract-stock.py`.
