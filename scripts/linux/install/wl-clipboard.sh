#!/usr/bin/env bash

set -euo pipefail

run_as_root() {
    if [[ "$(id -u)" -eq 0 ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

SKIP_PACKAGE_INSTALL="${LIBRECONNECT_SKIP_PACKAGE_INSTALL:-0}"
if [[ "$SKIP_PACKAGE_INSTALL" == "1" ]]; then
    echo "Package installation disabled (LIBRECONNECT_SKIP_PACKAGE_INSTALL=1)."
    if command -v wl-copy >/dev/null 2>&1 && command -v wl-paste >/dev/null 2>&1; then
        echo "wl-copy and wl-paste are available."
        exit 0
    fi
    echo "wl-clipboard is not installed."
    exit 0
fi

install_debian() {
    run_as_root apt update
    run_as_root apt install -y wl-clipboard
}

install_fedora() {
    run_as_root dnf install -y wl-clipboard
}

install_arch() {
    run_as_root pacman -S --noconfirm wl-clipboard
}

install_opensuse() {
    run_as_root zypper install -y wl-clipboard
}

install_alpine() {
    run_as_root apk add wl-clipboard
}

echo "Detecting distribution..."

if [ ! -f /etc/os-release ]; then
    echo "Cannot detect distribution (missing /etc/os-release)"
    exit 1
fi

. /etc/os-release

echo "Detected: $NAME"
echo

case "$ID" in
    ubuntu|debian|linuxmint|pop|elementary)
        install_debian
        ;;
    arch|manjaro)
        install_arch
        ;;
    fedora)
        install_fedora
        ;;
    opensuse*|suse)
        install_opensuse
        ;;
    alpine)
        install_alpine
        ;;
    *)
        echo "Unsupported distribution: $ID"
        echo "Please install wl-clipboard manually."
        exit 1
        ;;
esac

echo
echo "wl-clipboard installation complete."

if command -v wl-copy >/dev/null 2>&1 && command -v wl-paste >/dev/null 2>&1; then
    echo "wl-copy and wl-paste are available."
else
    echo "Installation finished, but wl-copy/wl-paste not found in PATH."
    exit 1
fi