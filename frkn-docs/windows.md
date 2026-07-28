# Windows Build & Distribution Guide

## Local Build

### Prerequisites

- Windows 10/11 64-bit
- Visual Studio 2022 with C++ workload
- Qt 6.10.1 for MSVC 2022 64-bit
- Qt Installer Framework (QIF) 4.7+
- WiX Toolset v4
- Windows SDK (for `signtool`)

### Install Qt

Use the Qt Online Installer or `aqtinstall` from a Python-enabled shell:

```powershell
pip install aqtinstall
aqt install-qt windows desktop 6.10.1 win64_msvc2022_64 `
  -m qtremoteobjects qt5compat qtshadertools
```

Install Qt Installer Framework:

```powershell
# Via Qt Maintenance Tool, or download from Qt website
# Expected path: C:\Qt\Tools\QtInstallerFramework\4.7\bin
```

Install WiX v4:

```powershell
dotnet tool install --global wix --version 4.0.6
wix extension add -g WixToolset.UI.wixext/4.0.6
wix extension add -g WixToolset.Util.wixext/4.0.6
```

### Environment variables

In `cmd.exe` or in your system environment:

```cmd
set QT_BIN_DIR=C:\Qt\6.10.1\msvc2022_64\bin
set QIF_BIN_DIR=C:\Qt\Tools\QtInstallerFramework\4.7\bin
set WIX_BIN_DIR=%USERPROFILE%\.dotnet\tools
set BUILD_ARCH=64
```

### Build

```cmd
call deploy\build_windows.bat
```

Outputs:

- `FRKN_x64.exe` — Qt Installer Framework offline installer (current artifact name is historical)
- `FRKN_x64.msi` — WiX-based MSI installer

Both are signed if a certificate matching `Privacy Technologies OU` is available in the Windows certificate store.

---

## Distribution Checklist

### 1. Code signing certificate

For public distribution the installer and executables must be signed with a
trusted code-signing certificate. The current script looks for:

```
Privacy Technologies OU
```

Update the `/n` parameter in `deploy/build_windows.bat` if your certificate uses a different subject name:

```cmd
signtool sign /v /n "Your Certificate Subject" /fd sha256 /tr ...
```

Common certificate sources:

- DigiCert, Sectigo, GlobalSign (OV/EV code signing)
- Azure Trusted Signing (cloud HSM signing)

### 2. Windows SmartScreen

An EV code-signing certificate or substantial reputation history is required to
avoid Windows SmartScreen warnings immediately after release.

### 3. Version numbers

The Windows package version comes from `CMakeLists.txt`:

```cmake
set(DOPAMINE_VERSION 4.8.14.5)
```

WiX upgrade GUID is fixed in `CMakeLists.txt`:

```cmake
set(CPACK_WIX_UPGRADE_GUID "{2D55AC62-96D6-4692-8C05-0D85BBF95485}")
```

Do **not** change the upgrade GUID — it is used by Windows Installer to identify
the product across versions.

### 4. Windows service

The installer deploys a system service `dopamine-service.exe`. Make sure the
service binary name matches `SERVICE_NAME` in `version.h.in`:

```cpp
#define SERVICE_NAME "dopamine-service"
```

### 5. Branding

The installer and MSI still produce files named `FRKN_*` because of legacy
variables in `deploy/build_windows.bat`. To fully switch to `Dopamine_*`:

```cmd
set APP_NAME=Dopamine
set APP_DOMAIN=org.frkn.dopamine.package
```

and update the `TARGET_FILENAME` / `TARGET_MSI_FILENAME` logic accordingly.

---

## CI Build

The project builds Windows artifacts in `.github/workflows/deploy.yml` using:

```yaml
env:
  QT_VERSION: 6.10.1
  QIF_VERSION: 4.7
  BUILD_ARCH: 64
```

Required CI secrets:

| Secret | Purpose |
|--------|---------|
| `MAC_TEAM_ID` | Not used for Windows (legacy naming) |
| `MAC_APP_CERT_CERT` / `MAC_APP_CERT_PW` | Not used for Windows |
| `MAC_INSTALLER_SIGNER_CERT` / `MAC_INSTALL_CERT_PW` | Not used for Windows |
| `APPLE_DEV_EMAIL` / `APPLE_DEV_PASSWORD` | Not used for Windows |
| Windows signing | Currently expects a certificate pre-installed on the runner or in the store |

To add automated Windows code signing in CI you can:

1. Base64-encode the `.pfx` certificate and store it as a GitHub secret.
2. Import it in the workflow:
   ```yaml
   - run: |
       echo ${{ secrets.WINDOWS_CERT_BASE64 }} | base64 -d > cert.pfx
       signtool sign /f cert.pfx /p ${{ secrets.WINDOWS_CERT_PASSWORD }} /fd sha256 /tr ... *.exe
   ```

---

## Common Issues

| Problem | Cause / Fix |
|---------|-------------|
| `WIX_BIN_DIR is not set` | `WIX_BIN_DIR` environment variable is missing. |
| `wix.exe was not found` | WiX is not installed or the path is wrong. Run `dotnet tool install --global wix`. |
| `Qt6 not found` | `QT_BIN_DIR` does not point to the MSVC 2022 64-bit Qt installation. |
| `signtool: certificate not found` | No certificate with subject `Privacy Technologies OU` in the store, or the subject name does not match. |
| `MSI install fails over old version` | Upgrade GUID is correct, but the **ProductVersion** must change for each release. |
| Service fails to start | `dopamine-service.exe` is missing or the service name does not match the installer/plist expectations. |
