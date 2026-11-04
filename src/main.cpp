// SONIA — Sonnette Opérationnelle, Non Infinie, Améliorée
#ifndef SONIA_DIAG
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"  // ignoré par git
#else
#error "Copier include/secrets.example.h en include/secrets.h et le remplir (voir README)"
#endif

SoftwareSerial dfSerial(PIN_DF_RX, PIN_DF_TX);
DFRobotDFPlayerMini player;
bool playerReady = false;

// Bouton avec anti-rebond. pressed() renvoie true une seule fois par appui
// (front descendant stable) : maintenir le bouton ne redéclenche rien.
class Button {
 public:
  explicit Button(uint8_t pin) : pin_(pin) {}

  void begin() {
    pinMode(pin_, INPUT_PULLUP);
    stable_ = lastRead_ = digitalRead(pin_);
  }

  bool pressed() {
    const bool reading = digitalRead(pin_);
    const uint32_t now = millis();
    if (reading != lastRead_) {
      lastRead_ = reading;
      changedAt_ = now;
    }
    if (reading != stable_ && now - changedAt_ >= DEBOUNCE_MS) {
      stable_ = reading;
      return stable_ == LOW;
    }
    return false;
  }

 private:
  uint8_t pin_;
  bool stable_ = HIGH;
  bool lastRead_ = HIGH;
  uint32_t changedAt_ = 0;
};

Button button(PIN_BUTTON);

// Idle : prêt à sonner. Playing : lecture en cours, appuis ignorés.
// Cooldown : courte pause après la lecture, appuis ignorés.
enum class State { Idle, Playing, Cooldown };
State state = State::Idle;
uint32_t stateSince = 0;

bool playingRing = false;        // la lecture en cours vient du bouton (pas de la page web)
bool hasPlayed = false;          // au moins une sonnerie terminée depuis le démarrage
uint32_t lastPlaybackEndAt = 0;  // fin de la dernière sonnerie
bool jokePlayed = false;
uint32_t lastJokeAt = 0;

void enter(State next, uint32_t now) {
  // Seules les vraies sonneries comptent pour détecter l'insistance.
  if (state == State::Playing && next != State::Playing && playingRing) {
    hasPlayed = true;
    lastPlaybackEndAt = now;
  }
  state = next;
  stateSince = now;
}

uint16_t chooseTrack(uint32_t now) {
  // La personne insiste : chaque sonnerie dans la fenêtre joue la piste d'insistance.
  if (hasPlayed && now - lastPlaybackEndAt <= INSIST_WINDOW_MS) return TRACK_INSIST;

  // 1re sonnerie : rarement remplacée par la blague.
  const bool jokeAllowed = !jokePlayed || now - lastJokeAt >= JOKE_MIN_GAP_MS;
  if (jokeAllowed && ESP.random() % JOKE_ODDS == 0) {  // générateur matériel de l'ESP8266
    jokePlayed = true;
    lastJokeAt = now;
    return TRACK_JOKE;
  }
  return TRACK_FIRST;
}

// Seul endroit du programme qui lance une lecture. Appelé uniquement depuis
// l'état Idle (bouton ou page web) : jamais pendant ou juste après une lecture.
void play(uint16_t track, bool fromButton, uint32_t now) {
  Serial.printf("%s : piste %u\n", fromButton ? "Ding !" : "Page web", track);
  // Commande « lire /mp3/000N.mp3 » : une seule lecture, pas de répétition.
  player.playMp3Folder(track);
  playingRing = fromButton;
  enter(State::Playing, now);
}

void ring(uint32_t now) { play(chooseTrack(now), /*fromButton=*/true, now); }

// --- Page web : http://sonia.local ---
ESP8266WebServer server(80);

static const char PAGE[] PROGMEM = R"html(<!doctype html>
<html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SONIA</title>
<style>
:root{color-scheme:light dark;--bg:#f5f4f0;--fg:#1c1c1a;--card:#fff;--mut:#6b6a64;--acc:#c2410c}
@media(prefers-color-scheme:dark){:root{--bg:#161614;--fg:#eceae4;--card:#23221f;--mut:#9c9a92;--acc:#fb923c}}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:420px;margin:0 auto;padding:32px 16px}
h1{margin:0;font-size:30px;letter-spacing:.02em}
p{margin:4px 0 24px;color:var(--mut);font-size:14px}
button{display:block;width:100%;margin:0 0 12px;padding:18px 20px;border:0;border-left:4px solid var(--acc);
border-radius:12px;background:var(--card);color:var(--fg);font:inherit;text-align:left;cursor:pointer;
box-shadow:0 1px 3px rgba(0,0,0,.12)}
button b{display:block;font-size:18px}
button span{font-size:13px;color:var(--mut)}
button:active{transform:scale(.99)}
#s{min-height:1.5em;margin-top:8px;font-size:15px;color:var(--mut)}
</style></head><body><main>
<h1>SONIA</h1>
<p>Sonnette Opérationnelle, Non Infinie, Améliorée</p>
<button data-t="1"><b>1re sonnerie</b><span>0001.mp3</span></button>
<button data-t="2"><b>Insistance</b><span>0002.mp3</span></button>
<button data-t="3"><b>Blague</b><span>0003.mp3</span></button>
<div id="s" role="status"></div>
<script>
const s=document.getElementById('s');
document.querySelectorAll('button').forEach(b=>b.onclick=async()=>{
  s.textContent='…';
  try{const r=await fetch('/play?track='+b.dataset.t,{method:'POST'});s.textContent=await r.text();}
  catch(e){s.textContent='SONIA ne répond pas';}
});
</script>
</main></body></html>)html";

void sendText(int code, const String& text) {
  server.send(code, "text/plain; charset=utf-8", text);
}

void handlePlayRequest() {
  const long track = server.arg("track").toInt();
  if (track < TRACK_FIRST || track > TRACK_JOKE) return sendText(400, "Piste inconnue");
  if (!playerReady) return sendText(503, "DFPlayer absent");
  // Mêmes garde-fous que le bouton : rien pendant une lecture ni pendant la pause qui suit.
  if (state != State::Idle) return sendText(409, "Occupé : un son est en cours");
  play(uint16_t(track), /*fromButton=*/false, millis());
  sendText(200, "Lecture de " + String(track < 10 ? "000" : "00") + String(track) + ".mp3");
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html; charset=utf-8", PAGE); });
  server.on("/play", HTTP_POST, handlePlayRequest);
  server.onNotFound([]() { sendText(404, "Introuvable"); });
}

void handlePlayerEvents(uint32_t now) {
  if (!playerReady || !player.available()) return;
  const uint8_t type = player.readType();
  const uint16_t value = player.read();
  switch (type) {
    case DFPlayerPlayFinished:
      // Le DFPlayer envoie souvent cet événement deux fois : seul le premier,
      // reçu pendant la lecture, compte. Il ne relance jamais de lecture.
      if (state == State::Playing) enter(State::Cooldown, now);
      break;
    case DFPlayerError:
      Serial.printf("Erreur DFPlayer %u\n", value);
      if (state == State::Playing) enter(State::Cooldown, now);
      break;
    case DFPlayerCardRemoved:
      Serial.println("Carte SD retirée");
      break;
    case DFPlayerCardInserted:
      Serial.println("Carte SD insérée");
      break;
    default:
      break;
  }
}

// Wi-Fi non bloquant : la sonnette fonctionne aussi sans réseau.
void setupNetwork() {
  WiFi.persistent(false);  // n'écrit pas les identifiants en flash
  WiFi.mode(WIFI_STA);
  WiFi.hostname(DEVICE_NAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  ArduinoOTA.setHostname(DEVICE_NAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    if (playerReady) player.stop();
    Serial.println("OTA : début");
  });
  ArduinoOTA.onEnd([]() { Serial.println("OTA : terminé"); });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA : erreur %u\n", error); });

  setupWebServer();
}

void handleNetwork() {
  static bool wasConnected = false;
  static bool servicesStarted = false;  // OTA et page web, démarrés à la 1re connexion
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    Serial.printf("Wi-Fi connecté : %s (%s.local)\n", WiFi.localIP().toString().c_str(), DEVICE_NAME);
    if (!servicesStarted) {
      ArduinoOTA.begin();
      server.begin();
      servicesStarted = true;
    }
  } else if (!connected && wasConnected) {
    Serial.println("Wi-Fi perdu, reconnexion automatique");
  }
  wasConnected = connected;
  if (servicesStarted) {
    ArduinoOTA.handle();
    server.handleClient();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nSONIA démarre");
  button.begin();
  setupNetwork();  // avant le DFPlayer : l'OTA doit marcher même s'il est absent

  dfSerial.begin(9600);
  // Sans accusé de réception : ce clone de DFPlayer n'acquitte pas toujours,
  // ce qui fait échouer begin() en mode ACK. begin() ne vérifie alors rien,
  // donc on s'assure qu'il répond en lui demandant le nombre de fichiers.
  // La première trame après le démarrage de l'ESP arrive souvent abîmée,
  // d'où les tentatives répétées.
  player.begin(dfSerial, /*isACK=*/false, /*doReset=*/true);
  for (int attempt = 1; attempt <= 3 && !playerReady; attempt++) {
    const int files = player.readFileCounts(DFPLAYER_DEVICE_SD);
    Serial.printf("DFPlayer : %d fichier(s) sur la carte SD\n", files);
    playerReady = files > 0;
    if (!playerReady) delay(500);
  }
  if (!playerReady) {
    Serial.println("DFPlayer introuvable : vérifier le câblage et la carte SD");
    return;
  }
  player.setTimeOut(500);
  player.outputDevice(DFPLAYER_DEVICE_SD);  // certains clones ne choisissent pas la SD seuls
  player.stop();            // rien ne joue au démarrage
  player.disableLoopAll();  // désactive toute répétition
  player.volume(VOLUME);
  Serial.println("DFPlayer prêt");
}

void loop() {
  handleNetwork();
  const uint32_t now = millis();
  const bool press = button.pressed();  // lu à chaque tour pour garder l'anti-rebond à jour
  handlePlayerEvents(now);

  switch (state) {
    case State::Idle:
      if (press) {
        if (playerReady) {
          ring(now);
        } else {
          Serial.println("Appui ignoré : DFPlayer absent");
        }
      }
      break;
    case State::Playing:
      if (now - stateSince >= MAX_PLAY_MS) {
        Serial.println("Lecture trop longue : arrêt forcé");
        player.stop();
        enter(State::Cooldown, now);
      }
      break;
    case State::Cooldown:
      if (now - stateSince >= COOLDOWN_MS) enter(State::Idle, now);
      break;
  }
}
#endif  // SONIA_DIAG
