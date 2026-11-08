#!/usr/bin/python3
"""ReRead shared camera service and local HTTP client for apps."""
import argparse
import fcntl
from contextlib import nullcontext
from http.client import HTTPConnection
from http.server import BaseHTTPRequestHandler
import json
import os
from pathlib import Path
import shutil
import signal
import socket
from socketserver import ThreadingMixIn, UnixStreamServer
import tempfile
import threading
import time
import uuid

SOCKET = '/run/reader-camera.sock'
FIRMWARE = Path('/lib/firmware/reread/CameraCtlr.hex')
FILES = Path('/var/lib/reader/files')
STATE = Path('/var/lib/reader/camera')


def request(action='status', **data):
    """Apps use this client, or ordinary HTTP over the Unix socket directly."""
    connection = HTTPConnection('localhost', timeout=60)
    connection.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connection.sock.settimeout(60)
    try:
        connection.sock.connect(SOCKET)
        method = 'GET' if action == 'status' or action.startswith('live?') else 'POST'
        connection.request(method, '/' + action, json.dumps(data), {'Content-Type': 'application/json'})
        response = connection.getresponse()
        body = response.read()
        if response.status == 200 and response.getheader('Content-Type') == 'image/jpeg':
            return body, response.getheader('X-Frame')
        result = json.loads(body)
        if response.status >= 400:
            raise RuntimeError(result.get('error', 'Camera request failed.'))
        return result
    finally:
        connection.close()


class Platform:
    def __init__(self):
        self.lock = threading.RLock()
        self.worker = None
        self.preview_until = 0
        self.live_frame = None
        self.live_sequence = 0
        self.live_condition = threading.Condition(self.lock)
        self.hardware_lock = threading.Lock()
        self.preview_stop = threading.Event()
        self.paused = False
        self.state = 'starting'
        self.error = ''
        self.pending_capture = None
        self.job = None
        self.latest = None
        STATE.mkdir(parents=True, exist_ok=True)
        FILES.mkdir(parents=True, exist_ok=True)
        try:
            self.latest = json.loads((STATE / 'latest.json').read_text())
        except (OSError, ValueError):
            pass
        # These are only our interrupted temporary frames, never user files.
        for path in STATE.glob('capture-*'):
            if path.is_dir():
                shutil.rmtree(path)
        self.prepare()

    def prepare(self):
        from reader_camera_hardware import Camera, load_firmware
        try:
            load_firmware(FIRMWARE)
            camera = Camera()
            try:
                try:
                    camera.query(0x0f)
                except Exception:
                    # Camera-only bus reset repairs endpoint sync after wake.
                    camera.dev.reset()
                    camera.close()
                    time.sleep(.5)
                    camera = Camera()
                    camera.query(0x0f)
                # Also clear a strobe left enabled by an interrupted process.
                camera.send(0x10, b'\x03\x00', subcommand=6)
                camera.send(0x10, b'\x05', subcommand=6)
                camera.stop_stream()
            finally:
                camera.close()
            self.state, self.error = 'idle', ''
        except Exception as error:
            self.state, self.error = 'error', str(error)
            print('Camera unavailable:', error, flush=True)

    def status(self):
        with self.lock:
            return dict(state=self.state, error=self.error, paused=self.paused,
                        job=self.job, latest=self.latest,
                        live=self.preview_until > time.monotonic() and not self.paused,
                        frame=self.live_sequence)

    def preview(self):
        """Renew a short lease; a closed browser leaves no camera running."""
        with self.lock:
            if self.paused:
                raise ValueError('The camera is paused for sleep.')
            self.preview_until = time.monotonic() + 8
            if not getattr(self, 'preview_worker', None) or not self.preview_worker.is_alive():
                self.preview_stop.clear()
                self.preview_worker = threading.Thread(target=self.stream, daemon=True)
                self.preview_worker.start()
        return self.status()

    def stream(self):
        from reader_camera_hardware import Camera
        from reader_camera_image import stream_jpeg
        try:
            with self.hardware_lock:
                camera = Camera()
                try:
                    camera.start_stream()
                    while not self.preview_stop.is_set() and (time.monotonic() < self.preview_until or self.pending_capture):
                        # Capture requests take ownership between complete USB bursts.
                        with self.lock:
                            job = self.pending_capture
                            self.pending_capture = None
                        if job:
                            self.take_photo(job, camera)
                        else:
                            try:
                                frame = stream_jpeg(camera.read_stream())
                            except ValueError:
                                continue  # Discard a partial FIFO frame, never show torn rows.
                            with self.live_condition:
                                self.live_frame = frame
                                self.live_sequence += 1
                                self.live_condition.notify_all()
                finally:
                    try:
                        camera.stop_stream()
                    finally:
                        camera.close()
        except Exception as error:
            print('Live view failed:', error, flush=True)
            with self.lock:
                self.error = str(error)
        finally:
            with self.lock:
                self.live_frame = None
                self.preview_worker = None
                job = self.pending_capture
                self.pending_capture = None
                self.live_condition.notify_all()
            if job:
                self.take_photo(job)

    def live(self, after=0):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            self.preview()
            with self.live_condition:
                self.live_condition.wait_for(
                    lambda: self.live_sequence > after or self.paused or self.preview_worker is None,
                    timeout=max(0, deadline-time.monotonic()))
                if self.paused:
                    raise ValueError('The camera is paused for sleep.')
                if self.live_frame is not None:
                    return self.live_frame, self.live_sequence
                if self.preview_worker is None and self.error:
                    raise RuntimeError(self.error)
                # A request can arrive while an expired stream is closing.
                # Once it has closed, renew the lease on a fresh owner.
        raise RuntimeError(self.error or 'Live view is starting.')

    def capture(self, data):
        flash = data.get('flash', False)
        if type(flash) is not bool:
            raise ValueError('Flash must be true or false.')
        with self.lock:
            if self.paused:
                raise ValueError('The camera is paused for sleep.')
            if self.state == 'capturing':
                raise ValueError('The camera is already taking a photo.')
            if shutil.disk_usage(FILES).free < 64 * 1024 * 1024:
                raise ValueError('Not enough free storage for a photo.')
            self.job = dict(id=uuid.uuid4().hex, state='running', flash=flash)
            self.state, self.error = 'capturing', ''
            if getattr(self, 'preview_worker', None) and self.preview_worker.is_alive():
                self.pending_capture = dict(self.job)
            else:
                self.worker = threading.Thread(target=self.take_photo, args=(dict(self.job),))
                self.worker.start()
            return dict(self.job)

    def take_photo(self, job, streaming_camera=None):
        try:
            from reader_camera_hardware import Camera, load_firmware
            from reader_camera_image import encode
            load_firmware(FIRMWARE)
            with tempfile.TemporaryDirectory(dir=STATE, prefix='capture-') as temporary:
                temporary = Path(temporary)
                with self.hardware_lock if streaming_camera is None else nullcontext():
                    camera = streaming_camera or Camera()
                    try:
                        camera.capture_still(temporary / 'frame.raw', flash=job['flash'], stream_ready=streaming_camera is not None)
                    finally:
                        if streaming_camera is None:
                            camera.close()
                name = 'photo-' + time.strftime('%Y%m%d-%H%M%S') + '-' + job['id'][:6] + '.jpg'
                photo, preview = FILES / name, STATE / 'preview.jpg'
                encode(temporary / 'frame.raw', temporary / 'photo.jpg', temporary / 'preview.jpg')
                os.replace(temporary / 'photo.jpg', photo)
                os.replace(temporary / 'preview.jpg', preview)
                latest = dict(filename=name, width=2592, height=1920,
                              bytes=photo.stat().st_size, flash=job['flash'], created=time.time())
                (temporary / 'latest.json').write_text(json.dumps(latest))
                os.replace(temporary / 'latest.json', STATE / 'latest.json')
            with self.lock:
                self.latest = latest
                self.job = {**job, 'state': 'complete', 'photo': latest}
                self.state, self.error = 'idle', ''
        except Exception as error:
            print('Capture failed:', error, flush=True)
            with self.lock:
                self.job = {**job, 'state': 'failed', 'error': str(error)}
                self.state, self.error = 'error', str(error)

    def suspend(self):
        # Stop accepting captures before waiting; no USB owner survives S3.
        with self.lock:
            self.paused = True
            self.preview_until = 0
            self.preview_stop.set()
            preview_worker = getattr(self, 'preview_worker', None)
            worker = self.worker
        if preview_worker:
            preview_worker.join(45)
            if preview_worker.is_alive():
                raise RuntimeError('Camera is still stopping live view.')
        if worker:
            worker.join(45)
            if worker.is_alive():
                raise RuntimeError('Camera is still finishing a capture. Try sleep again.')
        with self.lock:
            self.state = 'sleeping'
        return self.status()

    def resume(self):
        with self.lock:
            if (self.state == 'capturing' or
                    getattr(self, 'preview_worker', None) and self.preview_worker.is_alive()):
                raise ValueError('Camera is busy.')
            self.prepare()
            self.paused = False
        return self.status()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def reply(self, code, data):
        body = json.dumps(data).encode()
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith('/live?'):
            try:
                from urllib.parse import parse_qs, urlsplit
                after = int(parse_qs(urlsplit(self.path).query).get('after', ['0'])[0])
                frame, sequence = self.server.platform.live(after)
                self.send_response(200)
                self.send_header('Content-Type', 'image/jpeg')
                self.send_header('Content-Length', str(len(frame)))
                self.send_header('X-Frame', str(sequence))
                self.end_headers()
                self.wfile.write(frame)
                return
            except (ValueError, RuntimeError, OSError) as error:
                return self.reply(503, {'error': str(error)})
        if self.path == '/status':
            return self.reply(200, self.server.platform.status())
        self.reply(404, {'error': 'Unknown camera endpoint.'})

    def do_POST(self):
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 <= length <= 1024:
                raise ValueError('Invalid camera request.')
            data = json.loads(self.rfile.read(length) or b'{}')
            if not isinstance(data, dict):
                raise ValueError('Expected a settings object.')
            platform = self.server.platform
            if self.path == '/capture':
                return self.reply(202, platform.capture(data))
            if self.path == '/suspend':
                return self.reply(200, platform.suspend())
            if self.path == '/resume':
                return self.reply(200, platform.resume())
            self.reply(404, {'error': 'Unknown camera endpoint.'})
        except (ValueError, RuntimeError, OSError) as error:
            self.reply(409, {'error': str(error)})


class Server(ThreadingMixIn, UnixStreamServer):
    pass


def serve():
    with open('/run/reader-camera.lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        Path(SOCKET).unlink(missing_ok=True)
        platform = Platform()
        def stop(_signal, _frame):
            raise SystemExit(0)
        signal.signal(signal.SIGTERM, stop)
        try:
            with Server(SOCKET, Handler) as server:
                os.chmod(SOCKET, 0o660)
                server.platform = platform
                server.serve_forever()
        finally:
            platform.suspend()
            Path(SOCKET).unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('serve', 'status', 'capture', 'suspend', 'resume'))
    parser.add_argument('--flash', action='store_true')
    args = parser.parse_args()
    if args.action == 'serve':
        return serve()
    result = request(args.action, **({'flash': args.flash} if args.action == 'capture' else {}))
    if args.action == 'capture':
        job_id = result['id']
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            state = request()
            job = state.get('job') or {}
            if job.get('id') != job_id:
                raise RuntimeError('Another capture replaced this job.')
            if job['state'] != 'running':
                if job['state'] == 'failed':
                    raise RuntimeError(job['error'])
                result = job
                break
            time.sleep(.5)
        else:
            raise RuntimeError('Camera operation timed out.')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        raise SystemExit(str(error))
