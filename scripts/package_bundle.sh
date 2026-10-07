#!/bin/bash
# New layout plus read-only compatibility with historical image/<label>.
set -euo pipefail
cd "$(dirname "$0")/.."
version=$(sed -n 's/^#define INSTRUMENT_VERSION "\([^"]*\)"/\1/p' include/app.h)
label=$version
if [ "$#" -gt 0 ]; then label=$1; fi
case "$label" in ''|.*|*[!A-Za-z0-9._-]*) echo 'Invalid release label' >&2; exit 2;; esac
if [ -d "releases/$label/payload" ]; then
    release=releases/$label/payload
    output=releases/$label
    name=instrument-v$label
    [ ! -e "$output/$name.tar.gz" ] || { echo 'Bundle already exists' >&2; exit 2; }
    (cd "$release"; sh deploy/install_bundle.sh --check)
    tar --sort=name --transform="s,^payload,$name," -cf - -C "$output" payload | gzip -n > "$output/$name.tar.gz"
    (cd "$output"; sha256sum "$name.tar.gz" BUILD.txt VERSION.md > SHA256SUMS)
    echo "Bundle: $output/$name.tar.gz"
    exit 0
fi
# Compatibility when explicitly wrapping an existing historical release.
release=image/$label
[ -d "$release" ] || { echo "Missing release: $label" >&2; exit 2; }
(cd "$release"; sha256sum -c SHA256SUMS)
bundle_label=$label-bundle1
if [ "$#" -gt 1 ]; then bundle_label=$2; fi
case "$bundle_label" in ''|.*|*[!A-Za-z0-9._-]*) echo 'Invalid bundle label' >&2; exit 2;; esac
output=bundles/$bundle_label
[ ! -e "$output" ] || { echo "Bundle already exists: $output" >&2; exit 2; }
name=instrument-$bundle_label
mkdir -p "$output"
cp -a "$release" "$output/$name"
cp deploy/install_bundle.sh deploy/button_inputs.sh deploy/S79buttons deploy/wpa_supplicant.default.conf "$output/$name/deploy/"
cp "$release/SHA256SUMS" "$output/$name/SOURCE_SHA256SUMS"
printf 'bundle_source_release=%s\nbundle_tool_commit=%s\n' "$label" "$(git rev-parse HEAD)" > "$output/$name/BUNDLE.txt"
(cd "$output/$name"; find . -type f ! -name SHA256SUMS -print | LC_ALL=C sort | xargs sha256sum > SHA256SUMS; sh deploy/install_bundle.sh --check)
tar --sort=name -cf - -C "$output" "$name" | gzip -n > "$output/$name.tar.gz"
(cd "$output"; sha256sum "$name.tar.gz" > SHA256SUMS)
echo "Bundle: $output/$name.tar.gz"
