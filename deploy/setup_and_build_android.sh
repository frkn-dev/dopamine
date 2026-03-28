#!/bin/zsh
# Setup Android build environment on macOS and build FRKN
# Installs: aqtinstall, Qt (desktop + android), Android SDK/NDK, JDK 17, cmake, ninja
# Then runs the Android build

set -o errexit -o nounset -o pipefail

# ── Configuration ────────────────────────────────────────────────────────────
QT_VERSION="${QT_VERSION:-6.10.1}"
QT_INSTALL_DIR="${QT_INSTALL_DIR:-$HOME/Qt-aqt}"
ANDROID_SDK_DIR="${ANDROID_SDK_DIR:-$HOME/Library/Android/sdk}"
ANDROID_CMDLINE_TOOLS_VERSION="13114758"        # latest as of 2025
ANDROID_NDK_VERSION="${ANDROID_NDK_VERSION:-26.1.10909125}"
ANDROID_BUILD_TOOLS_VERSION="36.0.0"
ANDROID_PLATFORM="android-36"

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Build options forwarded to build_android.sh (default: debug arm64 apk)
BUILD_ARGS="${BUILD_ARGS:--d -m -a arm64-v8a}"

# ── Colors ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

# ── Homebrew ─────────────────────────────────────────────────────────────────
install_brew_deps() {
  info "Installing/upgrading Homebrew dependencies..."
  if ! command -v brew &>/dev/null; then
    error "Homebrew is not installed. Install it from https://brew.sh"
  fi

  local deps=(bash cmake ninja openjdk@17)
  for dep in "${deps[@]}"; do
    if brew list --formula "$dep" &>/dev/null; then
      info "  $dep — already installed"
    else
      info "  Installing $dep..."
      brew install "$dep"
    fi
  done

  # Make sure JDK 17 is visible
  if [[ ! -d /Library/Java/JavaVirtualMachines/openjdk-17.jdk ]]; then
    sudo ln -sfn "$(brew --prefix openjdk@17)/libexec/openjdk.jdk" \
      /Library/Java/JavaVirtualMachines/openjdk-17.jdk 2>/dev/null || true
  fi
  export JAVA_HOME="$(brew --prefix openjdk@17)/libexec/openjdk.jdk/Contents/Home"
  info "  JAVA_HOME=$JAVA_HOME"
}

# ── aqtinstall ───────────────────────────────────────────────────────────────
install_aqt() {
  if command -v aqt &>/dev/null; then
    info "aqt already available: $(which aqt)"
    return
  fi

  info "Installing aqtinstall via pip..."
  if command -v pipx &>/dev/null; then
    pipx install aqtinstall
  else
    pip3 install --user --break-system-packages aqtinstall 2>/dev/null \
      || pip3 install --user aqtinstall
  fi

  # Try common pip --user bin paths
  for p in \
    "$HOME/Library/Python/3.14/bin" \
    "$HOME/Library/Python/3.13/bin" \
    "$HOME/Library/Python/3.12/bin" \
    "$HOME/Library/Python/3.11/bin" \
    "$HOME/Library/Python/3.10/bin" \
    "$HOME/Library/Python/3.9/bin" \
    "$HOME/.local/bin"; do
    if [[ -x "$p/aqt" ]]; then
      export PATH="$p:$PATH"
      break
    fi
  done

  command -v aqt &>/dev/null || error "aqt not found in PATH after install"
  info "aqt installed: $(which aqt)"
}

# ── Qt (desktop host + android targets) ─────────────────────────────────────
install_qt() {
  local qt_host_dir="$QT_INSTALL_DIR/$QT_VERSION/macos"

  if [[ -d "$qt_host_dir/bin" && -x "$qt_host_dir/bin/qt-cmake" ]]; then
    info "Qt $QT_VERSION desktop (host) already installed at $qt_host_dir"
  else
    info "Installing Qt $QT_VERSION desktop (macOS host)..."
    aqt install-qt mac desktop "$QT_VERSION" clang_64 \
      --outputdir "$QT_INSTALL_DIR" \
      -m qt5compat qtremoteobjects qtshadertools
  fi

  local android_archs=(android_arm64_v8a android_armv7 android_x86_64 android_x86)
  for arch in "${android_archs[@]}"; do
    local arch_dir="$QT_INSTALL_DIR/$QT_VERSION/$arch"
    if [[ -d "$arch_dir" ]]; then
      info "Qt $QT_VERSION $arch already installed"
    else
      info "Installing Qt $QT_VERSION $arch..."
      aqt install-qt mac android "$QT_VERSION" "$arch" \
        --outputdir "$QT_INSTALL_DIR" \
        -m qt5compat qtremoteobjects qtshadertools
    fi
  done

  export QT_HOST_PATH="$qt_host_dir"
  info "QT_HOST_PATH=$QT_HOST_PATH"
}

# ── Android SDK & NDK ────────────────────────────────────────────────────────
install_android_sdk() {
  local cmdline_tools_dir="$ANDROID_SDK_DIR/cmdline-tools/latest"

  if [[ -x "$cmdline_tools_dir/bin/sdkmanager" ]]; then
    info "Android cmdline-tools already installed"
  else
    info "Downloading Android command-line tools..."
    mkdir -p "$ANDROID_SDK_DIR/cmdline-tools"
    local zip_url="https://dl.google.com/android/repository/commandlinetools-mac-${ANDROID_CMDLINE_TOOLS_VERSION}_latest.zip"
    local tmp_zip
    tmp_zip="$(mktemp /tmp/cmdline-tools-XXXXXX.zip)"
    curl -fsSL -o "$tmp_zip" "$zip_url"
    unzip -qo "$tmp_zip" -d "$ANDROID_SDK_DIR/cmdline-tools"
    rm "$tmp_zip"
    mv "$ANDROID_SDK_DIR/cmdline-tools/cmdline-tools" "$cmdline_tools_dir"
  fi

  export ANDROID_SDK_ROOT="$ANDROID_SDK_DIR"
  local sdkmanager="$cmdline_tools_dir/bin/sdkmanager"

  info "Accepting Android SDK licenses..."
  yes 2>/dev/null | "$sdkmanager" --licenses >/dev/null 2>&1 || true

  info "Installing Android SDK components..."
  "$sdkmanager" --install \
    "platform-tools" \
    "platforms;${ANDROID_PLATFORM}" \
    "build-tools;${ANDROID_BUILD_TOOLS_VERSION}" \
    "ndk;${ANDROID_NDK_VERSION}" \
    2>&1 | grep -v "^\[=" || true

  export ANDROID_NDK_ROOT="$ANDROID_SDK_DIR/ndk/$ANDROID_NDK_VERSION"
  info "ANDROID_SDK_ROOT=$ANDROID_SDK_ROOT"
  info "ANDROID_NDK_ROOT=$ANDROID_NDK_ROOT"
}

# ── Build ────────────────────────────────────────────────────────────────────
run_build() {
  info "Starting Android build..."
  info "Build args: $BUILD_ARGS"

  cd "$PROJECT_DIR"

  export QT_HOST_PATH
  export ANDROID_SDK_ROOT
  export ANDROID_NDK_ROOT
  export JAVA_HOME

  # build_android.sh needs bash 4.2+ (-v operator)
  local brew_bash
  brew_bash="$(brew --prefix)/bin/bash"
  if [[ ! -x "$brew_bash" ]]; then
    brew_bash="bash"
  fi

  # ${=...} forces zsh word splitting on the variable
  "$brew_bash" deploy/build_android.sh ${=BUILD_ARGS}
}

# ── Main ─────────────────────────────────────────────────────────────────────
main() {
  info "=== FRKN Android build setup ==="
  info "Qt version:  $QT_VERSION"
  info "Qt dir:      $QT_INSTALL_DIR"
  info "SDK dir:     $ANDROID_SDK_DIR"
  info "NDK version: $ANDROID_NDK_VERSION"
  info "Project:     $PROJECT_DIR"
  echo

  install_brew_deps
  install_aqt
  install_qt
  install_android_sdk
  run_build

  info "=== Build complete ==="
  info "APK/AAB output: $PROJECT_DIR/deploy/build/"
}

main "$@"
