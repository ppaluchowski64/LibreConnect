#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

DEPLOY_DIR_REL="../../build/desktop/build/Release/deploy/Release/appLibreConnect_desktop"
if [[ -f "${ROOT_DIR}/VERSION" ]]; then
    VERSION_FROM_FILE="$(tr -d '[:space:]' < "${ROOT_DIR}/VERSION")"
    if [[ -n "${VERSION_FROM_FILE}" ]]; then
        VERSION="${VERSION_FROM_FILE}"
    else
        VERSION="1.0.0"
    fi
else
    VERSION="1.0.0"
fi
OUTPUT_DIR_REL="../../out"
ARCH="amd64"

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --deploy-dir <path>   AppDir path (default: ${DEPLOY_DIR_REL})
  --version <x.y.z>     Package version (default: ${VERSION})
  --output-dir <path>   Output directory (default: ${OUTPUT_DIR_REL})
  --arch <arch>         Architecture (default: ${ARCH})
  -h, --help            Show help
EOF
}

resolve_path() {
    local base="$1"
    local value="$2"
    if [[ "$value" = /* ]]; then
        printf "%s\n" "$value"
    else
        printf "%s\n" "$(cd "$base" && realpath "$value")"
    fi
}

normalize_arch() {
    local arch="$1"
    case "$arch" in
        amd64|x86_64)
            printf "x86_64\n"
            ;;
        arm64|aarch64)
            printf "aarch64\n"
            ;;
        *)
            printf "%s\n" "$arch"
            ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --deploy-dir)
            DEPLOY_DIR_REL="$2"
            shift 2
            ;;
        --version)
            VERSION="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR_REL="$2"
            shift 2
            ;;
        --arch)
            ARCH="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

DEPLOY_DIR="$(resolve_path "$SCRIPT_DIR" "$DEPLOY_DIR_REL")"
OUTPUT_DIR="$(resolve_path "$SCRIPT_DIR" "$OUTPUT_DIR_REL")"
ARCH_TARGET="$(normalize_arch "$ARCH")"

if [[ ! -d "$DEPLOY_DIR" ]]; then
    echo "Deploy directory not found: $DEPLOY_DIR" >&2
    exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
    echo "ERROR: docker not found. Docker is required to build Arch packages." >&2
    exit 1
fi

echo "============================================="
echo " Building Arch Linux Package using Docker "
echo "============================================="

# Ensure directories exist
mkdir -p "$OUTPUT_DIR"

# Absolute path to local installation files that PKGBUILD will reference
LINUX_INSTALL_SCRIPTS_DIR="$(realpath "${ROOT_DIR}/scripts/linux/install")"
SOURCE_ICON="$(realpath "${ROOT_DIR}/apps/desktop/res/libreconnect_logo.png")"
LICENSE_FILE="$(realpath "${ROOT_DIR}/LICENSE")"
CMAKE_BUILD_DIR="$(cd "${DEPLOY_DIR}/../../.." && pwd)"
V4L2_HELPER_BIN="${CMAKE_BUILD_DIR}/v4l2loopback-helper"

# Execute in Docker
docker run --rm \
  -v "${DEPLOY_DIR}:/deploy:ro" \
  -v "${LINUX_INSTALL_SCRIPTS_DIR}:/scripts_install:ro" \
  -v "${SOURCE_ICON}:/libreconnect_logo.png:ro" \
  -v "${LICENSE_FILE}:/LICENSE:ro" \
  $([[ -f "${V4L2_HELPER_BIN}" ]] && echo "-v ${V4L2_HELPER_BIN}:/v4l2loopback-helper:ro") \
  -v "${OUTPUT_DIR}:/output" \
  -e VERSION="${VERSION}" \
  -e ARCH_TARGET="${ARCH_TARGET}" \
  archlinux:latest \
  /bin/bash -c "
    set -euo pipefail
    
    echo 'Initializing Arch keyring...'
    pacman -Sy --noconfirm archlinux-keyring

    echo 'Installing base-devel and essential packages...'
    pacman -Syu --noconfirm --needed base-devel git sudo
    
    echo 'Creating build user...'
    useradd -m builduser
    passwd -d builduser
    echo 'builduser ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers
    
    BUILD_DIR='/home/builduser/build'
    mkdir -p \$BUILD_DIR
    cd \$BUILD_DIR
    
    echo 'Generating PKGBUILD...'
    cat > PKGBUILD << 'EOF'
pkgname=libreconnect
pkgver=__VERSION__
pkgrel=1
pkgdesc=\"Privacy-focused peer-to-peer device connectivity app\"
arch=('__ARCH_TARGET__')
url=\"https://github.com/ppaluchowski64/LibreConnect\"
license=('GPL3')
depends=('bash' 'wl-clipboard' 'v4l2loopback-dkms')
install=libreconnect.install
options=('!strip' '!zipman')

package() {
    # 1. Install application files to /opt/libreconnect
    mkdir -p \"\${pkgdir}/opt/libreconnect\"
    cp -a /deploy/. \"\${pkgdir}/opt/libreconnect/\"

    if [[ -f /v4l2loopback-helper ]]; then
        mkdir -p \"\${pkgdir}/opt/libreconnect/tools\"
        install -Dm0755 /v4l2loopback-helper \"\${pkgdir}/opt/libreconnect/tools/v4l2loopback-helper\"
    fi
    
    # Copy install helper scripts
    mkdir -p \"\${pkgdir}/opt/libreconnect/scripts/linux/install\"
    cp -a /scripts_install/. \"\${pkgdir}/opt/libreconnect/scripts/linux/install/\"
    
    # Ensure correct permissions
    chmod 0755 \"\${pkgdir}/opt/libreconnect/usr/bin/LibreConnect\"
    if [[ -f \"\${pkgdir}/opt/libreconnect/AppRun\" ]]; then
        chmod 0755 \"\${pkgdir}/opt/libreconnect/AppRun\"
    fi
    if [[ -f \"\${pkgdir}/opt/libreconnect/AppRun.wrapped\" ]]; then
        chmod 0755 \"\${pkgdir}/opt/libreconnect/AppRun.wrapped\"
    fi
    find \"\${pkgdir}/opt/libreconnect/scripts/linux/install\" -type f -name \"*.sh\" -exec chmod 0755 {} +

    # 2. Build launcher wrappers inside /opt/libreconnect
    cat > \"\${pkgdir}/opt/libreconnect/libreconnect-run.sh\" << 'LAUNCHER'
#!/usr/bin/env bash
set -euo pipefail
APPDIR=\"/opt/libreconnect\"
APPRUN=\"\${APPDIR}/AppRun\"
MAIN_EXE=\"\${APPDIR}/usr/bin/LibreConnect\"

if [[ -x \"\${APPRUN}\" ]]; then
    exec \"\${APPRUN}\" \"\$@\"
fi

if [[ -d \"\${APPDIR}/usr/plugins\" ]]; then
    export QT_PLUGIN_PATH=\"\${APPDIR}/usr/plugins\"
    export QT_QPA_PLATFORM_PLUGIN_PATH=\"\${APPDIR}/usr/plugins/platforms\"
fi
if [[ -d \"\${APPDIR}/usr/qml\" ]]; then
    export QML2_IMPORT_PATH=\"\${APPDIR}/usr/qml\"
    export QML_IMPORT_PATH=\"\${APPDIR}/usr/qml\"
fi
if [[ -d \"\${APPDIR}/usr/translations\" ]]; then
    export QT_TRANSLATIONS_PATH=\"\${APPDIR}/usr/translations\"
fi

LIB_PATHS=()
for lib_dir in \"\${APPDIR}/usr/lib\" \"\${APPDIR}/usr/lib/aarch64-linux-gnu\" \"\${APPDIR}/usr/lib/x86_64-linux-gnu\"; do
    if [[ -d \"\${lib_dir}\" ]]; then
        LIB_PATHS+=(\"\${lib_dir}\")
    fi
done
if [[ \${#LIB_PATHS[@]} -gt 0 ]]; then
    BUNDLED_LD_PATH=\"\$(IFS=:; echo \"\${LIB_PATHS[*]}\")\"
    export LD_LIBRARY_PATH=\"\${BUNDLED_LD_PATH}\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}\"
fi
exec \"\${MAIN_EXE}\" \"\$@\"
LAUNCHER
    chmod 0755 \"\${pkgdir}/opt/libreconnect/libreconnect-run.sh\"

    cat > \"\${pkgdir}/opt/libreconnect/libreconnect-daemon-run.sh\" << 'LAUNCHER'
#!/usr/bin/env bash
set -euo pipefail
APPDIR=\"/opt/libreconnect\"
DAEMON_EXE_NAME=\"LibreConnect-daemon\"
DAEMON_EXE_ROOT=\"\${APPDIR}/\${DAEMON_EXE_NAME}\"
DAEMON_EXE_USR_BIN=\"\${APPDIR}/usr/bin/\${DAEMON_EXE_NAME}\"

if [[ -d \"\${APPDIR}/usr/plugins\" ]]; then
    export QT_PLUGIN_PATH=\"\${APPDIR}/usr/plugins\"
    export QT_QPA_PLATFORM_PLUGIN_PATH=\"\${APPDIR}/usr/plugins/platforms\"
fi
if [[ -d \"\${APPDIR}/usr/qml\" ]]; then
    export QML2_IMPORT_PATH=\"\${APPDIR}/usr/qml\"
    export QML_IMPORT_PATH=\"\${APPDIR}/usr/qml\"
fi
if [[ -d \"\${APPDIR}/usr/translations\" ]]; then
    export QT_TRANSLATIONS_PATH=\"\${APPDIR}/usr/translations\"
fi

LIB_PATHS=()
for lib_dir in \"\${APPDIR}/usr/lib\" \"\${APPDIR}/usr/lib/aarch64-linux-gnu\" \"\${APPDIR}/usr/lib/x86_64-linux-gnu\"; do
    if [[ -d \"\${lib_dir}\" ]]; then
        LIB_PATHS+=(\"\${lib_dir}\")
    fi
done
if [[ \${#LIB_PATHS[@]} -gt 0 ]]; then
    BUNDLED_LD_PATH=\"\$(IFS=:; echo \"\${LIB_PATHS[*]}\")\"
    export LD_LIBRARY_PATH=\"\${BUNDLED_LD_PATH}\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}\"
fi

if [[ -x \"\${DAEMON_EXE_ROOT}\" ]]; then
    exec \"\${DAEMON_EXE_ROOT}\" \"\$@\"
fi
exec \"\${DAEMON_EXE_USR_BIN}\" \"\$@\"
LAUNCHER
    chmod 0755 \"\${pkgdir}/opt/libreconnect/libreconnect-daemon-run.sh\"

    # 3. Create wrapper scripts in /usr/bin
    mkdir -p \"\${pkgdir}/usr/bin\"
    cat > \"\${pkgdir}/usr/bin/libreconnect\" << 'LAUNCHER'
#!/usr/bin/env bash
exec /opt/libreconnect/libreconnect-run.sh \"\$@\"
LAUNCHER
    chmod 0755 \"\${pkgdir}/usr/bin/libreconnect\"

    cat > \"\${pkgdir}/usr/bin/libreconnect-daemon\" << 'LAUNCHER'
#!/usr/bin/env bash
exec /opt/libreconnect/libreconnect-daemon-run.sh \"\$@\"
LAUNCHER
    chmod 0755 \"\${pkgdir}/usr/bin/libreconnect-daemon\"

    # 4. Desktop Integration
    mkdir -p \"\${pkgdir}/usr/share/applications\"
    cat > \"\${pkgdir}/usr/share/applications/libreconnect.desktop\" << 'LAUNCHER'
[Desktop Entry]
Type=Application
Name=LibreConnect
Comment=Privacy-focused peer-to-peer device connectivity app
Exec=libreconnect
Icon=libreconnect_logo
Terminal=false
Categories=Network;Utility;
Keywords=phone;android;sync;clipboard;notification;file transfer;remote;
LAUNCHER

    mkdir -p \"\${pkgdir}/etc/xdg/autostart\"
    cat > \"\${pkgdir}/etc/xdg/autostart/libreconnect-daemon.desktop\" << 'LAUNCHER'
[Desktop Entry]
Type=Application
Name=LibreConnect Daemon
Comment=Start the LibreConnect background daemon
Exec=libreconnect-daemon
Terminal=false
X-GNOME-Autostart-enabled=true
NoDisplay=true
LAUNCHER

    # 5. Icons
    mkdir -p \"\${pkgdir}/usr/share/icons/hicolor/512x512/apps\"
    cp /libreconnect_logo.png \"\${pkgdir}/usr/share/icons/hicolor/512x512/apps/\"

    # 6. Metadata
    mkdir -p \"\${pkgdir}/usr/share/metainfo\"
    cat > \"\${pkgdir}/usr/share/metainfo/com.libreconnect.desktop.metainfo.xml\" << 'LAUNCHER'
<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<component type=\"desktop-application\">
  <id>com.libreconnect.desktop</id>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>GPL-3.0-only</project_license>
  <name>LibreConnect</name>
  <summary>Privacy-focused peer-to-peer device connectivity app</summary>
  <description>
    <p>LibreConnect links desktop and mobile devices over local, encrypted peer-to-peer connections</p>
  </description>
  <launchable type=\"desktop-id\">libreconnect.desktop</launchable>
  <url type=\"homepage\">https://github.com/ppaluchowski64/LibreConnect</url>
  <url type=\"bugtracker\">https://github.com/ppaluchowski64/LibreConnect/issues</url>
  <url type=\"vcs-browser\">https://github.com/ppaluchowski64/LibreConnect</url>
  <developer_name>LibreConnect</developer_name>
  <categories>
    <category>Network</category>
    <category>Utility</category>
  </categories>
  <provides>
    <binary>libreconnect</binary>
  </provides>
</component>
LAUNCHER

    # 7. Documentation and License
    mkdir -p \"\${pkgdir}/usr/share/licenses/libreconnect\"
    cp /LICENSE \"\${pkgdir}/usr/share/licenses/libreconnect/\"
    mkdir -p \"\${pkgdir}/usr/share/doc/libreconnect\"
    cat > \"\${pkgdir}/usr/share/doc/libreconnect/copyright\" << 'LAUNCHER'
Upstream-Name: LibreConnect
Source: https://github.com/ppaluchowski64/LibreConnect

Files: *
Copyright: LibreConnect contributors
License: GPL-3.0-only
LAUNCHER
}
EOF
    sed -i \"s/__VERSION__/\${VERSION}/g\" PKGBUILD
    sed -i \"s/__ARCH_TARGET__/\${ARCH_TARGET}/g\" PKGBUILD

    echo 'Generating install script...'
    cat > libreconnect.install << 'EOF'
post_install() {
    # Run post-install helper scripts
    local scripts_dir=\"/opt/libreconnect/scripts/linux/install\"
    export LIBRECONNECT_SKIP_PACKAGE_INSTALL=1

    if [[ -x \"\$scripts_dir/uinput-setup.sh\" ]]; then
        bash \"\$scripts_dir/uinput-setup.sh\" || true
    fi
    if [[ -x \"\$scripts_dir/wl-clipboard.sh\" ]]; then
        bash \"\$scripts_dir/wl-clipboard.sh\" || true
    fi
    if [[ -x \"\$scripts_dir/v4l2loopback.sh\" ]]; then
        V4L2_HELPER_PATH=\"/opt/libreconnect/tools/v4l2loopback-helper\" bash \"\$scripts_dir/v4l2loopback.sh\" || true
    fi


    # Update caches
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
    fi
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications || true
    fi

    # Try starting the daemon for sudo user
    if [[ -n \"\${SUDO_USER:-}\" ]]; then
        local daemon_path=\"/usr/bin/libreconnect-daemon\"
        if [[ -x \"\$daemon_path\" ]] && ! pgrep -u \"\$SUDO_USER\" -f \"\$daemon_path\" >/dev/null 2>&1; then
            echo \"Starting LibreConnect daemon for user: \$SUDO_USER\"
            runuser -u \"\$SUDO_USER\" -- sh -lc \"nohup \\\"\$daemon_path\\\" >/dev/null 2>&1 &\" || true
        fi
    fi
}

post_upgrade() {
    post_install \"\$@\"
}

pre_remove() {
    # Stop running instances
    pkill -f \"/usr/bin/libreconnect-daemon\" || true
    pkill -f \"/opt/libreconnect/libreconnect-daemon-run.sh\" || true
}

post_remove() {
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
    fi
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications || true
    fi
}
EOF

    # Strip carriage returns to prevent CRLF bugs on Windows-checkout files
    tr -d '\r' < PKGBUILD > PKGBUILD.tmp && mv PKGBUILD.tmp PKGBUILD
    tr -d '\r' < libreconnect.install > libreconnect.install.tmp && mv libreconnect.install.tmp libreconnect.install

    # Align permissions
    chown -R builduser:builduser \$BUILD_DIR

    # Override CARCH so the output package filename matches the target architecture
    # (by default makepkg uses the host's CARCH, which is always x86_64 on GH runners)
    sed -i \"s|^CARCH=.*|CARCH=\\\"${ARCH_TARGET}\\\"|\" /etc/makepkg.conf
    sed -i \"s|^CHOST=.*|CHOST=\\\"${ARCH_TARGET}-pc-linux-gnu\\\"|\" /etc/makepkg.conf

    # Execute build as builduser
    echo 'Running makepkg...'
    sudo -u builduser makepkg --nodeps --noconfirm --ignorearch

    # Copy output package back to the host
    echo 'Copying final package...'
    find . -name '*.pkg.tar.zst' -exec cp -a {} /output/ \;

    # Verify that the package was successfully created and copied
    if [[ -z \$(ls -A /output/) ]]; then
        echo \"ERROR: No Arch package (*.pkg.tar.zst) was found or copied to output!\" >&2
        exit 1
    fi
"

echo "Arch Linux package successfully created!"
