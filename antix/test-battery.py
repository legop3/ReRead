"""Check UPower-to-UI translation; no battery policy or power-off commands."""
from unittest.mock import patch
import battery


class Interface:
    def __init__(self, properties):
        self.properties = properties
    def GetAll(self, _):
        return self.properties
    def GetDisplayDevice(self):
        return 'display'
    def EnumerateDevices(self):
        return ['battery']


class Bus:
    def __init__(self, external=False, present=True, state=2, warning=1):
        self.properties = {
            battery.ROOT: dict(OnBattery=not external),
            'display': dict(IsPresent=present, State=state, Percentage=42.5,
                            WarningLevel=warning, Energy=11.2, EnergyFull=26.2,
                            EnergyRate=3.5, TimeToEmpty=7200),
            'battery': dict(Type=2, PowerSupply=True, Voltage=8.1),
        }
    def get_object(self, service, path):
        assert service == battery.SERVICE
        return Interface(self.properties[path])


def main():
    with patch.object(battery.dbus, 'Interface', side_effect=lambda obj, _: obj):
        sample = battery.snapshot(Bus())
        assert sample['capacity_percent'] == 42 and sample['external_power'] is False
        assert sample['voltage_uv'] == 8_100_000 and sample['energy_wh'] == 11.2
        sample = battery.snapshot(Bus(external=True))
        assert sample['status'] == 'Not charging' and sample['raw_status'] == 'Discharging'
        sample = battery.snapshot(Bus(external=True, state=1))
        assert sample['status'] == 'Charging'
        sample = battery.snapshot(Bus(external=True, warning=5))
        # A presentation quirk must never suppress UPower's own action decision.
        assert sample['action_pending'] and sample['level'] == 'critical'
        assert sample['shutdown_in_seconds'] is None
        assert 'shutting down soon' in battery.description(sample)
        sample = battery.snapshot(Bus(present=False))
        assert sample['capacity_percent'] is None and sample['status'] == 'No battery'
    with patch.object(battery, 'snapshot', side_effect=battery.dbus.DBusException('offline')):
        assert battery.status()['monitor_running'] is False
    print('PASS: UPower units, AC presentation quirk, action warnings, absent battery and unavailable service')


if __name__ == '__main__':
    main()
