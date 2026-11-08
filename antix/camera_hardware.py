#!/usr/bin/env python3
"""ReRead hardware backend for the Intel Reader FX2/FPGA camera.

Firmware can be loaded into RAM with --firmware. No EEPROM or persistent FPGA
image programming is performed here. Protocol recovered from libcolbert-camera.so;
see analysis/camera-protocol.md for provenance and limitations.
"""
import json
from array import array
from contextlib import contextmanager
import hashlib
import os
from pathlib import Path
import subprocess
import struct
import time

import usb.core
import usb.util


@contextmanager
def capture_priority(percent):
    """Match stock ScopedSchedulerChange during the bounded USB transfer."""
    previous_policy = os.sched_getscheduler(0)
    previous_parameters = os.sched_getparam(0)
    minimum = os.sched_get_priority_min(os.SCHED_FIFO)
    maximum = os.sched_get_priority_max(os.SCHED_FIFO)
    priority = minimum + (maximum-minimum)*percent//100
    os.sched_setscheduler(0, os.SCHED_FIFO, os.sched_param(priority))
    try:
        yield
    finally:
        os.sched_setscheduler(0, previous_policy, previous_parameters)


def load_firmware(path):
    """Load the known stock firmware into RAM only, when not already loaded."""
    firmware = Path(path).resolve()
    expected = "61d003fca740a8ea4901c73f6a8fc2a07fbb3d1d8b203231e51fda1138a9b23e"
    if hashlib.sha256(firmware.read_bytes()).hexdigest() != expected:
        raise RuntimeError("Firmware does not match the investigated CameraCtlr.hex")
    if usb.core.find(idVendor=0x04b4, idProduct=0x1005) is not None:
        return
    devices = list(usb.core.find(find_all=True, idVendor=0x04b4, idProduct=0x0083))
    if len(devices) != 1:
        raise RuntimeError(f"Expected one unprogrammed camera, found {len(devices)}")
    device = devices[0]
    node = f"/dev/bus/usb/{device.bus:03d}/{device.address:03d}"
    subprocess.run(["/sbin/fxload", "-t", "fx2", "-I", str(firmware), "-D", node], check=True)
    usb.util.dispose_resources(device)
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        programmed = usb.core.find(idVendor=0x04b4, idProduct=0x1005)
        if programmed is not None:
            programmed.reset()
            usb.util.dispose_resources(programmed)
            time.sleep(0.5)
            return
        time.sleep(0.25)
    raise RuntimeError("Camera did not re-enumerate as 04b4:1005")


class Camera:
    def __init__(self):
        devices = list(usb.core.find(find_all=True, idVendor=0x04b4, idProduct=0x1005))
        if len(devices) != 1:
            raise RuntimeError(f"Expected one programmed Reader camera, found {len(devices)}")
        self.dev = devices[0]
        # Avoid an unnecessary SET_CONFIGURATION on the running firmware.
        if self.dev.get_active_configuration().bConfigurationValue != 1:
            self.dev.set_configuration(1)
        usb.util.claim_interface(self.dev, 0)

    def close(self):
        usb.util.release_interface(self.dev, 0)
        usb.util.dispose_resources(self.dev)

    def send(self, command, payload=b"", subcommand=0, address=0):
        body = bytes(payload)
        if address:
            body = bytes([address]) + body
        if subcommand:
            body = bytes([0x10, subcommand, len(body) + 2]) + body
        packet = bytes([command, len(body)]) + body
        sent = self.dev.write(0x01, packet, timeout=1000)
        if sent != len(packet):
            raise RuntimeError(f"Short command write: {sent}/{len(packet)}")

    def query(self, command, payload=b"", count=2):
        self.send(command, payload)
        time.sleep(0.01)
        reply = bytes(self.dev.read(0x81, count, timeout=1000))
        if len(reply) != count:
            raise RuntimeError(f"Short reply: {reply.hex()}")
        return reply

    def read_register(self, register):
        return struct.unpack(">H", self.query(0x0d, struct.pack(">H", register)))[0]

    def write_register(self, register, value):
        self.send(0x10, struct.pack(">HH", register, value), subcommand=1)

    def read_i2c(self, address, register):
        self.send(0x10, bytes([register]), subcommand=4, address=address)
        return self.read_register(0xfffe)

    def write_i2c(self, address, register, value):
        self.send(0x10, bytes([register]) + struct.pack(">H", value),
                  subcommand=3, address=address)

    def set_streaming(self, enabled):
        # SetState uses the old L1 format, without the length byte.
        self.dev.write(0x01, bytes([0x0a, int(enabled)]), timeout=1000)
        time.sleep(1)  # Stock SetState waits; it does not read an acknowledgement.

    def initialize(self):
        # sensor::SetDefaults(2592,1920), then detail::Start's volatile setup.
        sensor_defaults = [
            (0x0d,1),(0x10,0x50),(0x11,0x6404),(0x12,0),(0x0d,0),
            (1,0x32),(2,0x0a),(3,1919),(4,2591),(5,0),(7,0x1f82),
            (8,0),(9,0x1a0),(0x0a,0),(0x0b,0),
            (0x0c,0),(0x2b,8),(0x2c,8),(0x2d,8),(0x2e,8),(0x35,12),(0xa0,0),
        ]
        for register, value in sensor_defaults:
            self.write_i2c(0x5d, register, value)
        for register, value in [
            (0x102,15),(0x108,15),(0x104,0x140),(0x17,0),
            (0x19,3),(0x1a,3),(0x15,1919),(0x16,2591),
            (0x1b,0),(0x1c,0),(0x13,1919),(0x14,2591),
            (0x106,0x4cc),(0x105,0x4cc),(0x12,2),
        ]:
            self.write_register(register, value)
        # Stock OemInfo() reads the SMBIOS chassis OEM DWORD, keeping low 16
        # bits for the per-unit focus offset. Read the same field directly.
        table = Path("/sys/firmware/dmi/tables/DMI").read_bytes()
        offset = 0
        calibration = None
        while offset + 4 <= len(table):
            kind, length = table[offset:offset+2]
            if length < 4:
                break
            if kind == 3 and length >= 17:
                calibration = struct.unpack_from("<I", table, offset+13)[0] & 0xffff
                self.write_register(0x109, calibration)
                print(f"Focus calibration: {calibration:#06x}", flush=True)
                break
            end = table.find(b"\0\0", offset+length)
            if end < 0:
                break
            offset = end + 2
        if calibration is None:
            raise RuntimeError("SMBIOS chassis focus calibration missing")
        # Explicitly disable the strobe, then follow EnterStreamMode().
        self.send(0x10, b"\x03\x00", subcommand=6)
        self.send(0x10, b"\x05", subcommand=6)

    def start_stream(self, initialize=True):
        if initialize:
            self.initialize()
        for register, value in [(6,8),(0x20,0xc040),(0x22,3),(0x23,3),(0x1e,0x4006)]:
            self.write_i2c(0x5d, register, value)
        self.write_i2c(0x6b, 0, 0x0602)  # shutter::Open
        self.write_register(0x12, 2)
        self.write_register(0x17, 2)
        self.write_register(0x107, 3)
        for value in (1,0,0,1):
            self.send(0x10, bytes([value]), subcommand=5)
        return self.set_streaming(True)

    def focus(self, timeout=10):
        # Recovered stock focus entry point at 0x12da0. The FPGA handles the
        # lens/exposure loop; the host waits for its status instead of doing
        # an invented software focus sweep.
        self.write_register(0x12, 2)
        self.write_register(0x107, 3)
        self.set_streaming(False)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            time.sleep(0.25)
            raw = self.read_register(0x1000)
            raw = self.read_register(0x1000)
            status = self.read_register(0x1005)
            status = self.read_register(0x1005)
            print(f"Focus status: raw={raw:#06x} status={status:#06x}", flush=True)
            if raw != status and (status & 0x0fff).bit_count() == 1:
                if status & 0xc000 and not raw & 0x8000:
                    # Stock 0x134d0: focus/prelock before exposure is stale.
                    # Restart the FPGA focus search after exposure settles.
                    self.write_register(0x10a, 1)
                    time.sleep(.75)
                    continue
                if raw & status & 0x8000:
                    gain = self.read_i2c(0x5d, 0x35)
                    print(f"Focus locked; sensor gain={gain:#06x}", flush=True)
                    return gain
        raise RuntimeError("Camera did not report focus lock within the deadline")

    def capture_still(self, destination, flash=False, stream_ready=False):
        gain = None
        try:
            if not stream_ready:
                self.start_stream()
                # Stock normally streams while the user frames their photo.
                # A headless one-shot must also give the fresh sensor/FPGA
                # time to leave the previous exposure/focus state.
                settling = time.monotonic() + 2
                while time.monotonic() < settling:
                    self.read_stream()
            if flash:
                # Stock SetFlash(true): default automatic strobe timing,
                # with charging active during autofocus.
                self.send(0x10, b"\x03\x01", subcommand=6)
                self.send(0x10, b"\x04", subcommand=6)
            gain = self.focus()
            for register, value in [(0x1e,0x4198),(6,0x400),(0x20,0x4040),(0x22,0),(0x23,0)]:
                self.write_i2c(0x5d, register, value)
            self.write_i2c(0x6b, 0, 0x0601)  # shutter::Close
            self.write_register(0x12, 1)
            time.sleep(1)
            self.write_i2c(0x5d, 0x35, 12)  # detail::TakePicture before capture
            target = 2592 * 1920 * 2
            # Allocate and touch the full buffer before the shot. Allocating
            # it after the trigger leaves the small FX2 FIFO waiting on RAM.
            buffer = array("B", [0]) * target
            with capture_priority(100):
                self.dev.clear_halt(0x82)
                self.dev.write(0x01, b"\x08\x00\x64", timeout=1000)
                count = self.dev.read(0x82, buffer, timeout=5000)
            data = bytes(buffer[:count])
            Path(destination).write_bytes(data)
            print(f"Saved {len(data)}/{target} still bytes to {destination}", flush=True)
            if len(data) != target:
                raise RuntimeError("Incomplete still transfer")
        finally:
            try:
                self.send(0x10, b"\x03\x00", subcommand=6)
                self.send(0x10, b"\x05", subcommand=6)
            finally:
                if stream_ready:
                    if gain is not None:
                        self.write_i2c(0x5d, 0x35, gain)
                    self.start_stream(initialize=False)
                else:
                    self.stop_stream()

    def stop_stream(self):
        try:
            self.set_streaming(False)
        finally:
            self.write_register(0x17, 0)
            self.write_register(0x12, 1)

    def read_stream(self):
        """One stock-sized burst; leave the sensor and autofocus running."""
        if not hasattr(self, '_stream_buffer'):
            self._stream_buffer = array('B', [0]) * (648 * 480 * 2 * 3)
        with capture_priority(20):
            self.dev.clear_halt(0x82)
            count = self.dev.read(0x82, self._stream_buffer, timeout=5000)
        return self._stream_buffer[:count].tobytes()

    def capture_stream(self, destination):
        try:
            self.start_stream()
            Path(destination).write_bytes(self.read_stream())
        finally:
            self.stop_stream()
