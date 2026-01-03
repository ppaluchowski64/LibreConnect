#!/usr/bin/env bash

ROOT_DIR="$(pwd)"

# install v4l2loopback kernel module

set -e

MODULE="v4l2loopback"
VERSION="git"
SRC="/usr/src/${MODULE}-${VERSION}"

sudo apt update
sudo apt install -y \
    dkms \
    git \
    build-essential \
    linux-headers-$(uname -r) \
    libv4l-dev

sudo apt remove -y v4l2loopback-dkms v4l2loopback-utils || true

dkms status | awk '/v4l2loopback\/(?!git)/ {print $1, $2}' | while read -r mod ver; do
    sudo dkms remove "$mod/$ver" --all || true
done

if [ ! -d "$SRC" ]; then
    sudo git clone https://github.com/v4l2loopback/v4l2loopback.git "$SRC"
fi

if ! dkms status | grep -q "^v4l2loopback/git"; then
    sudo dkms add -m "$MODULE" -v "$VERSION"
fi

sudo dkms build -m "$MODULE" -v "$VERSION"
sudo dkms install -m "$MODULE" -v "$VERSION"

cd "$SRC/utils"
make
sudo install -m 0755 v4l2loopback-ctl /usr/local/bin/v4l2loopback-ctl

sudo modprobe -r v4l2loopback || true
sudo modprobe v4l2loopback exclusive_caps=1

cd "$ROOT_DIR"

# install v4l2loopback-helper

sudo install -m 755 v4l2loopback-helper /usr/libexec/

sudo bash -c 'cat > /usr/share/polkit-1/rules.d/50-v4l2loopback.rules <<EOF
polkit.addRule(function(action, subject) {
    if ((action.id === "org.example.v4l2loopback.manage" || (action.id === "org.freedesktop.policykit.exec" &&
    action.lookup("program") == "/usr/libexec/v4l2loopback-helper")) &&
        subject.isInGroup("sudo")) {
        return polkit.Result.YES;
    }
});
EOF'

sudo bash -c 'cat > /usr/share/polkit-1/actions/org.example.v4l2loopback.policy <<EOF
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
EOF'

sudo systemctl restart polkit || true