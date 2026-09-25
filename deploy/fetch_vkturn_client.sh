#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/client/android/wireguard/src/main/assets/vkturn"
mkdir -p "$OUT"
VER="${1:-v4.0.0}"
BASE="https://github.com/samosvalishe/free-turn-proxy/releases/download/${VER}"
curl -fL "$BASE/client-android-arm64" -o "$OUT/client-android-arm64"
chmod +x "$OUT/client-android-arm64"
if curl -fL "$BASE/client-android-armeabi-v7a" -o "$OUT/client-android-armeabi-v7a"; then
  chmod +x "$OUT/client-android-armeabi-v7a"
else
  rm -f "$OUT/client-android-armeabi-v7a"
  echo "armeabi-v7a binary not published for ${VER}; arm64 only"
fi
ls -lh "$OUT"
echo "OK. Rebuild Android APK after this."
