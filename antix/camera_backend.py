"""Control binding for ReRead's native camera engine; no pixel/USB capture loop."""
from contextlib import contextmanager
import ctypes as C
import hashlib
from pathlib import Path
import subprocess
import threading
import time


def load_firmware(path):
    """Load the owner-supplied FX2 RAM firmware, never EEPROM/FPGA storage."""
    import usb.core
    import usb.util
    firmware = Path(path).resolve()
    expected = '61d003fca740a8ea4901c73f6a8fc2a07fbb3d1d8b203231e51fda1138a9b23e'
    if hashlib.sha256(firmware.read_bytes()).hexdigest() != expected:
        raise RuntimeError('Unexpected CameraCtlr.hex firmware')
    if usb.core.find(idVendor=0x04b4, idProduct=0x1005) is not None:
        return
    devices = list(usb.core.find(find_all=True, idVendor=0x04b4, idProduct=0x0083))
    if len(devices) != 1:
        raise RuntimeError(f'Expected one unprogrammed Reader camera, found {len(devices)}')
    device = devices[0]
    node = f'/dev/bus/usb/{device.bus:03d}/{device.address:03d}'
    subprocess.run(['/sbin/fxload', '-t', 'fx2', '-I', str(firmware), '-D', node], check=True)
    usb.util.dispose_resources(device)
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        device = usb.core.find(idVendor=0x04b4, idProduct=0x1005)
        if device is not None:
            device.reset()
            usb.util.dispose_resources(device)
            time.sleep(.5)
            return
        time.sleep(.25)
    raise RuntimeError('Camera did not enumerate after RAM firmware loading')


class Stats(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ('received', 'displayed', 'dropped', 'damaged')] + [
        ('capture_fps', C.c_double), ('preview_fps', C.c_double),
        ('running', C.c_int), ('width', C.c_int), ('height', C.c_int)]


class Photo(C.Structure):
    _fields_ = [('width', C.c_int), ('height', C.c_int)] + [
        (name, C.c_uint) for name in ('raw', 'status', 'gain', 'prelocked')]


class NativeCamera:
    def __init__(self):
        self.condition = threading.Condition()
        self.active = 0
        self.closing = False
        self.library = C.CDLL('/usr/lib/reread/libreader-camera.so')
        library = self.library
        library.reader_camera_create.argtypes = [C.c_char_p, C.c_size_t]
        library.reader_camera_create.restype = C.c_void_p
        for name in ('shutdown', 'destroy'):
            function = getattr(library, 'reader_camera_' + name)
            function.argtypes, function.restype = [C.c_void_p], None
        library.reader_camera_free.argtypes = [C.c_void_p]
        library.reader_camera_free.restype = None
        library.reader_camera_frame.argtypes = [C.c_void_p, C.c_uint64, C.POINTER(C.c_void_p),
            C.POINTER(C.c_size_t), C.POINTER(C.c_uint64), C.c_char_p, C.c_size_t]
        library.reader_camera_frame.restype = C.c_int
        library.reader_camera_rgb_frame.argtypes = [C.c_void_p, C.c_uint64, C.POINTER(C.c_void_p),
            C.POINTER(C.c_size_t), C.POINTER(C.c_uint64), C.POINTER(C.c_int), C.POINTER(C.c_int), C.c_char_p, C.c_size_t]
        library.reader_camera_rgb_frame.restype = C.c_int
        library.reader_camera_capture.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_char_p, C.c_char_p,
            C.POINTER(Photo), C.c_char_p, C.c_size_t]
        library.reader_camera_capture.restype = C.c_int
        library.reader_camera_stats.argtypes = [C.c_void_p, C.POINTER(Stats)]
        library.reader_camera_stats.restype = None
        error = C.create_string_buffer(512)
        self.handle = library.reader_camera_create(error, len(error))
        if not self.handle:
            raise RuntimeError(error.value.decode(errors='replace'))

    @contextmanager
    def call(self):
        # CDLL calls release Python's GIL. Keep the engine alive until every
        # in-flight HTTP/capture caller has returned during shutdown.
        with self.condition:
            if self.closing:
                raise RuntimeError('Camera is paused for sleep or restarting.')
            self.active += 1
        try:
            yield self.handle
        finally:
            with self.condition:
                self.active -= 1
                self.condition.notify_all()

    def close(self):
        with self.condition:
            if self.closing:
                return
            self.closing = True
        self.library.reader_camera_shutdown(self.handle)
        with self.condition:
            self.condition.wait_for(lambda: self.active == 0)
        self.library.reader_camera_destroy(self.handle)
        self.handle = None

    def frame(self, after, raw=False):
        output, size, sequence = C.c_void_p(), C.c_size_t(), C.c_uint64()
        error = C.create_string_buffer(512)
        width, height = C.c_int(), C.c_int()
        with self.call() as handle:
            if raw:
                result = self.library.reader_camera_rgb_frame(handle, after, C.byref(output),
                    C.byref(size), C.byref(sequence), C.byref(width), C.byref(height), error, len(error))
            else:
                result = self.library.reader_camera_frame(handle, after, C.byref(output),
                    C.byref(size), C.byref(sequence), error, len(error))
            if result:
                raise RuntimeError(error.value.decode(errors='replace'))
            try:
                data = C.string_at(output, size.value), sequence.value
                return (*data, width.value, height.value) if raw else data
            finally:
                self.library.reader_camera_free(output)

    def capture(self, flash, photo, preview, document=False, rotation=0):
        result, error = Photo(), C.create_string_buffer(512)
        with self.call() as handle:
            code = self.library.reader_camera_capture(handle, int(flash), int(document), rotation, str(photo).encode(),
                str(preview).encode(), C.byref(result), error, len(error))
            if code:
                raise RuntimeError(error.value.decode(errors='replace'))
        return dict(width=result.width, height=result.height,
                    focus=dict(raw=result.raw, status=result.status, gain=result.gain,
                               prelocked=bool(result.prelocked)))

    def stats(self):
        stats = Stats()
        with self.call() as handle:
            self.library.reader_camera_stats(handle, C.byref(stats))
        return {name: getattr(stats, name) for name, _kind in Stats._fields_}
