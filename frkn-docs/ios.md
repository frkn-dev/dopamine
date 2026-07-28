# iOS Build & TestFlight Guide

## Local Build (macOS)

### Prerequisites

- macOS 13.0+
- Xcode 15.0+ for development; **Xcode 26.x required for App Store Connect uploads** (ASC accepts only iOS 26 SDK+ builds). On macOS 15 use Xcode 26.1.x — the same version CI targets.
- Qt 6.10.1 for iOS and macOS host
- Go 1.24+ and `gomobile`
- Apple Developer account enrolled in the **Apple Developer Program** ($99/year)

### Install dependencies

```bash
pip install aqtinstall
aqt install-qt mac ios 6.10.1 -m qtremoteobjects qt5compat qtshadertools qtmultimedia qtimageformats
aqt install-qt mac desktop 6.10.1 -m qtremoteobjects qt5compat qtshadertools qtmultimedia qtimageformats

go install golang.org/x/mobile/cmd/gomobile@latest
gomobile init
```

### Configure signing & provisioning (local development)

For local debug builds Xcode can manage signing automatically if you set
`CODE_SIGN_STYLE=Automatic` (default when `DEPLOY` is not set).

For App Store / TestFlight builds you need:

- **Distribution certificate** (`.p12`) and private key
- **iOS App provisioning profile** for bundle ID `org.frkn.dopamine`
- **Network Extension provisioning profile** for bundle ID `org.frkn.dopamine.network-extension`
- Both profiles must include:
  - App Group `group.org.frkn.dopamine`
  - Network Extension capability (`packet-tunnel-provider`)

### Build locally

```bash
export QT_BIN_DIR="$HOME/Qt/6.10.1/ios/bin"
export QT_MACOS_ROOT_DIR="$HOME/Qt/6.10.1/macos"
export PROD_AGW_PUBLIC_KEY="$(awk '{printf "%s\\n", $0}' agw_public.pem)"

bash deploy/build_ios.sh
```

The script generates `build-ios/Dopamine.xcodeproj` and builds a Release `.app`.

### Local TestFlight upload — verified CLI path (Xcode 26.1.1, macOS 15)

App Store Connect rejects builds made with an SDK older than iOS 26, so use
**Xcode 26.1.x** (the newest branch that still runs on macOS 15 — the same one CI uses).
Keep it side-by-side as `/Applications/Xcode-26.1.1.app`; do NOT replace the system Xcode.

One-time per machine:

1. Xcode → **Settings → Accounts** → add your Apple ID (must be Admin/App Manager
   in team `455SJ7P6J3`, FRKN LLP).
2. Clean-configure the project **with the new Xcode active** — otherwise CMake keeps
   compilers/SDK paths of the previously used Xcode and Swift compilation fails:

```bash
rm -rf build-ios-upload
export DEVELOPER_DIR=/Applications/Xcode-26.1.1.app/Contents/Developer
export QT_BIN_DIR="<qt>/6.10.1/ios/bin"
export QT_MACOS_ROOT_DIR="<qt>/6.10.1/macos"
export PROD_AGW_PUBLIC_KEY="$(awk '{printf "%s\\n", $0}' agw_public.pem)"
$QT_BIN_DIR/qt-cmake . -B build-ios-upload -GXcode -DQT_HOST_PATH=$QT_MACOS_ROOT_DIR -DDEPLOY=ON
```

Archive (note the signing overrides — the repo hardcodes CI/fastlane profile names
`match AppStore …` in `client/cmake/ios.cmake`, which do not exist locally;
`CODE_SIGN_STYLE=Automatic` lets Xcode create App Store profiles via your account):

```bash
export DEVELOPER_DIR=/Applications/Xcode-26.1.1.app/Contents/Developer
xcodebuild -configuration Release -scheme Dopamine -destination "generic/platform=iOS" \
  -project build-ios-upload/Dopamine.xcodeproj -archivePath build-ios-upload/Dopamine.xcarchive \
  -allowProvisioningUpdates CODE_SIGN_STYLE=Automatic CODE_SIGN_IDENTITY="Apple Development" \
  PROVISIONING_PROFILE_SPECIFIER= archive
```

Export + upload in one step (uses the Xcode account session, no API key needed):

```bash
xcodebuild -exportArchive -archivePath build-ios-upload/Dopamine.xcarchive \
  -exportPath build-ios-upload/upload -exportOptionsPlist build-ios-upload/exportOptionsUpload.plist \
  -allowProvisioningUpdates
```

`exportOptionsUpload.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>method</key>
	<string>app-store-connect</string>
	<key>destination</key>
	<string>upload</string>
	<key>teamID</key>
	<string>455SJ7P6J3</string>
	<key>signingStyle</key>
	<string>automatic</string>
	<key>stripSwiftSymbols</key>
	<false/>
	<key>uploadSymbols</key>
	<false/>
</dict>
</plist>
```

Known pitfalls hit on macOS 15.7 + Xcode 26.1.1:

- `exportArchive` fails with `Copy failed` / rsync `--extended-attributes: unknown option` —
  macOS 15.7 ships **openrsync** which breaks Xcode's symbol-copying step.
  Workaround: `stripSwiftSymbols=false` + `uploadSymbols=false` in export options (above).
- `database is locked` in `build/XCBuildData` — a concurrent build is running
  (Xcode GUI or another xcodebuild). Stop it / close Xcode; remove `build.db-journal` if stale.
- `unable to create directory ... Dopamine.app` — stale symlink in
  `client/Release-iphoneos` left by a previous archive. Delete the folder and rebuild.
- ASC rejects re-uploads of the same version+build: bump the tweak component of
  `DOPAMINE_VERSION` in the root `CMakeLists.txt` before every upload.

### Build & archive manually via Xcode

If you prefer a GUI or need to produce an `.ipa`:

1. Open `build-ios/Dopamine.xcodeproj` in Xcode.
2. Select the `Dopamine` target → **Signing & Capabilities**:
   - Team: your Apple Developer team
   - Bundle Identifier: `org.frkn.dopamine`
   - Provisioning Profile: match AppStore / Development
3. Select the `networkextension` target and repeat for bundle ID `org.frkn.dopamine.network-extension`.
4. Choose **Product → Archive**.
5. Use **Distribute App → App Store Connect → Upload** to send to TestFlight.

---

## TestFlight / App Store Release Checklist

### 1. App Store Connect

Create or verify the app:

- **Bundle ID**: `org.frkn.dopamine`
- **Name**: `Dopamine by FRKN` (must be unique on the App Store)
- **SKU**: your internal SKU
- **Primary Language**: e.g. English

### 2. Capabilities & identifiers

In [Apple Developer Portal](https://developer.apple.com/account/resources/identifiers/list):

| Identifier | Capabilities |
|------------|--------------|
| `org.frkn.dopamine` | App Groups, In-App Purchase, Network Extensions, **Push Notifications** |
| `org.frkn.dopamine.network-extension` | App Groups, Network Extensions (`packet-tunnel-provider`) |

App Group must be exactly: `group.org.frkn.dopamine`

### 3. Provisioning profiles

Generate or download fresh profiles after any identifier/capability change:

- `match AppStore org.frkn.dopamine`
- `match AppStore org.frkn.dopamine.network-extension`
- (or their Development equivalents for internal testing)

### 4. In-App Purchase

Create an **Auto-Renewable Subscription**:

- **Product ID**: `frkn_premium_6_month`
- Reference name: e.g. "Dopamine Premium 6 Months"
- Add it to a Subscription Group
- Set **Cleared for Sale**
- Add localization, pricing, screenshot for review

The app calls `IosController::purchaseProduct("frkn_premium_6_month")` — the ID must match exactly.

### 5. Privacy

- Fill **Privacy Nutrition Labels** in App Store Connect.
- Verify `client/ios/app/PrivacyInfo.xcprivacy` and `client/ios/networkextension/PrivacyInfo.xcprivacy` are included in the target resources.

### 6. App Review Information

Prepare before first submission:

- Contact name, phone, email
- Demo account (if the app requires one)
- Notes for reviewer explaining the VPN / subscription flow

### 7. Upload build

CI secrets used by `.github/workflows/deploy.yml`:

| Secret | Purpose |
|--------|---------|
| `IOS_SIGNING_CERT_BASE64` | Distribution certificate + key as base64 `.p12` |
| `IOS_SIGNING_CERT_PASSWORD` | Password for the `.p12` |
| `IOS_TRUST_CERT_BASE64` | Apple WWDR / intermediate certificate |
| `IOS_APP_PROVISIONING_PROFILE` | App provisioning profile as base64 |
| `IOS_NE_PROVISIONING_PROFILE` | Network Extension provisioning profile as base64 |
| `APPSTORE_CONNECT_KEY_ID` | App Store Connect API key ID |
| `APPSTORE_CONNECT_ISSUER_ID` | App Store Connect API issuer ID |
| `APPSTORE_CONNECT_PRIVATE_KEY` | App Store Connect API private key (`.p8`) |

To upload from local machine you can use Xcode Organizer or `xcrun altool`:

```bash
xcrun altool --upload-app \
  -f Dopamine.ipa \
  -t ios \
  --apiKey $APPSTORE_CONNECT_KEY_ID \
  --apiIssuer $APPSTORE_CONNECT_ISSUER_ID
```

### 8. TestFlight

- Add **Internal Testers** (up to 100 members of your App Store Connect team).
- Builds appear in App Store Connect → TestFlight within minutes.
- For **External Testers** you must submit the build for Beta App Review.

---

## Common Issues

| Problem | Cause / Fix |
|---------|-------------|
| `No signing certificate` | Provisioning profile does not match the installed certificate or bundle ID. |
| `Network Extension failed to load` | App Group mismatch between app and extension entitlements. Must be `group.org.frkn.dopamine` everywhere. |
| `Invalid product ID` during purchase | `frkn_premium_6_month` is missing in App Store Connect or not Cleared for Sale. |
| `Metadata rejected` | Info.plist usage descriptions or app name still contain `FRKN` / `Amnezia VPN`. |
| `Bundle ID already taken` | Another app in your account or globally uses `org.frkn.dopamine`. Use a different reverse-DNS identifier and update `client/CMakeLists.txt`. |
