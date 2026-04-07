#!/usr/bin/env bash
set -e

if [[ "${1:-}" = "remove" || "${1:-}" = "purge" ]]; then
    rm -f /usr/libexec/v4l2loopback-helper || true
    rm -f /usr/share/polkit-1/rules.d/50-v4l2loopback.rules || true
    rm -f /usr/share/polkit-1/actions/org.example.v4l2loopback.policy || true
    systemctl restart polkit || true
fi

exit 0
