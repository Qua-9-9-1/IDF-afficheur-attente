#pragma once
// Shared data model of the IDFM board app. Contract between:
//   - prim_client  (fills Departure / BoardData from the PRIM API)
//   - referential  (fills Line / Stop from the SD card referential)
//   - board_rer    (renders BoardData like an official RER platform display)
//   - board_metro  (renders BoardData like an official metro platform display)
//   - board_transilien / board_tram / board_bus (same for the other modes)

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

// One direction of a line at a station = one platform (metro displays are per platform).
struct Direction {
    String name;          // terminus / direction label shown as "Direction <name>", e.g. "La Défense"
    String monitoringRef; // platform MonitoringRef, e.g. "STIF:StopPoint:Q:22092:"
};

struct Stop {
    String name;          // "Châtelet les Halles"
    String monitoringRef; // MonitoringRef actually queried: the stop area for RER
                          // ("STIF:StopArea:SP:45102:"), the chosen platform for metro
    String direction;     // chosen direction name (empty when not applicable, e.g. RER)
    std::vector<Direction> directions; // available directions (empty: no direction choice)
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
    String lineName;     // public line name from PRIM (PublishedLineName), e.g. "72" (may be empty)
    String mission;      // RER mission code from JourneyNote, e.g. "ZEUS" (may be empty)
    String trainNumber;  // e.g. "ZEUS12" (may be empty)
    String destination;  // display name of the terminus
    String platform;     // "A", "2"... (may be empty)
    time_t aimed = 0;    // aimed departure, UTC epoch (0 = unknown)
    time_t expected = 0; // expected departure, UTC epoch (0 = unknown)
    DepartureStatus status = DepartureStatus::Unknown;
};

struct BoardData {
    Line line;  // line chosen by the user (multi-line boards: used for the header only)
    Stop stop;
    // Multi-line boards (bus shelters show every line of the stop): line info (name, colours) of
    // the lines present in `departures`, filled by the app via idfmFindLine(). Empty otherwise.
    std::vector<Line> lines;
    std::vector<Departure> departures; // sorted by expected (or aimed) time
    time_t fetchedAt = 0;              // UTC epoch of the successful fetch
    String error;                      // non-empty when the last fetch failed
    std::vector<String> messages;      // traffic info lines to scroll (may be empty)
};
