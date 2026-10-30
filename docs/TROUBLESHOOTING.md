# Troubleshooting

## LCD shows solid blocks on both rows

Power is reaching the LCD but it is not initialised. Check, in order:
RS to D1, E to D2, D4 to D5, D5 to D6, D6 to D7, D7 to D0, RW to GND,
and that the firmware upload actually succeeded. Re-seat the D4-D7
wires first; they are the usual culprit.

## LCD is blank

Check VDD is on VIN (5 V), not 3.3 V, and backlight pins 15/16 are
wired (15 to VIN, 16 to GND). If the backlight is on but no text,
see the blocks entry above.

## Garbled or flickering characters

Loose or long jumper wires on D4-D7. Shorten them and push them
fully in. Also confirm no other sketch or serial output is driving
those pins.

## It never joins my WiFi, or I cannot find it

- On first boot it creates an open access point called
  `Spotify-Lyrics-Setup`. Join it, open `http://192.168.4.1`, and
  enter your WiFi SSID and password exactly (case matters).
- The ESP8266 only does 2.4 GHz WiFi. A 5 GHz-only network name
  will never appear or connect.
- If it joined once and then vanished, its IP probably changed.
  Check your router's client list, or hold the setup path: erase
  the config by reflashing with LittleFS erase, or re-enter WiFi
  via the access point after a failed-connect timeout (it falls
  back to the access point after about 20 seconds).

## Management page loads but Spotify says "Nothing playing"

- Play a track in the Spotify app on the same account first.
- Check the page's Spotify status line: if it says not linked,
  finish `docs/SPOTIFY_SETUP.md` first.
- If it says linked but still nothing, the token may have expired
  without refreshing (for example the Client Secret was changed).
  Disconnect and reconnect once.

## Spotify login fails with INVALID_CLIENT or redirect mismatch

The redirect URI in the Spotify dashboard must match the ESP8266's
current IP exactly: `http://<IP>/callback`, no trailing slash.
If the IP changed since you registered it, update the dashboard
entry or reserve a static IP. If the dashboard rejects a plain-IP
redirect, use the refresh-token fallback in `docs/SPOTIFY_SETUP.md`.

## Lyrics show "No lyrics found"

lrclib has no entry for that track. Try a popular track to confirm
the pipeline works; if popular tracks work, the device is fine and
that track simply has no lyrics in lrclib. Lyrics are never
generated or guessed.

## Lyrics are out of sync

Synced lyrics come from lrclib's community data and are occasionally
off by a second or two, or matched to a different version of a track
(remaster, radio edit) with a slightly different duration. The
firmware matches on title, artist and duration to reduce this, but
it cannot fix a wrong community timestamp. Unsynced lyrics are
evenly spaced by design and will drift on tracks with long intros.

## Lyrics freeze while music keeps playing

The firmware polls every 2 seconds while playing. A freeze longer
than that usually means WiFi dropped or Spotify rate-limited the
polls after a network flap. The firmware backs off and retries;
if it persists, reboot the board (power cycle) and check WiFi
signal at the board's location.

## It worked, then stopped after weeks

Most often the router gave it a new IP (breaking the Spotify
redirect) or the refresh token was revoked (app removed in the
Spotify dashboard). Re-check the IP, re-link, and reserve a static
IP to stop the first cause recurring.
