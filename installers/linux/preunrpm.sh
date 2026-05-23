set -e

if [ "${1:-0}" -eq 0 ]; then
    rm -f /usr/libexec/v4l2loopback-helper || true
    rm -f /usr/share/polkit-1/rules.d/50-v4l2loopback.rules || true
    rm -f /usr/share/polkit-1/actions/org.example.v4l2loopback.policy || true
    systemctl restart polkit || true
    pkill -f "/usr/bin/libreconnect-daemon" || true
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi

exit 0
