// SONIA — Sonnette Opérationnelle, Non Infinie, Améliorée
#ifndef SONIA_DIAG
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include "config.h"
#include "credentials.h"
#include "settings.h"
#include "web_pages.h"

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

// Idle : prêt à sonner. Playing : lecture en cours, demandes ignorées.
// Cooldown : courte pause après la lecture, demandes ignorées.
enum class State { Idle, Playing, Cooldown };
State state = State::Idle;
uint32_t stateSince = 0;

// Origine d'une lecture. Seul le bouton compte pour détecter l'insistance.
enum class Source { Button, Web, Mqtt };

const char* sourceName(Source source) {
  switch (source) {
    case Source::Button: return "button";
    case Source::Web: return "web";
    default: return "mqtt";
  }
}

const char* trackName(uint16_t track) {
  if (track == TRACK_INSIST) return "insist";
  if (track == TRACK_JOKE) return "joke";
  return "first";
}

bool playingRing = false;        // la lecture en cours vient du bouton
bool hasPlayed = false;          // au moins une sonnerie terminée depuis le démarrage
uint32_t lastPlaybackEndAt = 0;  // fin de la dernière sonnerie
bool jokePlayed = false;
uint32_t lastJokeAt = 0;

uint16_t lastPlayTrack = 0;  // 0 : aucun son depuis le démarrage (affiché sur la page d'état)
Source lastPlaySource = Source::Button;
uint32_t lastPlayAt = 0;

void enter(State next, uint32_t now) {
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

ESP8266WebServer server(80);
WiFiClient mqttNet;
PubSubClient mqtt(mqttNet);

String mqttTopic(const char* leaf) { return String(settings.mqttTopic) + "/" + leaf; }

void publishPlay(uint16_t track, Source source) {
  if (!mqtt.connected()) return;
  char payload[80];
  snprintf(payload, sizeof(payload), "{\"track\":%u,\"name\":\"%s\",\"source\":\"%s\"}", track,
           trackName(track), sourceName(source));
  mqtt.publish(mqttTopic("ring").c_str(), payload);
}

// Seul endroit du programme qui lance une lecture. Appelé uniquement depuis
// l'état Idle : jamais pendant ou juste après une lecture.
void play(uint16_t track, Source source, uint32_t now) {
  Serial.printf("Lecture piste %u (%s)\n", track, sourceName(source));
  // Commande « lire /mp3/000N.mp3 » : une seule lecture, pas de répétition.
  player.playMp3Folder(track);
  playingRing = source == Source::Button;
  lastPlayTrack = track;
  lastPlaySource = source;
  lastPlayAt = now;
  enter(State::Playing, now);
  publishPlay(track, source);
}

void ring(uint32_t now) { play(chooseTrack(now), Source::Button, now); }

// Demande de lecture venant du réseau (page web ou MQTT), avec les mêmes garde-fous que le bouton.
enum class PlayResult { Started, UnknownTrack, NoPlayer, Occupied };

PlayResult requestPlay(long track, Source source) {
  if (track < TRACK_FIRST || track > TRACK_JOKE) return PlayResult::UnknownTrack;
  if (!playerReady) return PlayResult::NoPlayer;
  if (state != State::Idle) return PlayResult::Occupied;
  play(uint16_t(track), source, millis());
  return PlayResult::Started;
}

const char* describe(PlayResult result) {
  switch (result) {
    case PlayResult::Started: return "Lecture lancée";
    case PlayResult::UnknownTrack: return "Piste inconnue";
    case PlayResult::NoPlayer: return "DFPlayer absent";
    default: return "Occupé : un son est en cours";
  }
}

// --- MQTT ---

void onMqttMessage(char* topic, uint8_t* payload, unsigned int length) {
  if (mqttTopic("play") != topic) return;
  String text;
  for (unsigned int i = 0; i < length; i++) text += char(payload[i]);
  const PlayResult result = requestPlay(text.toInt(), Source::Mqtt);
  if (result != PlayResult::Started) Serial.printf("MQTT : lecture refusée (%s)\n", describe(result));
}

void handleMqtt(bool wifiUp) {
  static uint32_t lastAttemptAt = 0;
  static bool attempted = false;
  if (!settings.mqttEnabled || !settings.mqttHost[0] || !wifiUp) return;
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }
  const uint32_t now = millis();
  if (attempted && now - lastAttemptAt < MQTT_RETRY_MS) return;
  attempted = true;
  lastAttemptAt = now;

  const String status = mqttTopic("status");
  const char* user = settings.mqttUser[0] ? settings.mqttUser : nullptr;
  const char* password = user ? settings.mqttPassword : nullptr;
  // Testament : le serveur publie « offline » si SONIA disparaît sans prévenir.
  if (mqtt.connect(DEVICE_NAME, user, password, status.c_str(), 0, true, "offline")) {
    mqtt.publish(status.c_str(), "online", true);
    mqtt.subscribe(mqttTopic("play").c_str());
    Serial.printf("MQTT connecté à %s:%u\n", settings.mqttHost, settings.mqttPort);
  } else {
    Serial.printf("MQTT : échec de connexion (état %d), nouvel essai dans %u s\n", mqtt.state(),
                  unsigned(MQTT_RETRY_MS / 1000));
  }
}

String mqttStatusText() {
  if (!settings.mqttEnabled) return "désactivé";
  if (!settings.mqttHost[0]) return "serveur non configuré";
  if (mqtt.connected()) return "connecté à " + String(settings.mqttHost);
  return "déconnecté (état " + String(mqtt.state()) + ")";
}

// --- Page web et administration ---

bool setupApActive = false;
uint32_t restartAt = 0;  // 0 : pas de redémarrage prévu

void sendText(int code, const String& text) { server.send(code, "text/plain; charset=utf-8", text); }

String htmlEscape(const char* text) {
  String out;
  for (const char* c = text; *c; c++) {
    switch (*c) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      case '{': out += F("&#123;"); break;  // évite qu'une valeur imite un {{marqueur}}
      default: out += *c;
    }
  }
  return out;
}

String durationText(uint32_t ms) {
  const uint32_t s = ms / 1000;
  char buf[24];
  snprintf(buf, sizeof(buf), "%uj %02uh %02umin", unsigned(s / 86400), unsigned(s / 3600 % 24), unsigned(s / 60 % 60));
  return buf;
}

// Refuse les requêtes envoyées depuis une autre page web (CSRF) : un navigateur
// indique toujours l'origine d'un formulaire ou d'un fetch envoyé d'un autre site.
bool sameOrigin() {
  const String origin = server.header("Origin");
  if (origin.isEmpty()) return true;  // outils en ligne de commande, anciens navigateurs
  if (origin == "http://" + server.hostHeader()) return true;
  sendText(403, "Requête refusée : origine différente");
  return false;
}

// Authentification Digest : le mot de passe ne circule pas en clair.
bool requireAdmin() {
  if (server.authenticate(ADMIN_USER, ADMIN_PASSWORD)) return true;
  server.requestAuthentication(DIGEST_AUTH, "SONIA admin");
  return false;
}

// Déclenchement manuel depuis l'administration.
void handlePlayRequest() {
  if (!sameOrigin() || !requireAdmin()) return;
  const PlayResult result = requestPlay(server.arg("track").toInt(), Source::Web);
  static const int codes[] = {200, 400, 503, 409};
  sendText(codes[int(result)], describe(result));
}

const char* stateText() {
  switch (state) {
    case State::Idle: return "prête";
    case State::Playing: return "lecture en cours";
    default: return "pause après lecture";
  }
}

const char* sourceLabel(Source source) {
  switch (source) {
    case Source::Button: return "bouton";
    case Source::Web: return "admin";
    default: return "MQTT";
  }
}

// Page publique : informations de debug en lecture seule, sans secret ni action.
void handleStatusPage() {
  String page = FPSTR(STATUS_PAGE);
  const bool connected = WiFi.status() == WL_CONNECTED;
  page.replace("{{state}}", stateText());
  page.replace("{{player}}", playerReady ? "prêt" : "absent");
  page.replace("{{last}}", lastPlayTrack ? "piste " + String(lastPlayTrack) + " · " + sourceLabel(lastPlaySource) +
                                               " · il y a " + durationText(millis() - lastPlayAt)
                                         : String("aucun depuis le démarrage"));
  String wifi = connected ? String(WiFi.RSSI()) + " dBm" : String("non connecté");
  if (setupApActive) wifi += " · accès de secours actif";
  page.replace("{{wifi}}", wifi);
  page.replace("{{mqtt}}", !settings.mqttEnabled ? "désactivé" : mqtt.connected() ? "connecté" : "déconnecté");
  page.replace("{{uptime}}", durationText(millis()));
  page.replace("{{reset}}", htmlEscape(ESP.getResetReason().c_str()));
  page.replace("{{heap}}", String(ESP.getFreeHeap()) + " octets");
  page.replace("{{build}}", __DATE__ " " __TIME__);
  server.send(200, "text/html; charset=utf-8", page);
}

void handleAdminPage() {
  if (!requireAdmin()) return;
  String page = FPSTR(ADMIN_PAGE);
  const bool connected = WiFi.status() == WL_CONNECTED;
  page.replace("{{wifi_status}}", connected ? htmlEscape(WiFi.SSID().c_str()) + " · " + WiFi.localIP().toString() +
                                                  " · " + String(WiFi.RSSI()) + " dBm"
                                            : String("non connecté"));
  page.replace("{{setup_ap}}", setupApActive ? "actif : " + htmlEscape(SETUP_AP_SSID) : String("inactif"));
  page.replace("{{mqtt_status}}", htmlEscape(mqttStatusText().c_str()));
  page.replace("{{player_status}}", playerReady ? "prêt" : "absent");
  page.replace("{{uptime}}", durationText(millis()));
  page.replace("{{wifi_ssid}}", htmlEscape(settings.wifiSsid));
  page.replace("{{mqtt_checked}}", settings.mqttEnabled ? "checked" : "");
  page.replace("{{mqtt_host}}", htmlEscape(settings.mqttHost));
  page.replace("{{mqtt_port}}", String(settings.mqttPort));
  page.replace("{{mqtt_user}}", htmlEscape(settings.mqttUser));
  page.replace("{{mqtt_topic}}", htmlEscape(settings.mqttTopic));
  server.send(200, "text/html; charset=utf-8", page);
}

// Copie un champ du formulaire. Un champ secret laissé vide garde l'ancienne valeur.
// Renvoie false si la valeur est trop longue.
template <size_t N>
bool readField(char (&dst)[N], const char* name, bool secret) {
  String value = server.arg(name);
  if (!secret) value.trim();
  if (secret && value.isEmpty()) return true;
  if (value.length() >= N) return false;
  copyField(dst, value.c_str());
  return true;
}

void handleAdminSave() {
  if (!sameOrigin() || !requireAdmin()) return;
  Settings next = settings;
  const bool fits = readField(next.wifiSsid, "wifi_ssid", false) &&
                    readField(next.wifiPassword, "wifi_password", true) &&
                    readField(next.mqttHost, "mqtt_host", false) && readField(next.mqttUser, "mqtt_user", false) &&
                    readField(next.mqttPassword, "mqtt_password", true) &&
                    readField(next.mqttTopic, "mqtt_topic", false);
  if (!fits) return sendText(400, "Un champ est trop long");
  if (!next.wifiSsid[0]) return sendText(400, "Le nom du réseau Wi-Fi est obligatoire");

  next.mqttEnabled = server.hasArg("mqtt_enabled");
  const long port = server.arg("mqtt_port").toInt();
  if (port < 1 || port > 65535) return sendText(400, "Port MQTT invalide");
  next.mqttPort = uint16_t(port);

  size_t len = strlen(next.mqttTopic);
  while (len && next.mqttTopic[len - 1] == '/') next.mqttTopic[--len] = '\0';
  if (!len || strpbrk(next.mqttTopic, "+#")) return sendText(400, "Topic MQTT invalide (vide, ou contient + ou #)");
  if (next.mqttEnabled && !next.mqttHost[0]) return sendText(400, "Le serveur MQTT est obligatoire quand MQTT est activé");

  settings = next;
  if (!saveSettings()) return sendText(500, "Échec de l'enregistrement");
  server.send_P(200, "text/html; charset=utf-8", SAVED_PAGE);
  Serial.println("Réglages enregistrés, redémarrage");
  restartAt = millis() + 1500;  // laisse le temps d'envoyer la réponse
  if (!restartAt) restartAt = 1;
}

void setupWebServer() {
  server.collectHeaders("Origin");  // nécessaire à sameOrigin()
  server.on("/", HTTP_GET, handleStatusPage);
  server.on("/admin", HTTP_GET, handleAdminPage);
  server.on("/admin", HTTP_POST, handleAdminSave);
  server.on("/admin/play", HTTP_POST, handlePlayRequest);
  server.onNotFound([]() { sendText(404, "Introuvable"); });
  server.begin();
}

// --- Réseau ---

// Wi-Fi non bloquant : la sonnette fonctionne aussi sans réseau.
void setupNetwork() {
  WiFi.persistent(false);  // les identifiants viennent des réglages, pas de la flash du SDK
  WiFi.mode(WIFI_STA);
  WiFi.hostname(DEVICE_NAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(settings.wifiSsid, settings.wifiPassword);

  ArduinoOTA.setHostname(DEVICE_NAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    if (playerReady) player.stop();
    Serial.println("OTA : début");
  });
  ArduinoOTA.onEnd([]() { Serial.println("OTA : terminé"); });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA : erreur %u\n", error); });

  setupWebServer();

  mqttNet.setTimeout(MQTT_CONNECT_TIMEOUT_MS);
  mqtt.setServer(settings.mqttHost, settings.mqttPort);
  mqtt.setSocketTimeout(2);
  mqtt.setCallback(onMqttMessage);
}

// Point d'accès de secours : permet de corriger le Wi-Fi depuis http://192.168.4.1/admin.
void startSetupAp() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_AP_SSID, ADMIN_PASSWORD);
  setupApActive = true;
  Serial.printf("Pas de Wi-Fi : point d'accès « %s » ouvert (http://%s/admin)\n", SETUP_AP_SSID,
                WiFi.softAPIP().toString().c_str());
}

void stopSetupAp() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  setupApActive = false;
  Serial.println("Wi-Fi retrouvé : point d'accès de secours fermé");
}

void handleNetwork() {
  static bool wasConnected = false;
  static bool otaStarted = false;
  static uint32_t lastConnectedAt = 0;
  const uint32_t now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (connected) {
    lastConnectedAt = now;
    if (!wasConnected) {
      Serial.printf("Wi-Fi connecté : %s (%s.local)\n", WiFi.localIP().toString().c_str(), DEVICE_NAME);
      if (!otaStarted) {
        ArduinoOTA.begin();
        otaStarted = true;
      }
      if (setupApActive) stopSetupAp();
    }
  } else {
    if (wasConnected) Serial.println("Wi-Fi perdu, reconnexion automatique");
    if (!setupApActive && now - lastConnectedAt >= SETUP_AP_DELAY_MS) startSetupAp();
  }
  wasConnected = connected;

  if (otaStarted) ArduinoOTA.handle();
  server.handleClient();
  handleMqtt(connected);

  if (restartAt && int32_t(now - restartAt) >= 0) ESP.restart();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nSONIA démarre");
  button.begin();
  loadSettings();
  setupNetwork();  // avant le DFPlayer : l'OTA et l'admin doivent marcher même s'il est absent

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
