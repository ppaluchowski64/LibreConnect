set -u

LOG_FILE="/var/log/libreconnect-postinst.log"
exec >>"$LOG_FILE" 2>&1

echo "[postrpm] Started at $(date -Is)"

INSTALL_SCRIPTS_DIR="/opt/libreconnect/scripts/linux/install"
WL_SCRIPT="${INSTALL_SCRIPTS_DIR}/wl-clipboard.sh"
V4L2_SCRIPT="${INSTALL_SCRIPTS_DIR}/v4l2loopback.sh"
V4L2_HELPER="/opt/libreconnect/tools/v4l2loopback-helper"
DAEMON_LAUNCHER="/usr/bin/libreconnect-daemon"

export LIBRECONNECT_SKIP_PACKAGE_INSTALL=1

if [ -x "$WL_SCRIPT" ]; then
    bash "$WL_SCRIPT" || true
else
    echo "[postrpm] Missing script: $WL_SCRIPT"
fi

if [ -x "$V4L2_SCRIPT" ]; then
    V4L2_HELPER_PATH="$V4L2_HELPER" bash "$V4L2_SCRIPT" || true
else
    echo "[postrpm] Missing script: $V4L2_SCRIPT"
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi

start_daemon_for_user() {
    user_name="$1"
    if [ -z "$user_name" ] || [ "$user_name" = "root" ]; then
        return
    fi

    if [ ! -x "$DAEMON_LAUNCHER" ]; then
        echo "[postrpm] Missing daemon launcher: $DAEMON_LAUNCHER"
        return
    fi

    if pgrep -u "$user_name" -f "$DAEMON_LAUNCHER" >/dev/null 2>&1; then
        echo "[postrpm] Daemon already running for user: $user_name"
        return
    fi

    echo "[postrpm] Starting daemon for user: $user_name"
    runuser -u "$user_name" -- sh -lc "nohup \"$DAEMON_LAUNCHER\" >/dev/null 2>&1 &" || true
}

if [ -n "${SUDO_USER:-}" ]; then
    start_daemon_for_user "$SUDO_USER"
elif command -v logname >/dev/null 2>&1; then
    CURRENT_LOGIN_USER="$(logname 2>/dev/null || true)"
    if [ -n "$CURRENT_LOGIN_USER" ]; then
        start_daemon_for_user "$CURRENT_LOGIN_USER"
    fi
fi

echo "[postrpm] Finished at $(date -Is)"
exit 0
