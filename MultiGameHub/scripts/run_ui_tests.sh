#!/usr/bin/env bash
# Headless end-to-end UI test on desktop: drives the real app with scripted
# touches (offscreen video driver) and saves screenshots. Fails if the script
# detects that the wrong game is running (e.g. a launch/exit did not work).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build/desktop}"
OUT="${OUT_DIR:-$ROOT/build/ui-test}"
[[ -f "$ROOT/third_party/SDL/CMakeLists.txt" ]] || "$ROOT/scripts/fetch_deps.sh"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_UNIX_CONSOLE_BUILD=ON > /dev/null  # headless build
cmake --build "$BUILD" --target multigamehub
mkdir -p "$OUT/shots" "$OUT/data"
python3 "$ROOT/tests/ui/gen_gameplay_script.py" "$OUT/gameplay.txt"
# Phone-like layout: 540x1170 px at 1.3125 px/dp (= 411x891 dp).
export SDL_VIDEO_DRIVER=offscreen SDL_RENDER_DRIVER=software SDL_AUDIO_DRIVER=dummy
export MGH_WINDOW=540x1170 MGH_DP=1.3125 MGH_DATA_DIR="$OUT/data"
for s in "$ROOT/tests/ui/hub_tabs.txt" "$OUT/gameplay.txt"; do
  echo "== $(basename "$s")"
  "$BUILD/multigamehub" --script "$s" --shots "$OUT/shots"
done
echo "UI tests passed; screenshots in $OUT/shots"
