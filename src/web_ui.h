#pragma once
// Management page, served from flash. No filesystem upload step is
// needed: the firmware alone is the whole install.
// Buttons POST to /api/mode and /api/effect; the form POSTs to
// /api/config and reboots the board. Secrets are never returned by
// any API endpoint, so the form fields start empty on every load.
#include <pgmspace.h>
const char WEB_UI[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Spotify Lyrics LCD</title>
<style>
body{font-family:system-ui,-apple-system,sans-serif;background:#121212;color:#fff;max-width:640px;margin:0 auto;padding:20px}
h1{font-size:22px}h2{font-size:16px;margin-top:28px}
.lcd{background:#0b3d2e;border:6px solid #222;border-radius:10px;padding:14px;font-family:monospace;font-size:22px;letter-spacing:2px;line-height:1.6;white-space:pre;overflow-x:auto}
.card{background:#1e1e1e;border-radius:12px;padding:16px;margin:14px 0}
button{background:#1db954;color:#000;border:0;border-radius:20px;padding:9px 16px;margin:4px 4px 4px 0;font-weight:700;cursor:pointer}
button.off{background:#333;color:#fff}
input{width:100%;box-sizing:border-box;background:#333;color:#fff;border:1px solid #555;border-radius:8px;padding:10px;margin:6px 0}
label{font-size:13px;color:#bbb}
a{color:#1db954}.muted{color:#bbb;font-size:13px}
</style></head><body>
<h1>Spotify Lyrics LCD</h1>
<p class="muted">ESP8266 + 16x2 LCD. No buttons on the board: this page is the remote.</p>
<div class="lcd" id="lcd">                &#10;                </div>
<div class="card"><div id="track">Loading...</div><div class="muted" id="meta"></div><div class="muted" id="spot"></div></div>
<div class="card"><h2>Display mode</h2><div id="modes"></div>
<h2>Transition effect</h2><div id="effects"></div></div>
<div class="card"><h2>Spotify</h2>
<p class="muted">Register the app first (see docs/SPOTIFY_SETUP.md). Redirect URI must be exactly http://&lt;this IP&gt;/callback.</p>
<p><a href="/login"><button>Connect Spotify</button></a> <button class="off" onclick="logout()">Disconnect</button></p>
<p class="muted">If Spotify rejects a plain-IP redirect, paste a refresh token below instead (localhost helper on a PC).</p></div>
<div class="card"><h2>Settings</h2>
<label>WiFi SSID</label><input id="ssid" placeholder="Home WiFi name">
<label>WiFi password</label><input id="pass" type="password" placeholder="Leave blank only if the network is open">
<label>Spotify Client ID</label><input id="cid" placeholder="From developer.spotify.com dashboard">
<label>Spotify Client Secret</label><input id="sec" type="password" placeholder="Only needed for login / refresh">
<label>Refresh token (fallback only)</label><input id="rtok" type="password" placeholder="Paste only if using the fallback path">
<p><button onclick="save()">Save and reboot</button></p><p class="muted" id="saveMsg"></p></div>
<script>
const MODES=["Lyrics","Karaoke","Now Playing","Visualizer"];
const FX=["Typewriter","Slide","Center wipe","Decode","Dissolve","Instant","Auto"];
let S={};
function pad(s){return (s+"                ").slice(0,16)}
async function refresh(){
  const r=await fetch("/api/status"); S=await r.json();
  document.getElementById("lcd").textContent=pad(S.lcd_row0)+"\n"+pad(S.lcd_row1);
  document.getElementById("track").textContent=S.has_track?(S.playing?"▶ ":"⏸ ")+S.title+" — "+S.artist:"Nothing playing";
  document.getElementById("meta").textContent="Mode: "+S.mode_name+" · Effect: "+S.effect_name+(S.lyrics_found?" · Lyrics: "+(S.lyrics_synced?"synced":"unsynced"):" · No lyrics on lrclib");
  document.getElementById("spot").textContent="Spotify: "+S.spotify_status+" · IP: "+S.ip;
  document.getElementById("modes").innerHTML=MODES.map((m,i)=>`<button class="${i===S.mode?"":"off"}" onclick="post('/api/mode','mode=${i}')">${m}</button>`).join("");
  document.getElementById("effects").innerHTML=FX.map((m,i)=>`<button class="${i===S.effect?"":"off"}" onclick="post('/api/effect','effect=${i}')">${m}</button>`).join("");
}
async function post(u,b){await fetch(u,{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:b});refresh();}
async function logout(){await fetch("/logout",{method:"POST"});refresh();}
async function save(){
  const b=new URLSearchParams({wifi_ssid:ssid.value,wifi_pass:pass.value,client_id:cid.value,client_secret:sec.value,refresh_token:rtok.value});
  document.getElementById("saveMsg").textContent="Saving... board will reboot.";
  await fetch("/api/config",{method:"POST",body:b});
}
setInterval(refresh,2000); refresh();
</script></body></html>
)HTML";
