#!/usr/bin/python3
"""Battery readings and low-charge protection, using Linux power_supply."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

SUPPLIES = Path('/sys/class/power_supply')
CONFIG = Path('/etc/reader/power.json')
STATE = Path('/run/reader-battery.json')
TEXT = Path('/run/reader-battery-status')
DEFAULTS = dict(warning_percent=12, critical_percent=6, shutdown_delay_seconds=30)


def read(path):
    try:
        return path.read_text().strip()
    except OSError:
        return None


def number(path):
    try:
        value = int(read(path))
        return value if 0 <= value < 0xffffffff else None
    except (TypeError, ValueError):
        return None


def settings():
    values = {**DEFAULTS, **json.loads(CONFIG.read_text())} if CONFIG.exists() else DEFAULTS.copy()
    if (any(type(values[k]) is not int for k in DEFAULTS) or
            not 0 < values['critical_percent'] < values['warning_percent'] <= 100 or
            not 10 <= values['shutdown_delay_seconds'] <= 3600):
        raise ValueError('Invalid battery thresholds or shutdown delay in ' + str(CONFIG))
    return values


def snapshot(root=SUPPLIES):
    supplies = sorted(root.glob('*'))
    batteries = [p for p in supplies if read(p / 'type') == 'Battery']
    sources = [number(p / 'online') for p in supplies
               if read(p / 'type') in ('Mains', 'USB', 'USB_C', 'USB_PD')]
    external = True if 1 in sources else False if sources and all(v == 0 for v in sources) else None
    presence = [number(p / 'present') for p in batteries]
    present = [p for p, value in zip(batteries, presence) if value == 1]
    result = dict(present=True if present else False if presence and all(v == 0 for v in presence) else None, external_power=external,
                  capacity_percent=None, raw_status=None, status='Unknown', voltage_uv=None,
                  charge_now_uah=None, charge_full_uah=None, current_ua=None)
    if not present:
        result['status'] = 'No battery' if result['present'] is False else 'Battery unavailable'
        return result
    battery = present[0]
    result.update(capacity_percent=number(battery / 'capacity'), raw_status=read(battery / 'status'),
                  voltage_uv=number(battery / 'voltage_now'), charge_now_uah=number(battery / 'charge_now'),
                  charge_full_uah=number(battery / 'charge_full'), current_ua=number(battery / 'current_now'))
    capacity = result['capacity_percent']
    if capacity is None and result['charge_now_uah'] is not None and result['charge_full_uah']:
        capacity = min(100, result['charge_now_uah'] * 100 // result['charge_full_uah'])
    result['capacity_percent'] = capacity if capacity is not None and capacity <= 100 else None
    raw = result['raw_status'] or 'Unknown'
    # Colbert _BST calls every non-charging state "Discharging", even on AC.
    result['status'] = 'Not charging' if external is True and raw == 'Discharging' else raw
    return result


class Policy:
    def __init__(self, config):
        self.config = config
        self.critical_since = None

    def update(self, sample, now):
        sample = dict(sample)
        percent = sample['capacity_percent']
        on_battery = sample['present'] is True and sample['external_power'] is False and percent is not None
        critical = on_battery and percent <= self.config['critical_percent']
        if not critical:
            self.critical_since = None
        elif self.critical_since is None:
            self.critical_since = now
        level = 'critical' if critical else 'low' if on_battery and percent <= self.config['warning_percent'] else 'normal'
        remaining = None if not critical else max(0, int(self.config['shutdown_delay_seconds'] - (now - self.critical_since) + .999))
        sample.update(level=level, shutdown_in_seconds=remaining, monitor_running=True, sampled_monotonic=now)
        return sample


def description(sample):
    percent = sample['capacity_percent']
    text = 'Battery: ' + (str(percent) + '%' if percent is not None else sample['status'])
    if sample.get('level') == 'critical':
        return text + ' CRITICAL - shutdown in ' + str(sample['shutdown_in_seconds']) + 's; connect power'
    if sample.get('level') == 'low':
        return text + ' LOW - connect power'
    if sample['external_power'] is True:
        return text + ' - external power, ' + sample['status'].lower()
    if sample['external_power'] is False:
        return text + ' - on battery' if sample['present'] else text
    return text + ' - power source unknown'


def status():
    try:
        sample = json.loads(STATE.read_text())
        if 0 <= time.monotonic() - sample['sampled_monotonic'] < 20:
            return sample
    except (OSError, ValueError, KeyError, TypeError):
        pass
    return dict(snapshot(), monitor_running=False, level='unknown', shutdown_in_seconds=None)


def publish(sample):
    for path, value in ((STATE, json.dumps(sample) + '\n'), (TEXT, description(sample) + '\n')):
        temporary = path.with_suffix('.tmp')
        temporary.write_text(value)
        os.replace(temporary, path)


def monitor():
    try:
        config = settings()
    except (OSError, ValueError, TypeError) as error:
        print('Battery configuration error; using defaults:', error, flush=True)
        config = DEFAULTS.copy()
    policy = Policy(config)
    previous = None
    while True:
        sample = policy.update(snapshot(), time.monotonic())
        publish(sample)
        state = (sample['present'], sample['external_power'], sample['status'], sample['level'])
        if state != previous:
            print(time.strftime('%Y-%m-%dT%H:%M:%S%z'), description(sample), flush=True)
            previous = state
        if sample['shutdown_in_seconds'] == 0:
            # Recheck live power/charge immediately before requesting shutdown.
            sample = policy.update(snapshot(), time.monotonic())
            publish(sample)
            if sample['shutdown_in_seconds'] == 0:
                print('Critical battery confirmed; requesting clean power-off.', flush=True)
                try:
                    subprocess.run(['/sbin/poweroff'], check=True, timeout=20)
                except (OSError, subprocess.SubprocessError) as error:
                    print('Power-off failed:', error, flush=True)
                # A failed shutdown gets another grace period, rather than a tight loop.
                policy.critical_since = None
        time.sleep(5)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--once', action='store_true', help='Print live readings without starting protection')
    args = parser.parse_args()
    if args.once:
        print(json.dumps(snapshot(), indent=2))
    else:
        monitor()
