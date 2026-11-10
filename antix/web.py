#!/usr/bin/python3
"""USB-only system management; shared-file transfers use bundled Copyparty."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
from urllib.parse import urlsplit, parse_qs
import reader_wifi as wifi
import reader_repositories as repositories
import reader_manager as manager
import reader_battery as battery
import reader_app_settings as app_settings

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

    def do_GET(self):
        if not self.valid_host():
            return self.reply(403, 'Use http://' + ADDRESS)
        route = urlsplit(self.path).path
        if route == '/':
            return self.reply(200, PAGE.read_bytes(), 'text/html; charset=utf-8')
        if route == '/assets/app-settings.js':
            return self.reply(200, Path('/usr/share/reader/app-settings.js').read_bytes(), 'text/javascript; charset=utf-8')
        if route == '/assets/jsoneditor.min.js':
            return self.reply(200, Path('/usr/share/reader/jsoneditor.min.js').read_bytes(), 'text/javascript; charset=utf-8')
        if route == '/api/apps/settings':
            try:
                package = parse_qs(urlsplit(self.path).query).get('package', [''])[0]
                return self.reply(200, json.dumps(app_settings.load(package, manager.inventory())), 'application/json')
            except (ValueError, OSError) as error:
                return self.reply(400, str(error))
        if route == '/api/status':
            usage = shutil.disk_usage(FILES)
            body = dict(hostname=socket.gethostname(), version=manager.version(), kernel=os.uname().release,
                        free=usage.free, total=usage.total, files_root=str(FILES))
            return self.reply(200, json.dumps(body), 'application/json')
        if route == '/api/repositories':
            return self.reply(200, json.dumps(repositories.inventory()), 'application/json')
        if route == '/api/wifi/profile':
            try:
                identity = parse_qs(urlsplit(self.path).query).get('id', [''])[0]
                return self.reply(200, json.dumps(wifi.profile(wifi.require_device(), identity)), 'application/json')
            except (ValueError, OSError, subprocess.SubprocessError) as error:
                return self.reply(400, str(error))
        if route == '/api/wifi':
            return self.reply(200, json.dumps(wifi.status()), 'application/json')
        if route == '/api/platform':
            return self.reply(200, json.dumps(manager.status()), 'application/json')
        if route == '/api/battery':
            return self.reply(200, json.dumps(battery.status()), 'application/json')
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
            if route not in ('/api/settings', '/api/wifi/scan', '/api/wifi/save', '/api/wifi/action', '/api/apps/action',
                             '/api/packages/check', '/api/packages/install', '/api/repositories', '/api/apps/settings') or length > (131072 if route in ('/api/repositories', '/api/apps/settings') else 4096):
                self.close_connection = True
                return self.reply(400, 'Invalid request.')
            data = json.loads(self.rfile.read(length))
            if not isinstance(data, dict):
                raise ValueError('Expected settings object.')
            if route == '/api/apps/settings':
                return self.reply(200, json.dumps(app_settings.save(data, manager.inventory())), 'application/json')
            if route in ('/api/packages/check', '/api/packages/install'):
                job = manager.start('check' if route.endswith('/check') else 'online', data.get('package', ''))
                return self.reply(202, json.dumps(job), 'application/json')
            if route == '/api/apps/action':
                action = data.get('action')
                if action not in ('remove', 'enable', 'disable'):
                    raise ValueError('Unknown app action.')
                job = manager.start(action, data.get('package', ''))
                return self.reply(202, json.dumps(job), 'application/json')
            if route == '/api/wifi/scan':
                wifi.scan()
                return self.reply(200, 'Scanning for networks…')
            if route == '/api/wifi/save':
                wifi.save(data)
                return self.reply(200, 'Network saved.' + (' Connecting…' if data.get('connect') else ''))
            if route == '/api/wifi/action':
                wifi.action(data)
                return self.reply(200, 'Network settings saved.')
            if route == '/api/repositories':
                repositories.change(data)
                return self.reply(200, 'Repository saved. Check for apps and updates to refresh the catalog.')
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
        except (ValueError, TypeError, RuntimeError, OSError, subprocess.SubprocessError) as error:
            self.close_connection = True
            self.reply(400, str(error))
        finally:
            if temporary:
                temporary.unlink(missing_ok=True)


if __name__ == '__main__':
    FILES.mkdir(parents=True, exist_ok=True)
    ThreadingHTTPServer((ADDRESS, 80), Handler).serve_forever()
