#pragma once
// Renders BoardData like an official Ile-de-France tram stop display (T1..T14), one platform = one direction, on the 320x170 TFT. Same lifecycle as board_rer.h.

#include "idfm_model.h"

// Departures visible at once (bounds the encoder scrolling in idfm_app.cpp).
#define TRAM_BOARD_ROWS 2

// Draws the whole screen. now is UTC (primNow()); scrollOffset is the index of the first
// departure (or line) shown, clamped by the renderer.
void tramBoardDraw(const BoardData &data, time_t now, uint8_t scrollOffset = 0);

// Call every 20-40 ms: animations (blinking, ticker...) and redraw on minute change.
void tramBoardTick(const BoardData &data, time_t now);

// Frees the buffers allocated by the renderer (call when leaving the board).
void tramBoardRelease();

// Fills out with realistic hard-coded sample data (T3a, Porte d'Italie), used by the demo when there is
// no API key / no network.
void tramBoardSampleData(BoardData &out);

// Offline demo on the sample data; encoder scrolls, any other key returns.
void tramBoardDemo();