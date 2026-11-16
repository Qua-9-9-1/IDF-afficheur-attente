#pragma once
// Renders BoardData like an official SNCF Transilien platform display (lines H, J, K, L, N, P, R, U, V), on the 320x170 TFT. Same lifecycle as board_rer.h.

#include "idfm_model.h"

// Departures visible at once (bounds the encoder scrolling in idfm_app.cpp).
#define TRANSILIEN_BOARD_ROWS 2

// Draws the whole screen. now is UTC (primNow()); scrollOffset is the index of the first
// departure (or line) shown, clamped by the renderer.
void transilienBoardDraw(const BoardData &data, time_t now, uint8_t scrollOffset = 0);

// Call every 20-40 ms: animations (blinking, ticker...) and redraw on minute change.
void transilienBoardTick(const BoardData &data, time_t now);

// Frees the buffers allocated by the renderer (call when leaving the board).
void transilienBoardRelease();

// Fills out with realistic hard-coded sample data (line L, Paris Saint-Lazare), used by the demo when there is
// no API key / no network.
void transilienBoardSampleData(BoardData &out);

// Offline demo on the sample data; encoder scrolls, any other key returns.
void transilienBoardDemo();