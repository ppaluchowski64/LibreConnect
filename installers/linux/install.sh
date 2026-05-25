#!/usr/bin/env bash

set -euo pipefail

# Universal installer script for LibreConnect
# Installs application files to /opt/libreconnect

INSTALL_PREFIX="/opt/libreconnect"
PACKAGE_NAME="libreconnect"
DESCRIPTION="Privacy-focused peer-to-peer device connectivity app"
LONG_DESCRIPTION="LibreConnect links desktop and mobile devices over local, encrypted peer-to-peer connections"
HOMEPAGE="https://github.com/ppaluchowski64/LibreConnect"
LICENSE="GPL-3.0-only"

# Ensure script is run as root
if [[ "$EUID" -ne 0 ]]; then
    echo "ERROR: Please run this installer as root (e.g. using sudo)." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_SOURCE_DIR="${SCRIPT_DIR}/app"
SCRIPTS_SOURCE_DIR="${SCRIPT_DIR}/scripts"
RES_SOURCE_DIR="${SCRIPT_DIR}/res"

if [[ ! -d "$APP_SOURCE_DIR" ]]; then
    echo "ERROR: Missing application payload directory: $APP_SOURCE_DIR" >&2
    exit 1
fi

echo "============================================="
echo " Installing LibreConnect (Universal Installer) "
echo "============================================="

# 1. Install system package dependencies
echo "Checking and installing dependencies..."
if command -v apt-get >/dev/null 2>&1; then
    echo "Detected Debian/Ubuntu system. Installing dependencies..."
    apt-get update
    apt-get install -y wl-clipboard dkms gcc make git libv4l-dev || true
elif command -v dnf >/dev/null 2>&1; then
    echo "Detected Fedora/RHEL/CentOS system. Installing dependencies..."
    dnf install -y wl-clipboard dkms gcc make git libv4l-devel || true
elif command -v pacman >/dev/null 2>&1; then
    echo "Detected Arch Linux system. Installing dependencies..."
    pacman -Sy --noconfirm --needed wl-clipboard v4l2loopback-dkms git gcc make || true
else
    echo "WARNING: Could not automatically install dependencies. Please ensure 'wl-clipboard' and 'v4l2loopback' are installed."
fi

# 2. Copy application files
echo "Copying application files to ${INSTALL_PREFIX}..."
rm -rf "$INSTALL_PREFIX"
mkdir -p "$INSTALL_PREFIX"
cp -a "${APP_SOURCE_DIR}/." "$INSTALL_PREFIX/"

# Copy extra post-install scripts to install directory if available
if [[ -d "$SCRIPTS_SOURCE_DIR" ]]; then
    mkdir -p "${INSTALL_PREFIX}/scripts/linux"
    cp -a "$SCRIPTS_SOURCE_DIR" "${INSTALL_PREFIX}/scripts/linux/install"
fi

# Set executable permissions
chmod 0755 "${INSTALL_PREFIX}/usr/bin/LibreConnect"
if [[ -f "${INSTALL_PREFIX}/AppRun" ]]; then
    chmod 0755 "${INSTALL_PREFIX}/AppRun"
fi
if [[ -f "${INSTALL_PREFIX}/AppRun.wrapped" ]]; then
    chmod 0755 "${INSTALL_PREFIX}/AppRun.wrapped"
fi
if [[ -d "${INSTALL_PREFIX}/scripts/linux/install" ]]; then
    find "${INSTALL_PREFIX}/scripts/linux/install" -type f -name "*.sh" -exec chmod 0755 {} +
fi

# 3. Create wrapper scripts in /opt/libreconnect
LAUNCHER_REL="libreconnect-run.sh"
DAEMON_LAUNCHER_REL="libreconnect-daemon-run.sh"
DAEMON_EXE_NAME="LibreConnect-daemon"

cat > "${INSTALL_PREFIX}/${LAUNCHER_REL}" <<EOF
#!/usr/bin/env bash
set -euo pipefail

APPDIR="${INSTALL_PREFIX}"
APPRUN="\${APPDIR}/AppRun"
MAIN_EXE="\${APPDIR}/usr/bin/LibreConnect"

if [[ -x "\${APPRUN}" ]]; then
    exec "\${APPRUN}" "\$@"
fi

if [[ -d "\${APPDIR}/usr/plugins" ]]; then
    export QT_PLUGIN_PATH="\${APPDIR}/usr/plugins"
    export QT_QPA_PLATFORM_PLUGIN_PATH="\${APPDIR}/usr/plugins/platforms"
fi
if [[ -d "\${APPDIR}/usr/qml" ]]; then
    export QML2_IMPORT_PATH="\${APPDIR}/usr/qml"
    export QML_IMPORT_PATH="\${APPDIR}/usr/qml"
fi
if [[ -d "\${APPDIR}/usr/translations" ]]; then
    export QT_TRANSLATIONS_PATH="\${APPDIR}/usr/translations"
fi

LIB_PATHS=()
for lib_dir in "\${APPDIR}/usr/lib" "\${APPDIR}/usr/lib/aarch64-linux-gnu" "\${APPDIR}/usr/lib/x86_64-linux-gnu"; do
    if [[ -d "\${lib_dir}" ]]; then
        LIB_PATHS+=("\${lib_dir}")
    fi
done
if [[ \${#LIB_PATHS[@]} -gt 0 ]]; then
    BUNDLED_LD_PATH="\$(IFS=:; echo "\${LIB_PATHS[*]}")"
    export LD_LIBRARY_PATH="\${BUNDLED_LD_PATH}\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}"
fi

exec "\${MAIN_EXE}" "\$@"
EOF
chmod 0755 "${INSTALL_PREFIX}/${LAUNCHER_REL}"

cat > "${INSTALL_PREFIX}/${DAEMON_LAUNCHER_REL}" <<EOF
#!/usr/bin/env bash
set -euo pipefail

APPDIR="${INSTALL_PREFIX}"
DAEMON_EXE_ROOT="\${APPDIR}/${DAEMON_EXE_NAME}"
DAEMON_EXE_USR_BIN="\${APPDIR}/usr/bin/${DAEMON_EXE_NAME}"

if [[ -d "\${APPDIR}/usr/plugins" ]]; then
    export QT_PLUGIN_PATH="\${APPDIR}/usr/plugins"
    export QT_QPA_PLATFORM_PLUGIN_PATH="\${APPDIR}/usr/plugins/platforms"
fi
if [[ -d "\${APPDIR}/usr/qml" ]]; then
    export QML2_IMPORT_PATH="\${APPDIR}/usr/qml"
    export QML_IMPORT_PATH="\${APPDIR}/usr/qml"
fi
if [[ -d "\${APPDIR}/usr/translations" ]]; then
    export QT_TRANSLATIONS_PATH="\${APPDIR}/usr/translations"
fi

LIB_PATHS=()
for lib_dir in "\${APPDIR}/usr/lib" "\${APPDIR}/usr/lib/aarch64-linux-gnu" "\${APPDIR}/usr/lib/x86_64-linux-gnu"; do
    if [[ -d "\${lib_dir}" ]]; then
        LIB_PATHS+=("\${lib_dir}")
    fi
done
if [[ \${#LIB_PATHS[@]} -gt 0 ]]; then
    BUNDLED_LD_PATH="\$(IFS=:; echo "\${LIB_PATHS[*]}")"
    export LD_LIBRARY_PATH="\${BUNDLED_LD_PATH}\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}"
fi

if [[ -x "\${DAEMON_EXE_ROOT}" ]]; then
    exec "\${DAEMON_EXE_ROOT}" "\$@"
fi

exec "\${DAEMON_EXE_USR_BIN}" "\$@"
EOF
chmod 0755 "${INSTALL_PREFIX}/${DAEMON_LAUNCHER_REL}"

# 4. Create symlinks in /usr/bin
echo "Creating wrappers in /usr/bin..."
cat > "/usr/bin/libreconnect" <<EOF
#!/usr/bin/env bash
exec "${INSTALL_PREFIX}/${LAUNCHER_REL}" "\$@"
EOF
chmod 0755 "/usr/bin/libreconnect"

cat > "/usr/bin/libreconnect-daemon" <<EOF
#!/usr/bin/env bash
exec "${INSTALL_PREFIX}/${DAEMON_LAUNCHER_REL}" "\$@"
EOF
chmod 0755 "/usr/bin/libreconnect-daemon"

# 5. Desktop launcher and autostart configuration
echo "Installing desktop launcher..."
mkdir -p "/usr/share/applications"
cat > "/usr/share/applications/libreconnect.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=LibreConnect
Comment=${DESCRIPTION}
Exec=libreconnect
Icon=libreconnect_logo
Terminal=false
Categories=Network;Utility;
Keywords=phone;android;sync;clipboard;notification;file transfer;remote;
EOF

mkdir -p "/etc/xdg/autostart"
cat > "/etc/xdg/autostart/libreconnect-daemon.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=LibreConnect Daemon
Comment=Start the LibreConnect background daemon
Exec=libreconnect-daemon
Terminal=false
X-GNOME-Autostart-enabled=true
NoDisplay=true
EOF

# 6. Copy Icon
echo "Installing application icon..."
mkdir -p "/usr/share/icons/hicolor/512x512/apps"
ICON_NAME="libreconnect_logo.png"
if [[ -f "${RES_SOURCE_DIR}/${ICON_NAME}" ]]; then
    cp "${RES_SOURCE_DIR}/${ICON_NAME}" "/usr/share/icons/hicolor/512x512/apps/${ICON_NAME}"
elif [[ -d "${INSTALL_PREFIX}/usr/share/icons/hicolor" ]]; then
    cp -a "${INSTALL_PREFIX}/usr/share/icons/hicolor/." "/usr/share/icons/hicolor/"
fi

# 7. Metadata and Documentation
mkdir -p "/usr/share/metainfo"
cat > "/usr/share/metainfo/com.libreconnect.desktop.metainfo.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>com.libreconnect.desktop</id>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>${LICENSE}</project_license>
  <name>LibreConnect</name>
  <summary>${DESCRIPTION}</summary>
  <description>
    <p>${LONG_DESCRIPTION}</p>
  </description>
  <launchable type="desktop-id">libreconnect.desktop</launchable>
  <url type="homepage">${HOMEPAGE}</url>
  <url type="bugtracker">${HOMEPAGE}/issues</url>
  <url type="vcs-browser">${HOMEPAGE}</url>
  <developer_name>LibreConnect</developer_name>
  <categories>
    <category>Network</category>
    <category>Utility</category>
  </categories>
  <provides>
    <binary>libreconnect</binary>
  </provides>
</component>
EOF

mkdir -p "/usr/share/doc/${PACKAGE_NAME}"
cat > "/usr/share/doc/${PACKAGE_NAME}/copyright" <<EOF
Upstream-Name: LibreConnect
Source: ${HOMEPAGE}

Files: *
Copyright: LibreConnect contributors
License: GPL-3.0-only
EOF

if [[ -f "${SCRIPT_DIR}/LICENSE" ]]; then
    cp "${SCRIPT_DIR}/LICENSE" "/usr/share/doc/${PACKAGE_NAME}/GPL-3"
fi

# 8. Post-installation script tasks
echo "Running post-install hooks..."
INSTALL_SCRIPTS_DIR="${INSTALL_PREFIX}/scripts/linux/install"
WL_SCRIPT="${INSTALL_SCRIPTS_DIR}/wl-clipboard.sh"
V4L2_SCRIPT="${INSTALL_SCRIPTS_DIR}/v4l2loopback.sh"
UINPUT_SCRIPT="${INSTALL_SCRIPTS_DIR}/uinput-setup.sh"
V4L2_HELPER="${INSTALL_PREFIX}/tools/v4l2loopback-helper"

export LIBRECONNECT_SKIP_PACKAGE_INSTALL=1

if [[ -x "$UINPUT_SCRIPT" ]]; then
    bash "$UINPUT_SCRIPT" || true
fi

if [[ -x "$WL_SCRIPT" ]]; then
    bash "$WL_SCRIPT" || true
fi

if [[ -x "$V4L2_SCRIPT" ]]; then
    V4L2_HELPER_PATH="$V4L2_HELPER" bash "$V4L2_SCRIPT" || true
fi

# 9. Update system caches
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi

# 10. Start daemon for users
start_daemon_for_user() {
    local user_name="$1"
    if [[ -z "$user_name" || "$user_name" = "root" ]]; then
        return
    fi

    local daemon_path="/usr/bin/libreconnect-daemon"
    if [[ ! -x "$daemon_path" ]]; then
        return
    fi

    if pgrep -u "$user_name" -f "$daemon_path" >/dev/null 2>&1; then
        return
    fi

    echo "Starting LibreConnect daemon for user: $user_name"
    runuser -u "$user_name" -- sh -lc "nohup \"$daemon_path\" >/dev/null 2>&1 &" || true
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

echo "LibreConnect has been successfully installed!"
echo "You can launch it from your desktop applications menu, or by running: libreconnect"
