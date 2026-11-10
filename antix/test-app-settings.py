"""Real schema validation and atomic config edits in a disposable directory."""
import json
from pathlib import Path
import tempfile
import app_settings as settings


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    settings.CONFIG = root / 'config'; settings.CONFIG.mkdir()
    settings.SCHEMAS = root / 'schemas'
    schema_dir = settings.SCHEMAS / 'booru'; schema_dir.mkdir(parents=True)
    source = Path(__file__).parent / 'apps/booru'
    (schema_dir / 'settings.schema.json').write_bytes((source / 'settings.schema.json').read_bytes())
    config = settings.CONFIG / 'booru.json'
    config.write_bytes((source / 'config.example.json').read_bytes())
    apps = [dict(package='reread-app-booru', settings=True)]
    original = settings.load('reread-app-booru', apps)
    value = original['value']; value['hosts'][0]['searches'].append('bird rating:s')
    value['hosts'][0]['api_key'] = 'test-only-key'
    saved = settings.save(dict(original, value=value), apps)
    assert json.loads(config.read_text()) == value
    assert saved['revision'] != original['revision']
    before = config.read_bytes()
    try:
        settings.save(original, apps)
        raise AssertionError('Stale write accepted')
    except ValueError as error:
        assert 'changed' in str(error)
    invalid = dict(saved, value=dict(value, hosts=[]))
    try:
        settings.save(invalid, apps)
        raise AssertionError('Invalid host list accepted')
    except ValueError:
        pass
    assert config.read_bytes() == before
    try:
        settings.load('reread-app-booru', [])
        raise AssertionError('Uninstalled app exposed')
    except ValueError:
        pass
    assert not list(settings.CONFIG.glob('tmp*'))
print('PASS: schema form configuration, persistence, stale/invalid write protection and installed-app discovery')
