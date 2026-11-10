"""App-owned JSON configuration with packaged JSON Schema and atomic edits."""
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
from jsonschema import Draft7Validator
from jsonschema.exceptions import SchemaError

CONFIG = Path('/etc/reread/apps')
SCHEMAS = Path('/usr/share/reread/apps')


def paths(package, apps):
    app = next((app for app in apps if app['package'] == package), None)
    if not app or not app.get('settings') or not re.fullmatch(r'reread-app-[a-z0-9][a-z0-9+.-]*', package):
        raise ValueError('This installed app does not provide web settings.')
    identity = package.removeprefix('reread-app-')
    return CONFIG / (identity + '.json'), SCHEMAS / identity / 'settings.schema.json'


def load(package, apps):
    config, schema_path = paths(package, apps)
    schema = json.loads(schema_path.read_text())
    try:
        Draft7Validator.check_schema(schema)
    except SchemaError as error:
        raise ValueError('Invalid packaged settings schema: ' + error.message) from error
    # Schemas and the browser form are offline; references must stay in this document.
    def local_refs(value):
        if isinstance(value, dict):
            if '$ref' in value and not value['$ref'].startswith('#'):
                raise ValueError('Settings schema references must be local.')
            for item in value.values():
                local_refs(item)
        elif isinstance(value, list):
            for item in value:
                local_refs(item)
    local_refs(schema)
    raw = config.read_bytes()
    return dict(package=package, path=str(config), schema=schema,
                value=json.loads(raw), revision=hashlib.sha256(raw).hexdigest())


def save(data, apps):
    current = load(data.get('package'), apps)
    if data.get('revision') != current['revision']:
        raise ValueError('Settings changed since you opened them. Reload before saving.')
    value = data.get('value')
    errors = sorted(Draft7Validator(current['schema']).iter_errors(value), key=lambda error: str(error.path))
    if errors:
        error = errors[0]
        raise ValueError('/'.join(map(str, error.path)) + ': ' + error.message)
    config = Path(current['path'])
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', dir=config.parent, delete=False) as dest:
            temporary = Path(dest.name)
            json.dump(value, dest, ensure_ascii=False, indent=2, allow_nan=False)
            dest.write('\n')
            dest.flush()
            os.fsync(dest.fileno())
        os.replace(temporary, config)
    finally:
        if temporary:
            temporary.unlink(missing_ok=True)
    return load(data['package'], apps)
