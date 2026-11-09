#!/usr/bin/python3
"""UPower status adapter for ReRead's UI. UPower owns battery policy/actions."""
import argparse
import json
import os
from pathlib import Path
import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

SERVICE = 'org.freedesktop.UPower'
ROOT = '/org/freedesktop/UPower'
DEVICE = SERVICE + '.Device'
STATE = Path('/run/reader-battery.json')
TEXT = Path('/run/reader-battery-status')
STATES = {0: 'Unknown', 1: 'Charging', 2: 'Discharging', 3: 'Empty',
          4: 'Full', 5: 'Not charging', 6: 'Pending discharge'}


def unavailable():
    return dict(backend='UPower', present=None, external_power=None,
                capacity_percent=None, raw_status=None, status='Battery unavailable',
                voltage_uv=None, charge_now_uah=None, charge_full_uah=None,
                current_ua=None, level='unknown', action_pending=False,
                shutdown_in_seconds=None, monitor_running=False)


def snapshot(bus=None):
    bus = bus or dbus.SystemBus()
    root = bus.get_object(SERVICE, ROOT)
    power = dbus.Interface(root, 'org.freedesktop.DBus.Properties').GetAll(SERVICE)
    path = dbus.Interface(root, SERVICE).GetDisplayDevice()
    battery = dbus.Interface(bus.get_object(SERVICE, path),
                             'org.freedesktop.DBus.Properties').GetAll(DEVICE)
    voltage = None
    for device in dbus.Interface(root, SERVICE).EnumerateDevices():
        properties = dbus.Interface(bus.get_object(SERVICE, device),
                                    'org.freedesktop.DBus.Properties').GetAll(DEVICE)
        if int(properties['Type']) == 2 and properties['PowerSupply']:
            voltage = round(float(properties['Voltage']) * 1_000_000) or None
            break
    result = unavailable()
    present = bool(battery['IsPresent'])
    state = STATES.get(int(battery['State']), 'Unknown')
    external = not bool(power['OnBattery'])
    # Colbert can report Discharging while externally powered and not charging.
    # This is presentation only; never mask UPower's warning/action decisions.
    label = 'Not charging' if external and state == 'Discharging' else state
    warning = int(battery['WarningLevel'])
    result.update(present=present, external_power=external,
                  capacity_percent=round(float(battery['Percentage'])) if present else None,
                  raw_status=state, status=label if present else 'No battery', voltage_uv=voltage,
                  energy_wh=float(battery['Energy']), energy_full_wh=float(battery['EnergyFull']),
                  power_w=float(battery['EnergyRate']), time_to_empty_seconds=int(battery['TimeToEmpty']),
                  level='critical' if warning >= 4 else 'low' if warning == 3 else 'normal',
                  action_pending=warning == 5, monitor_running=True)
    return result


def status():
    try:
        return snapshot()
    except dbus.DBusException:
        return unavailable()


def description(sample):
    percent = sample['capacity_percent']
    text = 'Battery: ' + (str(percent) + '%' if percent is not None else sample['status'])
    if sample['level'] == 'critical':
        return text + (' CRITICAL - shutting down soon; connect power' if sample['action_pending']
                       else ' CRITICAL - connect power')
    if sample['level'] == 'low':
        return text + ' LOW - connect power'
    if sample['external_power'] is True:
        return text + ' - external power, ' + sample['status'].lower()
    return text + ' - on battery' if sample['external_power'] is False else text


def publish(sample):
    for path, value in ((STATE, json.dumps(sample) + '\n'), (TEXT, description(sample) + '\n')):
        temporary = path.with_suffix('.tmp')
        temporary.write_text(value)
        os.replace(temporary, path)


def monitor():
    DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    loop = GLib.MainLoop()

    def refresh(*_):
        try:
            publish(snapshot(bus))
        except dbus.DBusException:
            publish(unavailable())
            loop.quit()  # runit restarts the adapter; D-Bus activates UPower.

    def owner_changed(name, old, new):
        if old and not new:
            publish(unavailable())
            loop.quit()

    bus.add_signal_receiver(refresh, signal_name='PropertiesChanged',
                            dbus_interface='org.freedesktop.DBus.Properties',
                            bus_name=SERVICE)
    bus.add_signal_receiver(refresh, signal_name='DeviceAdded', bus_name=SERVICE)
    bus.add_signal_receiver(refresh, signal_name='DeviceRemoved', bus_name=SERVICE)
    bus.add_signal_receiver(owner_changed, signal_name='NameOwnerChanged',
                            dbus_interface='org.freedesktop.DBus', arg0=SERVICE)
    publish(snapshot(bus))
    loop.run()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--once', action='store_true', help='Print live UPower readings')
    args = parser.parse_args()
    if args.once:
        print(json.dumps(status(), indent=2))
    else:
        monitor()
