"""APT source files and per-repository keys; no separate repository database."""
from contextlib import contextmanager
import fcntl
import os
from pathlib import Path
import re
import tempfile
from urllib.parse import urlsplit

SOURCES = Path('/etc/apt/sources.list.d')
MAIN = Path('/etc/apt/sources.list')
KEYS = Path('/etc/apt/keyrings')
STATE = Path('/var/lib/reader')


def files():
    return sorted(p for p in SOURCES.iterdir() if p.suffix in ('.sources', '.list') and p.is_file())


def source_path(identity):
    if identity == '@main':
        return MAIN
    if not isinstance(identity, str) or not re.fullmatch(r'[a-zA-Z0-9_.-]+\.(sources|list)', identity):
        raise ValueError('Choose an APT source file.')
    return SOURCES / identity


def enabled(content, suffix):
    if suffix == '.list':
        return bool(re.search(r'^\s*deb(?:-src)?\s', content, re.M))
    return any(re.search(r'^URIs:', block, re.M | re.I) and not re.search(r'^Enabled:\s*no\s*$', block, re.M | re.I)
               for block in re.split(r'\n\s*\n', content))


def inventory():
    paths = ([MAIN] if MAIN.is_file() else []) + files()
    return [dict(id='@main' if p == MAIN else p.name, path=str(p), content=p.read_text(),
                 apps=p.name.startswith('reread') and p != MAIN,
                 enabled=enabled(p.read_text(), p.suffix)) for p in paths]


@contextmanager
def app_sources():
    # APT still reads its normal files. Limit catalog work to app sources so
    # browsing does not load every package in the distribution on this Atom.
    with tempfile.TemporaryDirectory(prefix='reread-sources-') as temporary:
        for path in files():
            if path.name.startswith('reread'):
                (Path(temporary) / path.name).symlink_to(path)
        yield {'Dir::Etc::sourcelist': '-', 'Dir::Etc::sourceparts': temporary}


def write(path, content, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as dest:
        temporary = Path(dest.name)
        try:
            os.fchmod(dest.fileno(), mode)
            dest.write(content)
            dest.flush()
            os.fsync(dest.fileno())
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)


def public_key(key):
    if not (key.startswith('-----BEGIN PGP PUBLIC KEY BLOCK-----') and key.endswith('-----END PGP PUBLIC KEY BLOCK-----')):
        raise ValueError('Paste an ASCII-armored public signing key.')


def validate(path, content):
    # Parse sources directly through libapt; do not load the distribution's
    # package cache just to validate an edit on this small Atom. Changes are
    # serialized by package.lock, including these process-local APT options.
    import apt_pkg
    if not apt_pkg.config.find('Dir'):
        apt_pkg.init()
    with tempfile.TemporaryDirectory(prefix='reread-source-check-') as temporary:
        directory = Path(temporary)
        for other in files():
            if other != path:
                (directory / other.name).symlink_to(other)
        replacement = directory / path.name
        replacement.write_text(content)
        main = str(MAIN) if MAIN.exists() and path != MAIN else '-'
        if path == MAIN:
            main = str(replacement)
            parts = str(SOURCES)
        else:
            parts = temporary
        options = {'Dir::Etc::sourcelist': main, 'Dir::Etc::sourceparts': parts}
        previous = {key: apt_pkg.config.find(key) if apt_pkg.config.exists(key) else None for key in options}
        try:
            for key, value in options.items():
                apt_pkg.config.set(key, value)
            if not apt_pkg.SourceList().read_main_list():
                raise ValueError('APT could not read that repository.')
        except apt_pkg.Error as error:
            raise ValueError(str(error)) from error
        finally:
            for key, value in previous.items():
                if value is None:
                    apt_pkg.config.clear(key)
                else:
                    apt_pkg.config.set(key, value)


def change(data):
    STATE.mkdir(parents=True, exist_ok=True)
    with (STATE / 'package.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError('Wait for the current package operation to finish.') from None
        operation = data.get('action')
        if operation == 'add':
            identity = data.get('name', '')
            if not isinstance(identity, str) or not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,47}', identity):
                raise ValueError('Repository name: lowercase letters, numbers and hyphens.')
            path = SOURCES / ('reread-' + identity + '.sources')
            if path.exists():
                raise ValueError('A repository with that name already exists.')
            uri, suite, components = (str(data.get(field, '')).strip() for field in ('uri', 'suite', 'components'))
            parsed = urlsplit(uri)
            if parsed.scheme not in ('https', 'http') or not parsed.netloc or any(c.isspace() for c in uri):
                raise ValueError('Enter an HTTP or HTTPS repository URL.')
            if not suite or any('\n' in field or '\r' in field for field in (suite, components)):
                raise ValueError('Enter a suite, for example stable.')
            key = data.get('key', '').strip()
            key_path = str(KEYS / ('reread-' + identity + '.asc')) if key else data.get('signed_by', '').strip()
            if not key_path.startswith('/') or any(c.isspace() for c in key_path):
                raise ValueError('Paste the public signing key or specify its absolute keyring path.')
            if key:
                public_key(key)
            elif not Path(key_path).is_file():
                raise ValueError('That keyring path does not exist on the Reader.')
            content = (f'Types: deb\nURIs: {uri}\nSuites: {suite}\n' +
                       (f'Components: {components}\n' if components else '') +
                       f'Architectures: i386\nSigned-By: {key_path}\n')
            validate(path, content)
            if key:
                write(Path(key_path), key + '\n')
            write(path, content)
        else:
            path = source_path(data.get('id'))
            if not path.is_file():
                raise ValueError('Repository no longer exists. Refresh the page.')
            if operation == 'remove':
                path.unlink()
            elif operation in ('save', 'toggle'):
                content = data.get('content', '') if operation == 'save' else path.read_text()
                if not isinstance(content, str) or len(content.encode()) > 65536:
                    raise ValueError('Source file must be at most 64 KiB.')
                if operation == 'toggle':
                    on = bool(data.get('enabled'))
                    if path.suffix == '.list':
                        if on:
                            content = re.sub(r'^(\s*)#\s*(deb(?:-src)?\s)', r'\1\2', content, flags=re.M)
                        else:
                            content = re.sub(r'^(\s*)(deb(?:-src)?\s)', r'\1# \2', content, flags=re.M)
                    else:
                        blocks = []
                        for block in re.split(r'\n\s*\n', content.strip()):
                            if re.search(r'^URIs:', block, re.M | re.I):
                                block = re.sub(r'^Enabled:.*\n?', '', block, flags=re.M | re.I)
                                block = ('Enabled: yes\n' if on else 'Enabled: no\n') + block
                            blocks.append(block)
                        content = '\n\n'.join(blocks) + '\n'
                key = data.get('key', '').strip() if operation == 'save' else ''
                key_path = None
                if key:
                    if not path.name.startswith('reread') or path.suffix != '.sources':
                        raise ValueError('Use Signed-By to select a keyring for system sources.')
                    public_key(key)
                    key_path = KEYS / (path.stem + '.asc')
                    if re.search(r'^Signed-By:', content, re.M | re.I):
                        content = re.sub(r'^Signed-By:.*$', 'Signed-By: ' + str(key_path), content, flags=re.M | re.I)
                    else:
                        content = 'Signed-By: ' + str(key_path) + '\n' + content
                validate(path, content)
                if key_path:
                    write(key_path, key + '\n')
                write(path, content)
            else:
                raise ValueError('Unknown repository action.')
        (STATE / 'package-catalog.json').unlink(missing_ok=True)
