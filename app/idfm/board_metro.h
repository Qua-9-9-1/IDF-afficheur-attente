#pragma once
// Renders BoardData like an official Paris metro platform display (one platform = one
// direction), on the 320x170 TFT. Same lifecycle as board_rer.h.

#include "idfm_model.h"

// Draws the whole screen. `now` is UTC (primNow()); `scrollOffset` is the index of the first
// departure shown (encoder), clamped by the renderer.
void metroBoardDraw(const BoardData &data, time_t now, uint8_t scrollOffset = 0);

// Call every 20-40 ms: animations (blinking, ticker...) and redraw on minute change.
void metroBoardTick(const BoardData &data, time_t now);

// Frees the buffers allocated by the renderer (call when leaving the board).
void metroBoardRelease();

// Offline demo with hard-coded data (line 1, Chatelet, direction La Defense); encoder scrolls,
// any other key returns.
void metroBoardDemo();
