#!/usr/bin/python3
"""Persistent LCD brightness through Linux's standard backlight interface."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import sys

BACKLIGHTS = Path('/sys/class/backlight')
CONFIG = Path('/etc/reader/display.json')
LOCK = Path('/run/reader-brightness.lock')
MINIMUM_PERCENT = 10
STEP_PERCENT = 10


def provider():
    """Use the Reader's native 100-step driver, with a generic fallback."""
    choices = []
    for path in BACKLIGHTS.iterdir():
        try:
            maximum = int((path / 'max_brightness').read_text())
            kind = (path / 'type').read_text().strip()
            if maximum > 0 and (path / 'brightness').exists():
                rank = 0 if path.name == 'psb-bl' else {'raw': 1, 'platform': 2, 'firmware': 3}.get(kind, 4)
                choices.append((rank, path.name, path, maximum))
        except (OSError, ValueError):
            continue
    if not choices:
        raise ValueError('No Linux backlight device is available.')
    _, _, path, maximum = min(choices)
    return path, maximum


def snapshot(path, maximum):
    level = int((path / 'brightness').read_text())
    try:
        actual = int((path / 'actual_brightness').read_text())
    except (OSError, ValueError):
        actual = level
    return dict(available=True, brightness_percent=(level * 100 + maximum // 2) // maximum,
                actual_percent=(actual * 100 + maximum // 2) // maximum,
                provider=path.name, raw_brightness=level, max_brightness=maximum,
                minimum_percent=MINIMUM_PERCENT)


def status():
    try:
        return snapshot(*provider())
    except (OSError, ValueError) as error:
        return dict(available=False, error=str(error))


def remember(percent):
    values = json.loads(CONFIG.read_text()) if CONFIG.exists() else {}
    if values.get('brightness_percent') == percent:
        return
    values['brightness_percent'] = percent
    CONFIG.parent.mkdir(parents=True, exist_ok=True)
    temporary = CONFIG.with_suffix('.tmp')
    temporary.write_text(json.dumps(values) + '\n')
    os.replace(temporary, CONFIG)


def change(action, value=None):
    with LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        path, maximum = provider()
        current = snapshot(path, maximum)
        percent = current['brightness_percent']
        if action == 'init':
            if CONFIG.exists():
                percent = json.loads(CONFIG.read_text()).get('brightness_percent', percent)
        elif action == 'set':
            percent = value
        elif action in ('up', 'down'):
            percent += STEP_PERCENT if action == 'up' else -STEP_PERCENT
            percent = max(MINIMUM_PERCENT, min(100, percent))
        else:
            raise ValueError('Unknown brightness action.')
        if type(percent) is not int or not MINIMUM_PERCENT <= percent <= 100:
            raise ValueError('Brightness must be between 10 and 100 percent.')
        level = max(1, min(maximum, (percent * maximum + 50) // 100))
        if level != current['raw_brightness']:
            (path / 'brightness').write_text(str(level) + '\n')
        state = snapshot(path, maximum)
        remember(state['brightness_percent'])
        return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['init', 'status', 'get', 'up', 'down', 'set'])
    parser.add_argument('value', nargs='?', type=int)
    args = parser.parse_args()
    try:
        state = status() if args.action in ('status', 'get') else change(args.action, args.value)
        print(json.dumps(state))
        return 0 if state['available'] else 1
    except (OSError, ValueError, TypeError) as error:
        print('Brightness:', error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
