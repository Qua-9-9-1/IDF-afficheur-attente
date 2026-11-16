#pragma once
// Renders BoardData like an official bus stop / shelter display: every line serving the stop,
// one row per line + destination with the next 2 waiting times (multi-line board: see
// BoardData.lines), on the 320x170 TFT. Same lifecycle as board_rer.h.
// Design reference: docs/reference/BUS.md

#include "idfm_model.h"

// Rows (line + destination groups) visible at once. The app bounds its encoder counter with
// departures.size() (>= number of rows); the renderer clamps the offset to the last full page.
#define BUS_BOARD_ROWS 4

// Draws the whole screen. now is UTC (primNow()); scrollOffset is the index of the first ROW
// shown (rows sorted by line, then soonest departure), clamped by the renderer.
void busBoardDraw(const BoardData &data, time_t now, uint8_t scrollOffset = 0);

// Number of rows (line + destination groups) of the last draw: the encoder scrolls rows, not
// departures, on this board.
size_t busBoardRowCount();

// Call every 20-40 ms: animations (clock colon, ticker) and redraw on minute change.
void busBoardTick(const BoardData &data, time_t now);

// Frees the buffers allocated by the renderer (call when leaving the board).
void busBoardRelease();

// Fills out with realistic hard-coded sample data (Paris, Hôtel de Ville, several lines), used
// by the demo when there is no API key / no network.
void busBoardSampleData(BoardData &out);

// Offline demo on the sample data (+ a few edge-case scenes); encoder scrolls, any other key returns.
void busBoardDemo();
