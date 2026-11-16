#include "idfm_app.h"

#include "board_bus.h"
#include "board_metro.h"
#include "board_rer.h"
#include "board_tram.h"
#include "board_transilien.h"
#include "idfm_model.h"
#include "prim_client.h"
#include "referential.h"
#include "selection.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/wifi/wifi_common.h"
#include <globals.h>

namespace {

// Traffic messages change rarely: fetch them once every N departure fetches (saves API quota).
constexpr uint8_t MESSAGES_EVERY_N_FETCHES = 5;

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

// ---------------------------------------------------------------------------------------
// Board renderers, one per transport mode (same lifecycle for every board)
// ---------------------------------------------------------------------------------------
struct BoardRenderer {
    void (*draw)(const BoardData &, time_t, uint8_t);
    void (*tick)(const BoardData &, time_t);
    void (*release)();
    void (*sampleData)(BoardData &); // offline demo data (nullptr: the board has its own demo)
    void (*offlineDemo)();
    uint8_t visibleRows; // departures shown at once, bounds the encoder scrolling
    bool allLines;       // multi-line board: query every line of the stop (bus shelters)
};

void rerSample(BoardData &out) { primMockDepartures(out); }

const BoardRenderer &rendererFor(TransportMode mode) {
    static const BoardRenderer rer = {rerBoardDraw,    rerBoardTick, rerBoardRelease, rerSample,
                                      rerBoardDemo,    RER_BOARD_ROWS, false};
    static const BoardRenderer metro = {metroBoardDraw, metroBoardTick, metroBoardRelease, nullptr,
                                        metroBoardDemo, 2,              false};
    static const BoardRenderer transilien = {
        transilienBoardDraw, transilienBoardTick, transilienBoardRelease, transilienBoardSampleData,
        transilienBoardDemo, TRANSILIEN_BOARD_ROWS, false};
    static const BoardRenderer tram = {tramBoardDraw,    tramBoardTick, tramBoardRelease, tramBoardSampleData,
                                       tramBoardDemo,    TRAM_BOARD_ROWS, false};
    static const BoardRenderer bus = {busBoardDraw,    busBoardTick, busBoardRelease, busBoardSampleData,
                                      busBoardDemo,    BUS_BOARD_ROWS, true};
    switch (mode) {
        case TransportMode::Metro: return metro;
        case TransportMode::Transilien: return transilien;
        case TransportMode::Tram: return tram;
        case TransportMode::Bus: return bus;
        default: return rer;
    }
}

// Multi-line boards: name and colour every line present in the departures (cached).
void resolveLines(BoardData &data) {
    for (const Departure &d : data.departures) {
        if (d.lineRef.isEmpty()) continue;
        bool known = false;
        for (const Line &l : data.lines) known = known || l.id == d.lineRef;
        if (known) continue;
        Line l;
        if (!idfmFindLine(d.lineRef, l)) { // unknown to the referential: PRIM name, default colours
            l.id = d.lineRef;
            l.shortName = d.lineName;
            l.mode = data.line.mode;
            l.color = 0;
            l.textColor = 0;
        }
        data.lines.push_back(l);
    }
}

// Live board for one line at one stop. Returns when the user presses Esc.
//   encoder: scroll departures · click: force refresh · Esc: back
void runBoard(const Line &line, const Stop &stop) {
    const BoardRenderer &board = rendererFor(line.mode);
    if (!primLoadConfig()) {
        showMessage("IDFM Board", primConfigError(), "SD: " PRIM_CONFIG_PATH);
        return;
    }
    if (!wifiConnected) wifiConnectMenu();

    // Bus shelters show every line of the stop: query without the line filter.
    Line query = line;
    if (board.allLines) query.id = "";

    BoardData data;
    data.line = line;
    data.stop = stop;
    uint8_t scroll = 0;
    uint8_t fetchCount = 0;
    uint32_t nextFetchMs = 0; // fetch immediately
    const uint32_t periodMs = (uint32_t)primRefreshSeconds() * 1000UL;

    board.draw(data, primNow(), scroll); // "Chargement..." state

    while (true) {
        if ((int32_t)(millis() - nextFetchMs) >= 0) {
            primEnsureTime();
            primFetchDepartures(query, stop, data);
            data.line = line; // primFetchDepartures() copies the query line
            if (board.allLines) resolveLines(data);
            if (fetchCount++ % MESSAGES_EVERY_N_FETCHES == 0) primFetchTrafficMessages(line, data);
            board.draw(data, primNow(), scroll);
            nextFetchMs = millis() + periodMs;
        }

        if (check(EscPress)) break;
        if (check(NextPress)) {
            if (scroll + board.visibleRows < data.departures.size()) scroll++;
            board.draw(data, primNow(), scroll);
        } else if (check(PrevPress)) {
            if (scroll > 0) scroll--;
            board.draw(data, primNow(), scroll);
        } else if (check(SelPress)) {
            nextFetchMs = millis(); // manual refresh
        }

        board.tick(data, primNow());
        delay(20);
    }
    board.release();
}

// Mode flow: line -> stop -> direction (platforms) -> board, one level back on Esc.
void runMode(TransportMode mode) {
    Line line;
    while (selectLine(mode, line)) {
        Stop stop;
        while (selectStop(line, stop)) {
            // selectDirection() needs no UI with 0 or 1 direction: back from the board then
            // returns straight to the stop list instead of looping.
            const bool directionMenu = stop.directions.size() > 1;
            Stop platform = stop;
            while (selectDirection(line, platform)) {
                runBoard(line, platform);
                if (!directionMenu) break;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------
// Live demos: a preset line / stop / direction per mode, real API data
// ---------------------------------------------------------------------------------------
struct DemoPreset {
    TransportMode mode;
    const char *line;      // short name as in the referential
    const char *stop;      // ASCII, case-insensitive substring of the stop name
    uint8_t direction;     // index in Stop::directions (when the stop has some)
};

constexpr DemoPreset kDemoPresets[] = {
    {TransportMode::Rer,        "A",  "chatelet",      0},
    {TransportMode::Metro,      "4",  "chatelet",      0},
    {TransportMode::Transilien, "L",  "saint-lazare",  0},
    {TransportMode::Tram,       "T3a", "porte d'italie", 0},
    {TransportMode::Bus,        "72", "hotel de ville", 0},
};

String folded(const String &s) {
    String a = idfmAscii(s);
    a.toLowerCase();
    return a;
}

// Resolves the preset against the referential. False when the line is unknown.
bool resolvePreset(const DemoPreset &p, Line &line, Stop &stop) {
    std::vector<Line> lines;
    idfmLoadLines(p.mode, lines);
    const String wanted = folded(p.line);
    bool found = false;
    for (const Line &l : lines) {
        if (folded(l.shortName) == wanted) {
            line = l;
            found = true;
            break;
        }
    }
    if (!found) return false;

    std::vector<Stop> stops;
    idfmLoadStops(line, stops);
    if (stops.empty()) return false;
    stop = stops[stops.size() / 2]; // fallback: a stop in the middle of the line
    for (const Stop &s : stops) {
        if (folded(s.name).indexOf(p.stop) >= 0) {
            stop = s;
            break;
        }
    }
    if (!stop.directions.empty()) {
        const Direction &d = stop.directions[p.direction < stop.directions.size() ? p.direction : 0];
        stop.monitoringRef = d.monitoringRef;
        stop.direction = d.name;
    }
    return true;
}

} // namespace

void idfmLiveDemo(TransportMode mode) {
    const BoardRenderer &board = rendererFor(mode);
    for (const DemoPreset &p : kDemoPresets) {
        if (p.mode != mode) continue;
        Line line;
        Stop stop;
        if (primLoadConfig() && resolvePreset(p, line, stop)) {
            Serial.printf("[idfm] live demo: %s @ %s %s\n", line.shortName.c_str(), stop.name.c_str(),
                          stop.monitoringRef.c_str());
            runBoard(line, stop);
            return;
        }
        Serial.println(primHasConfig() ? "[idfm] demo preset not found in the referential: offline demo"
                                       : "[idfm] no API key: offline demo");
        break;
    }
    board.offlineDemo();
}

void idfm_setup() {
    std::vector<Option> demos = {
        {"Metro (direct)",      []() { idfmLiveDemo(TransportMode::Metro); }     },
        {"RER (direct)",        []() { idfmLiveDemo(TransportMode::Rer); }       },
        {"Transilien (direct)", []() { idfmLiveDemo(TransportMode::Transilien); }},
        {"Tram (direct)",       []() { idfmLiveDemo(TransportMode::Tram); }      },
        {"Bus (direct)",        []() { idfmLiveDemo(TransportMode::Bus); }       },
    };
    std::vector<Option> modes = {
        {"Metro",      []() { runMode(TransportMode::Metro); }     },
        {"RER",        []() { runMode(TransportMode::Rer); }       },
        {"Transilien", []() { runMode(TransportMode::Transilien); }},
        {"Tram",       []() { runMode(TransportMode::Tram); }      },
        {"Bus",        []() { runMode(TransportMode::Bus); }       },
        {"Demos",      [&demos]() { loopOptions(demos, MENU_TYPE_SUBMENU, "Demos"); }},
    };
    loopOptions(modes, MENU_TYPE_SUBMENU, "IDFM Board");
}
