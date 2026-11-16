// PLACEHOLDER - to be replaced by the real transilien board renderer.
#include "board_transilien.h"

#include "board_rer.h"
#include "prim_client.h"

#include "core/mykeyboard.h"
#include <globals.h>

void transilienBoardDraw(const BoardData &data, time_t, uint8_t) {
    TFT_eSPI &g = *tft.native();
    g.fillScreen(0);
    g.setTextFont(1);
    g.setTextSize(2);
    g.setTextColor(0xFFFF);
    g.setTextDatum(TL_DATUM);
    g.drawString("TRANSILIEN " + data.line.shortName, 10, 10);
    g.drawString(idfmAscii(data.stop.name), 10, 40);
    g.drawString(String((int)data.departures.size()) + " departs", 10, 70);
    g.drawString(idfmAscii(data.error), 10, 100);
}

void transilienBoardTick(const BoardData &, time_t) {}

void transilienBoardRelease() {}

void transilienBoardSampleData(BoardData &out) {
    out = BoardData();
    out.stop.name = "Demo";
}

void transilienBoardDemo() {
    BoardData data;
    transilienBoardSampleData(data);
    transilienBoardDraw(data, primNow());
    while (!check(AnyKeyPress)) delay(20);
}