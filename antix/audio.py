#!/usr/bin/python3
"""ReRead ALSA playback and persistent master volume; no audio server."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import subprocess
import sys

CARD = 'MID'
PCM = 'default:CARD=' + CARD
SAVED = Path('/var/lib/alsa/asound.state')
LEGACY = Path('/var/lib/reader/audio.json')
DISPLAY = Path('/run/reader-audio-status')
LOCK = Path('/run/reader-audio.lock')


def mixer(*args):
    return subprocess.run(['/usr/bin/amixer', '-M', '-c', CARD, *args],
                          check=True, capture_output=True, text=True, timeout=5).stdout


def status():
    try:
        text = mixer('sget', 'Master')
        percent = re.search(r'\[(\d+)%\]', text)
        switches = re.findall(r'\[(on|off)\]', text)
        if percent is None or not switches:
            raise ValueError('Master volume is unavailable.')
        jack = mixer('cget', "iface=CARD,name='Front Headphone Jack'")
        return dict(available=True, volume_percent=int(percent[1]), muted=switches[0] == 'off',
                    headphones=': values=on' in jack, card=CARD, playback_device=PCM)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        return dict(available=False, error=str(error))


def remember():
    SAVED.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['/usr/sbin/alsactl', '-f', str(SAVED), 'store', CARD],
                   check=True, capture_output=True, text=True, timeout=5)


def display(state):
    text = 'Audio: unavailable' if not state['available'] else 'Volume: ' + str(state['volume_percent']) + '%' + (' (muted)' if state['muted'] else '')
    temporary = DISPLAY.with_suffix('.tmp')
    temporary.write_text(text + '\n')
    os.replace(temporary, DISPLAY)


def change(action, value=None):
    with LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        current = status()
        if not current['available']:
            display(current)
            raise ValueError(current['error'])
        percent, muted = current['volume_percent'], current['muted']
        if action == 'init':
            if SAVED.exists():
                subprocess.run(['/usr/sbin/alsactl', '-f', str(SAVED), 'restore', CARD],
                               check=True, capture_output=True, text=True, timeout=5)
                state = status()
                display(state)
                return state
            percent, muted = 35, False
            if LEGACY.exists():
                saved = json.loads(LEGACY.read_text())
                percent, muted = saved['volume_percent'], saved['muted']
                if type(percent) is not int or not 0 <= percent <= 100 or type(muted) is not bool:
                    raise ValueError('Invalid saved audio settings.')
            # First-boot defaults; subsequent boots restore the ALSA mixer state.
            mixer('sset', 'Speaker', '100%', 'unmute')
            mixer('sset', 'Headphone', '100%', 'unmute')
            mixer('sset', 'Auto-Mute Mode', 'Enabled')
        elif action in ('up', 'down'):
            percent = max(0, min(100, percent + (5 if action == 'up' else -5)))
            muted = False
        elif action == 'set':
            if value is None or not 0 <= value <= 100:
                raise ValueError('Volume must be between 0 and 100.')
            percent, muted = value, False
        elif action == 'mute':
            muted = True
        elif action == 'unmute':
            muted = False
        mixer('sset', 'Master', str(percent) + '%', 'mute' if muted else 'unmute')
        state = status()
        if not state['available']:
            raise ValueError(state['error'])
        if action == 'init' or state['volume_percent'] != current['volume_percent'] or state['muted'] != current['muted']:
            remember()
            LEGACY.unlink(missing_ok=True)
        display(state)
        return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['init', 'status', 'up', 'down', 'set', 'mute', 'unmute', 'play'])
    parser.add_argument('value', nargs='?')
    args = parser.parse_args()
    try:
        if args.action == 'play':
            if args.value is None or not Path(args.value).is_file():
                raise ValueError('Choose an existing WAV/audio file supported by aplay.')
            # No volume lock while playing: buttons must work during playback.
            os.execv('/usr/bin/aplay', ['aplay', '-q', '-D', PCM, '--', args.value])
        state = status() if args.action == 'status' else change(args.action, int(args.value) if args.value is not None else None)
        print(json.dumps(state))
        return 0 if state['available'] else 1
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print('Audio:', error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
