#!/usr/bin/env bash

set -uo pipefail

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

command_exists() {
    command -v "$1" >/dev/null 2>&1
}

matches_distro() {
    local needle="$1"
    if [[ "${ID:-}" == "$needle" ]]; then
        return 0
    fi

    local like_value=" ${ID_LIKE:-} "
    [[ "$like_value" == *" ${needle} "* ]]
}

if [[ -f "$HELPER_PATH" ]]; then
    echo "Installing v4l2loopback-helper to /usr/libexec/..."
    run_as_root install -Dm0755 "$HELPER_PATH" /usr/libexec/v4l2loopback-helper

    run_as_root mkdir -p /usr/share/polkit-1/rules.d /usr/share/polkit-1/actions

    cat <<'EOF' | run_as_root tee /usr/share/polkit-1/rules.d/50-v4l2loopback.rules >/dev/null
polkit.addRule(function(action, subject) {
    if ((action.id === "org.example.v4l2loopback.manage" || (action.id === "org.freedesktop.policykit.exec" &&
    action.lookup("program") == "/usr/libexec/v4l2loopback-helper")) &&
        (subject.isInGroup("sudo") || subject.isInGroup("wheel"))) {
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

    restart_polkit() {
        if command -v systemctl >/dev/null 2>&1; then
            run_as_root systemctl restart polkit.service 2>/dev/null || true
        elif command -v rc-service >/dev/null 2>&1; then
            run_as_root rc-service polkit restart 2>/dev/null || true
        elif command -v sv >/dev/null 2>&1; then
            run_as_root sv restart polkit 2>/dev/null || true
        elif command -v service >/dev/null 2>&1; then
            run_as_root service polkit restart 2>/dev/null || true
        fi
    }

    restart_polkit

    run_as_root systemctl restart polkit || true
    echo "v4l2loopback-helper installed successfully."
else
    echo "WARNING: v4l2loopback-helper not found at: $HELPER_PATH (skipping helper install)"
fi


install_dependencies_debian() {
    run_as_root apt update
    run_as_root apt install -y \
        dkms \
        git \
        build-essential \
        "linux-headers-$(uname -r)" \
        libv4l-dev
}

install_dependencies_fedora() {
    run_as_root dnf install -y \
        dkms \
        git \
        gcc \
        make \
        "kernel-devel-$(uname -r)" \
        libv4l-devel || run_as_root dnf install -y \
        dkms \
        git \
        gcc \
        make \
        kernel-devel \
        libv4l-devel
}

if [[ "$SKIP_PACKAGE_INSTALL" != "1" ]]; then
    if [[ -f /etc/os-release ]]; then
        source /etc/os-release
        if matches_distro debian; then
            install_dependencies_debian
        elif matches_distro fedora || matches_distro rhel; then
            install_dependencies_fedora
        else
            echo "Unsupported distro for auto dependency install (${ID:-unknown}). Skipping package install."
        fi
    fi
else
    echo "Package installation disabled (LIBRECONNECT_SKIP_PACKAGE_INSTALL=1)."
fi


MISSING_CMDS=()
for required_cmd in dkms git make modprobe; do
    if ! command_exists "$required_cmd"; then
        MISSING_CMDS+=("$required_cmd")
    fi
done

if [[ ${#MISSING_CMDS[@]} -gt 0 ]]; then
    echo "Skipping v4l2loopback kernel module build (missing: ${MISSING_CMDS[*]})."
    echo "Install v4l2loopback-dkms via your package manager, or install the missing tools and re-run this script."
    exit 0
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
