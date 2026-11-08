#!/usr/bin/env python3
"""Raw research CLI; maintained hardware implementation lives in antix/."""
import argparse
import json
from pathlib import Path
import sys

if Path('/run/reader-camera.sock').exists():
    raise SystemExit('Use reader-camera capture while the shared service is running.')
try:
    from reader_camera_hardware import Camera, load_firmware
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'antix'))
    from camera_hardware import Camera, load_firmware


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", metavar="HEX_FILE", help="stock CameraCtlr.hex, loaded into RAM if needed")
    parser.add_argument("--stream", metavar="RAW_FILE", help="capture a short raw Bayer stream")
    parser.add_argument("--still", metavar="RAW_FILE", help="focus and capture a full-resolution raw still")
    parser.add_argument("--flash", action="store_true", help="enable stock automatic flash for --still")
    args = parser.parse_args()
    if args.stream and args.still:
        parser.error("Choose --stream or --still")
    if args.flash and not args.still:
        parser.error("--flash requires --still")
    if args.firmware:
        load_firmware(args.firmware)
    camera = Camera()
    try:
        version = camera.query(0x0f)
        print(json.dumps({
            "controller_version_bytes": version.hex(),
            "controller_version": ".".join(map(str, version)),
            "fpga_version": hex(camera.read_register(0xffff)),
            "fpga_status_1005": hex(camera.read_register(0x1005)),
            "sensor_register_0": hex(camera.read_i2c(0x5d, 0)),
        }, indent=2), flush=True)
        if args.stream:
            camera.capture_stream(args.stream)
        elif args.still:
            camera.capture_still(args.still, flash=args.flash)
    finally:
        camera.close()


if __name__ == "__main__":
    main()
