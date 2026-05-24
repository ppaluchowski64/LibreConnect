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
PACKAGE_NAME="libreconnect"
ARCH="x86_64"
RELEASE="1"
MAINTAINER="LibreConnect"
DESCRIPTION="Privacy-focused peer-to-peer device connectivity app"
LONG_DESCRIPTION="LibreConnect links desktop and mobile devices over local, encrypted peer-to-peer connections"
HOMEPAGE="https://github.com/ppaluchowski64/LibreConnect"
LICENSE="GPL-3.0-only"
INSTALL_PREFIX="/opt/libreconnect"

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --deploy-dir <path>   AppDir path (default: ${DEPLOY_DIR_REL})
  --version <x.y.z>     Package version (default: ${VERSION})
  --release <n>         RPM release (default: ${RELEASE})
  --output-dir <path>   Output directory for .rpm (default: ${OUTPUT_DIR_REL})
  --package-name <name> RPM package name (default: ${PACKAGE_NAME})
  --arch <arch>         RPM architecture (default: ${ARCH})
  --maintainer <name>   Maintainer metadata (default: ${MAINTAINER})
  --description <text>  Summary field (default: ${DESCRIPTION})
  --homepage <url>      Homepage field (default: ${HOMEPAGE})
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

normalize_rpm_arch() {
    local arch="$1"
    case "$arch" in
        amd64)
            printf "x86_64\n"
            ;;
        arm64)
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
        --release)
            RELEASE="$2"
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
        --homepage)
            HOMEPAGE="$2"
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
DAEMON_EXE_NAME="LibreConnect-daemon"
LAUNCHER_REL="libreconnect-run.sh"
DAEMON_LAUNCHER_REL="libreconnect-daemon-run.sh"
CMAKE_BUILD_DIR="$(cd "${DEPLOY_DIR}/../../.." && pwd)"
V4L2_HELPER_BIN="${CMAKE_BUILD_DIR}/v4l2loopback-helper"
LINUX_INSTALL_SCRIPTS_DIR="${ROOT_DIR}/scripts/linux/install"
POST_TEMPLATE="${SCRIPT_DIR}/postrpm.sh"
PREUN_TEMPLATE="${SCRIPT_DIR}/preunrpm.sh"
SOURCE_ICON="${ROOT_DIR}/apps/desktop/res/libreconnect_logo.png"
ICON_NAME="libreconnect_logo.png"
LICENSE_FILE="${ROOT_DIR}/LICENSE"
RPM_ARCH="$(normalize_rpm_arch "$ARCH")"

if [[ ! -d "$DEPLOY_DIR" ]]; then
    echo "Deploy directory not found: $DEPLOY_DIR" >&2
    exit 1
fi

if [[ ! -f "$MAIN_EXE" ]]; then
    echo "Main executable not found: $MAIN_EXE" >&2
    exit 1
fi

if ! command -v rpmbuild >/dev/null 2>&1; then
    echo "rpmbuild not found. Install rpm-build first." >&2
    exit 1
fi

if [[ ! -d "$LINUX_INSTALL_SCRIPTS_DIR" ]]; then
    echo "Install scripts directory not found: $LINUX_INSTALL_SCRIPTS_DIR" >&2
    exit 1
fi

if [[ ! -f "$POST_TEMPLATE" || ! -f "$PREUN_TEMPLATE" ]]; then
    echo "RPM scriptlet templates missing in $SCRIPT_DIR" >&2
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

PAYLOAD_ROOT="${BUILD_ROOT}/payload"
RPM_TOPDIR="${BUILD_ROOT}/rpmbuild"
SPEC_FILE="${RPM_TOPDIR}/SPECS/${PACKAGE_NAME}.spec"

mkdir -p "${PAYLOAD_ROOT}/opt"
mkdir -p "${PAYLOAD_ROOT}/usr/bin"
mkdir -p "${PAYLOAD_ROOT}/etc/xdg/autostart"
mkdir -p "${PAYLOAD_ROOT}/usr/share/doc/${PACKAGE_NAME}"
mkdir -p "${PAYLOAD_ROOT}/usr/share/applications"
mkdir -p "${PAYLOAD_ROOT}/usr/share/icons/hicolor/512x512/apps"
mkdir -p "${PAYLOAD_ROOT}/usr/share/metainfo"
mkdir -p "${PAYLOAD_ROOT}${INSTALL_PREFIX}/tools"
mkdir -p "${PAYLOAD_ROOT}${INSTALL_PREFIX}/scripts/linux"
mkdir -p "${RPM_TOPDIR}/BUILD" "${RPM_TOPDIR}/BUILDROOT" "${RPM_TOPDIR}/RPMS" "${RPM_TOPDIR}/SOURCES" "${RPM_TOPDIR}/SPECS" "${RPM_TOPDIR}/SRPMS"

cp -a "${DEPLOY_DIR}/." "${PAYLOAD_ROOT}${INSTALL_PREFIX}/"
cp -a "${LINUX_INSTALL_SCRIPTS_DIR}" "${PAYLOAD_ROOT}${INSTALL_PREFIX}/scripts/linux/install"

PACKAGED_MAIN_EXE="${PAYLOAD_ROOT}${INSTALL_PREFIX}/usr/bin/LibreConnect"
if [[ ! -f "$PACKAGED_MAIN_EXE" ]]; then
    echo "Main executable missing in payload root: $PACKAGED_MAIN_EXE" >&2
    exit 1
fi

chmod 0755 "${PACKAGED_MAIN_EXE}"
if [[ -f "${PAYLOAD_ROOT}${INSTALL_PREFIX}/AppRun" ]]; then
    chmod 0755 "${PAYLOAD_ROOT}${INSTALL_PREFIX}/AppRun"
fi
if [[ -d "${PAYLOAD_ROOT}${INSTALL_PREFIX}/scripts/linux/install" ]]; then
    find "${PAYLOAD_ROOT}${INSTALL_PREFIX}/scripts/linux/install" -type f -name "*.sh" -exec chmod 0755 {} +
fi

cat > "${PAYLOAD_ROOT}${INSTALL_PREFIX}/${LAUNCHER_REL}" <<EOF
#!/usr/bin/env bash
set -euo pipefail

APPDIR="${INSTALL_PREFIX}"
APPRUN="\${APPDIR}/AppRun"
MAIN_EXE="\${APPDIR}/usr/bin/LibreConnect"

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
chmod 0755 "${PAYLOAD_ROOT}${INSTALL_PREFIX}/${LAUNCHER_REL}"

cat > "${PAYLOAD_ROOT}${INSTALL_PREFIX}/${DAEMON_LAUNCHER_REL}" <<EOF
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
chmod 0755 "${PAYLOAD_ROOT}${INSTALL_PREFIX}/${DAEMON_LAUNCHER_REL}"

cat > "${PAYLOAD_ROOT}/usr/bin/libreconnect" <<EOF
#!/usr/bin/env bash
exec "${INSTALL_PREFIX}/${LAUNCHER_REL}" "\$@"
EOF
chmod 0755 "${PAYLOAD_ROOT}/usr/bin/libreconnect"

cat > "${PAYLOAD_ROOT}/usr/bin/libreconnect-daemon" <<EOF
#!/usr/bin/env bash
exec "${INSTALL_PREFIX}/${DAEMON_LAUNCHER_REL}" "\$@"
EOF
chmod 0755 "${PAYLOAD_ROOT}/usr/bin/libreconnect-daemon"

cat > "${PAYLOAD_ROOT}/usr/share/applications/libreconnect.desktop" <<EOF
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

cat > "${PAYLOAD_ROOT}/etc/xdg/autostart/libreconnect-daemon.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=LibreConnect Daemon
Comment=Start the LibreConnect background daemon
Exec=libreconnect-daemon
Terminal=false
X-GNOME-Autostart-enabled=true
NoDisplay=true
EOF

cat > "${PAYLOAD_ROOT}/usr/share/metainfo/com.libreconnect.desktop.metainfo.xml" <<EOF
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
  <developer_name>${MAINTAINER}</developer_name>
  <categories>
    <category>Network</category>
    <category>Utility</category>
  </categories>
  <provides>
    <binary>libreconnect</binary>
  </provides>
</component>
EOF

if [[ -d "${DEPLOY_DIR}/usr/share/icons/hicolor" ]]; then
    cp -a "${DEPLOY_DIR}/usr/share/icons/hicolor/." \
        "${PAYLOAD_ROOT}/usr/share/icons/hicolor/"
fi

if [[ ! -f "${PAYLOAD_ROOT}/usr/share/icons/hicolor/512x512/apps/${ICON_NAME}" ]]; then
    install -Dm0644 "${SOURCE_ICON}" \
        "${PAYLOAD_ROOT}/usr/share/icons/hicolor/512x512/apps/${ICON_NAME}"
fi

if [[ ! -s "${PAYLOAD_ROOT}/usr/share/icons/hicolor/512x512/apps/${ICON_NAME}" ]]; then
    echo "Packaged icon is missing or empty: /usr/share/icons/hicolor/512x512/apps/${ICON_NAME}" >&2
    exit 1
fi

if [[ -f "${V4L2_HELPER_BIN}" ]]; then
    cp -a "${V4L2_HELPER_BIN}" "${PAYLOAD_ROOT}${INSTALL_PREFIX}/tools/v4l2loopback-helper"
    chmod 0755 "${PAYLOAD_ROOT}${INSTALL_PREFIX}/tools/v4l2loopback-helper"
fi

cp -a "${LICENSE_FILE}" "${PAYLOAD_ROOT}/usr/share/doc/${PACKAGE_NAME}/GPL-3"

cat > "${PAYLOAD_ROOT}/usr/share/doc/${PACKAGE_NAME}/copyright" <<EOF
Upstream-Name: LibreConnect
Source: ${HOMEPAGE}

Files: *
Copyright: LibreConnect contributors
License: GPL-3.0-only
EOF

{
    cat <<EOF
%global __os_install_post %{nil}
%global debug_package %{nil}

Name: ${PACKAGE_NAME}
Version: ${VERSION}
Release: ${RELEASE}%{?dist}
Summary: ${DESCRIPTION}
License: ${LICENSE}
URL: ${HOMEPAGE}
AutoReqProv: no
Requires: bash
Requires: dkms
Requires: gcc
Requires: git
Requires: kernel-devel
Requires: libv4l-devel
Requires: make
Requires: wl-clipboard

%description
${LONG_DESCRIPTION}

%prep

%build

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}
cp -a ${PAYLOAD_ROOT}/. %{buildroot}/

%post
EOF
    cat "${POST_TEMPLATE}"
    cat <<EOF

%preun
EOF
    cat "${PREUN_TEMPLATE}"
    cat <<EOF

%files
%defattr(-,root,root,-)
/opt/libreconnect
/usr/bin/libreconnect
/usr/bin/libreconnect-daemon
/usr/share/applications/libreconnect.desktop
/usr/share/icons/hicolor
/usr/share/metainfo/com.libreconnect.desktop.metainfo.xml
/etc/xdg/autostart/libreconnect-daemon.desktop
%license /usr/share/doc/${PACKAGE_NAME}/GPL-3
%doc /usr/share/doc/${PACKAGE_NAME}/copyright

%changelog
* $(LC_ALL=C date "+%a %b %d %Y") ${MAINTAINER} - ${VERSION}-${RELEASE}
- Automated Fedora package build for LibreConnect
EOF
} > "${SPEC_FILE}"

# Ensure rpmbuild knows that our host architecture is compatible with the target CPU for packaging
# on environments where the architecture mapping is sparse (like Ubuntu/Debian runners).
HOST_ARCH="$(uname -m)"
if [[ "${RPM_ARCH}" != "noarch" && "${RPM_ARCH}" != "${HOST_ARCH}" ]]; then
    touch ~/.rpmrc
    if ! grep -q "buildarch_compat: ${HOST_ARCH}: ${RPM_ARCH}" ~/.rpmrc; then
        echo "buildarch_compat: ${HOST_ARCH}: ${RPM_ARCH}" >> ~/.rpmrc
        echo "buildarch_compat: x86_64: ${RPM_ARCH}" >> ~/.rpmrc
        echo "buildarch_compat: amd64: ${RPM_ARCH}" >> ~/.rpmrc
    fi
fi

rpmbuild --target "${RPM_ARCH}" --define "_topdir ${RPM_TOPDIR}" -bb "${SPEC_FILE}"

OUT_RPM_SOURCE="$(find "${RPM_TOPDIR}/RPMS" -type f -name "*.rpm" | head -n 1)"
if [[ -z "${OUT_RPM_SOURCE}" ]]; then
    echo "RPM build completed but no output package was found." >&2
    exit 1
fi

OUT_RPM="${OUTPUT_DIR}/LibreConnect-${VERSION}-${RPM_ARCH}.rpm"
cp -a "${OUT_RPM_SOURCE}" "${OUT_RPM}"

echo "RPM created: ${OUT_RPM}"
