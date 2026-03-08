#!/usr/bin/env bash

set -e

install_debian() {
    sudo apt update
    sudo apt install -y wl-clipboard
}

install_fedora() {
    sudo dnf install -y wl-clipboard
}

install_arch() {
    sudo pacman -S --noconfirm wl-clipboard
}

install_opensuse() {
    sudo zypper install -y wl-clipboard
}

install_alpine() {
    sudo apk add wl-clipboard
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
