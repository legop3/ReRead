#!/usr/bin/python3
"""One-time migration of ReRead battery thresholds into UPower's configuration."""
import configparser
import json
import os
from pathlib import Path
import re
import signal
import dbus

marker = Path('/var/lib/reader/upower-configured')
if not marker.exists():
    path = Path('/etc/UPower/UPower.conf')
    text = path.read_text()
    config = configparser.ConfigParser()
    config.read_string(text)
    section = config['UPower']
    values = {}
    # Keep any already-customised UPower policy. Migrate the old defaults once.
    if [section.getfloat(k) for k in ('PercentageLow', 'PercentageCritical', 'PercentageAction')] == [20, 5, 2]:
        old = Path('/etc/reader/power.json')
        previous = json.loads(old.read_text()) if old.exists() else {}
        low, action = previous.get('warning_percent', 12), previous.get('critical_percent', 6)
        if not (isinstance(low, int) and isinstance(action, int) and 0 < action < low - 1 <= 99):
            low, action = 12, 6
        values.update(UsePercentageForPolicy='true', PercentageLow=str(low),
                      PercentageCritical=str(action + 1), PercentageAction=str(action))
    if section.get('CriticalPowerAction') == 'HybridSleep':
        values['CriticalPowerAction'] = 'PowerOff'
    for key, value in values.items():
        text = re.sub(r'^' + key + r'\s*=.*$', key + '=' + value, text, flags=re.M)
    if values:
        temporary = path.with_suffix('.conf.reread')
        temporary.write_text(text)
        temporary.replace(path)
    marker.parent.mkdir(parents=True, exist_ok=True)
    if values and Path('/run/dbus/system_bus_socket').exists():
        bus = dbus.SystemBus()
        try:
            daemon = dbus.Interface(bus.get_object('org.freedesktop.DBus', '/org/freedesktop/DBus'),
                                    'org.freedesktop.DBus')
            pid = daemon.GetConnectionUnixProcessID('org.freedesktop.UPower')
        except dbus.DBusException:
            pass  # D-Bus will activate the daemon with the new configuration.
        else:
            os.kill(pid, signal.SIGTERM)
    marker.write_text('UPower owns battery policy; configure /etc/UPower/UPower.conf\n')
