#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

assets_dir="${1:-release-assets}"
site_dir="${2:-site}"
repo_dir="$site_dir/apt"
suite="stable"
component="main"

require_command dpkg-deb
require_command apt-ftparchive
require_command gzip
require_command xz
install_linux_gpg_key

write_apt_package_record() {
    local package_file="$1"
    local filename="$2"

    dpkg-deb -f "$package_file"
    printf 'Filename: %s\n' "$filename"
    printf 'Size: %s\n' "$(stat -c '%s' "$package_file")"
    printf 'MD5sum: %s\n' "$(md5sum "$package_file" | awk '{print $1}')"
    printf 'SHA1: %s\n' "$(sha1sum "$package_file" | awk '{print $1}')"
    printf 'SHA256: %s\n' "$(sha256sum "$package_file" | awk '{print $1}')"
    printf '\n'
}

clean_dir "$repo_dir"
mkdir -p "$repo_dir/pool/main/l/$PACKAGE_NAME"
mkdir -p "$repo_dir/dists/$suite/$component/binary-amd64"
mkdir -p "$repo_dir/dists/$suite/$component/binary-arm64"

cp -a "$(first_match "$assets_dir" '*amd64.deb')" "$repo_dir/pool/main/l/$PACKAGE_NAME/"
cp -a "$(first_match "$assets_dir" '*arm64.deb')" "$repo_dir/pool/main/l/$PACKAGE_NAME/"

for arch in amd64 arm64; do
    packages_file="$repo_dir/dists/$suite/$component/binary-$arch/Packages"
    : > "$packages_file"

    while IFS= read -r package_file; do
        package_arch="$(dpkg-deb -f "$package_file" Architecture)"
        if [[ "$package_arch" == "$arch" ]]; then
            relative_package_file="${package_file#"$repo_dir"/}"
            write_apt_package_record "$package_file" "$relative_package_file" >> "$packages_file"
        fi
    done < <(find "$repo_dir/pool" -type f -name '*.deb' | sort -V)

    if [[ ! -s "$packages_file" ]]; then
        echo "No APT packages found for architecture: $arch" >&2
        exit 1
    fi
    
    gzip -9c "$packages_file" > "$packages_file.gz"
    xz -9ec "$packages_file" > "$packages_file.xz"
done

cat > "$repo_dir/apt-release.conf" <<EOF_CONF
APT::FTPArchive::Release::Origin "$DISPLAY_NAME";
APT::FTPArchive::Release::Label "$DISPLAY_NAME";
APT::FTPArchive::Release::Suite "$suite";
APT::FTPArchive::Release::Codename "$suite";
APT::FTPArchive::Release::Architectures "amd64 arm64";
APT::FTPArchive::Release::Components "$component";
APT::FTPArchive::Release::Description "$DISPLAY_NAME APT repository";
APT::FTPArchive::Release::NotAutomatic "no";
APT::FTPArchive::Release::ButAutomaticUpgrades "yes";
EOF_CONF

(
    cd "$repo_dir"
    apt-ftparchive -c apt-release.conf release "dists/$suite" > "dists/$suite/Release"
)

rm -f "$repo_dir/apt-release.conf"

sign_clearsign "$repo_dir/dists/$suite/Release" "$repo_dir/dists/$suite/InRelease"
sign_detached_ascii "$repo_dir/dists/$suite/Release" "$repo_dir/dists/$suite/Release.gpg"
export_public_gpg_key "$repo_dir/libreconnect-archive-keyring.asc"

cat > "$repo_dir/libreconnect.sources" <<EOF_SOURCES
Types: deb
URIs: $REPO_URL/apt
Suites: $suite
Components: $component
Architectures: amd64 arm64
Signed-By: /etc/apt/keyrings/libreconnect-archive-keyring.gpg
EOF_SOURCES
