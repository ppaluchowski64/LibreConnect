#!/usr/bin/env bash

set -u

LOG_FILE="/var/log/libreconnect-postinst.log"
exec > >(tee -a "$LOG_FILE") 2>&1

echo "[postinst] Started at $(date -Is)"

INSTALL_SCRIPTS_DIR="/opt/libreconnect/scripts/linux/install"
WL_SCRIPT="${INSTALL_SCRIPTS_DIR}/wl-clipboard.sh"
V4L2_SCRIPT="${INSTALL_SCRIPTS_DIR}/v4l2loopback.sh"
UINPUT_SCRIPT="${INSTALL_SCRIPTS_DIR}/uinput-setup.sh"
V4L2_HELPER="/opt/libreconnect/tools/v4l2loopback-helper"
DAEMON_LAUNCHER="/usr/bin/libreconnect-daemon"

export LIBRECONNECT_SKIP_PACKAGE_INSTALL=1

if [[ -x "$UINPUT_SCRIPT" ]]; then
    bash "$UINPUT_SCRIPT" || true
else
    echo "[postinst] Missing script: $UINPUT_SCRIPT"
fi

if [[ -x "$WL_SCRIPT" ]]; then
    bash "$WL_SCRIPT" || true
else
    echo "[postinst] Missing script: $WL_SCRIPT"
fi

if [[ -x "$V4L2_SCRIPT" ]]; then
    V4L2_HELPER_PATH="$V4L2_HELPER" bash "$V4L2_SCRIPT" || true
else
    echo "[postinst] Missing script: $V4L2_SCRIPT"
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi

start_daemon_for_user() {
    local user_name="$1"
    if [[ -z "$user_name" || "$user_name" = "root" ]]; then
        return
    fi

    if [[ ! -x "$DAEMON_LAUNCHER" ]]; then
        echo "[postinst] Missing daemon launcher: $DAEMON_LAUNCHER"
        return
    fi

    if pgrep -u "$user_name" -f "$DAEMON_LAUNCHER" >/dev/null 2>&1; then
        echo "[postinst] Daemon already running for user: $user_name"
        return
    fi

    echo "[postinst] Starting daemon for user: $user_name"
    runuser -u "$user_name" -- sh -lc "nohup \"$DAEMON_LAUNCHER\" >/dev/null 2>&1 &" || true
}

USERS_TO_START=()
if [[ -n "${SUDO_USER:-}" ]]; then
    USERS_TO_START+=("$SUDO_USER")
fi

CURRENT_LOGIN_USER="$(logname 2>/dev/null || true)"
if [[ -n "$CURRENT_LOGIN_USER" ]]; then
    USERS_TO_START+=("$CURRENT_LOGIN_USER")
fi

if command -v loginctl >/dev/null 2>&1; then
    while read -r uid user; do
        if [[ -n "$uid" && "$uid" =~ ^[0-9]+$ && "$uid" -ge 1000 ]]; then
            USERS_TO_START+=("$user")
        fi
    done < <(loginctl list-users --no-legend 2>/dev/null || true)
fi

# Unique users, excluding root
FINAL_USERS=$(printf "%s\n" "${USERS_TO_START[@]}" | grep -v "^root$" | sort -u || true)

for user in $FINAL_USERS; do
    start_daemon_for_user "$user"
done

echo "[postinst] Finished at $(date -Is)"
exit 0
