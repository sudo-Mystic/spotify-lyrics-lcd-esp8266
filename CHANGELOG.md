# Changelog

## 1.0.0 (2026-10-31)

- Initial release.
- ESP8266 + 16x2 LCD only: no buttons, no potentiometer, no extra parts.
- Port of the Arduino Uno serial sketch: 4 modes (Lyrics, Karaoke,
  Now Playing, Visualizer) and 7 transition effects, same wrap,
  paging, marquee, progress bar and visualizer behaviour.
- Spotify Web API polling for currently-playing track, progress
  and play state, with automatic token refresh.
- Synced lyrics from lrclib, unsynced fallback, honest "No lyrics
  found" when lrclib has nothing.
- Web management page on the board: live LCD preview, mode/effect
  control, WiFi and Spotify configuration, OAuth login and a
  refresh-token fallback.
- First-boot WiFi access-point setup, no filesystem upload step.
