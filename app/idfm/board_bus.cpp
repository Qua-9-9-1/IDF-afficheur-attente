// PLACEHOLDER - to be replaced by the real bus board renderer.
#include "board_bus.h"

#include "board_rer.h"
#include "prim_client.h"

#include "core/mykeyboard.h"
#include <globals.h>

void busBoardDraw(const BoardData &data, time_t, uint8_t) {
    TFT_eSPI &g = *tft.native();
    g.fillScreen(0);
    g.setTextFont(1);
    g.setTextSize(2);
    g.setTextColor(0xFFFF);
    g.setTextDatum(TL_DATUM);
    g.drawString("BUS " + data.line.shortName, 10, 10);
    g.drawString(idfmAscii(data.stop.name), 10, 40);
    g.drawString(String((int)data.departures.size()) + " departs", 10, 70);
    g.drawString(idfmAscii(data.error), 10, 100);
}

void busBoardTick(const BoardData &, time_t) {}

void busBoardRelease() {}

void busBoardSampleData(BoardData &out) {
    out = BoardData();
    out.stop.name = "Demo";
}

void busBoardDemo() {
    BoardData data;
    busBoardSampleData(data);
    busBoardDraw(data, primNow());
    while (!check(AnyKeyPress)) delay(20);
}