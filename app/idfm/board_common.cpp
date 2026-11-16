#include "board_common.h"

#include "prim_client.h"

#include <globals.h>

namespace idfmui {

int32_t textW(const String &s, uint8_t size) {
    if (s.length() == 0) return 0;
    return (int32_t)s.length() * 6 * size - size;
}

void drawText(TFT_eSPI &g, const String &s, int32_t x, int32_t y, uint8_t size, uint16_t fg) {
    g.setTextFont(1);
    g.setTextSize(size);
    g.setTextColor(fg); // transparent background: areas are pre-filled
    g.setTextDatum(TL_DATUM);
    g.drawString(s, x, y);
}

String abbreviate(String s) {
    s.replace("Sainte-", "Ste-");
    s.replace("Saint-", "St-");
    s.replace("SAINTE-", "STE-");
    s.replace("SAINT-", "ST-");
    return s;
}

String fitText(String s, int32_t maxW, uint8_t size) {
    if (textW(s, size) <= maxW) return s;
    s = abbreviate(s);
    if (textW(s, size) <= maxW) return s;
    int maxChars = (maxW + size) / (6 * size);
    if (maxChars <= 0) return String();
    if (maxChars == 1) return s.substring(0, 1);
    String t = s.substring(0, maxChars - 1);
    t.trim();
    return t + ".";
}

String clockText(time_t t) {
    if (t < 1600000000) return "--:--"; // clock not synchronised yet
    // Bruce never sets TZ (its system clock holds local time): use the explicit
    // UTC -> Europe/Paris conversion of prim_client instead of localtime_r.
    struct tm tmLocal;
    if (!primLocalTime(t, tmLocal)) return "--:--";
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &tmLocal);
    return String(buf);
}

} // namespace idfmui
