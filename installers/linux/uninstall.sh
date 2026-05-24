#!/usr/bin/env bash

set -euo pipefail

# Universal uninstaller script for LibreConnect
# Cleans up files installed by the universal installer

INSTALL_PREFIX="/opt/libreconnect"
PACKAGE_NAME="libreconnect"

# Ensure script is run as root
if [[ "$EUID" -ne 0 ]]; then
    echo "ERROR: Please run this uninstaller as root (e.g. using sudo)." >&2
    exit 1
fi

echo "============================================="
echo " Uninstalling LibreConnect "
echo "============================================="

# 1. Stop daemon processes
echo "Stopping background processes..."
pkill -f "/usr/bin/libreconnect-daemon" || true
pkill -f "${INSTALL_PREFIX}/usr/bin/LibreConnect-daemon" || true
pkill -f "${INSTALL_PREFIX}/LibreConnect-daemon" || true

# 2. Remove v4l2 helper and polkit rules if installed
echo "Removing loopback device settings..."
rm -f /usr/libexec/v4l2loopback-helper || true
rm -f /usr/share/polkit-1/rules.d/50-v4l2loopback.rules || true
rm -f /usr/share/polkit-1/actions/org.example.v4l2loopback.policy || true
if command -v systemctl >/dev/null 2>&1; then
    systemctl restart polkit || true
fi

# 3. Delete files and folders
echo "Removing application files..."
rm -rf "$INSTALL_PREFIX"

# 4. Remove /usr/bin wrappers
echo "Removing wrappers in /usr/bin..."
rm -f "/usr/bin/libreconnect"
rm -f "/usr/bin/libreconnect-daemon"

# 5. Remove desktop launchers and autostart
echo "Removing desktop integration..."
rm -f "/usr/share/applications/libreconnect.desktop"
rm -f "/etc/xdg/autostart/libreconnect-daemon.desktop"

# 6. Remove icons
echo "Removing icons..."
rm -f "/usr/share/icons/hicolor/512x512/apps/libreconnect_logo.png"

# 7. Remove metadata and docs
echo "Removing metainfo and documentation..."
rm -f "/usr/share/metainfo/com.libreconnect.desktop.metainfo.xml"
rm -rf "/usr/share/doc/${PACKAGE_NAME}"

# 8. Update caches
echo "Updating system caches..."
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi

echo "LibreConnect has been successfully uninstalled."
