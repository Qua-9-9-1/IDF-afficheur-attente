// PLACEHOLDER - to be replaced by the real tram board renderer.
#include "board_tram.h"

#include "board_rer.h"
#include "prim_client.h"

#include "core/mykeyboard.h"
#include <globals.h>

void tramBoardDraw(const BoardData &data, time_t, uint8_t) {
    TFT_eSPI &g = *tft.native();
    g.fillScreen(0);
    g.setTextFont(1);
    g.setTextSize(2);
    g.setTextColor(0xFFFF);
    g.setTextDatum(TL_DATUM);
    g.drawString("TRAM " + data.line.shortName, 10, 10);
    g.drawString(idfmAscii(data.stop.name), 10, 40);
    g.drawString(String((int)data.departures.size()) + " departs", 10, 70);
    g.drawString(idfmAscii(data.error), 10, 100);
}

void tramBoardTick(const BoardData &, time_t) {}

void tramBoardRelease() {}

void tramBoardSampleData(BoardData &out) {
    out = BoardData();
    out.stop.name = "Demo";
}

void tramBoardDemo() {
    BoardData data;
    tramBoardSampleData(data);
    tramBoardDraw(data, primNow());
    while (!check(AnyKeyPress)) delay(20);
}