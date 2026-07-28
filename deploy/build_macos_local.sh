#!/bin/bash
# ---------------------------------------------------------------------------
# Local unsigned macOS build
#
# This script builds the legacy macOS app bundle without any code signing.
# Use it for local development or for creating an unsigned artifact that can
# be signed later.
#
# Requirements:
#   - Qt 6.10.1 for macOS desktop
#   - cmake, ninja, macdeployqt
#
# Usage:
#   export QT_BIN_DIR="$HOME/Qt/6.10.1/macos/bin"
#   bash deploy/build_macos_local.sh
# ---------------------------------------------------------------------------

set -o errexit -o nounset

PROJECT_DIR=$(pwd)
DEPLOY_DIR="$PROJECT_DIR/deploy"
BUILD_DIR="$DEPLOY_DIR/build-macos-local"
APP_NAME=dopamine
APP_FILENAME="$APP_NAME.app"

echo "Project dir: $PROJECT_DIR"
echo "Build dir: $BUILD_DIR"

if [ -z "${QT_VERSION+x}" ]; then
  QT_VERSION=6.10.1
fi

QT_BIN_DIR="${QT_BIN_DIR:-$HOME/Qt/$QT_VERSION/macos/bin}"
echo "Using Qt in $QT_BIN_DIR"

"$QT_BIN_DIR/qt-cmake" --version
cmake --version

# Clean and configure
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

"$QT_BIN_DIR/qt-cmake" -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja

cmake --build "$BUILD_DIR" --config release --target all

BUNDLE_DIR="$BUILD_DIR/client/$APP_FILENAME"

echo "Deploying Qt frameworks..."
"$QT_BIN_DIR/macdeployqt" "$BUNDLE_DIR" -always-overwrite -qmldir="$PROJECT_DIR/client"

echo "Copying service binary..."
cp -v "$BUILD_DIR/service/server/$APP_NAME-service" "$BUNDLE_DIR/Contents/macOS/"

echo "Copying deployment data..."
rsync -av \
  --exclude="dopamine.plist" \
  --exclude=post_install.sh \
  --exclude=post_uninstall.sh \
  "$PROJECT_DIR/deploy/data/macos/" "$BUNDLE_DIR/Contents/macOS/"

echo ""
echo "Finished. Unsigned bundle: $BUNDLE_DIR"
echo ""
echo "To run locally (unsigned apps may require allowing in System Settings):"
echo "  $BUNDLE_DIR/Contents/MacOS/Dopamine"
