#!/usr/bin/env bash
set -u

LOG_FILE="/var/log/libreconnect-postinst.log"
exec > >(tee -a "$LOG_FILE") 2>&1

echo "[postinst] Started at $(date -Is)"

INSTALL_SCRIPTS_DIR="/opt/libreconnect/scripts/linux/install"
WL_SCRIPT="${INSTALL_SCRIPTS_DIR}/wl-clipboard.sh"
V4L2_SCRIPT="${INSTALL_SCRIPTS_DIR}/v4l2loopback.sh"
V4L2_HELPER="/opt/libreconnect/tools/v4l2loopback-helper"

export LIBRECONNECT_SKIP_PACKAGE_INSTALL=1

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

echo "[postinst] Finished at $(date -Is)"
exit 0
