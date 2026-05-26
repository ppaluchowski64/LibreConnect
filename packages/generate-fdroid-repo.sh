#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

assets_dir="${1:-release-assets}"
site_dir="${2:-site}"
work_dir="${3:-fdroid-work}"
icon_source="$PROJECT_ROOT/android/res/mipmap-xxxhdpi/ic_launcher.png"

require_command fdroid
require_env FDROID_KEYSTORE_BASE64
require_env FDROID_KEYSTORE_PASSWORD
FDROID_KEY_ALIAS="libreconnect"

apk="$(first_match "$assets_dir" '*.apk')"
require_file "$apk"
require_file "$icon_source"

clean_dir "$work_dir"
clean_dir "$site_dir/fdroid"
mkdir -p "$work_dir/repo/icons" "$work_dir/repo/$ANDROID_APP_ID/en-US" "$work_dir/metadata/$ANDROID_APP_ID/en-US"

cp -a "$apk" "$work_dir/repo/"
cp -a "$icon_source" "$work_dir/icon.png"
cp -a "$icon_source" "$work_dir/repo/icons/icon.png"
cp -a "$icon_source" "$work_dir/repo/$ANDROID_APP_ID/en-US/icon.png"
cp -a "$icon_source" "$work_dir/metadata/$ANDROID_APP_ID/en-US/icon.png"

keystore_path="$(realpath -m "$work_dir/fdroid-repo.jks")"
printf '%s' "$FDROID_KEYSTORE_BASE64" | base64 --decode > "$keystore_path"
chmod 600 "$keystore_path"

cat > "$work_dir/config.yml" <<EOF_CONFIG
repo_name: "$DISPLAY_NAME"
repo_description: "$LONG_DESCRIPTION"
repo_icon: "icon.png"
repo_url: "$REPO_URL/fdroid/repo"
archive_name: "$DISPLAY_NAME Archive"
archive_description: "$DISPLAY_NAME archived packages"
archive_url: "$REPO_URL/fdroid/archive"
repo_keyalias: "$FDROID_KEY_ALIAS"
keystore: "$keystore_path"
keystorepass: "$FDROID_KEYSTORE_PASSWORD"
keypass: "$FDROID_KEYSTORE_PASSWORD"
keydname: "CN=$DISPLAY_NAME, OU=$DISPLAY_NAME, O=$DISPLAY_NAME, L=Internet, ST=Internet, C=US"
make_current_version_link: false
archive_older: 0
EOF_CONFIG
chmod 600 "$work_dir/config.yml"

cat > "$work_dir/metadata/$ANDROID_APP_ID.yml" <<EOF_METADATA
Categories:
  - Connectivity
License: GPL-3.0-only
AuthorName: $DISTRIBUTOR_NAME
WebSite: $REPO_URL
SourceCode: $PROJECT_URL
IssueTracker: $PROJECT_URL/issues
Name: $DISPLAY_NAME
Summary: $DESCRIPTION
Description: |-
  $LONG_DESCRIPTION

AutoName: $DISPLAY_NAME
AllowedAPKSigningKeys: []
EOF_METADATA

(
    cd "$work_dir"
    fdroid update --verbose --create-metadata
)

cp -a "$work_dir/repo" "$site_dir/fdroid/repo"

if [[ -d "$work_dir/archive" ]]; then
    cp -a "$work_dir/archive" "$site_dir/fdroid/archive"
fi

cat > "$site_dir/fdroid/README.txt" <<EOF_README
LibreConnect F-Droid repository

Repository URL:
$REPO_URL/fdroid/repo
EOF_README
