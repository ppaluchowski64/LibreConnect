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
KERNEL=="uinput", SUBSYSTEM=="misc", MODE="0660", GROUP="uinput", OPTIONS+="static_node=uinput", TAG+="uaccess"
EOF

echo "[uinput-setup] Reloading udev rules"
run_as_root udevadm control --reload-rules
run_as_root udevadm trigger --verbose --sysname-match=uinput

USERS_TO_ADD=()
if [[ -n "${SUDO_USER:-}" ]]; then
    USERS_TO_ADD+=("$SUDO_USER")
fi

CURRENT_LOGIN_USER="$(logname 2>/dev/null || true)"
if [[ -n "$CURRENT_LOGIN_USER" ]]; then
    USERS_TO_ADD+=("$CURRENT_LOGIN_USER")
fi

if command -v loginctl >/dev/null 2>&1; then
    # Add users with active sessions (UID >= 1000)
    while read -r uid user; do
        if [[ -n "$uid" && "$uid" =~ ^[0-9]+$ && "$uid" -ge 1000 ]]; then
            USERS_TO_ADD+=("$user")
        fi
    done < <(loginctl list-users --no-legend 2>/dev/null || true)
fi

FINAL_USERS=$(printf "%s\n" "${USERS_TO_ADD[@]}" | grep -v "^root$" | sort -u || true)

for user in $FINAL_USERS; do
    echo "[uinput-setup] Adding user $user to uinput group"
    run_as_root usermod -aG uinput "$user" || true
    
    # Try to grant immediate access via ACLs if setfacl is available
    if command -v setfacl >/dev/null 2>&1; then
        if [[ -c /dev/uinput ]]; then
            echo "[uinput-setup] Granting immediate access to /dev/uinput for user $user via ACL"
            run_as_root setfacl -m "u:$user:rw" /dev/uinput || true
        fi
    fi
done

echo "[uinput-setup] uinput configuration complete."
