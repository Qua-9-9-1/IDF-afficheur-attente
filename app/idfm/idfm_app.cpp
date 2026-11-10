#include "idfm_app.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include <globals.h>

namespace {

// Placeholder screen until line/stop selection and board rendering exist.
void showPlaceholder(const char *mode) {
    drawMainBorderWithTitle("IDFM Board");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    tft.setCursor(10, 40);
    tft.print(mode);
    tft.setTextSize(FP);
    tft.setCursor(10, 70);
    tft.print("Not implemented yet");
    tft.setCursor(10, 90);
    tft.print("Press any key to go back");

    while (!check(AnyKeyPress)) { delay(50); }
}

} // namespace

void idfm_setup() {
    std::vector<Option> modes = {
        {"Metro",      []() { showPlaceholder("Metro"); }     },
        {"RER",        []() { showPlaceholder("RER"); }       },
        {"Transilien", []() { showPlaceholder("Transilien"); }},
        {"Tram",       []() { showPlaceholder("Tram"); }      },
        {"Bus",        []() { showPlaceholder("Bus"); }       },
    };
    loopOptions(modes, MENU_TYPE_SUBMENU, "IDFM Board");
}
