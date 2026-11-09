"""Saved Wi-Fi profiles through wpa_supplicant's native control interface."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import threading

CONFIG = Path('/etc/wpa_supplicant/reader.conf')
CONTROL = '/run/wpa_supplicant'
HEADER = 'ctrl_interface=DIR=/run/wpa_supplicant\nupdate_config=1\n'
LOCK = threading.Lock()


def interface():
    for path in sorted(Path('/sys/class/net').iterdir()):
        if (path / 'wireless').is_dir() or (path / 'phy80211').exists():
            return path.name
    return None


def cli(device, *args):
    try:
        result = subprocess.run(['wpa_cli', '-p', CONTROL, '-i', device, '--', *args],
                                text=True, capture_output=True, timeout=8)
    except subprocess.TimeoutExpired:
        raise ValueError('Wi-Fi service did not respond. Try again shortly.') from None
    if result.returncode:
        raise ValueError('Wi-Fi service is starting or unavailable. Try again shortly.')
    return result.stdout.rstrip('\r\n')


def decoded(text):
    """Decode wpa_supplicant's escaped UTF-8 SSIDs, preserving literal slashes."""
    data = bytearray()
    index = 0
    while index < len(text):
        if text[index] == '\\' and index + 1 < len(text):
            if text[index + 1] == 'x' and re.fullmatch('[0-9a-fA-F]{2}', text[index + 2:index + 4]):
                data.append(int(text[index + 2:index + 4], 16))
                index += 4
                continue
            escapes = {'n': b'\n', 'r': b'\r', 't': b'\t', 'e': b'\x1b', '\\': b'\\', '"': b'"'}
            if text[index + 1] in escapes:
                data.extend(escapes[text[index + 1]])
                index += 2
                continue
        data.extend(text[index].encode())
        index += 1
    return data.decode('utf-8', errors='replace')


def profiles(device):
    result = []
    for line in cli(device, 'list_networks').splitlines():
        fields = line.split('\t', 3)
        if len(fields) == 4 and fields[0].isdigit():
            result.append(dict(id=fields[0], ssid=decoded(fields[1]),
                               enabled='[DISABLED]' not in fields[3], current='[CURRENT]' in fields[3]))
    return result


def profile(device, identity):
    identity = str(identity)
    saved = next((p for p in profiles(device) if p['id'] == identity), None)
    if not saved:
        raise ValueError('Saved network no longer exists. Refresh the page.')
    security = cli(device, 'get_network', identity, 'key_mgmt')
    saved.update(security='open' if security == 'NONE' else 'personal' if 'WPA-PSK' in security else 'unsupported',
                 hidden=cli(device, 'get_network', identity, 'scan_ssid') == '1',
                 priority=int(cli(device, 'get_network', identity, 'priority')))
    return saved


def status():
    device = interface()
    result = dict(interface=device, state='NO_ADAPTER', ssid='', saved_ssid='', profiles=[],
                  address='', networks=[], error='')
    if not device:
        return result
    try:
        result['profiles'] = profiles(device)
        result['saved_ssid'] = next((p['ssid'] for p in result['profiles'] if p['current']), '')
        fields = dict(line.split('=', 1) for line in cli(device, 'status').splitlines() if '=' in line)
        result.update(state=fields.get('wpa_state', 'STARTING'), ssid=decoded(fields.get('ssid', '')))
        addresses = subprocess.run(['ip', '-j', '-4', 'address', 'show', 'dev', device],
                                   text=True, capture_output=True, timeout=3)
        if addresses.returncode == 0:
            for link in json.loads(addresses.stdout):
                for address in link.get('addr_info', []):
                    if address.get('scope') == 'global' and not address['local'].startswith('169.254.'):
                        result['address'] = address['local']
        networks = {}
        for line in cli(device, 'scan_results').splitlines():
            parts = line.split('\t', 4)
            if len(parts) != 5 or not parts[1].isdigit():
                continue
            _, frequency, signal, flags, name = parts
            ssid = decoded(name)
            if not ssid:
                continue
            security = ('personal' if 'PSK' in flags else 'wpa3' if 'SAE' in flags else
                        'unsupported' if any(tag in flags for tag in ['WEP', 'EAP', 'WPA', 'RSN', 'OWE']) else 'open')
            network = dict(ssid=ssid, signal=int(signal), security=security, frequency=int(frequency))
            if ssid not in networks or network['signal'] > networks[ssid]['signal']:
                networks[ssid] = network
        result['networks'] = sorted(networks.values(), key=lambda item: item['signal'], reverse=True)
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        result['error'] = str(error)
    return result


def scan():
    device = interface()
    if not device:
        raise ValueError('No Wi-Fi adapter detected.')
    response = cli(device, 'scan')
    if response not in ('OK', 'FAIL-BUSY'):
        raise ValueError('The adapter could not start a scan. Try again shortly.')


def store(content):
    CONFIG.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', dir=CONFIG.parent, delete=False) as dest:
        temporary = Path(dest.name)
        try:
            os.fchmod(dest.fileno(), 0o600)
            dest.write(content)
            dest.flush()
            os.fsync(dest.fileno())
            os.replace(temporary, CONFIG)
        finally:
            temporary.unlink(missing_ok=True)


def require_device():
    device = interface()
    if not device or cli(device, 'ping') != 'PONG':
        raise ValueError('Plug in a Wi-Fi adapter and wait for its service to start.')
    return device


def command(device, *args):
    if cli(device, *args) != 'OK':
        raise ValueError('Wi-Fi could not apply that setting. Previous saved profiles were restored.')


def persist(device):
    command(device, 'set', 'update_config', '1')
    command(device, 'save_config')
    CONFIG.chmod(0o600)


def select(device, identity):
    # SELECT_NETWORK temporarily disables alternatives. Save their intended
    # state first, then re-enable those alternatives without changing selection.
    enabled = [p['id'] for p in profiles(device) if p['enabled']]
    command(device, 'raw', 'ENABLE_NETWORK', identity, 'no-connect')
    persist(device)
    command(device, 'select_network', identity)
    for other in enabled:
        if other != identity:
            command(device, 'raw', 'ENABLE_NETWORK', other, 'no-connect')


def save(data):
    ssid, password = data.get('ssid', ''), data.get('password', '')
    security = data.get('security', 'personal')
    if not isinstance(ssid, str) or not 1 <= len(ssid.encode()) <= 32 or '\x00' in ssid:
        raise ValueError('Network name must be 1-32 bytes.')
    if not isinstance(password, str):
        raise ValueError('Invalid password.')
    if security not in ('personal', 'open'):
        raise ValueError('Use WPA/WPA2 Personal or an open network.')
    priority = int(data.get('priority', 0))
    if not -100 <= priority <= 100:
        raise ValueError('Priority must be between -100 and 100.')
    with LOCK:
        device = require_device()
        identity = str(data['id']) if data.get('id') is not None else None
        old = profile(device, identity) if identity is not None else None
        if old is None and any(p['ssid'] == ssid for p in profiles(device)):
            raise ValueError('That network is already saved. Choose Edit instead.')
        if security == 'personal' and not password and not (old and old['security'] == 'personal' and old['ssid'] == ssid):
            raise ValueError('Enter a password for this network. A renamed network needs its password again.')
        psk = None
        if security == 'personal' and password:
            if re.fullmatch('[0-9a-fA-F]{64}', password):
                psk = password.lower()
            elif 8 <= len(password) <= 63 and all(32 <= ord(c) <= 126 for c in password):
                psk = hashlib.pbkdf2_hmac('sha1', password.encode(), ssid.encode(), 4096, 32).hex()
            else:
                raise ValueError('Password must be 8-63 ASCII characters, or a 64-digit hexadecimal PSK.')
        previous = CONFIG.read_text() if CONFIG.exists() else HEADER
        try:
            if old is None:
                identity = cli(device, 'add_network')
                if not identity.isdigit():
                    raise ValueError('Could not add a Wi-Fi profile.')
            fields = dict(ssid=ssid.encode().hex(), key_mgmt='WPA-PSK' if security == 'personal' else 'NONE',
                          scan_ssid='1' if data.get('hidden') else '0', priority=str(priority))
            if psk:
                fields['psk'] = psk
            for key, value in fields.items():
                command(device, 'set_network', identity, key, value)
            if data.get('enabled', True):
                command(device, 'raw', 'ENABLE_NETWORK', identity, 'no-connect')
            else:
                command(device, 'disable_network', identity)
            persist(device)
            if data.get('connect', False):
                select(device, identity)
        except (ValueError, OSError, subprocess.SubprocessError):
            store(previous)
            cli(device, 'reconfigure')
            raise
    return identity


def action(data):
    with LOCK:
        device = require_device()
        identity = str(data.get('id', ''))
        saved = profile(device, identity)
        operation = data.get('action')
        previous = CONFIG.read_text()
        try:
            if operation == 'connect':
                select(device, identity)
            elif operation in ('enable', 'disable', 'forget'):
                command(device, {'enable':'enable_network', 'disable':'disable_network', 'forget':'remove_network'}[operation], identity)
                persist(device)
            else:
                raise ValueError('Unknown network action.')
        except (ValueError, OSError, subprocess.SubprocessError):
            store(previous)
            cli(device, 'reconfigure')
            raise
    return saved['ssid']
