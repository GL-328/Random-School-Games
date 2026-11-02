# Generates an end-to-end UI script for a 540x1170 px screen at 1.3125 px/dp (411x891 dp).
# Coordinates are dp relative to the safe area origin (safe.y = 8dp).
BX, BY, SQ = 13.71, 192.7, 48.0
def sq(name):
    f = ord(name[0]) - 97; r = int(name[1]) - 1
    return BX + (f + 0.5) * SQ, BY + (7 - r + 0.5) * SQ
out = []
def tap(x, y): out.append(f"tap {x:.1f} {y:.1f}"); out.append("wait 12")
def move(a, b): tap(*sq(a)); tap(*sq(b))
out += ["launch chess", "wait 30", "expect_game chess", "shot chess_start"]
move("e2", "e4"); move("e7", "e5")
# drag-and-drop the queen d1 -> h5
x0, y0 = sq("d1"); x1, y1 = sq("h5")
out += [f"down 1 {x0:.1f} {y0:.1f}", "wait 2"]
for i in range(1, 11):
    t = i / 10; out += [f"move 1 {x0+(x1-x0)*t:.1f} {y0+(y1-y0)*t:.1f}", "wait 1"]
out += [f"up 1 {x1:.1f} {y1:.1f}", "wait 15"]
move("b8", "c6"); move("f1", "c4")
tap(*sq("g8")); out.append("shot chess_selected"); tap(*sq("f6"))
move("h5", "f7")
out += ["wait 20", "shot chess_mate"]
# Undo via the game-over card, then the toolbar Undo
out += ["log undo"]; tap(205.7 - 80, 448)  # left button of result card
out += ["wait 10", "shot chess_after_undo", "back", "wait 20", "shot chess_menu"]
tap(205.7, 487.7)  # "Exit to hub" in pause card
out += ["wait 40", "expect_game none"]
# Air hockey
out += ["launch air_hockey", "wait 30", "expect_game air_hockey", "shot ah_start"]
tap(276.7, 441.7)
out += ["wait 60", "shot ah_countdown", "wait 140"]
out += ["down 1 205 760", "down 2 205 120", "wait 2"]
for i in range(1, 40):
    out += [f"move 1 {205 + (i % 10) * 4:.1f} {760 - i * 6:.1f}", f"move 2 {205 - (i % 10) * 4:.1f} {120 + i * 6:.1f}", "wait 1"]
out += ["shot ah_play", "up 1 205 520", "up 2 205 360", "wait 30", "back", "wait 10", "shot ah_paused"]
tap(205.7, 521.7)  # Exit to hub
out += ["wait 40", "expect_game none"]
# Snake
out += ["launch snake", "wait 30", "expect_game snake", "shot snake_ready"]
out += ["down 1 275 782", "up 1 275 782", "wait 40", "down 1 205.7 719", "up 1 205.7 719", "wait 30"]
out += ["shot snake_play", "wait 600", "shot snake_over"]
out += ["back", "wait 40", "expect_game none", "tab 0", "wait 30", "shot home_after"]
import sys
open(sys.argv[1] if len(sys.argv) > 1 else "gameplay.txt", "w").write("\n".join(out) + "\n")
