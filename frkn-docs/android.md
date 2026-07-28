# Android Build & Google Play Guide

## Local Build

### Prerequisites

- Linux, macOS, or Windows with WSL
- Android SDK (API 36+ recommended)
- Android NDK r26b
- Qt 6.10.1 for Android (required ABIs: `x86_64`, `x86`, `armeabi-v7a`, `arm64-v8a`)
- Qt 6.10.1 for desktop host (for `qt-cmake` tools)
- Java 17 (Temurin recommended)

### Install Qt

```bash
pip install aqtinstall

# Host tools
aqt install-qt linux desktop 6.10.1 -m qtremoteobjects qt5compat qtimageformats qtshadertools

# Android targets
aqt install-qt linux android 6.10.1 android_x86_64   -m qtremoteobjects qt5compat qtimageformats qtshadertools
aqt install-qt linux android 6.10.1 android_x86       -m qtremoteobjects qt5compat qtimageformats qtshadertools
aqt install-qt linux android 6.10.1 android_armv7     -m qtremoteobjects qt5compat qtimageformats qtshadertools
aqt install-qt linux android 6.10.1 android_arm64_v8a -m qtremoteobjects qt5compat qtimageformats qtshadertools
```

### Create a signing keystore

```bash
keytool -genkey -v \
  -keystore dopamine-release-key.jks \
  -keyalg RSA -keysize 2048 -validity 10000 \
  -alias dopamine-key
```

Keep the keystore file and passwords in a safe place — losing them means you cannot update the app on Google Play.

### Environment variables

Add to your `~/.bashrc`, `~/.zshrc`, or export before building:

```bash
export ANDROID_KEYSTORE_PATH="/path/to/dopamine-release-key.jks"
export ANDROID_KEYSTORE_KEY_PASS="your_keystore_password"
export ANDROID_KEYSTORE_KEY_ALIAS="dopamine-key"

export ANDROID_NDK_ROOT="/path/to/android-ndk-r26b"
export QT_HOST_PATH="$HOME/Qt/6.10.1/gcc_64"

# Optional: override the default API platform
export ANDROID_BUILD_PLATFORM=android-36
```

### Build APKs

```bash
./deploy/build_android.sh --apk all
```

Output APKs are placed in `deploy/build/`.

### Build Android App Bundle (AAB) for Google Play

```bash
./deploy/build_android.sh --aab
```

Output: `deploy/build/FRKN-release.aab` (the artifact name is still historical; the package contents use the Dopamine branding).

### Sign manually after a CI build

If the CI produced an unsigned AAB:

```bash
jarsigner \
  -keystore dopamine-release-key.jks \
  -storepass "PASS" \
  -keypass "PASS" \
  -sigalg SHA256withRSA \
  -digestalg SHA-256 \
  FRKN-release.aab \
  dopamine-key
```

For Google Play you can also let Play App Signing handle the final signing after upload.

---

## Google Play Release Checklist

### 1. Google Play Console

Create or verify the app:

- **Package name**: `org.amnezia.vpn` (legacy package name used by the project) — **cannot be changed after first upload**
- **App name**: `Dopamine by FRKN`

### 2. Signing

Google Play requires either:

- **Google Play App Signing** (recommended): upload your signing keystore or let Google generate one.
- **Legacy self-managed signing**: you keep the private key and sign locally.

Upload key (for AAB/APK upload) must match the certificate registered in Play Console.

### 3. Build variant

Use **AAB** for production. Google Play uses Dynamic Delivery to generate optimized APKs per device.

The CI workflow `.github/workflows/deploy.yml` builds AAB with:

```bash
./deploy/build_android.sh --aab --apk all --build-platform android-36
```

### 4. API keys and endpoints

The Android build expects these secrets/variables (set in CI or local env):

| Variable / Secret | Purpose |
|-------------------|---------|
| `PROD_AGW_PUBLIC_KEY` | Public key for AGW-encrypted API requests |
| `PROD_S3_ENDPOINT` | Production endpoint for configs |
| `DEV_AGW_PUBLIC_KEY` / `DEV_AGW_ENDPOINT` | Dev / staging environment |
| `FREE_V2_ENDPOINT` / `PREM_V1_ENDPOINT` | VPN endpoints |

Locally you can pass them to CMake or set in the environment before running `build_android.sh`.

### 5. Google Play Console setup

Before first release:

- **App access**: free / paid / subscription
- **Content rating**: fill the questionnaire
- **Target audience**: 18+ or with age gates
- **News apps**: no
- **Data safety**: disclose collected data types (VPN apps usually collect none)
- **Privacy policy URL**: e.g. `https://frkn.org/privacy`

### 6. Store listing

Required assets:

- Short description (up to 80 chars)
- Full description
- App icon (512×512 PNG)
- Feature graphic (1024×500 PNG)
- Screenshots (phone + tablet)

Project metadata lives in `metadata/en-US/`.

### 7. Release tracks

| Track | Use case |
|-------|----------|
| Internal testing | Immediate distribution to up to 100 testers |
| Closed testing | Larger controlled group, requires review for some regions |
| Open testing | Public beta, review required |
| Production | Public release, full review |

Upload the signed AAB to the desired track and roll out.

### 8. In-app purchases / subscriptions

If you add subscriptions later:

- Create products in Play Console → Monetize → Products → Subscriptions
- Use the same product IDs as in the iOS build if you want cross-platform restore
- Implement Google Play Billing Library in the Android layer

Currently the Android build does **not** use Google Play Billing; subscriptions are managed via the FRKN API.

---

## Common Issues

| Problem | Cause / Fix |
|---------|-------------|
| `Could not find Android NDK` | `ANDROID_NDK_ROOT` is not set or points to the wrong directory. |
| `qt-cmake not found` | `QT_HOST_PATH` or `QT_BIN_DIR` is missing or wrong ABI. |
| `Keystore file not found` | `ANDROID_KEYSTORE_PATH` must be an absolute path. |
| `JAR signer error` | Wrong keystore password, alias, or the AAB is already signed with a different certificate. |
| `Google Play rejects upload` | Version code is not incremented or certificate does not match the upload key in Play Console. |
| `App name shows FRKN` | Translations or `AndroidManifest.xml` still reference old branding; run a global search for `FRKN`. |
