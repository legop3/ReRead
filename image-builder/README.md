# ReRead builder

The Dockerfile supplies the tools for the single [ReRead antiX build](../antix/README.md#build).
Run `./build.sh` from the repository root to produce release files without writing a USB drive,
or `./make-usb.sh --build` to build and then select a USB drive to flash.

Source builds need the antiX Core i386 ISO and the small extracted camera firmware
file described in [camera build inputs](../antix/CAMERA.md). The installer uses
antiX's kernel, BusyBox, libc and modules. A full stock disk dump is optional
research material. Installing a [published release](../README.md#install-reread)
requires none of these build inputs.
