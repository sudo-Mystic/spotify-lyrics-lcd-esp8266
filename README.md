# Spotify Lyrics LCD: ESP8266 + 16x2 LCD

A standalone Spotify lyrics display built from exactly two parts: an
ESP8266 board and a 16x2 HD44780 LCD. No buttons, no potentiometer,
no resistors, no serial PC in the loop.

This is a port of an Arduino Uno sketch that needed a PC script over
serial. Here the ESP8266 does the whole job over WiFi: it polls your
Spotify account for what is playing, fetches synced lyrics, and
renders them on the LCD. A web page served by the board itself is the
remote control and the setup screen.

## What it does

- **Follows Spotify playback** on any device on your account, within
  about 2 seconds of a track change.
- **Synced lyrics** from lrclib, matched by track, artist and
  duration. Unsynced lyrics fall back to even spacing; tracks with
  no lrclib entry show "No lyrics found" instead of invented text.
- **4 display modes**, ported from the Uno sketch:
  Lyrics (word-wrapped, paged) | Karaoke (scrolling current + next
  line) | Now Playing (title/artist, play/pause icon, time and a
  9-cell progress bar) | Visualizer (fake spectrum bars).
- **7 transition effects**: Typewriter, Slide, Center wipe, Decode,
  Dissolve, Instant, Auto (random per line).
- **Web management page**: live LCD preview, track status, mode and
  effect buttons, WiFi settings, Spotify link/unlink, and a
  refresh-token fallback field.

## Parts

- 1x ESP8266 board (NodeMCU v2 / ESP-12E, USB powered)
- 1x HD44780 16x2 LCD, 5 V module
- Jumper wires only

Full wiring, including why these exact GPIO pins, is in
[docs/WIRING.md](docs/WIRING.md). Short version:

| LCD | NodeMCU |
| --- | --- |
| RS, E, D4, D5, D6, D7 | D1, D2, D5, D6, D7, D0 |
| VSS, RW, V0, K | GND |
| VDD, A | VIN (5 V from USB) |

## Build and flash

Requires [PlatformIO](https://platformio.org) (VS Code extension or
`pip install platformio`).

```sh
pio run -e nodemcuv2 -t upload
pio device monitor
```

No filesystem upload step: the web page is embedded in flash, and
the config file is created by the firmware itself on first save.

## First run

1. Flash, then power the board. With no WiFi saved it creates an
   open access point called `Spotify-Lyrics-Setup`. The LCD
   alternates between the AP name and `192.168.4.1`.
2. Join that AP from a phone, open `http://192.168.4.1`, enter your
   home WiFi (2.4 GHz only), save. The board reboots and joins it.
3. The LCD shows its home-network IP for 5 seconds. Open that IP in
   a browser (`http://spotify-lcd.local` also works on most networks).
4. Follow [docs/SPOTIFY_SETUP.md](docs/SPOTIFY_SETUP.md): create a
   Spotify app, add `http://<ESP-IP>/callback` as its Redirect URI,
   paste the Client ID and Secret into the management page, and
   click Connect Spotify.
5. Play something on Spotify. The LCD follows.

If Spotify rejects a plain-IP redirect for your account, the same
page accepts a pasted refresh token generated on a PC with a
localhost redirect. That path is documented in the setup guide.

## Web API

The page is a thin client over these endpoints, all on the board:

| Endpoint | Purpose |
| --- | --- |
| `GET /` | Management page |
| `GET /api/status` | JSON: track, progress, mode, effect, lyric lines, LCD rows |
| `POST /api/mode` | Form field `mode` 0-3 |
| `POST /api/effect` | Form field `effect` 0-6 |
| `POST /api/config` | WiFi / Spotify fields, then reboots |
| `GET /login`, `GET /callback` | Spotify OAuth code flow |
| `POST /logout` | Clears stored refresh token |

No endpoint ever returns the Client Secret, WiFi password or
refresh token.

## Honest limitations

- **Spotify has no official lyrics API.** Lyrics come from lrclib's
  community database, so coverage and sync quality are
  track-dependent. Popular tracks are usually good; obscure tracks
  may have nothing.
- **Polling, not push.** Spotify is polled every 2 seconds while
  playing (5 seconds when paused). The LCD interpolates progress
  locally between polls, so lyrics stay smooth, but a track change
  can lag up to 2 seconds.
- **Polling blocks the display briefly.** HTTPS on the ESP8266
  takes 1-3 seconds; the animation pauses during a poll and
  resumes after. That is the trade-off of doing this on one small
  chip with no second core.
- **HTTPS is unverified** (`setInsecure()`). The ESP8266 has no
  system CA store. Keep the board on a trusted home network and
  do not port-forward it. See the security notes in the setup guide.
- **2.4 GHz WiFi only**, and the Spotify redirect URI is tied to
  the board's IP. Reserve a static IP in your router to stop
  login breaking when the IP changes.

Stuck? See [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Project layout

```text
src/main.cpp   Firmware: LCD engine, Spotify client, lyrics, web server
src/web_ui.h   Management page (embedded in flash)
docs/          Wiring, Spotify setup, troubleshooting
platformio.ini PlatformIO environment (NodeMCU v2, LittleFS)
```

## Version

1.0.0. See [CHANGELOG.md](CHANGELOG.md).
