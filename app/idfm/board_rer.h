#pragma once
// RER platform display renderer (320x170), modelled on the official IDFM / RATP / SNCF
// "prochains trains" platform screens. Design reference: docs/reference/RER.md
//
// Typical use from the app loop:
//   rerBoardDraw(data, primNow());                // after each fetch / user scroll
//   while (running) { rerBoardTick(data, primNow()); ...; delay(20); }
//   rerBoardRelease();                            // when leaving the board screen

#include "idfm_model.h"

// Number of departure rows visible at once.
constexpr uint8_t RER_BOARD_ROWS = 3;

// Transliterate UTF-8 text to plain 7-bit ASCII ("Châtelet" -> "Chatelet", "œ" -> "oe",
// typographic quotes/dashes -> ASCII). Unknown code points become '?'.
String idfmAscii(const String &utf8);

// Convert UTF-8 text to the byte encoding of the built-in TFT_eSPI GLCD font (CP437):
// French lowercase accents (é è ê ë à â ç î ï ô ù û ü) and É, Ç are kept as CP437 glyphs,
// everything else falls back to idfmAscii() rules. Only meant for drawing with font 1.
String idfmGlcd(const String &utf8);

// Render the whole board. `now` is the current UTC epoch, normally primNow() (0 = unknown:
// clock shows "--:--"). Clock times are converted with primLocalTime() (Europe/Paris), never
// with localtime_r (Bruce does not set TZ). `scrollOffset` is the index of the first departure shown,
// letting the caller scroll through the list with the encoder (clamped internally).
// Draws into a full-frame PSRAM sprite when available (flicker-free), else straight to tft.
void rerBoardDraw(const BoardData &data, time_t now, uint8_t scrollOffset = 0);

// Animate the board without a full redraw: bottom ticker scrolling and blinking of the
// "à quai" / "à l'approche" labels. Performs a full redraw by itself when the minute
// changes (waiting times and clock). Call it often (every ~20-40 ms).
void rerBoardTick(const BoardData &data, time_t now);

// Free the frame / ticker sprites (call when leaving the board screen).
void rerBoardRelease();

// Self-contained demo with hard-coded RER A data (no network). Returns on any key.
void rerBoardDemo();
