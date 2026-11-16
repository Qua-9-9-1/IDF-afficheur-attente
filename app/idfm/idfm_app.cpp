#include "idfm_app.h"

#include "board_rer.h"
#include "idfm_model.h"
#include "prim_client.h"
#include "selection.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/wifi/wifi_common.h"
#include <globals.h>

namespace {

// Traffic messages change rarely: fetch them once every N departure fetches (saves API quota).
constexpr uint8_t MESSAGES_EVERY_N_FETCHES = 5;

// Placeholder for modes whose board is not implemented yet.
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

// Short blocking message screen, dismissed by any key.
void showMessage(const char *title, const String &line1, const String &line2 = "") {
    drawMainBorderWithTitle(title);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FP);
    tft.setCursor(10, 40);
    tft.print(line1);
    tft.setCursor(10, 60);
    tft.print(line2);
    tft.setCursor(10, 100);
    tft.print("Press any key to go back");
    while (!check(AnyKeyPress)) { delay(50); }
}

// Live board for one line at one stop. Returns when the user presses Esc.
//   encoder: scroll departures · click: force refresh · Esc: back to stop list
void runRerBoard(const Line &line, const Stop &stop) {
    if (!primLoadConfig()) {
        showMessage("IDFM Board", primConfigError(), "SD: " PRIM_CONFIG_PATH);
        return;
    }
    if (!wifiConnected) wifiConnectMenu();

    BoardData data;
    data.line = line;
    data.stop = stop;
    uint8_t scroll = 0;
    uint8_t fetchCount = 0;
    uint32_t nextFetchMs = 0; // fetch immediately
    const uint32_t periodMs = (uint32_t)primRefreshSeconds() * 1000UL;

    rerBoardDraw(data, primNow(), scroll); // "Chargement..." state

    while (true) {
        if ((int32_t)(millis() - nextFetchMs) >= 0) {
            primEnsureTime();
            primFetchDepartures(line, stop, data);
            if (fetchCount++ % MESSAGES_EVERY_N_FETCHES == 0) primFetchTrafficMessages(line, data);
            rerBoardDraw(data, primNow(), scroll);
            nextFetchMs = millis() + periodMs;
        }

        if (check(EscPress)) break;
        if (check(NextPress)) {
            if (scroll + RER_BOARD_ROWS < data.departures.size()) scroll++;
            rerBoardDraw(data, primNow(), scroll);
        } else if (check(PrevPress)) {
            if (scroll > 0) scroll--;
            rerBoardDraw(data, primNow(), scroll);
        } else if (check(SelPress)) {
            nextFetchMs = millis(); // manual refresh
        }

        rerBoardTick(data, primNow());
        delay(20);
    }
    rerBoardRelease();
}

// Mode flow: line -> stop -> board, going back one level on Esc.
void runMode(TransportMode mode) {
    Line line;
    while (selectLine(mode, line)) {
        Stop stop;
        while (selectStop(line, stop)) { runRerBoard(line, stop); }
    }
}

} // namespace

void idfm_setup() {
    std::vector<Option> modes = {
        {"Metro",      []() { showPlaceholder("Metro"); }     },
        {"RER",        []() { runMode(TransportMode::Rer); }  },
        {"Transilien", []() { showPlaceholder("Transilien"); }},
        {"Tram",       []() { showPlaceholder("Tram"); }      },
        {"Bus",        []() { showPlaceholder("Bus"); }       },
        {"RER demo",   []() { rerBoardDemo(); }               },
    };
    loopOptions(modes, MENU_TYPE_SUBMENU, "IDFM Board");
}
