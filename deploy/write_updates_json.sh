#!/bin/bash
# Build dopamine/updates.json for the desktop updater.
#
# The client fetches https://frkn.org/dopamine/updates.json, checks sha256
# and opens the same installer that is published next to it.
#
# Usage (from the repo root, paths are the files you are about to upload):
#   bash deploy/write_updates_json.sh 4.8.14.62 ~/c/f/frkn.org/dopamine/updates.json \
#     --windows Dopamine-4.8.14.62-win64.msi \
#     --macos-arm64 Dopamine-arm64-4.8.14.62.pkg \
#     --macos-x86_64 Dopamine-intel-4.8.14.62.pkg \
#     [--linux Dopamine-4.8.14.62-linux.bin] \
#     [--notes "text"] [--notes-ru "text"] [--notes-uk "text"]

set -o errexit -o nounset -o pipefail

if [ "$#" -lt 2 ]; then
  echo "Usage: deploy/write_updates_json.sh VERSION OUTPUT --windows MSI --macos-arm64 PKG --macos-x86_64 PKG [--linux BIN] [--notes TEXT]" >&2
  exit 1
fi

VERSION="$1"
OUTPUT="$2"
shift 2

WIN_FILE=""
ARM_FILE=""
INTEL_FILE=""
LINUX_FILE=""
NOTES=""
NOTES_RU=""
NOTES_UK=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    --windows) WIN_FILE="$2"; shift 2 ;;
    --macos-arm64) ARM_FILE="$2"; shift 2 ;;
    --macos-x86_64) INTEL_FILE="$2"; shift 2 ;;
    --linux) LINUX_FILE="$2"; shift 2 ;;
    --notes) NOTES="$2"; shift 2 ;;
    --notes-ru) NOTES_RU="$2"; shift 2 ;;
    --notes-uk) NOTES_UK="$2"; shift 2 ;;
    *) echo "Unknown argument: $1" >&2; exit 1 ;;
  esac
done

if [ -z "$WIN_FILE" ] && [ -z "$ARM_FILE" ] && [ -z "$INTEL_FILE" ] && [ -z "$LINUX_FILE" ]; then
  echo "Pass at least one installer" >&2
  exit 1
fi

hash_file() {
  if [ ! -f "$1" ]; then
    echo "File not found: $1" >&2
    exit 1
  fi
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  else
    sha256sum "$1" | awk '{print $1}'
  fi
}

export UPD_VERSION="$VERSION"
export UPD_OUTPUT="$OUTPUT"
export UPD_NOTES="$NOTES"
export UPD_NOTES_RU="$NOTES_RU"
export UPD_NOTES_UK="$NOTES_UK"
export UPD_WIN_URL="" UPD_WIN_SHA=""
export UPD_ARM_URL="" UPD_ARM_SHA=""
export UPD_INTEL_URL="" UPD_INTEL_SHA=""
export UPD_LINUX_URL="" UPD_LINUX_SHA=""

base_url() {
  local name
  name=$(basename "$1")
  printf 'https://frkn.org/dopamine/%s' "$name"
}

if [ -n "$WIN_FILE" ]; then
  UPD_WIN_SHA=$(hash_file "$WIN_FILE")
  UPD_WIN_URL=$(base_url "$WIN_FILE")
  export UPD_WIN_SHA UPD_WIN_URL
fi
if [ -n "$ARM_FILE" ]; then
  UPD_ARM_SHA=$(hash_file "$ARM_FILE")
  UPD_ARM_URL=$(base_url "$ARM_FILE")
  export UPD_ARM_SHA UPD_ARM_URL
fi
if [ -n "$INTEL_FILE" ]; then
  UPD_INTEL_SHA=$(hash_file "$INTEL_FILE")
  UPD_INTEL_URL=$(base_url "$INTEL_FILE")
  export UPD_INTEL_SHA UPD_INTEL_URL
fi
if [ -n "$LINUX_FILE" ]; then
  UPD_LINUX_SHA=$(hash_file "$LINUX_FILE")
  UPD_LINUX_URL=$(base_url "$LINUX_FILE")
  export UPD_LINUX_SHA UPD_LINUX_URL
fi

python3 - << 'PY'
import json, os

def add(platforms, key, url, sha):
    if not url:
        return
    platforms[key] = {"url": url, "sha256": sha}

platforms = {}
add(platforms, "windows-x64", os.environ.get("UPD_WIN_URL", ""), os.environ.get("UPD_WIN_SHA", ""))
add(platforms, "macos-arm64", os.environ.get("UPD_ARM_URL", ""), os.environ.get("UPD_ARM_SHA", ""))
add(platforms, "macos-x86_64", os.environ.get("UPD_INTEL_URL", ""), os.environ.get("UPD_INTEL_SHA", ""))
add(platforms, "linux-x64", os.environ.get("UPD_LINUX_URL", ""), os.environ.get("UPD_LINUX_SHA", ""))

doc = {"version": os.environ["UPD_VERSION"], "platforms": platforms}
for key, env in (("notes", "UPD_NOTES"), ("notes_ru", "UPD_NOTES_RU"), ("notes_uk", "UPD_NOTES_UK")):
    text = os.environ.get(env, "")
    if text:
        doc[key] = text

out = os.environ["UPD_OUTPUT"]
with open(out, "w", encoding="utf-8", newline="\n") as fh:
    json.dump(doc, fh, ensure_ascii=False, indent=2)
    fh.write("\n")
print(out)
PY
