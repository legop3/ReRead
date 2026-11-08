"""One saved Wi-Fi network using the distro's wpa_supplicant and dhcpcd."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import threading

CONFIG = Path('/etc/wpa_supplicant/reader.conf')
HEADER = 'ctrl_interface=DIR=/run/wpa_supplicant\nupdate_config=0\n'
LOCK = threading.Lock()


def interface():
    for path in sorted(Path('/sys/class/net').iterdir()):
        if (path / 'wireless').is_dir() or (path / 'phy80211').exists():
            return path.name
    return None


def cli(device, *args):
    result = subprocess.run(['wpa_cli', '-p', '/run/wpa_supplicant', '-i', device, *args],
                            text=True, capture_output=True, timeout=8)
    if result.returncode:
        raise ValueError('Wi-Fi service is starting or unavailable. Try again shortly.')
    return result.stdout.strip()


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


def saved_ssid():
    if CONFIG.exists():
        match = re.search(r'^\s*ssid=([0-9a-fA-F]+)\s*$', CONFIG.read_text(), re.M)
        if match:
            return bytes.fromhex(match[1]).decode('utf-8', errors='replace')
    return ''


def status():
    device = interface()
    result = dict(interface=device, state='NO_ADAPTER', ssid='', saved_ssid=saved_ssid(),
                  address='', networks=[], error='')
    if not device:
        return result
    try:
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


def connect(data):
    ssid, password = data.get('ssid', ''), data.get('password', '')
    security = data.get('security', 'personal')
    if not isinstance(ssid, str) or not 1 <= len(ssid.encode()) <= 32 or '\x00' in ssid:
        raise ValueError('Network name must be 1-32 bytes.')
    if not isinstance(password, str):
        raise ValueError('Invalid password.')
    if security not in ('personal', 'open'):
        raise ValueError('Use a WPA/WPA2 Personal or open network. WPA3-only and enterprise networks are not supported yet.')
    network = '\nnetwork={\n    ssid=' + ssid.encode().hex() + '\n'
    if data.get('hidden'):
        network += '    scan_ssid=1\n'
    if security == 'personal':
        if re.fullmatch('[0-9a-fA-F]{64}', password):
            psk = password.lower()
        elif 8 <= len(password) <= 63 and all(32 <= ord(c) <= 126 for c in password):
            psk = hashlib.pbkdf2_hmac('sha1', password.encode(), ssid.encode(), 4096, 32).hex()
        else:
            raise ValueError('Wi-Fi password must be 8-63 ASCII characters, or a 64-digit hexadecimal PSK.')
        network += '    key_mgmt=WPA-PSK\n    psk=' + psk + '\n'
    else:
        network += '    key_mgmt=NONE\n'
    network += '}\n'
    with LOCK:
        device = interface()
        if not device:
            raise ValueError('No Wi-Fi adapter detected.')
        # Confirm the service is ready before replacing the saved network.
        if cli(device, 'ping') != 'PONG':
            raise ValueError('Wi-Fi service is not ready.')
        previous = CONFIG.read_text() if CONFIG.exists() else HEADER
        store(HEADER + network)
        if cli(device, 'reconfigure') != 'OK':
            store(previous)
            cli(device, 'reconfigure')
            raise ValueError('Could not apply the network settings.')
        cli(device, 'reconnect')


def forget():
    with LOCK:
        store(HEADER)
        device = interface()
        if device:
            cli(device, 'disconnect')
            cli(device, 'reconfigure')
