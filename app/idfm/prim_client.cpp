#include "prim_client.h"

#include "core/sd_functions.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <algorithm>
#include <esp_heap_caps.h>
#include <string.h>
#include <strings.h>

// NOTE: user-facing messages are plain ASCII French on purpose: Bruce's TFT
// fonts do not render accented characters.

namespace {

const char *const kPrimBaseUrl = "https://prim.iledefrance-mobilites.fr/marketplace";
const uint32_t kHttpTimeoutMs = 10000;
const uint32_t kTimeMaxAgeMs = 6UL * 3600UL * 1000UL; // re-sync NTP every 6 h
const time_t kMinValidEpoch = 1704067200;             // 2024-01-01T00:00:00Z
const int kApproachingSeconds = 60;                   // "A l'approche" threshold
const int kDelayedSeconds = 60;                       // expected - aimed >= 1 min
const size_t kMaxMessages = 4;
const size_t kMaxMessageLength = 240;

// ---------------------------------------------------------------------------
// Configuration state
// ---------------------------------------------------------------------------
String s_apiKey;
uint16_t s_refreshSeconds = 60;
bool s_configLoaded = false;
String s_configError;

// ---------------------------------------------------------------------------
// UTC anchor (independent from the Bruce system clock)
// ---------------------------------------------------------------------------
time_t s_anchorUtc = 0;      // UTC epoch at s_anchorMillis
uint32_t s_anchorMillis = 0; // millis() when the anchor was taken
bool s_anchorFromNtp = false;

void setAnchor(time_t utc, bool fromNtp) {
    if (utc < kMinValidEpoch) return;
    s_anchorUtc = utc;
    s_anchorMillis = millis();
    s_anchorFromNtp = fromNtp;
}

// ---------------------------------------------------------------------------
// Calendar helpers (portable, no timegm / TZ dependency)
// ---------------------------------------------------------------------------

// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's algorithm).
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

// timegm() equivalent for already validated fields.
time_t makeUtc(int year, int month, int day, int hour, int minute, int second) {
    int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    return static_cast<time_t>(days * 86400 + hour * 3600 + minute * 60 + second);
}

// Day of month of the last Sunday of a 31-day month (March, October).
int lastSundayOf31DayMonth(int year, int month) {
    int64_t days = daysFromCivil(year, static_cast<unsigned>(month), 31);
    int weekday = static_cast<int>(((days % 7) + 7 + 4) % 7); // 1970-01-01 was a Thursday; 0 = Sunday
    return 31 - weekday;
}

// Reads exactly `n` digits; returns -1 on error.
int readDigits(const char *&p, int n) {
    int v = 0;
    for (int i = 0; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9') return -1;
        v = v * 10 + (p[i] - '0');
    }
    p += n;
    return v;
}

int monthFromAbbrev(const char *s) {
    static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; ++i) {
        if (strncasecmp(s, months[i], 3) == 0) return i + 1;
    }
    return -1;
}

// Parses an RFC 1123 HTTP date: "Tue, 10 Nov 2026 12:34:56 GMT". Returns 0 on error.
time_t parseHttpDate(const String &value) {
    const char *p = value.c_str();
    const char *comma = strchr(p, ',');
    if (comma) p = comma + 1;
    while (*p == ' ') ++p;
    int day = 0;
    while (*p >= '0' && *p <= '9') day = day * 10 + (*p++ - '0');
    if (*p++ != ' ') return 0;
    int month = monthFromAbbrev(p);
    if (month < 0) return 0;
    p += 3;
    if (*p++ != ' ') return 0;
    int year = readDigits(p, 4);
    if (year < 0 || *p++ != ' ') return 0;
    int hh = readDigits(p, 2);
    if (hh < 0 || *p++ != ':') return 0;
    int mm = readDigits(p, 2);
    if (mm < 0 || *p++ != ':') return 0;
    int ss = readDigits(p, 2);
    if (ss < 0 || day < 1 || day > 31 || hh > 23 || mm > 59 || ss > 60) return 0;
    return makeUtc(year, month, day, hh, mm, ss);
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

// ArduinoJson allocator preferring PSRAM when available (large stations can
// produce big documents even after filtering). Falls back to the default heap.
struct SpiRamAllocator : ArduinoJson::Allocator {
    void *allocate(size_t size) override {
        void *p = nullptr;
        if (psramFound()) p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p ? p : malloc(size);
    }
    void deallocate(void *ptr) override { free(ptr); } // free() handles both heaps on ESP-IDF
    void *reallocate(void *ptr, size_t newSize) override {
        void *p = nullptr;
        if (psramFound()) p = heap_caps_realloc(ptr, newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p ? p : realloc(ptr, newSize);
    }
};
SpiRamAllocator s_jsonAllocator;

// Small read buffer in front of the TLS stream: ArduinoJson reads byte per
// byte, which is slow on WiFiClientSecure. Only reads what is available so
// that the last refill never waits for the socket timeout.
class BufferedReader {
public:
    explicit BufferedReader(Client &client) : _client(client) {}

    int read() {
        if (_pos >= _len && !fill()) return -1;
        return _buf[_pos++];
    }

    size_t readBytes(char *dst, size_t length) {
        size_t done = 0;
        while (done < length) {
            if (_pos >= _len && !fill()) break;
            size_t n = std::min(length - done, _len - _pos);
            memcpy(dst + done, _buf + _pos, n);
            _pos += n;
            done += n;
        }
        return done;
    }

private:
    bool fill() {
        _pos = 0;
        _len = 0;
        int avail = _client.available();
        if (avail > 0) { // non-blocking bulk read of what is already decrypted
            int n = _client.read(_buf, std::min(static_cast<size_t>(avail), sizeof(_buf)));
            _len = n > 0 ? static_cast<size_t>(n) : 0;
        } else {
            _len = _client.readBytes(_buf, 1); // waits up to the client timeout
        }
        return _len > 0;
    }

    Client &_client;
    uint8_t _buf[512];
    size_t _len = 0;
    size_t _pos = 0;
};

// SIRI Lite wraps most texts: "x", {"value": "x"} or [{"value": "x"}, ...].
const char *siriText(JsonVariantConst v) {
    if (v.is<const char *>()) return v.as<const char *>();
    if (v.is<JsonArrayConst>()) {
        JsonArrayConst arr = v.as<JsonArrayConst>();
        if (arr.size() == 0) return "";
        return siriText(arr[0]);
    }
    if (v.is<JsonObjectConst>()) {
        JsonVariantConst inner = v["value"];
        if (inner.is<const char *>()) return inner.as<const char *>();
    }
    return "";
}

// "STIF:Line::C01742:" -> "C01742"; "IDFM:C01742" -> "C01742"; "C01742" -> "C01742".
String bareLineId(const char *ref) {
    String s(ref ? ref : "");
    while (s.endsWith(":")) s.remove(s.length() - 1);
    int idx = s.lastIndexOf(':');
    return idx >= 0 ? s.substring(idx + 1) : s;
}

String lineRefParam(const String &lineId) { return "STIF:Line::" + lineId + ":"; }

// Minimal query-string encoding (identifiers only contain [A-Za-z0-9:_-]).
String urlEncode(const String &s) {
    static const char *hex = "0123456789ABCDEF";
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        if (isalnum(static_cast<unsigned char>(c)) || c == ':' || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            out += c;
        } else {
            out += '%';
            out += hex[(static_cast<uint8_t>(c) >> 4) & 0xF];
            out += hex[static_cast<uint8_t>(c) & 0xF];
        }
    }
    return out;
}

String httpErrorMessage(int code) {
    switch (code) {
        case 400: return "Requete refusee (400)";
        case 401:
        case 403: return "Cle API invalide (" + String(code) + ")";
        case 404: return "Arret inconnu (404)";
        case 429: return "Quota API depasse (429)";
        default: break;
    }
    if (code >= 500) return "Serveur PRIM indispo (" + String(code) + ")";
    if (code < 0) return "Connexion PRIM echouee";
    return "Erreur HTTP " + String(code);
}

// Performs a GET on the PRIM API and deserializes the filtered JSON body.
// Returns the HTTP status code (or a negative HTTPClient error code); on any
// failure `error` is filled. Also anchors the UTC clock on the HTTP Date header.
int primGetJson(const String &url, JsonDocument &doc, const JsonDocument &filter, String &error) {
    error = "";
    if (!WiFi.isConnected()) {
        error = "Pas de Wi-Fi";
        return -1;
    }
    if (!s_configLoaded && !primLoadConfig()) {
        error = s_configError;
        return -1;
    }

    // PRIM uses a public CA, but Bruce ships no CA bundle for it: certificate
    // validation is skipped (the API key travels encrypted, the server is not
    // authenticated). TODO: pin the ISRG/DigiCert root if this becomes a concern.
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.useHTTP10(true); // no chunked transfer encoding -> body can be streamed to ArduinoJson
    http.setReuse(false);
    http.setConnectTimeout(kHttpTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(client, url)) {
        error = "URL PRIM invalide";
        return -1;
    }
    http.addHeader("apikey", s_apiKey);
    http.addHeader("Accept", "application/json");
    const char *headerKeys[] = {"Date"};
    http.collectHeaders(headerKeys, 1);

    int code = http.GET();
    if (code > 0 && !s_anchorFromNtp) {
        time_t serverTime = parseHttpDate(http.header("Date"));
        if (serverTime) setAnchor(serverTime, false);
    }
    if (code != HTTP_CODE_OK) {
        error = httpErrorMessage(code);
        log_w("PRIM GET failed: %d", code);
        http.end();
        return code;
    }

    BufferedReader reader(http.getStream());
    DeserializationError err = deserializeJson(
        doc, reader, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(20)
    );
    http.end();
    if (err) {
        log_w("PRIM JSON error: %s", err.c_str());
        error = err == DeserializationError::NoMemory ? "Memoire insuffisante" : "Reponse PRIM illisible";
        return -2;
    }
    return code;
}

// Keeps a SIRI ErrorCondition as a short error text (field layout not
// guaranteed: try the usual SIRI paths, then a generic message).
String siriErrorText(JsonVariantConst errorCondition) {
    const char *text = siriText(errorCondition["ErrorInformation"]["ErrorText"]);
    if (!*text) text = siriText(errorCondition["Description"]);
    String msg = "Erreur PRIM";
    if (*text) {
        msg += ": ";
        msg += text;
        if (msg.length() > 60) msg = msg.substring(0, 60);
    }
    return msg;
}

// ---------------------------------------------------------------------------
// Departures mapping
// ---------------------------------------------------------------------------

bool statusIs(const char *status, const char *value) { return status && strcasecmp(status, value) == 0; }

DepartureStatus mapStatus(
    const char *departureStatus, const char *arrivalStatus, bool atStop, time_t aimed, time_t expected,
    time_t now
) {
    const char *st = (departureStatus && *departureStatus) ? departureStatus : arrivalStatus;
    if (statusIs(st, "cancelled") || statusIs(st, "missed") || statusIs(st, "notExpected")) {
        return DepartureStatus::Cancelled;
    }
    if (atStop) return DepartureStatus::AtStop;
    time_t t = expected ? expected : aimed;
    if (now && t && t - now <= kApproachingSeconds) return DepartureStatus::Approaching;
    if (statusIs(st, "delayed")) return DepartureStatus::Delayed;
    if (aimed && expected && expected - aimed >= kDelayedSeconds) return DepartureStatus::Delayed;
    if (statusIs(st, "onTime") || statusIs(st, "early")) return DepartureStatus::OnTime;
    if (t) return DepartureStatus::OnTime;
    return DepartureStatus::Unknown;
}

time_t departureSortKey(const Departure &d) { return d.expected ? d.expected : d.aimed; }

void buildDeparturesFilter(JsonDocument &filter) {
    JsonObject sd = filter["Siri"]["ServiceDelivery"].to<JsonObject>();
    sd["ResponseTimestamp"] = true;
    JsonObject delivery = sd["StopMonitoringDelivery"][0].to<JsonObject>();
    delivery["ErrorCondition"] = true;
    JsonObject mvj = delivery["MonitoredStopVisit"][0]["MonitoredVehicleJourney"].to<JsonObject>();
    mvj["LineRef"] = true;
    mvj["DestinationName"] = true;
    mvj["JourneyNote"] = true;
    mvj["TrainNumbers"] = true;
    mvj["VehicleJourneyName"] = true;
    JsonObject call = mvj["MonitoredCall"].to<JsonObject>();
    call["DestinationDisplay"] = true;
    call["ExpectedDepartureTime"] = true;
    call["AimedDepartureTime"] = true;
    call["ExpectedArrivalTime"] = true;
    call["AimedArrivalTime"] = true;
    call["DepartureStatus"] = true;
    call["ArrivalStatus"] = true;
    call["VehicleAtStop"] = true;
    call["DeparturePlatformName"] = true;
    call["ArrivalPlatformName"] = true;
}

// Mission code fallback: RATP train numbers start with the 4-letter mission
// ("ZEUS12" -> "ZEUS").
String missionFromTrainNumber(const String &trainNumber) {
    if (trainNumber.length() < 4) return "";
    for (int i = 0; i < 4; ++i) {
        char c = trainNumber[i];
        if (c < 'A' || c > 'Z') return "";
    }
    return trainNumber.substring(0, 4);
}

bool parseVisit(JsonObjectConst mvj, const String &lineId, time_t now, Departure &d) {
    String lineRef = bareLineId(siriText(mvj["LineRef"]));
    if (lineId.length() && !lineRef.equalsIgnoreCase(lineId)) return false;

    JsonObjectConst call = mvj["MonitoredCall"];
    const char *depStatus = call["DepartureStatus"] | "";
    const char *arrStatus = call["ArrivalStatus"] | "";
    if (statusIs(depStatus, "departed")) return false;

    time_t aimed = primParseIso8601(call["AimedDepartureTime"] | "");
    time_t expected = primParseIso8601(call["ExpectedDepartureTime"] | "");
    if (!aimed && !expected) { // no departure info: fall back on arrival times
        aimed = primParseIso8601(call["AimedArrivalTime"] | "");
        expected = primParseIso8601(call["ExpectedArrivalTime"] | "");
    }
    bool atStop = call["VehicleAtStop"] | false;

    d.lineRef = lineRef;
    d.destination = siriText(mvj["DestinationName"]);
    if (d.destination.isEmpty()) d.destination = siriText(call["DestinationDisplay"]);
    d.mission = siriText(mvj["JourneyNote"]);
    d.mission.trim();
    d.trainNumber = siriText(mvj["TrainNumbers"]["TrainNumberRef"]);
    if (d.trainNumber.isEmpty()) d.trainNumber = siriText(mvj["VehicleJourneyName"]);
    d.trainNumber.trim();
    if (d.mission.isEmpty()) d.mission = missionFromTrainNumber(d.trainNumber);
    d.platform = siriText(call["DeparturePlatformName"]);
    if (d.platform.isEmpty()) d.platform = siriText(call["ArrivalPlatformName"]);
    d.aimed = aimed;
    d.expected = expected;
    d.status = mapStatus(depStatus, arrStatus, atStop, aimed, expected, now);

    if (d.status == DepartureStatus::Cancelled) return aimed || expected;
    time_t t = departureSortKey(d);
    if (!t) return false;
    if (now && t < now - 60 && d.status != DepartureStatus::AtStop) return false; // already gone
    return true;
}

} // namespace

// ===========================================================================
// Configuration
// ===========================================================================

bool primLoadConfig() {
    s_configLoaded = false;
    s_configError = "";

    File file;
    if (setupSdCard() && SD.exists(PRIM_CONFIG_PATH)) file = SD.open(PRIM_CONFIG_PATH, FILE_READ);
    if (!file && LittleFS.exists(PRIM_CONFIG_PATH)) file = LittleFS.open(PRIM_CONFIG_PATH, FILE_READ);
    if (!file) {
        s_configError = "Fichier " PRIM_CONFIG_PATH " absent";
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        s_configError = "config.json invalide";
        return false;
    }

    String key = doc["apiKey"] | "";
    key.trim();
    if (key.isEmpty() || key.startsWith("PUT_YOUR")) {
        s_configError = "apiKey manquante";
        return false;
    }
    int refresh = doc["refreshSeconds"] | 60;
    s_refreshSeconds = static_cast<uint16_t>(std::max(15, std::min(3600, refresh)));
    s_apiKey = key;
    s_configLoaded = true;
    return true;
}

bool primHasConfig() { return s_configLoaded; }

const String &primApiKey() { return s_apiKey; }

uint16_t primRefreshSeconds() { return s_refreshSeconds; }

const String &primConfigError() { return s_configError; }

// ===========================================================================
// Departures
// ===========================================================================

bool primFetchDepartures(const Line &line, const Stop &stop, BoardData &out) {
    out.line = line;
    out.stop = stop;

    if (stop.monitoringRef.isEmpty()) {
        out.error = "Arret non configure";
        return false;
    }
    primEnsureTime();

    JsonDocument filter;
    buildDeparturesFilter(filter);

    String baseUrl = String(kPrimBaseUrl) + "/stop-monitoring?MonitoringRef=" + urlEncode(stop.monitoringRef);
    JsonDocument doc(&s_jsonAllocator);
    String error;
    int code;
    if (line.id.length()) {
        // Server-side line filter keeps the response small at big stations.
        // Some MonitoringRef/LineRef pairs are rejected: retry without it.
        code = primGetJson(baseUrl + "&LineRef=" + urlEncode(lineRefParam(line.id)), doc, filter, error);
        if (code == 400 || code == 404) {
            doc.clear();
            code = primGetJson(baseUrl, doc, filter, error);
        }
    } else {
        code = primGetJson(baseUrl, doc, filter, error);
    }
    if (code != HTTP_CODE_OK) {
        out.error = error;
        return false;
    }

    JsonObjectConst sd = doc["Siri"]["ServiceDelivery"];
    if (!s_anchorFromNtp) {
        time_t serverTime = primParseIso8601(sd["ResponseTimestamp"] | "");
        if (serverTime) setAnchor(serverTime, false);
    }
    time_t now = primNow();

    std::vector<Departure> departures;
    bool sawError = false;
    String siriError;
    for (JsonObjectConst delivery : sd["StopMonitoringDelivery"].as<JsonArrayConst>()) {
        if (!delivery["ErrorCondition"].isNull()) {
            sawError = true;
            siriError = siriErrorText(delivery["ErrorCondition"]);
        }
        for (JsonObjectConst visit : delivery["MonitoredStopVisit"].as<JsonArrayConst>()) {
            Departure d;
            if (parseVisit(visit["MonitoredVehicleJourney"], line.id, now, d)) departures.push_back(d);
        }
    }
    doc.clear();

    if (departures.empty() && sawError) {
        out.error = siriError;
        return false;
    }

    std::stable_sort(departures.begin(), departures.end(), [](const Departure &a, const Departure &b) {
        return departureSortKey(a) < departureSortKey(b);
    });
    if (departures.size() > PRIM_MAX_DEPARTURES) departures.resize(PRIM_MAX_DEPARTURES);

    out.departures = std::move(departures);
    out.fetchedAt = now;
    out.error = "";
    return true;
}

// ===========================================================================
// Traffic messages (general-message, "Messages affiches sur les ecrans")
// ===========================================================================
// Field paths follow the SIRI Lite GeneralMessage structure; they could not be
// checked against a live response yet (see docs/IDFM_API.md).

bool primFetchTrafficMessages(const Line &line, BoardData &out) {
    if (line.id.isEmpty()) return false;

    JsonDocument filter;
    JsonObject delivery = filter["Siri"]["ServiceDelivery"]["GeneralMessageDelivery"][0].to<JsonObject>();
    JsonObject message = delivery["InfoMessage"][0]["Content"]["Message"][0].to<JsonObject>();
    message["MessageType"] = true;
    message["MessageText"] = true;

    String url = String(kPrimBaseUrl) + "/general-message?LineRef=" + urlEncode(lineRefParam(line.id));
    JsonDocument doc(&s_jsonAllocator);
    String error;
    if (primGetJson(url, doc, filter, error) != HTTP_CODE_OK) {
        log_w("PRIM general-message: %s", error.c_str());
        return false;
    }

    std::vector<String> messages;
    JsonArrayConst deliveries = doc["Siri"]["ServiceDelivery"]["GeneralMessageDelivery"];
    for (JsonObjectConst gmd : deliveries) {
        for (JsonObjectConst info : gmd["InfoMessage"].as<JsonArrayConst>()) {
            // Prefer the short version of a message when several are provided.
            String text;
            for (JsonObjectConst m : info["Content"]["Message"].as<JsonArrayConst>()) {
                const char *t = siriText(m["MessageText"]);
                if (!*t) continue;
                if (text.isEmpty() || statusIs(m["MessageType"] | "", "shortMessage")) text = t;
            }
            text.trim();
            if (text.isEmpty()) continue;
            if (text.length() > kMaxMessageLength) text = text.substring(0, kMaxMessageLength - 3) + "...";
            if (std::find(messages.begin(), messages.end(), text) != messages.end()) continue;
            messages.push_back(text);
            if (messages.size() >= kMaxMessages) break;
        }
        if (messages.size() >= kMaxMessages) break;
    }
    out.messages = std::move(messages);
    return true;
}

// ===========================================================================
// Mock data
// ===========================================================================

void primMockDepartures(BoardData &out) {
    out.line.id = "C01742";
    out.line.shortName = "A";
    out.line.color = 0xE023;     // RER A red #E3051C in RGB565
    out.line.textColor = 0xFFFF; // white
    out.line.mode = TransportMode::Rer;
    out.stop.name = "Chatelet les Halles";
    out.stop.monitoringRef = "STIF:StopArea:SP:474151:";

    time_t now = primNow();
    if (!now) now = makeUtc(2026, 11, 10, 7, 30, 0); // arbitrary weekday morning

    struct MockRow {
        const char *mission;
        const char *train;
        const char *destination;
        const char *platform;
        int aimedOffset;    // seconds from now
        int expectedOffset; // seconds from now (ignored when cancelled)
        DepartureStatus status;
    };
    static const MockRow rows[] = {
        {"QIKI", "QIKI24", "Boissy-Saint-Leger",     "2", 0,    20,   DepartureStatus::AtStop     },
        {"ZEUS", "ZEUS31", "Marne-la-Vallee Chessy", "2", 45,   50,   DepartureStatus::Approaching},
        {"NELY", "NELY12", "Saint-Germain-en-Laye",  "1", 180,  180,  DepartureStatus::OnTime     },
        {"TEDY", "TEDY08", "Cergy-le-Haut",          "1", 300,  540,  DepartureStatus::Delayed    },
        {"QIKI", "QIKI26", "Boissy-Saint-Leger",     "2", 420,  420,  DepartureStatus::OnTime     },
        {"UPAC", "UPAC74", "Poissy",                 "1", 480,  0,    DepartureStatus::Cancelled  },
        {"ZEUS", "ZEUS33", "Marne-la-Vallee Chessy", "2", 600,  660,  DepartureStatus::Delayed    },
        {"NELY", "NELY14", "Saint-Germain-en-Laye",  "1", 780,  780,  DepartureStatus::OnTime     },
        {"OPAL", "OPAL41", "Torcy",                  "2", 900,  900,  DepartureStatus::OnTime     },
        {"TEDY", "TEDY10", "Cergy-le-Haut",          "1", 1080, 1080, DepartureStatus::Unknown    },
    };

    out.departures.clear();
    for (const MockRow &r : rows) {
        Departure d;
        d.lineRef = out.line.id;
        d.mission = r.mission;
        d.trainNumber = r.train;
        d.destination = r.destination;
        d.platform = r.platform;
        d.aimed = now + r.aimedOffset;
        d.expected = r.status == DepartureStatus::Cancelled ? 0 : now + r.expectedOffset;
        d.status = r.status;
        out.departures.push_back(d);
    }
    out.messages = {
        "Trafic perturbe entre Nanterre-Prefecture et Cergy-le-Haut (incident technique).",
        "Travaux : pas de trains entre La Defense et Auber les 14 et 15 novembre.",
    };
    out.fetchedAt = now;
    out.error = "";
}

// ===========================================================================
// Time
// ===========================================================================

bool primTimeValid() { return s_anchorUtc >= kMinValidEpoch; }

time_t primNow() {
    if (!primTimeValid()) return 0;
    return s_anchorUtc + static_cast<time_t>((millis() - s_anchorMillis) / 1000UL);
}

bool primEnsureTime() {
    if (s_anchorFromNtp && primTimeValid() && millis() - s_anchorMillis < kTimeMaxAgeMs) return true;
    if (!WiFi.isConnected()) return primTimeValid();

    // Private NTP client (UTC, offset 0) on its own local port so it does not
    // interfere with Bruce's global `timeClient`.
    WiFiUDP udp;
    NTPClient ntp(udp, "pool.ntp.org", 0, 0);
    ntp.begin(2390);
    bool ok = false;
    for (int attempt = 0; attempt < 2 && !ok; ++attempt) ok = ntp.forceUpdate();
    if (ok) setAnchor(static_cast<time_t>(ntp.getEpochTime()), true);
    ntp.end();
    if (!ok) log_w("PRIM NTP sync failed");
    return primTimeValid();
}

bool primLocalTime(time_t utc, struct tm &out) {
    if (utc <= 0) return false;
    struct tm u;
    gmtime_r(&utc, &u);
    int year = u.tm_year + 1900;
    // EU rule: CEST from last Sunday of March 01:00 UTC to last Sunday of October 01:00 UTC.
    time_t dstStart = makeUtc(year, 3, lastSundayOf31DayMonth(year, 3), 1, 0, 0);
    time_t dstEnd = makeUtc(year, 10, lastSundayOf31DayMonth(year, 10), 1, 0, 0);
    bool dst = utc >= dstStart && utc < dstEnd;
    time_t local = utc + (dst ? 7200 : 3600);
    gmtime_r(&local, &out);
    out.tm_isdst = dst ? 1 : 0;
    return true;
}

time_t primParseIso8601(const char *s) {
    if (!s || !*s) return 0;
    const char *p = s;
    int year = readDigits(p, 4);
    if (year < 0 || *p++ != '-') return 0;
    int month = readDigits(p, 2);
    if (month < 1 || month > 12 || *p++ != '-') return 0;
    int day = readDigits(p, 2);
    if (day < 1 || day > 31) return 0;
    if (*p != 'T' && *p != 't' && *p != ' ') return 0;
    ++p;
    int hour = readDigits(p, 2);
    if (hour < 0 || hour > 23 || *p++ != ':') return 0;
    int minute = readDigits(p, 2);
    if (minute < 0 || minute > 59) return 0;
    int second = 0;
    if (*p == ':') {
        ++p;
        second = readDigits(p, 2);
        if (second < 0 || second > 60) return 0;
    }
    if (*p == '.' || *p == ',') { // fractional seconds: ignored
        ++p;
        while (*p >= '0' && *p <= '9') ++p;
    }
    int offsetSeconds = 0;
    if (*p == '+' || *p == '-') {
        int sign = *p == '-' ? -1 : 1;
        ++p;
        int oh = readDigits(p, 2);
        if (oh < 0) return 0;
        if (*p == ':') ++p;
        int om = 0;
        if (*p >= '0' && *p <= '9') {
            om = readDigits(p, 2);
            if (om < 0) return 0;
        }
        offsetSeconds = sign * (oh * 3600 + om * 60);
    } else if (*p == 'Z' || *p == 'z') {
        ++p;
    } // no designator: treated as UTC (PRIM always sends "Z")
    return makeUtc(year, month, day, hour, minute, second) - offsetSeconds;
}
