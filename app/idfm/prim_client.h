#pragma once
// Client for the PRIM API (Ile-de-France Mobilites).
//   - configuration read from /idfm/config.json (SD card, LittleFS fallback)
//   - "Prochains passages" (SIRI Lite stop-monitoring) -> BoardData
//   - "Messages affiches sur les ecrans" (SIRI Lite general-message) -> BoardData::messages
//   - UTC clock independent from the Bruce system clock (see primEnsureTime)
// See docs/IDFM_API.md for the verified API details.

#include "idfm_model.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

// Path of the configuration file (same path on SD and LittleFS).
#define PRIM_CONFIG_PATH "/idfm/config.json"

// Loads PRIM_CONFIG_PATH from the SD card (falls back to LittleFS).
// Returns false if the file is missing/invalid or has no usable "apiKey".
bool primLoadConfig();

// True once primLoadConfig() succeeded.
bool primHasConfig();

// API key ("apiKey"). Empty if not loaded. Never log it.
const String &primApiKey();

// Board refresh period ("refreshSeconds"), clamped to [10, 3600]. Default 60.
uint16_t primRefreshSeconds();

// Human readable reason of the last primLoadConfig() failure (French, short).
const String &primConfigError();

// ---------------------------------------------------------------------------
// Departures
// ---------------------------------------------------------------------------

// Maximum number of departures kept in BoardData::departures.
#define PRIM_MAX_DEPARTURES 20

// Fetches the next departures of `line` at `stop`.
// On success: out.departures is replaced (filtered on line.id, sorted by time,
// at most PRIM_MAX_DEPARTURES), out.fetchedAt is set, out.error is cleared and
// true is returned. out.line / out.stop are set to the arguments.
// On failure: out.error holds a short French message, out.departures is left
// untouched (the UI can keep showing the previous data) and false is returned.
// Blocking (TLS handshake + download): typically 1-3 s.
bool primFetchDepartures(const Line &line, const Stop &stop, BoardData &out);

// Fetches the traffic messages displayed on official screens for `line`
// (general-message endpoint) into out.messages (at most a few short lines).
// Returns false on failure; out.messages is then left untouched and out.error
// is NOT modified (messages are optional information).
bool primFetchTrafficMessages(const Line &line, BoardData &out);

// Fills `out` with realistic RER A sample data (Chatelet les Halles) for
// offline UI testing. Times are relative to primNow() (or to a fixed date if
// the clock is not synchronized).
void primMockDepartures(BoardData &out);

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------
// Bruce stores the *local* wall-clock time in the system clock (settimeofday
// with an epoch shifted by bruceConfig.tmz/dst) and does not set TZ. Calling
// configTime()/setenv("TZ") would therefore break Bruce's status bar clock.
// This module keeps its own UTC reference instead: an anchor (UTC epoch +
// millis()) obtained from a private NTP client, or from the server timestamp of
// the last PRIM response. The system clock is never modified.

// Makes sure a UTC reference is available, querying NTP (pool.ntp.org) if
// Wi-Fi is connected and the reference is missing or older than 6 hours.
// Blocking up to ~2 s. Returns true if primNow() is usable.
bool primEnsureTime();

// True if a UTC reference is available (NTP or PRIM server timestamp).
bool primTimeValid();

// Current UTC epoch, or 0 if unknown.
time_t primNow();

// Converts a UTC epoch to Europe/Paris local time (CET/CEST, EU rules),
// without touching the TZ environment. Returns false if utc == 0.
bool primLocalTime(time_t utc, struct tm &out);

// Parses an ISO-8601 timestamp ("2024-05-02T14:03:00.000Z",
// "2024-05-02T16:03:00+02:00"...) into a UTC epoch. Returns 0 on error.
time_t primParseIso8601(const char *s);
