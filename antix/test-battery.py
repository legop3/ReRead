"""Battery fixtures and shutdown/cancellation paths; never powers off hardware."""
import contextlib
import io
from pathlib import Path
import tempfile
from unittest.mock import patch
import battery


def sample(percent=50, external=False, present=True):
    return dict(present=present, external_power=external, capacity_percent=percent,
                status='Discharging', raw_status='Discharging')


def daemon_check(final_external):
    # Second sample expires the grace period; third sample is the live recheck.
    readings = [sample(5), sample(5), sample(5, external=final_external)]
    with patch.object(battery, 'settings', return_value=battery.DEFAULTS), \
            patch.object(battery, 'snapshot', side_effect=readings), \
            patch.object(battery, 'publish') as publish, \
            patch.object(battery.time, 'monotonic', side_effect=[0, 31, 31]), \
            patch.object(battery.time, 'sleep', side_effect=[None, StopIteration]), \
            patch.object(battery.subprocess, 'run') as shutdown, \
            contextlib.redirect_stdout(io.StringIO()):
        try:
            battery.monitor()
        except StopIteration:
            pass
        if final_external:
            shutdown.assert_not_called()
            assert publish.call_args.args[0]['shutdown_in_seconds'] is None
        else:
            shutdown.assert_called_once_with(['/sbin/poweroff'], check=True, timeout=20)


def main():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name, values in {
            'adapter': dict(type='Mains', online='1'),
            'pack': dict(type='Battery', present='1', capacity='97', status='Discharging',
                         voltage_now='8307000', charge_now='3449000', charge_full='3541000'),
        }.items():
            (root / name).mkdir()
            for key, value in values.items():
                (root / name / key).write_text(value)
        s = battery.snapshot(root)
        assert s['status'] == 'Not charging' and s['raw_status'] == 'Discharging'
        assert s['capacity_percent'] == 97 and s['current_ua'] is None
        (root / 'pack/status').write_text('Charging')
        assert battery.snapshot(root)['status'] == 'Charging'
        (root / 'adapter/online').write_text('0')
        assert battery.snapshot(root)['external_power'] is False
        (root / 'pack/capacity').unlink()
        assert battery.snapshot(root)['capacity_percent'] == 97
        (root / 'pack/charge_now').unlink()
        assert battery.snapshot(root)['capacity_percent'] is None
        (root / 'pack/present').write_text('0')
        assert battery.snapshot(root)['present'] is False
        (root / 'pack/present').unlink()
        assert battery.snapshot(root)['present'] is None
        (root / 'adapter/online').unlink()
        assert battery.snapshot(root)['external_power'] is None
    policy = battery.Policy(battery.DEFAULTS)
    assert policy.update(sample(12), 0)['level'] == 'low'
    assert policy.update(sample(6), 0)['shutdown_in_seconds'] == 30
    assert policy.update(sample(6), 29)['shutdown_in_seconds'] == 1
    assert policy.update(sample(6, external=True), 30)['shutdown_in_seconds'] is None
    assert policy.update(sample(6), 31)['shutdown_in_seconds'] == 30
    for uncertain in [sample(None), sample(5, external=None), sample(5, present=None), sample(5, present=False)]:
        assert policy.update(uncertain, 100)['shutdown_in_seconds'] is None
    assert policy.update(sample(6), 101)['shutdown_in_seconds'] == 30
    assert policy.update(sample(8), 110)['shutdown_in_seconds'] is None
    daemon_check(final_external=False)
    daemon_check(final_external=True)
    print('PASS: real-reading fixtures, unknown/absent cases, critical delay, clean-shutdown command and last-second AC cancellation.')


if __name__ == '__main__':
    main()
