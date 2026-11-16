#pragma once
// Shared data model of the IDFM board app. Contract between:
//   - prim_client  (fills Departure / BoardData from the PRIM API)
//   - referential  (fills Line / Stop from the SD card referential)
//   - board_rer    (renders BoardData like an official RER platform display)

#include <Arduino.h>
#include <time.h>
#include <vector>

enum class TransportMode : uint8_t { Metro, Rer, Transilien, Tram, Bus };

struct Line {
    String id;        // PRIM line id, e.g. "C01742" (LineRef = "STIF:Line::C01742:")
    String shortName; // "A", "B", ... as shown in the line pictogram
    uint16_t color;   // RGB565 background of the line pictogram
    uint16_t textColor; // RGB565 text color of the line pictogram
    TransportMode mode;
};

struct Stop {
    String name;          // "Châtelet les Halles"
    String monitoringRef; // full MonitoringRef, e.g. "STIF:StopArea:SP:474151:"
};

enum class DepartureStatus : uint8_t {
    OnTime,
    Delayed,    // expected later than aimed
    Cancelled,  // "Supprimé"
    Approaching,// "À l'approche"
    AtStop,     // "À quai"
    Unknown,
};

struct Departure {
    String lineRef;      // "C01742" (bare id, without STIF:Line:: prefix)
    String mission;      // RER mission code from JourneyNote, e.g. "ZEUS" (may be empty)
    String trainNumber;  // e.g. "ZEUS12" (may be empty)
    String destination;  // display name of the terminus
    String platform;     // "A", "2"... (may be empty)
    time_t aimed = 0;    // aimed departure, UTC epoch (0 = unknown)
    time_t expected = 0; // expected departure, UTC epoch (0 = unknown)
    DepartureStatus status = DepartureStatus::Unknown;
};

struct BoardData {
    Line line;
    Stop stop;
    std::vector<Departure> departures; // sorted by expected (or aimed) time
    time_t fetchedAt = 0;              // UTC epoch of the successful fetch
    String error;                      // non-empty when the last fetch failed
    std::vector<String> messages;      // traffic info lines to scroll (may be empty)
};
