/*
  Spotify Lyrics LCD - ESP8266 + 16x2 LCD only (no other components)
  =================================================================
  Port of the Arduino Uno serial version. The PC + serial cable are gone:
  the ESP8266 polls the Spotify Web API over WiFi, fetches synced lyrics
  from lrclib (Spotify has no official lyrics API), and renders them with
  the same modes and transition effects as the Uno sketch.

  Hardware: NodeMCU v2, HD44780 16x2 LCD in 4-bit mode, nothing else.
    LCD RS -> D1 (GPIO5)   LCD E  -> D2 (GPIO4)
    LCD D4 -> D5 (GPIO14)  LCD D5 -> D6 (GPIO12)
    LCD D6 -> D7 (GPIO13)  LCD D7 -> D0 (GPIO16)
    LCD VSS/RW/V0/K -> GND, VDD/A -> VIN (5 V from USB).
  GPIO 0/2/15 are deliberately avoided: they are boot-strapping pins.
  V0 to GND = maximum contrast, the standard no-potentiometer wiring.

  Control: there are no buttons in this build. Everything is managed
  from the web page served by the ESP8266 (mode, effect, WiFi, Spotify
  link). Serial 'M' / 'E' still cycle mode/effect as a debug aid.

  Modes   : Lyrics | Karaoke | Now Playing | Visualizer
  Effects : Typewriter | Slide | Center wipe | Decode | Dissolve | Instant | Auto
  Version : 1.0.0
*/
#include <Arduino.h>
#include <memory>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <LiquidCrystal.h>
#include "web_ui.h"

// ---------------- LCD wiring ----------------
LiquidCrystal lcd(5, 4, 14, 12, 13, 16);  // RS, E, D4, D5, D6, D7

#define COLS 16
#define LYR_MAX 100
#define FRAME_MS 45
#define PAGE_MS 2600
#define MQ_STEP 320

enum { M_LYRICS, M_KARAOKE, M_NOWPLAY, M_VIZ, N_MODES };
const char* const modeNames[N_MODES] = {"Lyrics", "Karaoke", "Now Playing", "Visualizer"};

enum { FX_TYPE, FX_SLIDE, FX_CENTER, FX_DECODE, FX_DISSOLVE, FX_INSTANT, N_FX, FX_AUTO = N_FX };
const char* const fxNames[N_FX + 1] = {"Typewriter", "Slide", "Center wipe", "Decode", "Dissolve", "Instant", "Auto"};
const byte fxLast[N_FX] = {16, 8, 7, 19, 11, 0};

// ---------------- display state (ported from the Uno sketch) ----------------
byte mode = M_LYRICS;
byte fx = FX_AUTO;

char curLine[LYR_MAX + 1] = "Waiting for";
char nextLine[LYR_MAX + 1] = "Spotify...";
char nowLine[90] = "Waiting for Spotify";
char kind = 'i';  // 'l' = lyric line, 'i' = info text
byte pct = 0;
unsigned int elapsed = 0;
bool playing = false;

byte tgt[2][COLS];
byte shown[2][COLS];

bool animating = false;
byte curFx = 0, fr = 0;
unsigned long nextFrame = 0;
unsigned long splashEnd = 0;

byte chunkStart[10], chunkLen[10], nChunks = 0;
byte nPages = 1, page = 0;
unsigned long pageT = 0;
unsigned long lineMs = 0, lineStart = 0;

signed char mOff[2], mDir[2];
unsigned long mT[2];
unsigned int mStep[2] = {MQ_STEP, MQ_STEP};
bool dirty = false;

signed char lvl[COLS], tg[COLS];
unsigned long vizT = 0, beatT = 0;

// ---------------- config + Spotify state ----------------
struct Config {
  char wifiSsid[64] = "";
  char wifiPass[64] = "";
  char clientId[128] = "";
  char clientSecret[128] = "";
  char refreshToken[512] = "";
};
Config cfg;
String accessToken = "";
unsigned long tokenExpiresAt = 0;  // millis() when the access token expires
bool spotifyLinked = false;
String spotifyStatus = "Not linked";

ESP8266WebServer server(80);
bool apMode = false;
unsigned long setupScreenT = 0;
bool setupScreenAlt = false;

char trackId[64] = "";
char trackTitle[96] = "";
char trackArtist[96] = "";
long trackDurationMs = 0;
long trackProgressMs = 0;
unsigned long lastPollMs = 0;
unsigned long lastProgressTick = 0;
bool hasTrack = false;
bool spotifyPollFailed = false;

struct LyricLine { long ms; char text[80]; };
LyricLine lyrics[120];
int lyricCount = 0;
bool lyricsSynced = false;
bool lyricsFound = false;
int lyricIndex = -1;

// ---------------- low level drawing (ported) ----------------
void flushCell(byte r, byte c, byte ch) {
  if (shown[r][c] != ch) {
    lcd.setCursor(c, r);
    lcd.write((uint8_t)ch);
    shown[r][c] = ch;
  }
}
void flushTarget() {
  for (byte r = 0; r < 2; r++)
    for (byte c = 0; c < COLS; c++) flushCell(r, c, tgt[r][c]);
}
void setRow(byte r, const char* s, byte n, bool center) {
  if (n > COLS) n = COLS;
  byte pad = center ? (COLS - n) / 2 : 0;
  for (byte c = 0; c < COLS; c++) tgt[r][c] = ' ';
  for (byte i = 0; i < n; i++) tgt[r][pad + i] = (byte)s[i];
}
void setRowStr(byte r, const char* s, bool center) {
  byte n = strlen(s);
  setRow(r, s, n > COLS ? COLS : n, center);
}

void loadChars(byte set) {
  byte g[8];
  if (set == 0) {
    for (byte n = 1; n <= 5; n++) {
      byte p = (0x1F << (5 - n)) & 0x1F;
      for (byte i = 0; i < 8; i++) g[i] = (i >= 1 && i <= 6) ? p : 0;
      lcd.createChar(n - 1, g);
    }
    for (byte i = 0; i < 8; i++) g[i] = (i == 3 || i == 4) ? 0x1F : 0;
    lcd.createChar(5, g);
    const byte playIcon[8]  = {0b01000, 0b01100, 0b01110, 0b01111, 0b01110, 0b01100, 0b01000, 0};
    const byte pauseIcon[8] = {0b01010, 0b01010, 0b01010, 0b01010, 0b01010, 0b01010, 0b01010, 0};
    lcd.createChar(6, (byte*)playIcon);
    lcd.createChar(7, (byte*)pauseIcon);
  } else if (set == 1) {
    for (byte k = 0; k < 8; k++) {
      for (byte i = 0; i < 8; i++) g[i] = (i >= 7 - k) ? 0x1F : 0;
      lcd.createChar(k, g);
    }
  }
  lcd.clear();
  for (byte r = 0; r < 2; r++)
    for (byte c = 0; c < COLS; c++) shown[r][c] = ' ';
}

// ---------------- transition effects (ported) ----------------
byte fxCell(byte r, byte c) {
  byte t = tgt[r][c];
  byte idx = r * COLS + c;
  switch (curFx) {
    case FX_TYPE: {
      byte lim = fr * 2;
      if (idx < lim) return t;
      if (idx == lim) return '_';
      return ' ';
    }
    case FX_SLIDE: {
      int off = 16 - 2 * fr;
      if (off < 0) off = 0;
      if (r == 0) return (c >= off) ? tgt[0][c - off] : ' ';
      return (c + off < COLS) ? tgt[1][c + off] : ' ';
    }
    case FX_CENTER:
      return (abs(2 * c - 15) <= 2 * fr + 1) ? t : ' ';
    case FX_DECODE:
      if (fr >= c + r * 2 + 2) return t;
      return 33 + random(58);
    case FX_DISSOLVE:
      return (((idx * 7 + 3) & 31) < fr * 3) ? t : ' ';
  }
  return t;
}
void startAnim() {
  curFx = (fx == FX_AUTO) ? random(N_FX - 1) : fx;
  fr = 0;
  animating = true;
  nextFrame = 0;
  pageT = millis();
}
void startAnimFast() {
  startAnim();
  if (curFx == FX_TYPE || curFx == FX_DECODE) curFx = FX_CENTER;
}
void animTick() {
  if (!animating || millis() < nextFrame) return;
  nextFrame = millis() + FRAME_MS;
  bool last = fr >= fxLast[curFx];
  for (byte r = 0; r < 2; r++)
    for (byte c = 0; c < COLS; c++)
      flushCell(r, c, last ? tgt[r][c] : fxCell(r, c));
  if (last) { animating = false; pageT = millis(); }
  else fr++;
}

// ---------------- lyric wrap / pages (ported) ----------------
void wrapText(const char* s) {
  nChunks = 0;
  int n = strlen(s), i = 0;
  while (i < n && nChunks < 10) {
    while (i < n && s[i] == ' ') i++;
    if (i >= n) break;
    int end = i + COLS;
    if (end >= n) end = n;
    else {
      int j = end;
      while (j > i && s[j] != ' ') j--;
      if (j > i) end = j;
    }
    chunkStart[nChunks] = i;
    chunkLen[nChunks] = end - i;
    nChunks++;
    i = end;
  }
}
void showLyricPage() {
  if (kind != 'l') {
    setRowStr(0, curLine, true);
    setRowStr(1, nextLine, true);
    return;
  }
  byte a = page * 2, b = a + 1;
  if (a < nChunks) setRow(0, curLine + chunkStart[a], chunkLen[a], true); else setRow(0, curLine, 0, false);
  if (b < nChunks) setRow(1, curLine + chunkStart[b], chunkLen[b], true); else setRow(1, curLine, 0, false);
}
void buildLyric() {
  if (kind == 'l') {
    wrapText(curLine);
    nPages = (nChunks + 1) / 2;
    if (nPages < 1) nPages = 1;
  } else nPages = 1;
  page = 0;
  showLyricPage();
}

// ---------------- marquee (ported) ----------------
unsigned int stepFor(const char* s, unsigned long ms) {
  byte len = strlen(s);
  if (len <= COLS || ms == 0) return MQ_STEP;
  unsigned long budget = ms > 1500 ? ms - 1000 : 500;
  unsigned long st = budget / (len - COLS);
  if (st < 110) st = 110;
  if (st > 450) st = 450;
  return st;
}
void marqReset(unsigned int step0) {
  for (byte r = 0; r < 2; r++) { mOff[r] = 0; mDir[r] = 1; mT[r] = millis() + 1500; }
  mStep[0] = step0;
  mStep[1] = MQ_STEP;
}
void marqTick(byte r, const char* s) {
  byte len = strlen(s);
  if (len <= COLS || millis() < mT[r]) return;
  for (byte c = 0; c < COLS; c++) tgt[r][c] = s[mOff[r] + c];
  dirty = true;
  signed char maxOff = len - COLS;
  signed char nx = mOff[r] + mDir[r];
  if (nx < 0 || nx > maxOff) {
    mDir[r] = -mDir[r];
    nx = mOff[r] + mDir[r];
    mT[r] = millis() + 1000;
  } else mT[r] = millis() + mStep[r];
  mOff[r] = nx;
}

// ---------------- now playing / visualizer (ported) ----------------
void composeProgress() {
  tgt[1][0] = playing ? 6 : 7;
  char tm[10];
  unsigned int m = elapsed / 60;
  if (m > 99) m = 99;
  snprintf(tm, sizeof(tm), "%2u:%02u", m, elapsed % 60);
  for (byte i = 0; i < 5; i++) tgt[1][1 + i] = tm[i];
  tgt[1][6] = ' ';
  int steps = (int)pct * 45 / 100;
  for (byte cell = 0; cell < 9; cell++) {
    int fill = steps - cell * 5;
    byte ch = (fill >= 5) ? 4 : (fill > 0 ? fill - 1 : 5);
    tgt[1][7 + cell] = ch;
  }
}
void vizTick() {
  unsigned long now = millis();
  if (now < vizT) return;
  vizT = now + 55;
  if (playing && now >= beatT) {
    beatT = now + random(380, 520);
    for (byte c = 0; c < 6; c++) tg[c] = random(11, 17);
  }
  for (byte c = 0; c < COLS; c++) {
    if (!playing) tg[c] = 0;
    else if (random(100) < 30) tg[c] = random(1, 17 - c / 3);
    if (lvl[c] < tg[c]) { lvl[c] += 4; if (lvl[c] > tg[c]) lvl[c] = tg[c]; }
    else if (lvl[c] > tg[c]) { lvl[c] -= 2; if (lvl[c] < tg[c]) lvl[c] = tg[c]; }
    byte h = lvl[c];
    byte bot = h > 8 ? 8 : h;
    byte top = h > 8 ? h - 8 : 0;
    tgt[1][c] = bot ? bot - 1 : ' ';
    tgt[0][c] = top ? top - 1 : ' ';
  }
  flushTarget();
}

// ---------------- modes ----------------
void renderModeFull() {
  switch (mode) {
    case M_LYRICS:
      buildLyric();
      if (lineMs > 0 && (lineMs < 1800 || (nPages > 1 && lineMs < 4500))) startAnimFast();
      else startAnim();
      break;
    case M_KARAOKE:
      setRowStr(0, curLine, true);
      setRowStr(1, nextLine, true);
      marqReset(stepFor(curLine, lineMs));
      startAnim();
      break;
    case M_NOWPLAY:
      setRowStr(0, nowLine, true);
      composeProgress();
      marqReset(MQ_STEP);
      startAnim();
      break;
    case M_VIZ:
      break;
  }
}
void splash(const char* a, const char* b) {
  setRowStr(0, a, true);
  setRowStr(1, b, true);
  startAnim();
  splashEnd = millis() + 1400;
}
void enterMode() {
  loadChars(mode == M_NOWPLAY ? 0 : (mode == M_VIZ ? 1 : 2));
  splash("MODE", modeNames[mode]);
}
void setMode(byte m) {
  if (m >= N_MODES) return;
  mode = m;
  enterMode();
}
void setEffect(byte e) {
  if (e > N_FX) return;
  fx = e;
  splash("EFFECT", fxNames[fx]);
}
void showInfo(const char* a, const char* b) {
  kind = 'i';
  strncpy(curLine, a, LYR_MAX); curLine[LYR_MAX] = 0;
  strncpy(nextLine, b, LYR_MAX); nextLine[LYR_MAX] = 0;
  lineMs = 0;
  if (mode <= M_KARAOKE) renderModeFull();
}

// ---------------- config storage ----------------
bool loadConfig() {
  if (!LittleFS.begin()) return false;
  if (!LittleFS.exists("/config.json")) return false;
  File f = LittleFS.open("/config.json", "r");
  if (!f) return false;
  DynamicJsonDocument doc(1536);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  strncpy(cfg.wifiSsid, doc["wifi_ssid"] | "", sizeof(cfg.wifiSsid) - 1);
  strncpy(cfg.wifiPass, doc["wifi_pass"] | "", sizeof(cfg.wifiPass) - 1);
  strncpy(cfg.clientId, doc["client_id"] | "", sizeof(cfg.clientId) - 1);
  strncpy(cfg.clientSecret, doc["client_secret"] | "", sizeof(cfg.clientSecret) - 1);
  strncpy(cfg.refreshToken, doc["refresh_token"] | "", sizeof(cfg.refreshToken) - 1);
  return true;
}
bool saveConfig() {
  DynamicJsonDocument doc(1536);
  doc["wifi_ssid"] = cfg.wifiSsid;
  doc["wifi_pass"] = cfg.wifiPass;
  doc["client_id"] = cfg.clientId;
  doc["client_secret"] = cfg.clientSecret;
  doc["refresh_token"] = cfg.refreshToken;
  File f = LittleFS.open("/config.json", "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

// ---------------- small helpers ----------------
String urlEncode(const String& s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') out += c;
    else if (c == ' ') out += "%20";
    else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      out += buf;
    }
  }
  return out;
}
String base64Encode(const String& s) {
  static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out;
  int i = 0;
  while (i < (int)s.length()) {
    int a = (unsigned char)s[i++];
    int b = i < (int)s.length() ? (unsigned char)s[i++] : -1;
    int c = i < (int)s.length() ? (unsigned char)s[i++] : -1;
    out += tbl[a >> 2];
    out += tbl[((a & 3) << 4) | (b >= 0 ? b >> 4 : 0)];
    out += b >= 0 ? tbl[((b & 15) << 2) | (c >= 0 ? c >> 6 : 0)] : '=';
    out += c >= 0 ? tbl[c & 63] : '=';
  }
  return out;
}
String redirectUri() {
  // Must match a Redirect URI registered in the Spotify dashboard exactly.
  return "http://" + WiFi.localIP().toString() + "/callback";
}
// Extract a JSON string field without a full document parse. This keeps
// large lrclib responses (synced lyrics can be many KB) off the heap as
// one big ArduinoJson document. Returns false for null / missing.
bool extractJsonString(const String& body, const char* key, String& out) {
  int ki = body.indexOf(key);
  if (ki < 0) return false;
  int colon = body.indexOf(':', ki + strlen(key));
  if (colon < 0) return false;
  int i = colon + 1;
  while (i < (int)body.length() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\n' || body[i] == '\r')) i++;
  if (i + 4 <= (int)body.length() && body.substring(i, i + 4) == "null") return false;
  if (i >= (int)body.length() || body[i] != '"') return false;
  i++;
  out = "";
  while (i < (int)body.length()) {
    char c = body[i++];
    if (c == '"') return true;
    if (c == '\\' && i < (int)body.length()) {
      char e = body[i++];
      if (e == 'n') out += '\n';
      else if (e == 'r') out += '\r';
      else if (e == 't') out += '\t';
      else if (e == 'u' && i + 4 <= (int)body.length()) { i += 4; out += '?'; }
      else out += e;
    } else out += c;
  }
  return false;
}
// HTTPS request helper. setInsecure() is the documented ESP8266 trade-off:
// no system CA store, full verification would need bundled roots + RAM.
// See docs/SPOTIFY_SETUP.md security notes.
int httpsRequest(const String& url, const String& method, const String& body,
                 const String& contentType, const String& authHeader, String& resp) {
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  client->setTimeout(15000);
  HTTPClient http;
  if (!http.begin(*client, url)) return -1;
  if (authHeader.length()) http.addHeader("Authorization", authHeader);
  if (contentType.length()) http.addHeader("Content-Type", contentType);
  int code;
  if (method == "POST") code = http.POST(body);
  else code = http.GET();
  resp = http.getString();
  http.end();
  return code;
}

// ---------------- lyrics ----------------
void parseSyncedLyrics(const String& synced) {
  lyricCount = 0;
  lyricsSynced = true;
  int pos = 0;
  while (pos < (int)synced.length() && lyricCount < 120) {
    int nl = synced.indexOf('\n', pos);
    String line = (nl < 0) ? synced.substring(pos) : synced.substring(pos, nl);
    pos = (nl < 0) ? synced.length() : nl + 1;
    line.trim();
    if (line.length() < 3 || line[0] != '[') continue;
    int close = line.indexOf(']');
    if (close < 0) continue;
    String ts = line.substring(1, close);
    int colon = ts.indexOf(':');
    if (colon < 0) continue;
    float mins = ts.substring(0, colon).toFloat();
    float secs = ts.substring(colon + 1).toFloat();
    String text = line.substring(close + 1);
    text.trim();
    if (text.length() == 0) text = "♪";
    lyrics[lyricCount].ms = (long)((mins * 60 + secs) * 1000);
    strncpy(lyrics[lyricCount].text, text.c_str(), sizeof(lyrics[lyricCount].text) - 1);
    lyrics[lyricCount].text[sizeof(lyrics[lyricCount].text) - 1] = 0;
    lyricCount++;
  }
}
void parseUnsyncedLyrics(const String& plain) {
  lyricCount = 0;
  lyricsSynced = false;
  long step = trackDurationMs > 0 ? trackDurationMs / 20 : 10000;
  int pos = 0;
  while (pos < (int)plain.length() && lyricCount < 120) {
    int nl = plain.indexOf('\n', pos);
    String line = (nl < 0) ? plain.substring(pos) : plain.substring(pos, nl);
    pos = (nl < 0) ? plain.length() : nl + 1;
    line.trim();
    if (line.length() == 0) continue;
    if (trackDurationMs > 0) step = trackDurationMs / 40;  // refined below
    lyrics[lyricCount].ms = 0;  // assigned after counting
    strncpy(lyrics[lyricCount].text, line.c_str(), sizeof(lyrics[lyricCount].text) - 1);
    lyrics[lyricCount].text[sizeof(lyrics[lyricCount].text) - 1] = 0;
    lyricCount++;
  }
  if (lyricCount > 0 && trackDurationMs > 0) {
    long per = trackDurationMs / lyricCount;
    for (int i = 0; i < lyricCount; i++) lyrics[i].ms = per * i;
  } else {
    for (int i = 0; i < lyricCount; i++) lyrics[i].ms = step * i;
  }
  (void)step;
}
void fetchLyrics() {
  lyricCount = 0;
  lyricsFound = false;
  lyricIndex = -1;
  if (strlen(trackTitle) == 0) return;
  long durSec = trackDurationMs / 1000;
  String url = "https://lrclib.net/api/get?artist_name=" + urlEncode(trackArtist) +
               "&track_name=" + urlEncode(trackTitle) + "&duration=" + String(durSec);
  String resp;
  int code = httpsRequest(url, "GET", "", "", "", resp);
  if (code != 200) {
    Serial.printf("[lyrics] lrclib HTTP %d for %s - %s\n", code, trackTitle, trackArtist);
    return;
  }
  String synced, plain;
  if (extractJsonString(resp, "\"syncedLyrics\"", synced) && synced.length() > 0) {
    parseSyncedLyrics(synced);
  } else if (extractJsonString(resp, "\"plainLyrics\"", plain) && plain.length() > 0) {
    parseUnsyncedLyrics(plain);
  }
  lyricsFound = lyricCount > 0;
  Serial.printf("[lyrics] %d lines, synced=%d\n", lyricCount, (int)lyricsSynced);
}
void updateLyricForProgress() {
  if (!lyricsFound || lyricCount == 0) {
    if (lyricIndex != -2) {
      lyricIndex = -2;
      showInfo("No lyrics found", "on lrclib");
    }
    return;
  }
  int idx = -1;
  for (int i = 0; i < lyricCount; i++) {
    if (lyrics[i].ms <= trackProgressMs) idx = i;
    else break;
  }
  if (idx < 0) idx = 0;
  if (idx == lyricIndex) return;
  lyricIndex = idx;
  kind = 'l';
  strncpy(curLine, lyrics[idx].text, LYR_MAX); curLine[LYR_MAX] = 0;
  if (idx + 1 < lyricCount) strncpy(nextLine, lyrics[idx + 1].text, LYR_MAX);
  else nextLine[0] = 0;
  nextLine[LYR_MAX] = 0;
  long nextMs = (idx + 1 < lyricCount) ? lyrics[idx + 1].ms : trackDurationMs;
  lineMs = nextMs > lyrics[idx].ms ? (unsigned long)(nextMs - lyrics[idx].ms) : 3000;
  lineStart = millis();
  if (mode <= M_KARAOKE) renderModeFull();
}

// ---------------- Spotify ----------------
bool spotifyRefresh() {
  if (strlen(cfg.refreshToken) == 0 || strlen(cfg.clientId) == 0) return false;
  String body = "grant_type=refresh_token&refresh_token=" + urlEncode(cfg.refreshToken);
  String auth = "Basic " + base64Encode(String(cfg.clientId) + ":" + String(cfg.clientSecret));
  String resp;
  int code = httpsRequest("https://accounts.spotify.com/api/token", "POST", body,
                          "application/x-www-form-urlencoded", auth, resp);
  if (code != 200) {
    Serial.printf("[spotify] refresh failed HTTP %d\n", code);
    spotifyStatus = "Token refresh failed, relink";
    return false;
  }
  DynamicJsonDocument doc(1536);
  if (deserializeJson(doc, resp)) return false;
  accessToken = doc["access_token"].as<String>();
  if (doc.containsKey("refresh_token")) {
    strncpy(cfg.refreshToken, doc["refresh_token"].as<String>().c_str(), sizeof(cfg.refreshToken) - 1);
    saveConfig();
  }
  long expires = doc["expires_in"] | 3600;
  tokenExpiresAt = millis() + (unsigned long)expires * 1000 - 60000;
  spotifyLinked = true;
  spotifyStatus = "Linked";
  return accessToken.length() > 0;
}
bool spotifyEnsureToken() {
  if (accessToken.length() == 0) return spotifyRefresh();
  if (millis() > tokenExpiresAt) return spotifyRefresh();
  return true;
}
void spotifyPoll() {
  if (!spotifyEnsureToken()) { spotifyPollFailed = true; return; }
  String resp;
  int code = httpsRequest("https://api.spotify.com/v1/me/player/currently-playing",
                          "GET", "", "", "Bearer " + accessToken, resp);
  if (code == 401) {  // token rotated or revoked: refresh once, retry once
    if (!spotifyRefresh()) { spotifyPollFailed = true; return; }
    code = httpsRequest("https://api.spotify.com/v1/me/player/currently-playing",
                        "GET", "", "", "Bearer " + accessToken, resp);
  }
  lastPollMs = millis();
  if (code == 204 || resp.length() == 0) {
    if (hasTrack) {
      hasTrack = false;
      playing = false;
      strncpy(nowLine, "Nothing playing", sizeof(nowLine) - 1);
      if (mode == M_NOWPLAY) renderModeFull();
      else showInfo("Nothing playing", "Open Spotify app");
    }
    spotifyPollFailed = false;
    return;
  }
  if (code != 200) {
    Serial.printf("[spotify] currently-playing HTTP %d\n", code);
    spotifyPollFailed = true;
    return;
  }
  spotifyPollFailed = false;
  DynamicJsonDocument filter(512);
  filter["item"]["id"] = true;
  filter["item"]["name"] = true;
  filter["item"]["artists"][0]["name"] = true;
  filter["item"]["duration_ms"] = true;
  filter["progress_ms"] = true;
  filter["is_playing"] = true;
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, resp, DeserializationOption::Filter(filter))) return;
  if (doc["item"].isNull()) return;
  String id = doc["item"]["id"].as<String>();
  String title = doc["item"]["name"].as<String>();
  String artist = doc["item"]["artists"][0]["name"].as<String>();
  long duration = doc["item"]["duration_ms"] | 0;
  long progress = doc["progress_ms"] | 0;
  bool isPlaying = doc["is_playing"] | false;
  bool changed = strcmp(trackId, id.c_str()) != 0;
  strncpy(trackId, id.c_str(), sizeof(trackId) - 1);
  strncpy(trackTitle, title.c_str(), sizeof(trackTitle) - 1);
  strncpy(trackArtist, artist.c_str(), sizeof(trackArtist) - 1);
  trackDurationMs = duration;
  trackProgressMs = progress;
  playing = isPlaying;
  hasTrack = true;
  elapsed = (unsigned int)(progress / 1000);
  pct = duration > 0 ? (byte)(progress * 100 / duration) : 0;
  snprintf(nowLine, sizeof(nowLine), "%.40s - %.40s", trackTitle, trackArtist);
  if (changed) {
    lyricIndex = -1;
    fetchLyrics();
    updateLyricForProgress();
    if (mode == M_NOWPLAY) renderModeFull();
  } else {
    updateLyricForProgress();
  }
}

// ---------------- web management ----------------
String lcdRowString(byte r) {
  String out;
  for (byte c = 0; c < COLS; c++) out += (char)tgt[r][c];
  return out;
}
String jsonEscape(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c < 32) out += ' ';
    else out += c;
  }
  return out;
}
void handleRoot() { server.send_P(200, "text/html", WEB_UI); }
void handleStatus() {
  String s = "{";
  s += "\"linked\":" + String(spotifyLinked ? "true" : "false");
  s += ",\"spotify_status\":\"" + jsonEscape(spotifyStatus) + "\"";
  s += ",\"ap_mode\":" + String(apMode ? "true" : "false");
  s += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  s += ",\"title\":\"" + jsonEscape(trackTitle) + "\"";
  s += ",\"artist\":\"" + jsonEscape(trackArtist) + "\"";
  s += ",\"playing\":" + String(playing ? "true" : "false");
  s += ",\"has_track\":" + String(hasTrack ? "true" : "false");
  s += ",\"progress_ms\":" + String(trackProgressMs);
  s += ",\"duration_ms\":" + String(trackDurationMs);
  s += ",\"mode\":" + String(mode) + ",\"mode_name\":\"" + String(modeNames[mode]) + "\"";
  s += ",\"effect\":" + String(fx) + ",\"effect_name\":\"" + String(fxNames[fx]) + "\"";
  s += ",\"lyric_current\":\"" + jsonEscape(curLine) + "\"";
  s += ",\"lyric_next\":\"" + jsonEscape(nextLine) + "\"";
  s += ",\"lyrics_found\":" + String(lyricsFound ? "true" : "false");
  s += ",\"lyrics_synced\":" + String(lyricsSynced ? "true" : "false");
  s += ",\"lcd_row0\":\"" + jsonEscape(lcdRowString(0)) + "\"";
  s += ",\"lcd_row1\":\"" + jsonEscape(lcdRowString(1)) + "\"";
  s += "}";
  server.send(200, "application/json", s);
}
void handleSetMode() {
  if (!server.hasArg("mode")) { server.send(400, "text/plain", "missing mode"); return; }
  setMode((byte)server.arg("mode").toInt());
  server.send(200, "application/json", "{\"ok\":true}");
}
void handleSetEffect() {
  if (!server.hasArg("effect")) { server.send(400, "text/plain", "missing effect"); return; }
  setEffect((byte)server.arg("effect").toInt());
  server.send(200, "application/json", "{\"ok\":true}");
}
void handleConfig() {
  // Secrets are accepted here but never echoed back by any endpoint.
  if (server.hasArg("wifi_ssid")) strncpy(cfg.wifiSsid, server.arg("wifi_ssid").c_str(), sizeof(cfg.wifiSsid) - 1);
  if (server.hasArg("wifi_pass")) strncpy(cfg.wifiPass, server.arg("wifi_pass").c_str(), sizeof(cfg.wifiPass) - 1);
  if (server.hasArg("client_id")) strncpy(cfg.clientId, server.arg("client_id").c_str(), sizeof(cfg.clientId) - 1);
  if (server.hasArg("client_secret")) strncpy(cfg.clientSecret, server.arg("client_secret").c_str(), sizeof(cfg.clientSecret) - 1);
  if (server.hasArg("refresh_token") && server.arg("refresh_token").length() > 0)
    strncpy(cfg.refreshToken, server.arg("refresh_token").c_str(), sizeof(cfg.refreshToken) - 1);
  saveConfig();
  server.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
  delay(300);
  ESP.restart();
}
void handleLogin() {
  if (strlen(cfg.clientId) == 0) {
    server.send(400, "text/html", "<p>Enter a Spotify Client ID first, then save.</p><p><a href='/'>Back</a></p>");
    return;
  }
  String url = "https://accounts.spotify.com/authorize?client_id=" + urlEncode(cfg.clientId) +
               "&response_type=code&redirect_uri=" + urlEncode(redirectUri()) +
               "&scope=" + urlEncode("user-read-currently-playing user-read-playback-state");
  server.sendHeader("Location", url);
  server.send(302, "text/plain", "");
}
void handleCallback() {
  if (!server.hasArg("code")) {
    server.send(400, "text/html", "<p>Spotify login failed: no code returned.</p><p><a href='/'>Back</a></p>");
    return;
  }
  String body = "grant_type=authorization_code&code=" + urlEncode(server.arg("code")) +
                "&redirect_uri=" + urlEncode(redirectUri());
  String auth = "Basic " + base64Encode(String(cfg.clientId) + ":" + String(cfg.clientSecret));
  String resp;
  int code = httpsRequest("https://accounts.spotify.com/api/token", "POST", body,
                          "application/x-www-form-urlencoded", auth, resp);
  if (code != 200) {
    server.send(400, "text/html", "<p>Token exchange failed. Check Client ID/Secret and that the Redirect URI in the Spotify dashboard matches this device IP exactly.</p><p><a href='/'>Back</a></p>");
    return;
  }
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, resp)) {
    server.send(500, "text/plain", "Bad token response");
    return;
  }
  accessToken = doc["access_token"].as<String>();
  if (doc.containsKey("refresh_token"))
    strncpy(cfg.refreshToken, doc["refresh_token"].as<String>().c_str(), sizeof(cfg.refreshToken) - 1);
  long expires = doc["expires_in"] | 3600;
  tokenExpiresAt = millis() + (unsigned long)expires * 1000 - 60000;
  spotifyLinked = true;
  spotifyStatus = "Linked";
  saveConfig();
  server.send(200, "text/html", "<p>Spotify linked. You can close this page.</p><p><a href='/'>Back to management</a></p>");
  lastPollMs = 0;  // poll immediately
}
void handleLogout() {
  accessToken = "";
  cfg.refreshToken[0] = 0;
  spotifyLinked = false;
  spotifyStatus = "Not linked";
  hasTrack = false;
  saveConfig();
  showInfo("Spotify logged", "out");
  server.send(200, "application/json", "{\"ok\":true}");
}
void handleNotFound() { server.send(404, "text/plain", "Not found"); }
void setupWeb() {
  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/mode", HTTP_POST, handleSetMode);
  server.on("/api/effect", HTTP_POST, handleSetEffect);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/login", handleLogin);
  server.on("/callback", handleCallback);
  server.on("/logout", HTTP_POST, handleLogout);
  server.onNotFound(handleNotFound);
  server.begin();
  MDNS.begin("spotify-lcd");
  MDNS.addService("http", "tcp", 80);
}

// ---------------- setup / loop ----------------
void showSetupScreen() {
  // Alternates AP name and IP so both fit on 16x2. No lcd.clear():
  // flushTarget() only rewrites changed cells, tracked in shown[].
  if (millis() - setupScreenT < 3000) return;
  setupScreenT = millis();
  setupScreenAlt = !setupScreenAlt;
  animating = false;
  splashEnd = 0;
  if (setupScreenAlt) {
    setRowStr(0, "Setup WiFi:", false);
    setRowStr(1, "192.168.4.1", false);
  } else {
    setRowStr(0, "AP Spotify-", false);
    setRowStr(1, "Lyrics-Setup", false);
  }
  flushTarget();
}
void setup() {
  Serial.begin(115200);
  lcd.begin(16, 2);
  pinMode(A0, INPUT);
  randomSeed(analogRead(A0));
  loadChars(2);
  renderModeFull();

  loadConfig();
  spotifyLinked = strlen(cfg.refreshToken) > 0;
  if (spotifyLinked) spotifyStatus = "Linked (stored token)";

  if (strlen(cfg.wifiSsid) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Spotify-Lyrics-Setup");
    setupWeb();
    showInfo("WiFi setup:", "Join AP above");
    // Show the AP screen on a timer; the info text is the fallback.
    setupScreenT = millis() - 3000;
    Serial.println("[wifi] AP mode: Spotify-Lyrics-Setup at 192.168.4.1");
    return;
  }
  Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
  setupWeb();
  char ipLine[32];
  snprintf(ipLine, sizeof(ipLine), "IP %s", WiFi.localIP().toString().c_str());
  showInfo("Spotify Lyrics", ipLine);
  delay(5000);  // give the user time to read the IP before the next screen
  if (spotifyLinked) showInfo("Spotify linked", "Play a track...");
  else showInfo("Link Spotify on", "the web page");
}
void loop() {
  server.handleClient();
  MDNS.update();
  if (apMode) {
    showSetupScreen();
    return;
  }
  animTick();

  // Serial debug aid (Uno familiarity): M cycles mode, E cycles effect.
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'M' || c == 'm') setMode((mode + 1) % N_MODES);
    else if (c == 'E' || c == 'e') setEffect((fx + 1) % (N_FX + 1));
  }

  if (splashEnd && !animating && millis() >= splashEnd) {
    splashEnd = 0;
    renderModeFull();
  }

  // Progress interpolation + lyric sync between Spotify polls.
  // Advance from the last tick, not from lastPollMs, or progress
  // would be double-counted on every tick.
  if (hasTrack && playing && millis() - lastProgressTick >= 250) {
    unsigned long now = millis();
    trackProgressMs += (long)(now - lastProgressTick);
    lastProgressTick = now;
    if (trackProgressMs > trackDurationMs) trackProgressMs = trackDurationMs;
    elapsed = (unsigned int)(trackProgressMs / 1000);
    pct = trackDurationMs > 0 ? (byte)(trackProgressMs * 100 / trackDurationMs) : 0;
    updateLyricForProgress();
    if (mode == M_NOWPLAY && !animating && !splashEnd) { composeProgress(); flushTarget(); }
  }

  // Spotify poll: 2 s while playing, 5 s otherwise. Polls are blocking
  // HTTPS (1-3 s on ESP8266); the LCD animation pauses during a poll
  // and resumes after, which is the honest trade-off on this chip.
  unsigned long interval = playing ? 2000 : 5000;
  if (spotifyLinked && millis() - lastPollMs >= interval) {
    unsigned long pollStart = millis();
    spotifyPoll();
    // Do not let a slow poll be counted as playback progress.
    lastPollMs = millis();
    lastProgressTick = millis();
    (void)pollStart;
  }

  if (!splashEnd && !animating) {
    switch (mode) {
      case M_LYRICS:
        if (nPages > 1 && kind == 'l') {
          byte want;
          if (lineMs > 0) {
            unsigned long t = millis() - lineStart;
            want = (t * nPages) / lineMs;
            if (want >= nPages) want = nPages - 1;
          } else {
            want = (millis() - pageT > PAGE_MS) ? (page + 1) % nPages : page;
          }
          if (want != page) {
            page = want;
            showLyricPage();
            startAnimFast();
          }
        }
        break;
      case M_KARAOKE:
        marqTick(0, curLine);
        marqTick(1, nextLine);
        break;
      case M_NOWPLAY:
        marqTick(0, nowLine);
        break;
      case M_VIZ:
        vizTick();
        break;
    }
    if (dirty) { flushTarget(); dirty = false; }
  }
}
