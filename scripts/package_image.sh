#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
version=$(sed -n 's/^#define INSTRUMENT_VERSION "\([^"]*\)"/\1/p' include/app.h)
label=${1:-v$version-$(date +%Y%m%d)}
case "$label" in *[!A-Za-z0-9._-]*|''|.*) echo 'Invalid version directory' >&2; exit 2;; esac
case "$label" in v"$version"-*) ;; *) echo 'Directory must match INSTRUMENT_VERSION' >&2; exit 2;; esac
package=image/$label
[ ! -e "$package" ] || { echo "Version already exists: $package" >&2; exit 2; }
make -B all mux-adapter
mkdir -p "$package/sounds" "$package/deploy"
cp instrument_app luckfox_mux_adapter.so config.example.json CHANGELOG.md "$package/"
cp sounds/*.wav "$package/sounds/"
cp deploy/RkLunch.sh deploy/S80instrument deploy/README.md "$package/deploy/"
printf '%s\n' "$version" > "$package/VERSION"
printf 'version=%s\nrelease_tag=instrument-%s\nsource_base_commit=%s\n' "$version" "$label" "$(git rev-parse HEAD)" > "$package/BUILD.txt"
printf 'Built in WSL2, ARM/uClibc. Source: git tag instrument-%s.\nDeployment: deploy/README.md. Existing config and pending files must be retained.\n' "$label" > "$package/README.txt"
(cd "$package"; find . -type f ! -name SHA256SUMS -print | LC_ALL=C sort | xargs sha256sum > SHA256SUMS; sha256sum -c SHA256SUMS)
echo "Packaged: $package"
