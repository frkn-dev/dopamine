#!/bin/bash
# ---------------------------------------------------------------------------
# Package an already-built local macOS bundle into an installer PKG.
#
# The PKG is required (a bare DMG is not enough): the app needs its privileged
# helper service in /Library/LaunchDaemons, and only the installer's
# postinstall script runs with the root rights to put it there.
#
# When the Developer ID identities are present in the keychain, the app and
# the installer are signed, notarized (keychain profile "frkn-notary") and
# stapled. Otherwise the script falls back to an unsigned PKG — recipients
# open it via right-click -> Open / "Open Anyway" in Privacy & Security.
#
# Usage (from the repo root):
#   bash deploy/package_macos_pkg.sh <build-dir> [output-name]
# e.g.
#   bash deploy/package_macos_pkg.sh deploy/build-macos-local-arm64 Dopamine-arm64.pkg
# ---------------------------------------------------------------------------

set -o errexit -o nounset

DEVID_APP="Developer ID Application: FRKN LLP (455SJ7P6J3)"
DEVID_INSTALLER="Developer ID Installer: FRKN LLP (455SJ7P6J3)"
NOTARY_PROFILE="frkn-notary"

SIGN_APP=0
SIGN_PKG=0
if security find-identity -v -p codesigning | grep -qF "$DEVID_APP"; then
  SIGN_APP=1
fi
if security find-identity -v | grep -qF "$DEVID_INSTALLER"; then
  SIGN_PKG=1
fi
if [ "$SIGN_APP" = 1 ] && [ "$SIGN_PKG" = 1 ]; then
  echo "Developer ID identities found — will sign and notarize"
else
  echo "Developer ID identities incomplete (app=$SIGN_APP installer=$SIGN_PKG) — building unsigned PKG"
fi

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

# stamp the version into the artifact name: Dopamine-arm64.pkg -> Dopamine-arm64-X.Y.Z.W.pkg
case "$OUT_NAME" in
  *-${APP_VERSION}.pkg) ;; # already versioned
  *.pkg) OUT_NAME="${OUT_NAME%.pkg}-${APP_VERSION}.pkg" ;;
esac
echo "Packaging $BUNDLE_DIR as $OUT_NAME (version $APP_VERSION)"

# launchd plist must live in Resources (post_install.sh copies it into
# /Library/LaunchDaemons from there)
mkdir -p "$BUNDLE_DIR/Contents/Resources"
cp "$DEPLOY_DATA_DIR/$PLIST_NAME" "$BUNDLE_DIR/Contents/Resources/$PLIST_NAME"

# re-seal after adding the plist
if [ "$SIGN_APP" = 1 ]; then
  # hardened runtime + timestamp are required by notarization.
  # Fresh bundles (ad-hoc from the linker) seal fine with --deep — it also signs
  # the non-Mach-O helpers we keep in Contents/MacOS (html/sh/dat), which a plain
  # bundle-level sign rejects. But --deep cannot reliably RE-sign an already
  # Developer-ID-signed bundle ("A timestamp was expected but was not found" on
  # subcomponents), so incremental repackaging goes inside-out instead.
  if codesign -dv "$BUNDLE_DIR" 2>&1 | grep -qF "$DEVID_APP"; then
    echo "Bundle already Developer-ID signed — re-signing inside-out"
    find "$BUNDLE_DIR/Contents" -type f -name "*.dylib" \
      -exec codesign --force --options runtime --timestamp --sign "$DEVID_APP" {} \;
    for helper in wireguard-go tun2socks openvpn ss-local ss-tunnel ck-client dopamine-service; do
      if [ -f "$BUNDLE_DIR/Contents/MacOS/$helper" ]; then
        codesign --force --options runtime --timestamp --sign "$DEVID_APP" "$BUNDLE_DIR/Contents/MacOS/$helper"
      fi
    done
    find "$BUNDLE_DIR/Contents/Frameworks" -maxdepth 1 -name "*.framework" \
      -exec codesign --force --options runtime --timestamp --sign "$DEVID_APP" {} \;
    codesign --force --options runtime --timestamp --sign "$DEVID_APP" "$BUNDLE_DIR"
  else
    codesign --deep --force --options runtime --timestamp --sign "$DEVID_APP" "$BUNDLE_DIR"
  fi
else
  codesign --deep --force --sign - "$BUNDLE_DIR"
fi

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
if [ "$SIGN_PKG" = 1 ]; then
  productbuild --distribution "$DEPLOY_DATA_DIR/distribution.xml" \
               --package-path "$PKG_DIR" \
               --resources "$RESOURCES_DIR" \
               --sign "$DEVID_INSTALLER" \
               "$FINAL_PKG"

  echo "Notarizing $FINAL_PKG ..."
  xcrun notarytool submit "$FINAL_PKG" --keychain-profile "$NOTARY_PROFILE" --wait

  echo "Stapling ticket..."
  xcrun stapler staple "$FINAL_PKG"

  echo ""
  echo "Finished. Signed and notarized installer: $FINAL_PKG"
else
  productbuild --distribution "$DEPLOY_DATA_DIR/distribution.xml" \
               --package-path "$PKG_DIR" \
               --resources "$RESOURCES_DIR" \
               "$FINAL_PKG"

  echo ""
  echo "Finished. Unsigned installer: $FINAL_PKG"
fi
