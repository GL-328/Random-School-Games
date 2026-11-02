#!/usr/bin/env bash
# Fetches SDL3 sources (not committed to keep the repository small).
# stb_truetype and miniz are small and vendored in third_party/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDL_TAG="release-3.4.18"
SDL_COMMIT="829a65d769d935c4852f8159e964312c0957260a"
DEST="$ROOT/third_party/SDL"
if [[ -f "$DEST/CMakeLists.txt" ]]; then
  echo "SDL already present in $DEST"
  exit 0
fi
git clone --depth 1 --branch "$SDL_TAG" https://github.com/libsdl-org/SDL "$DEST"
ACTUAL="$(git -C "$DEST" rev-parse HEAD)"
if [[ "$ACTUAL" != "$SDL_COMMIT" ]]; then
  echo "SDL commit mismatch: expected $SDL_COMMIT got $ACTUAL" >&2
  exit 1
fi
echo "SDL $SDL_TAG ready"
