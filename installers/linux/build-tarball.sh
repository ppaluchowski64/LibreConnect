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
MAIN_EXE="${DEPLOY_DIR}/usr/bin/LibreConnect"
CMAKE_BUILD_DIR="$(cd "${DEPLOY_DIR}/../../.." && pwd)"
V4L2_HELPER_BIN="${CMAKE_BUILD_DIR}/v4l2loopback-helper"
LINUX_INSTALL_SCRIPTS_DIR="${ROOT_DIR}/scripts/linux/install"
INSTALL_TEMPLATE="${SCRIPT_DIR}/install.sh"
UNINSTALL_TEMPLATE="${SCRIPT_DIR}/uninstall.sh"
SOURCE_ICON="${ROOT_DIR}/apps/desktop/res/libreconnect_logo.png"
LICENSE_FILE="${ROOT_DIR}/LICENSE"

if [[ ! -d "$DEPLOY_DIR" ]]; then
    echo "Deploy directory not found: $DEPLOY_DIR" >&2
    exit 1
fi

if [[ ! -f "$MAIN_EXE" ]]; then
    echo "Main executable not found: $MAIN_EXE" >&2
    exit 1
fi

if [[ ! -d "$LINUX_INSTALL_SCRIPTS_DIR" ]]; then
    echo "Install scripts directory not found: $LINUX_INSTALL_SCRIPTS_DIR" >&2
    exit 1
fi

if [[ ! -f "$INSTALL_TEMPLATE" || ! -f "$UNINSTALL_TEMPLATE" ]]; then
    echo "Installer script templates missing in $SCRIPT_DIR" >&2
    exit 1
fi

if [[ ! -f "$SOURCE_ICON" ]]; then
    echo "Source icon not found: $SOURCE_ICON" >&2
    exit 1
fi

if [[ ! -f "$LICENSE_FILE" ]]; then
    echo "License file not found: $LICENSE_FILE" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

BUILD_ROOT="$(mktemp -d)"
trap 'rm -rf "$BUILD_ROOT"' EXIT

# Create standard package name and layout
PKG_DIR_NAME="libreconnect-${VERSION}-${ARCH}"
PKG_ROOT="${BUILD_ROOT}/${PKG_DIR_NAME}"

mkdir -p "${PKG_ROOT}/app"
mkdir -p "${PKG_ROOT}/scripts"
mkdir -p "${PKG_ROOT}/res"

# Copy payload elements
echo "Preparing package layout..."
cp -a "${DEPLOY_DIR}/." "${PKG_ROOT}/app/"

if [[ -f "${V4L2_HELPER_BIN}" ]]; then
    echo "Including v4l2loopback-helper..."
    mkdir -p "${PKG_ROOT}/app/tools"
    cp -a "${V4L2_HELPER_BIN}" "${PKG_ROOT}/app/tools/v4l2loopback-helper"
    chmod 0755 "${PKG_ROOT}/app/tools/v4l2loopback-helper"
fi

cp -a "${LINUX_INSTALL_SCRIPTS_DIR}/." "${PKG_ROOT}/scripts/"
cp -a "${SOURCE_ICON}" "${PKG_ROOT}/res/libreconnect_logo.png"
cp -a "${LICENSE_FILE}" "${PKG_ROOT}/LICENSE"
cp -a "${INSTALL_TEMPLATE}" "${PKG_ROOT}/install.sh"
cp -a "${UNINSTALL_TEMPLATE}" "${PKG_ROOT}/uninstall.sh"

# Set executable bits on installers
chmod 0755 "${PKG_ROOT}/install.sh"
chmod 0755 "${PKG_ROOT}/uninstall.sh"
if [[ -d "${PKG_ROOT}/scripts" ]]; then
    find "${PKG_ROOT}/scripts" -type f -name "*.sh" -exec chmod 0755 {} +
fi

# Build tarball
OUT_TARBALL="${OUTPUT_DIR}/LibreConnect-${VERSION}-${ARCH}.tar.gz"
echo "Creating compressed tarball: ${OUT_TARBALL}..."

cd "$BUILD_ROOT"
tar -czf "$OUT_TARBALL" "$PKG_DIR_NAME"

echo "Universal tarball package successfully created!"
