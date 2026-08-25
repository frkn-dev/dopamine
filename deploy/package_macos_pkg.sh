#!/bin/bash
# ---------------------------------------------------------------------------
# Package an already-built local macOS bundle into an installer PKG.
#
# The PKG is required (a bare DMG is not enough): the app needs its privileged
# helper service in /Library/LaunchDaemons, and only the installer's
# postinstall script runs with the root rights to put it there.
#
# Unsigned by design (no Developer ID in the team yet) — recipients open it
# via right-click -> Open / "Open Anyway" in Privacy & Security.
#
# Usage (from the repo root):
#   bash deploy/package_macos_pkg.sh <build-dir> [output-name]
# e.g.
#   bash deploy/package_macos_pkg.sh deploy/build-macos-local-arm64 Dopamine-arm64.pkg
# ---------------------------------------------------------------------------

set -o errexit -o nounset

PROJECT_DIR=$(pwd)
BUILD_DIR="$1"
OUT_NAME="${2:-Dopamine.pkg}"

APP_NAME=dopamine
APP_FILENAME=$APP_NAME.app
APP_DOMAIN=org.frkn.dopamine.package
PLIST_NAME=$APP_NAME.plist

BUNDLE_DIR="$BUILD_DIR/client/$APP_FILENAME"
DEPLOY_DATA_DIR=$PROJECT_DIR/deploy/data/macos

APP_VERSION=$(grep -m1 -E 'set\(DOPAMINE_VERSION' "$PROJECT_DIR/CMakeLists.txt" | sed -E 's/.*DOPAMINE_VERSION ([0-9.]+).*/\1/')
echo "Packaging $BUNDLE_DIR as $OUT_NAME (version $APP_VERSION)"

# launchd plist must live in Resources (post_install.sh moves it into
# /Library/LaunchDaemons from there)
mkdir -p "$BUNDLE_DIR/Contents/Resources"
cp "$DEPLOY_DATA_DIR/$PLIST_NAME" "$BUNDLE_DIR/Contents/Resources/$PLIST_NAME"

# re-seal after adding the plist (ad-hoc)
codesign --deep --force --sign - "$BUNDLE_DIR"

PKG_DIR="$BUILD_DIR/pkg"
rm -rf "$PKG_DIR"
PKG_ROOT=$PKG_DIR/root
SCRIPTS_DIR=$PKG_DIR/scripts
RESOURCES_DIR=$PKG_DIR/resources
UNINSTALL_SCRIPTS_DIR=$PKG_DIR/uninstall_scripts
INSTALL_PKG=$PKG_DIR/${APP_NAME}_install.pkg
UNINSTALL_COMPONENT_PKG=$PKG_DIR/${APP_NAME}_uninstall_component.pkg
FINAL_PKG=$PKG_DIR/$OUT_NAME

mkdir -p "$PKG_ROOT/Applications" "$SCRIPTS_DIR" "$RESOURCES_DIR" "$UNINSTALL_SCRIPTS_DIR"

cp -R "$BUNDLE_DIR" "$PKG_ROOT/Applications"

cp "$DEPLOY_DATA_DIR/post_install.sh" "$SCRIPTS_DIR/post_install.sh"
cp "$DEPLOY_DATA_DIR/post_uninstall.sh" "$UNINSTALL_SCRIPTS_DIR/postinstall"
mkdir -p "$RESOURCES_DIR/scripts"
cp "$DEPLOY_DATA_DIR/check_install.sh" "$RESOURCES_DIR/scripts/check_install.sh"
cp "$DEPLOY_DATA_DIR/check_uninstall.sh" "$RESOURCES_DIR/scripts/check_uninstall.sh"

cat > "$SCRIPTS_DIR/postinstall" <<'EOS'
#!/bin/bash
SCRIPT_DIR="$(dirname "$0")"
bash "$SCRIPT_DIR/post_install.sh"
exit 0
EOS

chmod +x "$SCRIPTS_DIR"/* "$UNINSTALL_SCRIPTS_DIR"/* "$RESOURCES_DIR/scripts"/*
cp "$PROJECT_DIR/LICENSE" "$RESOURCES_DIR/LICENSE"

# keep the app pinned to /Applications (no bundle relocation)
COMPONENT_PLIST="$PKG_DIR/component.plist"
pkgbuild --analyze --root "$PKG_ROOT" "$COMPONENT_PLIST"
plutil -convert xml1 "$COMPONENT_PLIST"
for bundle_key in $(/usr/libexec/PlistBuddy -c "Print" "$COMPONENT_PLIST" | awk '/^[ \t]*[A-Za-z0-9].*\.app/ {print $1}'); do
  /usr/libexec/PlistBuddy -c "Set :'${bundle_key}':BundleIsRelocatable false" "$COMPONENT_PLIST" || true
done

echo "Building component package..."
pkgbuild --root "$PKG_ROOT" \
         --identifier "$APP_DOMAIN" \
         --version "$APP_VERSION" \
         --install-location "/" \
         --scripts "$SCRIPTS_DIR" \
         --component-plist "$COMPONENT_PLIST" \
         "$INSTALL_PKG"

echo "Building uninstaller component package..."
pkgbuild --nopayload \
         --identifier "org.frkn.dopamine.uninstall" \
         --version "$APP_VERSION" \
         --scripts "$UNINSTALL_SCRIPTS_DIR" \
         "$UNINSTALL_COMPONENT_PKG"

echo "Creating installer $FINAL_PKG ..."
productbuild --distribution "$DEPLOY_DATA_DIR/distribution.xml" \
             --package-path "$PKG_DIR" \
             --resources "$RESOURCES_DIR" \
             "$FINAL_PKG"

echo ""
echo "Finished. Unsigned installer: $FINAL_PKG"
