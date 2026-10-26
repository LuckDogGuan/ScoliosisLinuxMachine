#!/bin/sh
# One formal version -> one immutable archive plus metadata under releases/<version>.
set -eu
cd "$(dirname "$0")/.."
version=$(sed -n 's/^#define INSTRUMENT_VERSION "\([^"]*\)"/\1/p' include/app.h)
label=$version
if [ "$#" -gt 0 ]; then label=$1; fi
[ "$label" = "$version" ] || { echo "Use formal version $version, not a dated label" >&2; exit 2; }
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo 'Commit reviewed source changes before packaging' >&2; exit 2; }
package=releases/$version/payload
[ ! -e "releases/$version" ] || { echo "Version already exists: releases/$version" >&2; exit 2; }
make -B all mux-adapter
mkdir -p "$package/sounds" "$package/deploy" "$package/docs"
cp instrument_app luckfox_mux_adapter.so config.example.json CHANGELOG.md "$package/"
cp sounds/*.wav sounds/stability-voice-manifest.json "$package/sounds/"
cp docs/light-test.md "$package/docs/"
cp deploy/TZ deploy/RkLunch.sh deploy/S80instrument deploy/README.md deploy/install_bundle.sh \
   deploy/wpa_supplicant.default.conf "$package/deploy/"
printf '%s\n' "$version" > "$package/VERSION"
printf 'version=%s\nrelease_tag=instrument-v%s\nsource_commit=%s\nbuild_utc=%s\n' \
  "$version" "$version" "$(git rev-parse HEAD)" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$package/BUILD.txt"
printf 'Complete ARM/uClibc application payload. Keep config, pairing, state and recordings.\nSee deploy/README.md and docs/light-test.md for light commands/acceptance.\n' > "$package/README.txt"
# Version notes come from this version's changelog, never a fixed old feature doc.
awk -v heading="# $version " '
    /^# / { if (keep) exit; if (index($0, heading) == 1) keep=1 }
    keep { print }
' CHANGELOG.md > "releases/$version/VERSION.md"
[ -s "releases/$version/VERSION.md" ] || { echo "Missing changelog entry for $version" >&2; exit 2; }
cp "$package/BUILD.txt" "releases/$version/BUILD.txt"
(cd "$package"; find . -type f ! -name SHA256SUMS -print | LC_ALL=C sort | xargs sha256sum > SHA256SUMS; cp SHA256SUMS SOURCE_SHA256SUMS; find . -type f ! -name SHA256SUMS -print | LC_ALL=C sort | xargs sha256sum > SHA256SUMS; sh deploy/install_bundle.sh --check)
bash scripts/package_bundle.sh "$version"
