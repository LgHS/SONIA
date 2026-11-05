// Pages HTML servies par l'ESP. Les {{marqueurs}} de ADMIN_PAGE sont remplacés à l'envoi.
#pragma once
#include <Arduino.h>

#define SONIA_CSS                                                                                      \
  ":root{color-scheme:light dark;--bg:#f5f4f0;--fg:#1c1c1a;--card:#fff;--mut:#6b6a64;--line:#d9d7d0;" \
  "--acc:#c2410c}"                                                                                     \
  "@media(prefers-color-scheme:dark){:root{--bg:#161614;--fg:#eceae4;--card:#23221f;--mut:#9c9a92;"   \
  "--line:#3a3935;--acc:#fb923c}}"                                                                     \
  "*{box-sizing:border-box}"                                                                           \
  "body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--fg)}" \
  "main{max-width:460px;margin:0 auto;padding:32px 16px}"                                              \
  "h1{margin:0;font-size:30px;letter-spacing:.02em}"                                                   \
  "h2{margin:0 0 8px;font-size:17px}"                                                                  \
  "p{margin:4px 0 24px;color:var(--mut);font-size:14px}"                                               \
  "a{color:var(--acc)}"                                                                                \
  ".card{background:var(--card);border-radius:12px;padding:16px;margin:0 0 16px;"                     \
  "box-shadow:0 1px 3px rgba(0,0,0,.12)}"

static const char PAGE[] PROGMEM = R"html(<!doctype html>
<html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SONIA</title>
<style>)html" SONIA_CSS R"html(
button{display:block;width:100%;margin:0 0 12px;padding:18px 20px;border:0;border-left:4px solid var(--acc);
border-radius:12px;background:var(--card);color:var(--fg);font:inherit;text-align:left;cursor:pointer;
box-shadow:0 1px 3px rgba(0,0,0,.12)}
button b{display:block;font-size:18px}
button span{font-size:13px;color:var(--mut)}
button:active{transform:scale(.99)}
#s{min-height:1.5em;margin-top:8px;font-size:15px;color:var(--mut)}
.foot{margin-top:32px;font-size:13px}
</style></head><body><main>
<h1>SONIA</h1>
<p>Sonnette Opérationnelle, Non Infinie, Améliorée</p>
<button data-t="1"><b>1re sonnerie</b><span>0001.mp3</span></button>
<button data-t="2"><b>Insistance</b><span>0002.mp3</span></button>
<button data-t="3"><b>Blague</b><span>0003.mp3</span></button>
<div id="s" role="status"></div>
<p class="foot"><a href="/admin">Administration</a></p>
<script>
const s=document.getElementById('s');
document.querySelectorAll('button').forEach(b=>b.onclick=async()=>{
  s.textContent='…';
  try{const r=await fetch('/play?track='+b.dataset.t,{method:'POST'});s.textContent=await r.text();}
  catch(e){s.textContent='SONIA ne répond pas';}
});
</script>
</main></body></html>)html";

static const char ADMIN_PAGE[] PROGMEM = R"html(<!doctype html>
<html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SONIA admin</title>
<style>)html" SONIA_CSS R"html(
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 16px;margin:0;font-size:14px}
dt{color:var(--mut)}dd{margin:0;overflow-wrap:anywhere}
label{display:block;margin:12px 0 4px;font-size:14px;color:var(--mut)}
label.row{display:flex;align-items:center;gap:8px;color:var(--fg);margin-top:4px}
input[type=text],input[type=password],input[type=number]{width:100%;padding:10px 12px;border:1px solid var(--line);
border-radius:8px;background:var(--bg);color:var(--fg);font:inherit}
input[type=checkbox]{width:18px;height:18px;accent-color:var(--acc)}
.hint{margin:12px 0 0;font-size:13px}
code{font-size:12px}
.save{width:100%;padding:14px;border:0;border-radius:12px;background:var(--acc);color:#fff;font:inherit;
font-weight:600;cursor:pointer}
</style></head><body><main>
<h1>SONIA</h1>
<p>Administration · <a href="/">retour à la sonnette</a></p>
<section class="card"><h2>État</h2><dl>
<dt>Wi-Fi</dt><dd>{{wifi_status}}</dd>
<dt>Accès de secours</dt><dd>{{setup_ap}}</dd>
<dt>MQTT</dt><dd>{{mqtt_status}}</dd>
<dt>DFPlayer</dt><dd>{{player_status}}</dd>
<dt>Allumé depuis</dt><dd>{{uptime}}</dd>
</dl></section>
<form method="post" action="/admin">
<section class="card"><h2>Wi-Fi</h2>
<label for="ws">Nom du réseau (SSID)</label>
<input id="ws" name="wifi_ssid" type="text" value="{{wifi_ssid}}" maxlength="32" required>
<label for="wp">Mot de passe</label>
<input id="wp" name="wifi_password" type="password" maxlength="64" placeholder="inchangé si vide" autocomplete="new-password">
</section>
<section class="card"><h2>MQTT</h2>
<label class="row"><input type="checkbox" name="mqtt_enabled" {{mqtt_checked}}> Activer MQTT</label>
<label for="mh">Serveur (adresse IP de préférence)</label>
<input id="mh" name="mqtt_host" type="text" value="{{mqtt_host}}" maxlength="64">
<label for="mpo">Port</label>
<input id="mpo" name="mqtt_port" type="number" min="1" max="65535" value="{{mqtt_port}}" required>
<label for="mu">Utilisateur</label>
<input id="mu" name="mqtt_user" type="text" value="{{mqtt_user}}" maxlength="32" placeholder="vide = sans authentification">
<label for="mpw">Mot de passe</label>
<input id="mpw" name="mqtt_password" type="password" maxlength="64" placeholder="inchangé si vide" autocomplete="new-password">
<label for="mt">Topic de base</label>
<input id="mt" name="mqtt_topic" type="text" value="{{mqtt_topic}}" maxlength="64" required>
<p class="hint">Publie <code>{{mqtt_topic}}/status</code> (online / offline) et <code>{{mqtt_topic}}/ring</code>
à chaque son. Écoute <code>{{mqtt_topic}}/play</code> (1, 2 ou 3).</p>
</section>
<button class="save" type="submit">Enregistrer et redémarrer</button>
</form>
</main></body></html>)html";

static const char SAVED_PAGE[] PROGMEM = R"html(<!doctype html>
<html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="12;url=/admin">
<title>SONIA admin</title>
<style>)html" SONIA_CSS R"html(</style></head><body><main>
<h1>SONIA</h1>
<section class="card"><h2>Réglages enregistrés</h2>
<p>SONIA redémarre. Cette page se recharge dans quelques secondes. Si le Wi-Fi a changé,
l'adresse de SONIA peut changer aussi.</p></section>
</main></body></html>)html";
