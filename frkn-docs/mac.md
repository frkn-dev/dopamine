# macOS Build & Distribution Guide

## Standard macOS Build (Legacy, no Network Extension)

This build produces a classic `.app` bundle plus a `.pkg` installer. It does **not**
use the modern NetworkExtension packet-tunnel provider.

### Prerequisites

- macOS 13.0+
- Xcode Command Line Tools
- Qt 6.10.1 for macOS desktop
- CMake, Ninja

### Install Qt

```bash
pip install aqtinstall
aqt install-qt mac desktop 6.10.1 clang_64 \
  -m qtremoteobjects qt5compat qtshadertools qtmultimedia qtimageformats
```

### Local unsigned build (no certificates needed)

If you do not yet have Apple Developer certificates, use the helper script that
builds an unsigned `.app` bundle:

```bash
git submodule update --init --recursive
export QT_BIN_DIR="$HOME/Qt/6.10.1/macos/bin"
export PROD_AGW_PUBLIC_KEY="$(awk '{printf "%s\\n", $0}' agw_public.pem)"

bash deploy/build_macos_local.sh
```

Output:

- `deploy/build-macos-local/client/dopamine.app`

To run the unsigned app:

```bash
./deploy/build-macos-local/client/dopamine.app/Contents/MacOS/Dopamine
```

> macOS will warn about an unsigned app. You can allow it once in
> **System Settings → Privacy & Security**.

### Signed build locally

```bash
git submodule update --init --recursive
export QT_BIN_DIR="$HOME/Qt/6.10.1/macos/bin"
export PROD_AGW_PUBLIC_KEY="$(awk '{printf "%s\\n", $0}' agw_public.pem)"

bash deploy/build_macos.sh
```

Output:

- `deploy/build/client/dopamine.app`
- `deploy/build/pkg/dopamine.pkg` (when signing is configured)

### Notarization (for direct distribution)

The script supports notarization when you pass `-n` and provide credentials:

```bash
export MAC_APP_CERT_PW='pw-for-DeveloperID-Application'
export MAC_INSTALL_CERT_PW='pw-for-DeveloperID-Installer'
export MAC_SIGNER_ID='Developer ID Application: Your Name (TEAMID)'
export MAC_INSTALLER_SIGNER_ID='Developer ID Installer: Your Name (TEAMID)'
export APPLE_DEV_EMAIL='your@email.com'
export APPLE_DEV_PASSWORD='app-specific-password'

bash deploy/build_macos.sh -n
```

> For CI use an App Store Connect API key instead of Apple ID password.

---

## macOS Network Extension Build (Modern)

This build uses `NetworkExtension` with a system extension/packet-tunnel provider
and is required for the full Dopamine VPN experience on modern macOS.

### Prerequisites

- macOS 13.0+
- Xcode 15.0+ (CI uses Xcode 26.1)
- Qt 6.10.1 for macOS desktop
- Go 1.24+ and `gomobile`
- Apple Developer Program membership

### Install Qt

```bash
pip install aqtinstall
aqt install-qt mac desktop 6.10.1 clang_64 \
  -m qtremoteobjects qt5compat qtshadertools qtmultimedia qtimageformats

go install golang.org/x/mobile/cmd/gomobile@latest
gomobile init
```

### Required certificates & provisioning

For App Store distribution:

- **Mac Distribution certificate** (`.p12`)
- **Mac App Distribution provisioning profile** for bundle ID `org.frkn.dopamine`
- **Mac Network Extension provisioning profile** for bundle ID `org.frkn.dopamine.network-extension`
- Both profiles must include:
  - App Group `group.org.frkn.dopamine`
  - Network Extension capability (`packet-tunnel-provider`)
  - Push Notifications capability

For direct distribution outside the App Store you need **Developer ID Application**
and **Developer ID Installer** certificates instead.

### Build locally

```bash
export QT_BIN_DIR="$HOME/Qt/6.10.1/macos/bin"
export QT_MACOS_ROOT_DIR="$HOME/Qt/6.10.1/macos"
export PROD_AGW_PUBLIC_KEY="$(awk '{printf "%s\\n", $0}' agw_public.pem)"

bash deploy/build_macos_ne.sh
```

The script generates `build-macos/Dopamine.xcodeproj` (or `Dopamine by FRKN.xcodeproj`
depending on the CMake `PROJECT` value) and builds a signed `.app`.

### Build manually via Xcode

1. Generate the Xcode project:
   ```bash
   $QT_BIN_DIR/qt-cmake . -B build-macos -GXcode \
     -DQT_HOST_PATH=$QT_MACOS_ROOT_DIR \
     -DMACOS_NE=TRUE -DCMAKE_BUILD_TYPE=Release -DDEPLOY=ON
   ```
2. Open `build-macos/Dopamine.xcodeproj`.
3. Sign both the main app target and the `DopamineNetworkExtension` target.
4. Choose **Product → Archive**.
5. Distribute via **App Store Connect** or export a notarized `.app`.

---

## Distribution Options

| Channel | Certificate | Notes |
|---------|-------------|-------|
| Mac App Store | Mac Distribution | NetworkExtension is supported; must use sandbox |
| Direct download | Developer ID Application + Installer | Requires notarization; user must allow system extension in Settings |
| TestFlight | Mac Distribution | Same as App Store, available on macOS 12+ |

---

## Common Issues

| Problem | Cause / Fix |
|---------|-------------|
| `DopamineNetworkExtension` not found | The extension target name or scheme changed; verify `XCODE_EMBED_APP_EXTENSIONS` in `client/cmake/macos_ne.cmake`. |
| `App Group mismatch` | Main app and extension must both use `group.org.frkn.dopamine`. Check entitlements after any identifier change. |
| `codesign: identity not found` | Certificate not imported into the temporary keychain or provisioning profile UUID mismatch. |
| `System extension blocked` | User must open **System Settings → Privacy & Security** and allow the extension. |
| Notarization fails | Apple ID password must be an app-specific password; better use App Store Connect API key. |
