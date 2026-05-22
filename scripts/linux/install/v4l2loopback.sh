#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKIP_PACKAGE_INSTALL="${LIBRECONNECT_SKIP_PACKAGE_INSTALL:-0}"
HELPER_PATH="${V4L2_HELPER_PATH:-${SCRIPT_DIR}/../../../tools/v4l2loopback-helper}"

run_as_root() {
    if [[ "$(id -u)" -eq 0 ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

install_dependencies_debian() {
    run_as_root apt update
    run_as_root apt install -y \
        dkms \
        git \
        build-essential \
        "linux-headers-$(uname -r)" \
        libv4l-dev
}

if [[ "$SKIP_PACKAGE_INSTALL" != "1" ]]; then
    if [[ -f /etc/os-release ]]; then
        # shellcheck disable=SC1091
        source /etc/os-release
        case "${ID:-}" in
            ubuntu|debian|linuxmint|pop|elementary)
                install_dependencies_debian
                ;;
            *)
                echo "Unsupported distro for auto dependency install (${ID:-unknown}). Skipping package install."
                ;;
        esac
    fi
else
    echo "Package installation disabled (LIBRECONNECT_SKIP_PACKAGE_INSTALL=1)."
fi

MODULE="v4l2loopback"
VERSION="git"
SRC="/usr/src/${MODULE}-${VERSION}"

if [[ ! -d "$SRC" ]]; then
    run_as_root git clone https://github.com/v4l2loopback/v4l2loopback.git "$SRC"
fi

if ! dkms status | grep -q "^${MODULE}/${VERSION}"; then
    run_as_root dkms add -m "$MODULE" -v "$VERSION"
fi

run_as_root dkms build -m "$MODULE" -v "$VERSION"
run_as_root dkms install -m "$MODULE" -v "$VERSION"

if [[ -d "$SRC/utils" ]]; then
    make -C "$SRC/utils"
    if [[ -f "$SRC/utils/v4l2loopback-ctl" ]]; then
        run_as_root install -m 0755 "$SRC/utils/v4l2loopback-ctl" /usr/local/bin/v4l2loopback-ctl
    fi
fi

run_as_root mkdir -p /etc/modprobe.d
cat <<'EOF' | run_as_root tee /etc/modprobe.d/v4l2loopback.conf >/dev/null
options v4l2loopback exclusive_caps=1
EOF

run_as_root modprobe -r v4l2loopback || true
run_as_root modprobe v4l2loopback || true

if [[ -f "$HELPER_PATH" ]]; then
    run_as_root install -Dm0755 "$HELPER_PATH" /usr/libexec/v4l2loopback-helper

    run_as_root mkdir -p /usr/share/polkit-1/rules.d /usr/share/polkit-1/actions

    cat <<'EOF' | run_as_root tee /usr/share/polkit-1/rules.d/50-v4l2loopback.rules >/dev/null
polkit.addRule(function(action, subject) {
    if ((action.id === "org.example.v4l2loopback.manage" || (action.id === "org.freedesktop.policykit.exec" &&
    action.lookup("program") == "/usr/libexec/v4l2loopback-helper")) &&
        subject.isInGroup("sudo")) {
        return polkit.Result.YES;
    }
});
EOF

    cat <<'EOF' | run_as_root tee /usr/share/polkit-1/actions/org.example.v4l2loopback.policy >/dev/null
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE policyconfig PUBLIC
 "-//freedesktop//DTD PolicyKit Policy Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/PolicyKit/1/policyconfig.dtd">
<policyconfig>
  <action id="org.example.v4l2loopback.manage">
    <description>Create or remove virtual cameras</description>
    <message>Authentication is required to manage virtual cameras</message>
    <defaults>
      <allow_any>no</allow_any>
      <allow_inactive>no</allow_inactive>
      <allow_active>yes</allow_active>
    </defaults>
    <annotate key="org.freedesktop.policykit.exec.path">/usr/libexec/v4l2loopback-helper</annotate>
  </action>
</policyconfig>
EOF

    run_as_root systemctl restart polkit || true
else
    echo "v4l2loopback-helper not found at: $HELPER_PATH"
fi
