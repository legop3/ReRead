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

1. Increase `antix/VERSION` for a platform change, or the app's own `VERSION`
   for an app change. Build using the normal [release process](../antix/README.md).
2. Commit and push the source. Create a **draft GitHub release** targeting that
   commit and attach the new `.deb` files. Only attach an app package when its
   version changes: different bytes under the same package/version are rejected.
   Installer images may be attached separately when rebuilt for that release.
   Pages deployment rules currently allow branch `main` and tags `v*` and
   `files-v*`. Before using another app's release tag pattern, add that pattern
   under **Settings → Environments → github-pages → Deployment branches and tags**;
   a release workflow deploys from its tag even though it checks out `main`.
3. Publish the release. The **Publish APT repository** Actions workflow collects
   `.deb` files from all published stable releases, generates indexes with
   `apt-ftparchive`, signs them with GnuPG, and deploys to Pages. Drafts and
   prereleases are excluded. Check the workflow completes before announcing it.

The workflow can also be run manually to rebuild the archive after release
asset changes. Retain published stable packages for existing installations.
Concurrent deployments are serialized. Build failure leaves the existing site
in place. Updating packages remains an ordinary APT transaction, with no
automatic rollback. The hardware-tested kernel dependency is still pinned.

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
