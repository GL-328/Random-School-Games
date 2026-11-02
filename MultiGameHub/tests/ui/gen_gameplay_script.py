#!/usr/bin/env python3
"""Generates an end-to-end UI script for the scripted desktop build.
Usage: gen_gameplay_script.py OUT_FILE [WIDTH_PX HEIGHT_PX DP]
Coordinates are dp relative to the safe area origin; the chess board position
mirrors ChessGame::layout()."""
import math, sys

out_path = sys.argv[1] if len(sys.argv) > 1 else "gameplay.txt"
W, H, DP = (int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4])) if len(sys.argv) > 4 else (540, 1170, 1.3125)
SAFE_Y = 8.0                          # App adds 8dp under the top edge
SW, SH = W / DP, H / DP - SAFE_Y      # safe area in dp

# --- chess layout (keep in sync with ChessGame::layout)
bar, hist, gap, tool = 56, 32, 6, 60
tool_y = SH - tool
avail = tool_y - hist - 2 * bar - 4 * gap
size_px = max(200 * DP, min(SW * DP, avail * DP))
sq_px = math.floor(size_px / 8)
bs = sq_px * 8 / DP
block = hist + gap + bar + gap + bs + gap + bar
y = max(0.0, (tool_y - block) * 0.5) + hist + gap + bar + gap
BX = round((SW / 2 - bs / 2) * DP) / DP
BY = round((y + SAFE_Y) * DP) / DP - SAFE_Y
SQ = sq_px / DP

def sq(name):
    f = ord(name[0]) - 97; r = int(name[1]) - 1
    return BX + (f + 0.5) * SQ, BY + (7 - r + 0.5) * SQ

out = []
def tap(x, y): out.append(f"tap {x:.1f} {y:.1f}"); out.append("wait 12")
def move(a, b): tap(*sq(a)); tap(*sq(b))
def toolbar(i): tap(SW / 2 - min(SW, 480) / 2 + (i + 0.5) * min(SW, 480) / 4, tool_y + 30)

# Hub screens
out += ["wait 20", "shot hub_home", "down 1 200 600", "wait 2"]
for k in range(1, 16): out += [f"move 1 200 {600 - k * 40}", "wait 1"]
out += ["up 1 200 0", "wait 40", "shot hub_home_bottom"]
for i, name in enumerate(["games", "installed", "updates", "settings"], 1):
    out += [f"tab {i}", f"shot hub_{name}"]
out += ["page storage", "shot hub_storage", "back", "wait 20", "page about", "shot hub_about", "back", "wait 20",
        "tab 0", "detail chess", "shot hub_detail", "back", "wait 20"]

# Chess: scholar's mate with one drag-and-drop move, then undo
out += ["launch chess", "wait 30", "expect_game chess", "shot chess_start"]
move("e2", "e4"); move("e7", "e5")
x0, y0 = sq("d1"); x1, y1 = sq("h5")
out += [f"down 1 {x0:.1f} {y0:.1f}", "wait 2"]
for i in range(1, 11):
    t = i / 10; out += [f"move 1 {x0 + (x1 - x0) * t:.1f} {y0 + (y1 - y0) * t:.1f}", "wait 1"]
out += [f"up 1 {x1:.1f} {y1:.1f}", "wait 15"]
move("b8", "c6"); move("f1", "c4")
tap(*sq("g8")); out.append("shot chess_selected"); tap(*sq("f6"))
move("h5", "f7")
out += ["wait 20", "shot chess_mate"]
toolbar(1)                                # Undo from the toolbar
out += ["wait 10", "expect_game chess", "back", "wait 20", "shot chess_menu"]
tap(SW / 2, (SH / 2) - 88 + 112 + 22)     # "Exit to hub" in the pause card
out += ["wait 40", "expect_game none"]

# Air hockey: start, two simultaneous fingers, pause, exit
aspect = min(max((SH - 20) / (SW - 20), 1.4), 2.1)
scale = min(SW - 20, (SH - 20) / aspect)
cy = SH / 2
out += ["launch air_hockey", "wait 30", "expect_game air_hockey", "shot ah_start"]
bw = min(130, (scale - 36) / 2)
tap(SW / 2 + 6 + bw / 2, cy)              # Start
out += ["wait 60", "shot ah_countdown", "wait 140"]
out += [f"down 1 {SW/2:.0f} {cy + scale * 0.6:.0f}", f"down 2 {SW/2:.0f} {cy - scale * 0.6:.0f}", "wait 2"]
for i in range(1, 40):
    out += [f"move 1 {SW/2 + (i % 10) * 4:.1f} {cy + scale * 0.6 - i * 5:.1f}",
            f"move 2 {SW/2 - (i % 10) * 4:.1f} {cy - scale * 0.6 + i * 5:.1f}", "wait 1"]
out += ["shot ah_play", f"up 1 {SW/2:.0f} {cy:.0f}", f"up 2 {SW/2:.0f} {cy:.0f}", "wait 30", "back", "wait 10", "shot ah_paused"]
tap(SW / 2, cy - 120 + 78 + 100 + 22)     # Exit to hub
out += ["wait 40", "expect_game none"]

# Snake: steer with the D-pad
pad_h = min(190, SH * 0.28)
pad_cy = SH - pad_h - 6 + pad_h / 2
b = min(pad_h / 3, 64)
out += ["launch snake", "wait 30", "expect_game snake", "shot snake_ready"]
out += [f"down 1 {SW/2 + b + 6:.1f} {pad_cy:.1f}", f"up 1 {SW/2 + b + 6:.1f} {pad_cy:.1f}", "wait 25",
        f"down 1 {SW/2:.1f} {pad_cy - b:.1f}", f"up 1 {SW/2:.1f} {pad_cy - b:.1f}", "wait 6", "shot snake_turn", "wait 20",
        f"down 1 {SW/2 - b - 6:.1f} {pad_cy:.1f}", f"up 1 {SW/2 - b - 6:.1f} {pad_cy:.1f}", "wait 30"]
out += ["shot snake_play", "wait 600", "shot snake_over"]
out += ["back", "wait 40", "expect_game none"]
open(out_path, "w").write("\n".join(out) + "\n")
