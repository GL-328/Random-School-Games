// Vector icons drawn with primitives (no image assets).
#pragma once

#include "Gfx.hpp"

namespace mgh {

class Renderer2D;

enum class Icon {
    None, Home, Grid, Download, Gear, Back, Pause, Play, Restart, Undo, List, Trash, Refresh, Check, Close,
    Storage, Info, Sound, Music, Vibrate, Moon, Sun, ChevronRight, Star, Trophy, Flag, Box, Clock
};

void drawIcon(Renderer2D& r, Icon icon, const Rect& box, Color c);

}  // namespace mgh
