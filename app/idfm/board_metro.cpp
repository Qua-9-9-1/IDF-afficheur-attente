// PLACEHOLDER - to be replaced by the real metro board renderer.
#include "board_metro.h"

#include "board_rer.h"
#include "prim_client.h"

#include "core/mykeyboard.h"
#include <globals.h>

void metroBoardDraw(const BoardData &data, time_t now, uint8_t) {
    TFT_eSPI &g = *tft.native();
    g.fillScreen(0);
    g.setTextFont(1);
    g.setTextSize(2);
    g.setTextColor(0xFFFF);
    g.setTextDatum(TL_DATUM);
    g.drawString("METRO " + data.line.shortName, 10, 10);
    g.drawString(idfmAscii(data.stop.name), 10, 40);
    g.drawString("TODO", 10, 70);
}

void metroBoardTick(const BoardData &, time_t) {}

void metroBoardRelease() {}

void metroBoardDemo() {
    BoardData data;
    data.line.shortName = "1";
    data.stop.name = "Chatelet";
    metroBoardDraw(data, primNow());
    while (!check(AnyKeyPress)) delay(20);
}
