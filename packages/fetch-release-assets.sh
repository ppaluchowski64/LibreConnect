#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

release_tag="${1:-latest}"
output_dir="${2:-release-assets}"

require_command gh
require_command jq

clean_dir "$output_dir"

if [[ "$release_tag" == "latest" || -z "$release_tag" ]]; then
    release_tag="$(gh release view --json tagName --jq '.tagName')"
fi

echo "Using GitHub Release: $release_tag"
printf '%s\n' "$release_tag" > "$output_dir/.release-tag"

gh release download "$release_tag" \
    --pattern '*.apk' \
    --pattern '*.deb' \
    --pattern '*.rpm' \
    --pattern '*.pkg.tar.zst' \
    --dir "$output_dir" \
    --clobber

require_file "$(first_match "$output_dir" '*.apk')"
require_file "$(first_match "$output_dir" '*amd64.deb')"
require_file "$(first_match "$output_dir" '*arm64.deb')"
require_file "$(first_match "$output_dir" '*x86_64.rpm')"
require_file "$(first_match "$output_dir" '*aarch64.rpm')"
require_file "$(first_match "$output_dir" '*x86_64.pkg.tar.zst')"
require_file "$(first_match "$output_dir" '*aarch64.pkg.tar.zst')"
