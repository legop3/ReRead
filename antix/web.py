#!/usr/bin/python3
"""Small USB-only control page; uploads/downloads stream directly to SSD."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
from urllib.parse import unquote, urlsplit
import reader_wifi as wifi
import reader_manager as manager

FILES = Path('/var/lib/reader/files')
PAGE = Path('/usr/share/reader/web.html')
ADDRESS = '192.168.7.1'


class Handler(BaseHTTPRequestHandler):
    def reply(self, status, body, kind='text/plain; charset=utf-8'):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(status)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        self.wfile.write(body)

    def valid_host(self):
        # Keep browser requests on this USB origin, including state changes.
        return self.headers.get('Host') == ADDRESS and self.headers.get('Origin', 'http://' + ADDRESS) == 'http://' + ADDRESS

    def filename(self):
        name = unquote(urlsplit(self.path).path.removeprefix('/files/'))
        if not name or name in ('.', '..') or '/' in name or '\\' in name or '\x00' in name or name.startswith('.'):
            raise ValueError('Use a plain filename without a leading dot.')
        return FILES / name

    def do_GET(self):
        if not self.valid_host():
            return self.reply(403, 'Use http://' + ADDRESS)
        route = urlsplit(self.path).path
        if route == '/':
            return self.reply(200, PAGE.read_bytes(), 'text/html; charset=utf-8')
        if route == '/api/status':
            usage = shutil.disk_usage(FILES)
            body = dict(hostname=socket.gethostname(), version=manager.version(), kernel=os.uname().release,
                        free=usage.free, total=usage.total,
                        files=[dict(name=p.name, bytes=p.stat().st_size) for p in sorted(FILES.iterdir())
                               if p.is_file() and not p.is_symlink() and not p.name.startswith('.')])
            return self.reply(200, json.dumps(body), 'application/json')
        if route == '/api/wifi':
            return self.reply(200, json.dumps(wifi.status()), 'application/json')
        if route == '/api/platform':
            return self.reply(200, json.dumps(manager.status()), 'application/json')
        if route.startswith('/files/'):
            try:
                path = self.filename()
                if path.is_symlink() or not path.is_file():
                    return self.reply(404, 'File not found')
                with path.open('rb') as source:
                    self.send_response(200)
                    self.send_header('Content-Type', 'application/octet-stream')
                    self.send_header('Content-Length', str(os.fstat(source.fileno()).st_size))
                    self.send_header('Content-Disposition', 'attachment')
                    self.send_header('X-Content-Type-Options', 'nosniff')
                    self.end_headers()
                    shutil.copyfileobj(source, self.wfile, 65536)
                return
            except ValueError as error:
                return self.reply(400, str(error))
        self.reply(404, 'Not found')

    def do_POST(self):
        if not self.valid_host():
            return self.reply(403, 'Use http://' + ADDRESS)
        self.connection.settimeout(120)
        temporary = None
        try:
            length = int(self.headers.get('Content-Length', '-1'))
            if length < 0 or self.headers.get('Transfer-Encoding'):
                raise ValueError('Content-Length required.')
            route = urlsplit(self.path).path
            if route in ('/api/updates/install', '/api/apps/install'):
                manager.STAGING.mkdir(parents=True, exist_ok=True)
                if length > max(0, shutil.disk_usage(manager.STAGING).free - 64 * 1024 * 1024):
                    self.close_connection = True
                    return self.reply(507, 'Not enough storage for the package.')
                with tempfile.NamedTemporaryFile(dir=manager.STAGING, prefix='package-', suffix='.deb', delete=False) as dest:
                    temporary = Path(dest.name)
                    remaining = length
                    while remaining:
                        chunk = self.rfile.read(min(remaining, 65536))
                        if not chunk:
                            raise ValueError('Upload interrupted.')
                        dest.write(chunk)
                        remaining -= len(chunk)
                    dest.flush()
                    os.fsync(dest.fileno())
                    os.fchmod(dest.fileno(), 0o644)
                job = manager.start('install', str(temporary), 'platform' if route == '/api/updates/install' else 'app')
                temporary = None  # The detached worker now owns the upload.
                return self.reply(202, json.dumps(job), 'application/json')
            if route.startswith('/files/'):
                target = self.filename()
                if length > max(0, shutil.disk_usage(FILES).free - 32 * 1024 * 1024):
                    self.close_connection = True
                    return self.reply(507, 'Not enough free storage.')
                with tempfile.NamedTemporaryFile(dir=FILES, prefix='.upload-', delete=False) as dest:
                    temporary = Path(dest.name)
                    remaining = length
                    while remaining:
                        chunk = self.rfile.read(min(remaining, 65536))
                        if not chunk:
                            raise ValueError('Upload interrupted.')
                        dest.write(chunk)
                        remaining -= len(chunk)
                    dest.flush()
                    os.fsync(dest.fileno())
                os.replace(temporary, target)
                return self.reply(200, 'Uploaded ' + target.name)
            if route not in ('/api/settings', '/api/wifi/scan', '/api/wifi/connect', '/api/wifi/forget', '/api/apps/action') or length > 4096:
                self.close_connection = True
                return self.reply(400, 'Invalid request.')
            data = json.loads(self.rfile.read(length))
            if not isinstance(data, dict):
                raise ValueError('Expected settings object.')
            if route == '/api/apps/action':
                action = data.get('action')
                if action not in ('remove', 'enable', 'disable'):
                    raise ValueError('Unknown app action.')
                job = manager.start(action, data.get('package', ''))
                return self.reply(202, json.dumps(job), 'application/json')
            if route == '/api/wifi/scan':
                wifi.scan()
                return self.reply(200, 'Scanning for networks…')
            if route == '/api/wifi/connect':
                wifi.connect(data)
                return self.reply(200, 'Network saved. Connecting…')
            if route == '/api/wifi/forget':
                wifi.forget()
                return self.reply(200, 'Saved network forgotten.')
            hostname = data.get('hostname', '')
            password = data.get('password', '')
            if hostname and not re.fullmatch(r'[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,30}[a-zA-Z0-9])?', hostname):
                raise ValueError('Hostname: 1-32 letters, numbers or hyphens.')
            if password and (len(password) < 8 or any(c in password for c in '\r\n\x00:')):
                raise ValueError('SSH password: at least 8 characters, without colons or newlines.')
            if password:
                subprocess.run(['chpasswd'], input='root:' + password + '\n', text=True, check=True)
            if hostname:
                subprocess.run(['hostname', hostname], check=True)
                Path('/etc/hostname').write_text(hostname + '\n')
            return self.reply(200, 'Saved. SSH: ssh root@' + ADDRESS)
        except (ValueError, TypeError, OSError, subprocess.SubprocessError) as error:
            self.close_connection = True
            self.reply(400, str(error))
        finally:
            if temporary:
                temporary.unlink(missing_ok=True)


if __name__ == '__main__':
    FILES.mkdir(parents=True, exist_ok=True)
    # Remove incomplete uploads from an interrupted prior boot.
    for path in FILES.glob('.upload-*'):
        path.unlink()
    ThreadingHTTPServer((ADDRESS, 80), Handler).serve_forever()
