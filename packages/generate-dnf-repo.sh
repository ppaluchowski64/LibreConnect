#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

assets_dir="${1:-release-assets}"
site_dir="${2:-site}"
repo_dir="$site_dir/packages/dnf"

require_command createrepo_c
require_command rpm
install_linux_gpg_key

clean_dir "$repo_dir"
mkdir -p "$repo_dir/x86_64" "$repo_dir/aarch64"

cp -a "$(first_match "$assets_dir" '*x86_64.rpm')" "$repo_dir/x86_64/"
cp -a "$(first_match "$assets_dir" '*aarch64.rpm')" "$repo_dir/aarch64/"
export_public_gpg_key "$repo_dir/RPM-GPG-KEY-libreconnect"

passphrase_file="$repo_dir/.rpm-passphrase"
printf '%s' "$PACKAGE_GPG_PASSPHRASE" > "$passphrase_file"
chmod 600 "$passphrase_file"

cat > "$repo_dir/.rpmmacros" <<EOF_MACROS
%_signature gpg
%_gpg_name $PACKAGE_GPG_KEY_ID
%__gpg /usr/bin/gpg
%__gpg_sign_cmd %{__gpg} gpg --batch --yes --pinentry-mode loopback --passphrase-file $passphrase_file --no-verbose --no-armor --local-user "%{_gpg_name}" --detach-sign --output %{__signature_filename} %{__plaintext_filename}
EOF_MACROS

HOME="$repo_dir" rpm --addsign "$repo_dir"/x86_64/*.rpm "$repo_dir"/aarch64/*.rpm
rm -f "$passphrase_file" "$repo_dir/.rpmmacros"

for arch in x86_64 aarch64; do
    createrepo_c --database --unique-md-filenames "$repo_dir/$arch"
    sign_detached_ascii "$repo_dir/$arch/repodata/repomd.xml" "$repo_dir/$arch/repodata/repomd.xml.asc"

    cat > "$repo_dir/libreconnect-$arch.repo" <<EOF_REPO
[libreconnect]
name=LibreConnect
baseurl=$REPO_URL/packages/dnf/$arch
enabled=1
type=rpm-md
gpgcheck=1
repo_gpgcheck=1
gpgkey=$REPO_URL/packages/dnf/RPM-GPG-KEY-libreconnect
metadata_expire=6h
skip_if_unavailable=False
EOF_REPO
done
