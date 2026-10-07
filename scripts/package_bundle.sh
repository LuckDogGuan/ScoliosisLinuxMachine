#!/bin/bash
# Finalize a staged payload into the sole formal archive; never rebuild old bundles.
set -euo pipefail
cd "$(dirname "$0")/.."
version=$(sed -n 's/^#define INSTRUMENT_VERSION "\([^"]*\)"/\1/p' include/app.h)
label=${1:-$version}
case "$label" in ''|.*|*[!A-Za-z0-9._-]*) echo 'Invalid release label' >&2; exit 2;; esac
output=releases/$label
release=$output/payload
name=instrument-v$label
archive=$output/$name.tar.gz
if [ -f "$archive" ]; then
    [ ! -d "$release" ] || { echo 'Archive and payload both exist; review before cleanup' >&2; exit 2; }
    (cd "$output"; sha256sum -c SHA256SUMS)
    echo "Verified existing bundle: $archive"
    exit 0
fi
[ -d "$release" ] || { echo "Missing staged release: $label" >&2; exit 2; }
(cd "$release"; sh deploy/install_bundle.sh --check)
tar --sort=name --transform="s,^payload,$name," -cf - -C "$output" payload | gzip -n > "$archive.tmp"
gzip -t "$archive.tmp"
tar --compare -zf "$archive.tmp" --transform="s,^$name,payload," -C "$output"
mv -- "$archive.tmp" "$archive"
(cd "$output"; sha256sum "$name.tar.gz" BUILD.txt VERSION.md > SHA256SUMS; sha256sum -c SHA256SUMS)
# Delete one explicit staged file at a time only after archive equality is proven.
find "$release" -type f -print > "$output/.payload-files"
while IFS= read -r file; do rm -- "$file"; done < "$output/.payload-files"
rm -- "$output/.payload-files"
find "$release" -depth -type d -print > "$output/.payload-dirs"
while IFS= read -r directory; do rmdir -- "$directory"; done < "$output/.payload-dirs"
rm -- "$output/.payload-dirs"
echo "Bundle: $archive"
