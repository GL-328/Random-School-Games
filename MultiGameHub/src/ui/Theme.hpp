#pragma once

#include "Gfx.hpp"

namespace mgh {

struct Theme {
    bool dark = true;
    Color bg, bgElevated, surface, surface2, surfacePressed, border, text, textDim, textFaint;
    Color accent, accentText, accentSoft, accent2, success, warning, danger, overlay;

    static Theme make(bool dark) {
        Theme t;
        t.dark = dark;
        if (dark) {
            t.bg = Color::hex(0x121419);
            t.bgElevated = Color::hex(0x171A21);
            t.surface = Color::hex(0x1E222B);
            t.surface2 = Color::hex(0x262B36);
            t.surfacePressed = Color::hex(0x2E3441);
            t.border = Color::hex(0x2F3542);
            t.text = Color::hex(0xEEF1F7);
            t.textDim = Color::hex(0xA6AEBD);
            t.textFaint = Color::hex(0x6E7688);
            t.accent = Color::hex(0x7C5CFF);
            t.accentSoft = Color::hex(0x7C5CFF, 46);
            t.overlay = Color(5, 6, 9, 190);
        } else {
            t.bg = Color::hex(0xF3F4F8);
            t.bgElevated = Color::hex(0xFAFAFC);
            t.surface = Color::hex(0xFFFFFF);
            t.surface2 = Color::hex(0xECEEF3);
            t.surfacePressed = Color::hex(0xE1E4EC);
            t.border = Color::hex(0xDCE0E8);
            t.text = Color::hex(0x161922);
            t.textDim = Color::hex(0x586072);
            t.textFaint = Color::hex(0x8C93A3);
            t.accent = Color::hex(0x6646F2);
            t.accentSoft = Color::hex(0x6646F2, 36);
            t.overlay = Color(20, 22, 30, 150);
        }
        t.accentText = Color::hex(0xFFFFFF);
        t.accent2 = Color::hex(0x1FC8AA);
        t.success = Color::hex(0x2FCB86);
        t.warning = Color::hex(0xFFB547);
        t.danger = Color::hex(0xFF5470);
        return t;
    }
};

}  // namespace mgh
