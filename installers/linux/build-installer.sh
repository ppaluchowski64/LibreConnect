#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

DEPLOY_DIR_REL="../../build/desktop/build/Release/deploy/Release/appLibreConnect_desktop"
VERSION="1.0.0"
OUTPUT_DIR_REL="../../out"
PACKAGE_NAME="libreconnect"
ARCH="amd64"
MAINTAINER="LibreConnect"
DESCRIPTION="LibreConnect Desktop"
INSTALL_PREFIX="/opt/libreconnect"

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --deploy-dir <path>   AppDir path (default: ${DEPLOY_DIR_REL})
  --version <x.y.z>     Package version (default: ${VERSION})
  --output-dir <path>   Output directory for .deb (default: ${OUTPUT_DIR_REL})
  --package-name <name> Debian package name (default: ${PACKAGE_NAME})
  --arch <arch>         Debian architecture (default: ${ARCH})
  --maintainer <name>   Maintainer field (default: ${MAINTAINER})
  --description <text>  Description field (default: ${DESCRIPTION})
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
        --package-name)
            PACKAGE_NAME="$2"
            shift 2
            ;;
        --arch)
            ARCH="$2"
            shift 2
            ;;
        --maintainer)
            MAINTAINER="$2"
            shift 2
            ;;
        --description)
            DESCRIPTION="$2"
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
MAIN_EXE="${DEPLOY_DIR}/usr/bin/appLibreConnect_desktop"
LAUNCHER_REL="libreconnect-run.sh"
CMAKE_BUILD_DIR="$(cd "${DEPLOY_DIR}/../../.." && pwd)"
V4L2_HELPER_BIN="${CMAKE_BUILD_DIR}/v4l2loopback-helper"
LINUX_INSTALL_SCRIPTS_DIR="${ROOT_DIR}/scripts/linux/install"
POSTINST_TEMPLATE="${SCRIPT_DIR}/postinst.sh"
PRERM_TEMPLATE="${SCRIPT_DIR}/prerm.sh"

if [[ ! -d "$DEPLOY_DIR" ]]; then
    echo "Deploy directory not found: $DEPLOY_DIR" >&2
    exit 1
fi

if [[ ! -f "$MAIN_EXE" ]]; then
    echo "Main executable not found: $MAIN_EXE" >&2
    exit 1
fi

if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "dpkg-deb not found. Install dpkg first." >&2
    exit 1
fi

if [[ ! -d "$LINUX_INSTALL_SCRIPTS_DIR" ]]; then
    echo "Install scripts directory not found: $LINUX_INSTALL_SCRIPTS_DIR" >&2
    exit 1
fi

if [[ ! -f "$POSTINST_TEMPLATE" || ! -f "$PRERM_TEMPLATE" ]]; then
    echo "Maintainer script templates missing in $SCRIPT_DIR" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

BUILD_ROOT="$(mktemp -d)"
trap 'rm -rf "$BUILD_ROOT"' EXIT

PKG_ROOT="${BUILD_ROOT}/${PACKAGE_NAME}_${VERSION}_${ARCH}"
mkdir -p "${PKG_ROOT}/DEBIAN"
mkdir -p "${PKG_ROOT}/opt"
mkdir -p "${PKG_ROOT}/usr/bin"
mkdir -p "${PKG_ROOT}/usr/share/applications"
mkdir -p "${PKG_ROOT}/usr/share/icons/hicolor/512x512/apps"
mkdir -p "${PKG_ROOT}${INSTALL_PREFIX}/tools"
mkdir -p "${PKG_ROOT}${INSTALL_PREFIX}/scripts/linux"

cp -a "${DEPLOY_DIR}/." "${PKG_ROOT}${INSTALL_PREFIX}/"
cp -a "${LINUX_INSTALL_SCRIPTS_DIR}" "${PKG_ROOT}${INSTALL_PREFIX}/scripts/linux/install"

PACKAGED_MAIN_EXE="${PKG_ROOT}${INSTALL_PREFIX}/usr/bin/appLibreConnect_desktop"
if [[ ! -f "$PACKAGED_MAIN_EXE" ]]; then
    echo "Main executable missing in package root: $PACKAGED_MAIN_EXE" >&2
    exit 1
fi

chmod 0755 "${PACKAGED_MAIN_EXE}"
if [[ -f "${PKG_ROOT}${INSTALL_PREFIX}/AppRun" ]]; then
    chmod 0755 "${PKG_ROOT}${INSTALL_PREFIX}/AppRun"
fi
if [[ -d "${PKG_ROOT}${INSTALL_PREFIX}/scripts/linux/install" ]]; then
    find "${PKG_ROOT}${INSTALL_PREFIX}/scripts/linux/install" -type f -name "*.sh" -exec chmod 0755 {} +
fi

cat > "${PKG_ROOT}${INSTALL_PREFIX}/${LAUNCHER_REL}" <<EOF
#!/usr/bin/env bash
set -euo pipefail

APPDIR="${INSTALL_PREFIX}"
APPRUN="\${APPDIR}/AppRun"
MAIN_EXE="\${APPDIR}/usr/bin/appLibreConnect_desktop"

if [[ -x "\${APPRUN}" ]]; then
    exec "\${APPRUN}" "\$@"
fi

# If AppRun is unavailable (common on ARM), force bundled Qt paths to avoid
# mixing bundled Qt with system Qt.
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
chmod 0755 "${PKG_ROOT}${INSTALL_PREFIX}/${LAUNCHER_REL}"

cat > "${PKG_ROOT}/usr/bin/libreconnect" <<EOF
#!/usr/bin/env bash
exec "${INSTALL_PREFIX}/${LAUNCHER_REL}" "\$@"
EOF
chmod 0755 "${PKG_ROOT}/usr/bin/libreconnect"

cat > "${PKG_ROOT}/usr/share/applications/libreconnect.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=LibreConnect
Comment=LibreConnect Desktop
Exec=libreconnect
Icon=libreconnect_logo
Terminal=false
Categories=Network;Utility;
EOF

if [[ -f "${DEPLOY_DIR}/usr/share/icons/hicolor/512x512/apps/libreconnect_logo.png" ]]; then
    cp -a "${DEPLOY_DIR}/usr/share/icons/hicolor/512x512/apps/libreconnect_logo.png" \
        "${PKG_ROOT}/usr/share/icons/hicolor/512x512/apps/libreconnect_logo.png"
fi

if [[ -f "${V4L2_HELPER_BIN}" ]]; then
    cp -a "${V4L2_HELPER_BIN}" "${PKG_ROOT}${INSTALL_PREFIX}/tools/v4l2loopback-helper"
    chmod 0755 "${PKG_ROOT}${INSTALL_PREFIX}/tools/v4l2loopback-helper"
fi

cat > "${PKG_ROOT}/DEBIAN/control" <<EOF
Package: ${PACKAGE_NAME}
Version: ${VERSION}
Section: net
Priority: optional
Architecture: ${ARCH}
Maintainer: ${MAINTAINER}
Description: ${DESCRIPTION}
EOF

cp -a "${POSTINST_TEMPLATE}" "${PKG_ROOT}/DEBIAN/postinst"
cp -a "${PRERM_TEMPLATE}" "${PKG_ROOT}/DEBIAN/prerm"

chmod 0755 "${PKG_ROOT}/DEBIAN"
chmod 0644 "${PKG_ROOT}/DEBIAN/control"
chmod 0755 "${PKG_ROOT}/DEBIAN/postinst"
chmod 0755 "${PKG_ROOT}/DEBIAN/prerm"

OUT_DEB="${OUTPUT_DIR}/LibreConnect-${VERSION}-${ARCH}.deb"
dpkg-deb --build --root-owner-group "${PKG_ROOT}" "${OUT_DEB}"

echo "DEB created: ${OUT_DEB}"
