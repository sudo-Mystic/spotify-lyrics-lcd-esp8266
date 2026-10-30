# Spotify setup

Spotify has no official lyrics API, so this project uses two separate
sources and is honest about both:

- **Now playing, progress and play/pause state:** Spotify Web API
  (`/v1/me/player/currently-playing`), via the user's own Spotify app.
- **Lyrics:** the free lrclib API (`https://lrclib.net`), matched by
  track name, artist and duration. Synced (time-stamped) lyrics are
  used when lrclib has them; unsynced lyrics fall back to even
  spacing across the track; if lrclib has nothing, the LCD says so
  instead of inventing lyrics.

Lyrics coverage is therefore track-dependent. Popular tracks usually
have synced lyrics; obscure tracks often have none. That is a data
limitation, not a bug in this firmware.

## 1. Create a Spotify app

1. Go to https://developer.spotify.com/dashboard and log in.
2. Create an app (any name, any description).
3. Open the app settings and add a Redirect URI. This is the one
   step that trips people up, so read it carefully:
   - First get the ESP8266 on your WiFi and read its IP from the LCD
     (or from the setup page).
   - Add exactly `http://<that-IP>/callback`, for example
     `http://192.168.1.42/callback`. No trailing slash, and the IP
     must match the one the LCD shows.
   - If your router gives the ESP8266 a different IP later, the
     redirect stops matching and login fails. Reserve a static IP
     for the ESP8266 in your router, or re-add the new redirect URI.
4. Copy the Client ID and Client Secret from the dashboard.

Spotify's dashboard historically accepts an `http://` IP redirect for
this kind of local device app. If it ever rejects a plain-IP redirect
for your account, use the fallback in step 3 below instead of fighting
the dashboard.

## 2. Link from the web management page (normal path)

1. Open the ESP8266's IP in a browser (the management page).
2. Enter the Client ID and Client Secret, save.
3. Click **Connect Spotify**. You are sent to Spotify to approve the
   app (scopes: read currently-playing and playback state only).
4. Spotify redirects back to `http://<ESP-IP>/callback`. The firmware
   exchanges the code for tokens, keeps the refresh token in its
   flash config, and the LCD starts following playback.

The access token lasts about an hour; the firmware refreshes it
automatically from the stored refresh token. You only log in again
if you click Disconnect, or if Spotify revokes the app.

## 3. Fallback: paste a refresh token (if the IP redirect is rejected)

1. On a PC, run any standard Spotify token helper that uses a
   `http://localhost` redirect (Spotify always allows localhost),
   with the same Client ID and Secret.
2. Copy the refresh token it prints.
3. On the ESP8266 management page, paste it into the refresh-token
   field and save. The firmware refreshes from it exactly as if you
   had logged in through step 2.

Never paste a refresh token into a public issue, screenshot or chat:
it grants playback-state access until revoked in the Spotify
dashboard (Apps with access).

## 4. Play something

Play a track on any Spotify device on the same account: phone, PC,
speaker. The ESP8266 polls the account, not a specific device, so
it follows whatever is playing. The LCD follows within about
2 seconds of a track change.

## Security notes

- Client Secret and refresh token are stored in LittleFS on the
  ESP8266 (`/config.json`). They are readable by anyone who can
  reach the device's web page on your LAN, and anyone with physical
  access to the board. This is a local hobby device: keep it on a
  trusted home network, do not port-forward it to the internet.
- HTTPS to Spotify and lrclib uses `setInsecure()` (no certificate
  verification). That is the standard ESP8266 trade-off: the chip
  has no system certificate store and full verification needs
  bundled roots and more RAM. On a home network this is acceptable;
  it is not suitable for handling anything more sensitive.
- No data leaves your network except the Spotify and lrclib API
  calls needed for the feature itself.
