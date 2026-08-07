#!/usr/bin/env bash
#
# Ion Engine - .app bundler for developers.
#
# Builds an executable and assembles a macOS application bundle so it can be
# launched from Finder with the proper name and icon.
#
# Usage:
#   ./scripts/build_app.sh [APP_NAME] [TARGET] [BUILD_TYPE]
#
#   APP_NAME    bundle/display name, default: Ion
#   TARGET      cmake target to bundle, default: basic_example
#   BUILD_TYPE  Debug or Release, default: Debug
#
set -euo pipefail

APP_NAME="${1:-Ion}"
TARGET="${2:-basic_example}"
BUILD_TYPE="${3:-Debug}"

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
APP_DIR="$BUILD_DIR/$APP_NAME.app"
CONTENTS="$APP_DIR/Contents"
RESOURCES="$CONTENTS/Resources"
MACOS_DIR="$CONTENTS/MacOS"

ICON_SOURCE="$ROOT_DIR/assets/icons/macos/ion-iOS-Default-1024x1024@1x.png"
if [[ ! -f "$ICON_SOURCE" ]]; then
    ICON_SOURCE="$ROOT_DIR/assets/ion_default_window_icon.png"
fi

VERSION_MAJOR=0
VERSION_MINOR=1
VERSION_PATCH=0

echo "==> Building ($BUILD_TYPE)"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE"

BINARY="$(find "$BUILD_DIR" -type f -perm -111 -name "$TARGET" | head -n 1)"
if [[ -z "$BINARY" ]]; then
    echo "error: could not locate built binary for target '$TARGET'" >&2
    exit 1
fi

echo "==> Assembling $APP_DIR"
rm -rf "$APP_DIR"
mkdir -p "$MACOS_DIR" "$RESOURCES"

cp "$BINARY" "$MACOS_DIR/$APP_NAME"

echo "==> Generating icon.icns"
ICONSET_DIR="$BUILD_DIR/icon.iconset"
rm -rf "$ICONSET_DIR"
mkdir -p "$ICONSET_DIR"

sips -z 16 16 "$ICON_SOURCE" --out "$ICONSET_DIR/icon_16x16.png" >/dev/null
sips -z 32 32 "$ICON_SOURCE" --out "$ICONSET_DIR/icon_16x16@2x.png" >/dev/null
sips -z 32 32 "$ICON_SOURCE" --out "$ICONSET_DIR/icon_32x32.png" >/dev/null
sips -z 64 64 "$ICON_SOURCE" --out "$ICONSET_DIR/icon_32x32@2x.png" >/dev/null
sips -z 128 128 "$ICON_SOURCE" --out "$ICONSET_DIR/icon_128x128.png" >/dev/null
sips -z 256 256 "$ICON_SOURCE" --out "$ICONSET_DIR/icon_128x128@2x.png" >/dev/null
sips -z 256 256 "$ICON_SOURCE" --out "$ICONSET_DIR/icon_256x256.png" >/dev/null
sips -z 512 512 "$ICON_SOURCE" --out "$ICONSET_DIR/icon_256x256@2x.png" >/dev/null
sips -z 512 512 "$ICON_SOURCE" --out "$ICONSET_DIR/icon_512x512.png" >/dev/null
cp "$ICON_SOURCE" "$ICONSET_DIR/icon_512x512@2x.png"
iconutil -c icns "$ICONSET_DIR" -o "$RESOURCES/icon.icns"
rm -rf "$ICONSET_DIR"

echo "==> Bundling assets"
cp -R "$ROOT_DIR/assets" "$RESOURCES/assets"

echo "==> Writing Info.plist"
cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>$APP_NAME</string>
    <key>CFBundleDisplayName</key>
    <string>$APP_NAME</string>
    <key>CFBundleExecutable</key>
    <string>$APP_NAME</string>
    <key>CFBundleIdentifier</key>
    <string>com.ionengine.$APP_NAME</string>
    <key>CFBundleVersion</key>
    <string>$VERSION_MAJOR.$VERSION_MINOR.$VERSION_PATCH</string>
    <key>CFBundleShortVersionString</key>
    <string>$VERSION_MAJOR.$VERSION_MINOR.$VERSION_PATCH</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleIconFile</key>
    <string>icon</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
</dict>
</plist>
PLIST

echo "==> Done: $APP_DIR"
echo "    Launch with: open '$APP_DIR'"
