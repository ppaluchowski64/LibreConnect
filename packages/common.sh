#!/usr/bin/env bash

set -euo pipefail

PACKAGE_NAME="libreconnect"
DISPLAY_NAME="LibreConnect"
PROJECT_URL="https://github.com/ppaluchowski64/LibreConnect"
REPO_URL="https://libreconnect.one"
DESCRIPTION="Privacy-focused peer-to-peer device connectivity app"
LONG_DESCRIPTION="LibreConnect links desktop and mobile devices over local, encrypted peer-to-peer connections."
ANDROID_APP_ID="com.LibreConnect.mobile"
PACKAGE_GPG_KEY_ID="01E6938CCB71C239"

require_command() {
    local name="$1"

    if ! command -v "$name" >/dev/null 2>&1; then
        echo "Required command not found: $name" >&2
        exit 1
    fi
}

require_file() {
    local file="$1"

    if [[ ! -f "$file" ]]; then
        echo "Required file not found: $file" >&2
        exit 1
    fi
}

require_env() {
    local name="$1"

    if [[ -z "${!name:-}" ]]; then
        echo "Required environment variable is not set: $name" >&2
        exit 1
    fi
}

clean_dir() {
    local dir="$1"
    rm -rf "$dir"
    mkdir -p "$dir"
}

first_match() {
    local dir="$1"
    local pattern="$2"
    find "$dir" -maxdepth 1 -type f -name "$pattern" | sort -V | tail -n 1
}

sha256_file() {
    sha256sum "$1" | awk '{print $1}'
}

install_linux_gpg_key() {
    require_env PACKAGE_GPG_PRIVATE_KEY
    require_env PACKAGE_GPG_PASSPHRASE
    require_command gpg

    export GNUPGHOME="${GNUPGHOME:-$PWD/.gnupg}"
    mkdir -p "$GNUPGHOME"
    chmod 700 "$GNUPGHOME"

    printf '%s\n' "$PACKAGE_GPG_PRIVATE_KEY" | gpg --batch --import
    printf 'allow-loopback-pinentry\n' > "$GNUPGHOME/gpg-agent.conf"
    gpgconf --kill gpg-agent >/dev/null 2>&1 || true
}

sign_detached_ascii() {
    local input="$1"
    local output="${2:-$input.asc}"

    gpg --batch --yes --pinentry-mode loopback \
        --passphrase "$PACKAGE_GPG_PASSPHRASE" \
        --local-user "$PACKAGE_GPG_KEY_ID" \
        --armor --detach-sign --output "$output" "$input"
}

sign_clearsign() {
    local input="$1"
    local output="$2"
    
    gpg --batch --yes --pinentry-mode loopback \
        --passphrase "$PACKAGE_GPG_PASSPHRASE" \
        --local-user "$PACKAGE_GPG_KEY_ID" \
        --clearsign --digest-algo SHA512 --output "$output" "$input"
}

export_public_gpg_key() {
    local output="$1"
    gpg --batch --yes --armor --export "$PACKAGE_GPG_KEY_ID" > "$output"
}
