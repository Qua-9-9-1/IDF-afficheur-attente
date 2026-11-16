#pragma once
// Small drawing helpers shared by the board renderers (board_rer, board_metro).
// Text is always drawn with the GLCD 5x7 font (font 1) scaled by setTextSize: x1 = 6x8 px
// cell, x2 = 12x16, x3 = 18x24, x4 = 24x32. Strings passed here are GLCD-encoded
// (see idfmGlcd() in board_rer.h).

#include <Arduino.h>
#include <time.h>

class TFT_eSPI;

namespace idfmui {

// RGB888 -> RGB565.
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Pixel width of a GLCD string (bytes == glyphs), without the trailing blank column.
int32_t textW(const String &s, uint8_t size);

// Draw `s` top-left anchored at (x, y), transparent background (areas are pre-filled).
void drawText(TFT_eSPI &g, const String &s, int32_t x, int32_t y, uint8_t size, uint16_t fg);

// Usual French abbreviations of station names ("Saint-" -> "St-"...).
String abbreviate(String s);

// Shorten a GLCD string to fit `maxW` pixels: abbreviations first, then truncation with a
// trailing dot.
String fitText(String s, int32_t maxW, uint8_t size);

// "HH:MM" Europe/Paris time of a UTC epoch (via primLocalTime), "--:--" when unknown.
String clockText(time_t t);

} // namespace idfmui
