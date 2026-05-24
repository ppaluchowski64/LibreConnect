#!/usr/bin/env bash

set -euo pipefail

run_as_root() {
    if [[ "$(id -u)" -eq 0 ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

echo "[uinput-setup] Configuring uinput permissions..."

echo "[uinput-setup] Loading uinput kernel module"
run_as_root modprobe uinput || true

if [[ -d /etc/modules-load.d ]]; then
    echo "[uinput-setup] Setting up uinput to load on boot"
    echo "uinput" | run_as_root tee /etc/modules-load.d/uinput.conf >/dev/null
fi

if ! getent group uinput >/dev/null; then
    echo "[uinput-setup] Creating uinput group"
    run_as_root groupadd -r uinput
fi

UDEV_RULE="/etc/udev/rules.d/99-uinput.rules"
echo "[uinput-setup] Creating udev rule at $UDEV_RULE"
cat <<EOF | run_as_root tee "$UDEV_RULE" >/dev/null
KERNEL=="uinput", MODE="0660", GROUP="uinput", OPTIONS+="static_node=uinput", TAG+="uaccess"
EOF

echo "[uinput-setup] Reloading udev rules"
run_as_root udevadm control --reload-rules
run_as_root udevadm trigger

if [[ -n "${SUDO_USER:-}" ]]; then
    echo "[uinput-setup] Adding user $SUDO_USER to uinput group"
    run_as_root usermod -aG uinput "$SUDO_USER"
elif command -v logname >/dev/null 2>&1; then
    CURRENT_LOGIN_USER="$(logname 2>/dev/null || true)"

    if [[ -n "$CURRENT_LOGIN_USER" ]]; then
        echo "[uinput-setup] Adding user $CURRENT_LOGIN_USER to uinput group"
        run_as_root usermod -aG uinput "$CURRENT_LOGIN_USER"
    fi
fi

echo "[uinput-setup] uinput configuration complete."
