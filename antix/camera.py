#!/usr/bin/python3
"""ReRead shared camera service and local HTTP client for apps."""
import argparse
import fcntl
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
        self.backend = None
        self.paused = False
        self.state = 'starting'
        self.error = ''
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
        from reader_camera_backend import NativeCamera, load_firmware
        try:
            if self.backend:
                self.backend.close()
                self.backend = None
            load_firmware(FIRMWARE)
            self.backend = NativeCamera()
            self.state, self.error = 'idle', ''
        except Exception as error:
            self.state, self.error = 'error', str(error)
            print('Camera unavailable:', error, flush=True)

    def status(self):
        with self.lock:
            stats = self.backend.stats() if self.backend and not self.paused else {}
            return dict(state=self.state, error=self.error, paused=self.paused,
                        job=self.job, latest=self.latest, engine='native',
                        live=self.preview_until > time.monotonic() and not self.paused,
                        frame=stats.get('displayed', 0), stream=stats)

    def live(self, after=0):
        with self.lock:
            if self.paused:
                raise ValueError('The camera is paused for sleep.')
            if not self.backend:
                raise RuntimeError(self.error or 'The camera is unavailable.')
            backend = self.backend
            self.preview_until = time.monotonic() + 8
        return backend.frame(after)

    def capture(self, data):
        flash = data.get('flash', False)
        mode = data.get('mode', 'photo')
        if mode not in ('photo', 'document'):
            raise ValueError('Camera mode must be photo or document.')
        if type(flash) is not bool:
            raise ValueError('Flash must be true or false.')
        with self.lock:
            if self.paused:
                raise ValueError('The camera is paused for sleep.')
            if not self.backend:
                raise ValueError(self.error or 'The camera is unavailable.')
            if self.state == 'capturing':
                raise ValueError('The camera is already taking a photo.')
            if shutil.disk_usage(FILES).free < 64 * 1024 * 1024:
                raise ValueError('Not enough free storage for a photo.')
            self.job = dict(id=uuid.uuid4().hex, state='running', flash=flash, mode=mode)
            self.state, self.error = 'capturing', ''
            self.worker = threading.Thread(target=self.take_photo, args=(dict(self.job), self.backend))
            self.worker.start()
            return dict(self.job)

    def take_photo(self, job, backend):
        try:
            with tempfile.TemporaryDirectory(dir=STATE, prefix='capture-') as temporary:
                temporary = Path(temporary)
                result = backend.capture(job['flash'], temporary / 'photo.jpg', temporary / 'preview.jpg', document=job['mode'] == 'document')
                name = 'photo-' + time.strftime('%Y%m%d-%H%M%S') + '-' + job['id'][:6] + '.jpg'
                photo, preview = FILES / name, STATE / 'preview.jpg'
                os.replace(temporary / 'photo.jpg', photo)
                os.replace(temporary / 'preview.jpg', preview)
                latest = dict(filename=name, **result,
                              bytes=photo.stat().st_size, flash=job['flash'], mode=job['mode'], created=time.time())
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
        with self.lock:
            self.paused = True
            self.preview_until = 0
            worker = self.worker
            backend = self.backend
        if worker:
            worker.join(45)
            if worker.is_alive():
                raise RuntimeError('Camera is still finishing a capture. Try sleep again.')
        if backend:
            backend.close()
        with self.lock:
            self.backend = None
            self.state = 'sleeping'
        return self.status()

    def resume(self):
        with self.lock:
            if self.state == 'capturing':
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
    # Requests can bunch up during the stock-priority still USB transfer.
    request_queue_size = 32


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
    parser.add_argument('--document', action='store_true')
    args = parser.parse_args()
    if args.action == 'serve':
        return serve()
    result = request(args.action, **({'flash': args.flash, 'mode': 'document' if args.document else 'photo'} if args.action == 'capture' else {}))
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
