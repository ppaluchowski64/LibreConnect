#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

assets_dir="${1:-release-assets}"
site_dir="${2:-site}"
repo_dir="$site_dir/pacman"
repo_name="libreconnect"

require_command repo-add
require_command bsdtar
require_command zstd
install_linux_gpg_key

validate_pacman_package() {
    local package_file="$1"

    if ! zstd --test --quiet "$package_file"; then
        echo "Invalid zstd-compressed package archive: $package_file" >&2
        exit 1
    fi

    if ! bsdtar -tf "$package_file" .PKGINFO >/dev/null; then
        echo "Invalid Pacman package: $package_file does not contain .PKGINFO" >&2
        exit 1
    fi
}

clean_dir "$repo_dir"
mkdir -p "$repo_dir/x86_64" "$repo_dir/aarch64"

cp -a "$(first_match "$assets_dir" '*x86_64.pkg.tar.zst')" "$repo_dir/x86_64/"
cp -a "$(first_match "$assets_dir" '*aarch64.pkg.tar.zst')" "$repo_dir/aarch64/"
export_public_gpg_key "$repo_dir/libreconnect-packages.gpg"

for arch in x86_64 aarch64; do
    arch_dir="$repo_dir/$arch"

    for package_file in "$arch_dir"/*.pkg.tar.zst; do
        validate_pacman_package "$package_file"
        sign_detached_binary "$package_file" "$package_file.sig"
    done

    repo-add "$arch_dir/$repo_name.db.tar.gz" "$arch_dir"/*.pkg.tar.zst
    sign_detached_binary "$arch_dir/$repo_name.db.tar.gz" "$arch_dir/$repo_name.db.tar.gz.sig"
    cp -a "$arch_dir/$repo_name.db.tar.gz.sig" "$arch_dir/$repo_name.db.sig"
    
    if [[ -f "$arch_dir/$repo_name.files.tar.gz" ]]; then
        sign_detached_binary "$arch_dir/$repo_name.files.tar.gz" "$arch_dir/$repo_name.files.tar.gz.sig"
        cp -a "$arch_dir/$repo_name.files.tar.gz.sig" "$arch_dir/$repo_name.files.sig"
    fi

    cat > "$repo_dir/libreconnect-$arch.conf" <<EOF_CONF
[libreconnect]
SigLevel = Required DatabaseRequired
Server = $REPO_URL/pacman/$arch
EOF_CONF
done
