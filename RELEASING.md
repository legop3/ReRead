# Building and releasing ReRead

Run everything from the repository root. `build.sh` still builds a complete local
image without publishing; `make-usb.sh` only flashes a local image. The shared
`release.py` coordinates maintained releases using Docker, Git, GitHub CLI and
the existing signed Debian archive. It adds no device-side updater or service.

## Maintainer commands

```sh
./release.py app images                  # Local app bundle
./release.py platform                    # Local platform + USB installer bundle
./release.py app images --publish        # Publish just the app
./release.py platform --publish          # Publish platform and installer together
```

A platform release uses `antix/VERSION`. An app release uses
`antix/apps/<id>/VERSION`. Bump the version for changed package contents.
Validate the change and commit it before publishing; publication requires a
clean `main` checkout, pushes its exact commit to `origin/main`, and targets that
full commit in the release. It does not create commits or invent version bumps.
Use `--notes-file path.md` for curated notes; otherwise GitHub generates them.

Host requirements: Python 3.11+, Docker/buildx, Git and authenticated `gh` with
repository write access. Repository identity is inferred from the GitHub origin;
`--repo OWNER/REPO` is available for forks. The tool creates/reuses the `reread`
Docker builder and uses the cached `reread-builder` image. For iterative development,
`--container intelreader-validator` reuses a running builder with this checkout
mounted at `/work`. These commands never open or flash host disks.

Platform builds need the [antiX ISO and extracted camera firmware](antix/README.md#build).
App builds do not. App-specific dependencies (for example Java ME's upstream JDK)
stay in the corresponding packager. `antix/apps/package.py` is the single app build
entry point; app folders do not implement GitHub release logic.

## What gets published

A platform bundle contains the new core `.deb`, the six default app `.deb` files,
the USB installer, its checksum, the image manifest, `release.json` and
`SHA256SUMS`. Default apps are listed once in `antix/apps/package.py`.
Published package versions are downloaded by asset ID and verified against
GitHub SHA-256 digests, then installed into the image unchanged. Missing versions
are built. This prevents rebuilt SDKs or timestamps from silently replacing an
already-published app version.

An app bundle contains only its `.deb`, source/asset metadata and checksums.
No kernel, stock firmware or installer is rebuilt. App releases are explicitly
not GitHub's overall Latest release; complete platform releases are.

Bundles are prepared in `out/releases/<tag>/`. `release.json` records the source
commit and hashes. Publication creates a draft, uploads assets, verifies GitHub's
reported hashes, publishes it and waits for that release's APT workflow to pass.
Existing published releases are never overwritten. Conflicting draft assets stop
publication rather than being deleted. Do not publish concurrently from the same
checkout, since the image and SDK builders share their staging directories.

The permanent installation URL is:

**https://legop3.github.io/ReRead/install/**

Its download links and `latest.json` are generated from completed public installer
releases during every APT/Pages deployment. App-only releases do not change the
selected installer. README links never need a version-number edit.

## Validation and recovery

The release tool checks source identity, package identities, installer metadata
and upload digests; it does not replace app behavior tests or hardware checks.
For a platform build, run the existing disposable VM installer test in the builder:

```sh
python3 /work/antix/test-installer.py
```

Run relevant app tests and hardware checks before publishing. To build first,
validate, then publish the exact prepared bytes without rebuilding:

```sh
./release.py platform
# Validate the generated installer and packages.
./release.py platform --skip-build --publish
```

`--skip-build` requires the same committed source and verifies the bundle hashes.
It can also resume an interrupted draft upload. If publication succeeded but APT
failed, inspect Actions and rerun `apt-repository.yml`; do not rebuild the same
package version. `gh workflow run apt-repository.yml` redeploys existing assets.
A failed upload leaves a draft. A failed Pages build leaves the previous live
archive intact. A published release awaiting deployment is reported as a failure,
not as a completed update.
