# ReRead APT repository

Stable platform and application packages are published at
**https://legop3.github.io/ReRead/apt/**. This is a normal signed Debian archive:
APT handles versions, dependencies, downloads and installation. antiX remains
the OS and supplies system packages. There is one stable channel.

The source, workflow, package releases and Pages site all belong to
[`legop3/ReRead`](https://github.com/legop3/ReRead). GitHub Releases stores the
published `.deb` assets and installer images. Pages serves a copy of the small
packages plus APT metadata; installer images stay in Releases.

## On the Reader

ReRead 0.11.0 and later include the public key and source automatically. For an
older installation, copy `reread-archive-keyring.asc` to
`/usr/share/keyrings/reread-archive-keyring.asc` and `reread.sources` to
`/etc/apt/sources.list.d/reread.sources` first. The archive key fingerprint is:

```text
9D9B DFF2 37B9 924D C742 15FA EDC9 F0CE 71BB 68FB
```

Then, as root:

```sh
apt-get update
apt-cache policy reread reread-app-camera
elogind-inhibit --what=idle --mode=block --why='ReRead update' \
  apt-get -y -o Dpkg::Options::=--force-confold -o Dpkg::Options::=--force-confdef \
  --only-upgrade install reread reread-app-camera
```

Install an available app with `apt-get install reread-app-NAME`; remove it with
`apt-get remove reread-app-NAME` to keep its data and configuration. These are
in-place package operations. No reflash is needed. The existing web upload
pages still work. Starting with 0.12.0, use **Updates → Check for updates** for
platform releases, or **Apps → Check for apps and updates** to browse available
apps and update installed ones. The Reader needs Wi-Fi/internet access; USB
alone provides the management connection. Nothing checks, downloads or installs
updates automatically in the background.

The check refreshes only the ReRead archive, retaining other APT lists for
dependency resolution. Python APT reads trusted stable candidates and normal
Debian version ordering; `apt-get` installs the selected version and resolves its
dependencies. This does not run a distribution-wide upgrade. Package jobs retain
their log/status across management-server restarts and inhibit automatic idle sleep.

## Publishing

Use the shared [release tool](../RELEASING.md), from the repository root:

```sh
./release.py app images --publish    # Build and publish just Images
./release.py platform --publish      # Build and publish platform + current installer
```

Bump the relevant `VERSION`, validate the behavior and commit changes first.
Publishing pushes that exact commit, uploads a draft, verifies asset digests,
publishes it and waits for the public signed APT/Pages verification. Normal
builds omit `--publish` and do not push or upload anything. GitHub authentication
stays on the development computer; apps and build containers never need it.

The archive workflow collects stable release `.deb` assets and indexes/signs them
using `apt-ftparchive` and GnuPG. Drafts and prereleases are excluded. Existing
published package/version bytes are immutable. Platform installers reuse published
default app packages rather than rebuilding their versions.

The permanent [installation page](https://legop3.github.io/ReRead/install/) and
its `latest.json` are generated during deployment from the newest stable release
containing an installer image and checksum. An app release cannot replace this
selection. Installer images remain in Releases, outside the Pages storage budget.
Pages allows `main` and all tags (`**`); no per-app deployment rules are needed.

Retain stable releases for installed devices. A failed archive build leaves the
previous site in place. The workflow can be dispatched manually to recover from
a deployment failure without rebuilding/reuploading packages. APT installation is
an ordinary package transaction, with no automatic rollback. The tested kernel
remains pinned.

## Signing key

The repository contains **only the public key**. The private signing key is
stored in the GitHub Actions secret `APT_SIGNING_KEY`. The owner's local GnuPG
backup is at `~/.local/share/reread/repository-signing/`; back up that directory
privately. It is outside this source checkout and is never installed on Readers.
Its loss would require a separately distributed trust-key update.

For local archive builds, install `apt-utils` and `gnupg`, provide a signing
keyring through `GNUPGHOME`, and run:

```sh
python3 repository/build.py path/to/packages --output build/repository/site \
  --key "$(cat repository/key-fingerprint)"
python3 repository/test.py build/repository/site
```

The output directory must be new. The integration check uses isolated APT lists
and a local HTTP server to check a real package download and rejection of a
tampered signed index; it does not install packages or change system APT sources.

## Additional repositories

Use **Repositories** in USB web management to add an app archive with its URL,
suite, components and ASCII-armored public signing key (or an existing keyring path).
The page can enable/disable, edit and remove sources. Its advanced section exposes
existing antiX/Debian source files too. Editing uses the actual `.sources`/`.list`
text; APT parses proposed changes before the old file is replaced. App sources can
also replace their public key through the editor. Changes wait for package jobs to
finish and clear the cached catalog; check for updates afterward.

App sources are ordinary files named `reread*.sources` or `reread*.list` under
`/etc/apt/sources.list.d/`. The add form writes `reread-<name>.sources`, native i386
architecture and an explicit `Signed-By` reference. Uploaded public keys live under
`/etc/apt/keyrings/`; removing a source leaves its unused key file for later reuse.
There is no separate repository registry or global `apt-key` trust import.

Software and the web catalog load trusted candidates from these app sources.
Packages still need the `reread` / `reread-app-*` identities. Repository origin,
host and suite are not hardcoded. Version selection follows APT preferences and
Debian version comparisons. Dependency installation uses all enabled system sources.
The catalog is intentionally limited to app-source metadata to keep it small on the
Reader; system repositories remain normal APT sources for Linux packages.
