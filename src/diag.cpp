// Diagnostic DFPlayer : compilé seulement avec -DSONIA_DIAG (env "diag").
#ifdef SONIA_DIAG
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "config.h"

static void sendCmd(SoftwareSerial& s, uint8_t cmd, uint16_t param) {
  uint8_t f[10] = {0x7E, 0xFF, 0x06, cmd, 0x00, uint8_t(param >> 8), uint8_t(param), 0, 0, 0xEF};
  uint16_t sum = 0;
  for (int i = 1; i < 7; i++) sum += f[i];
  sum = uint16_t(0 - sum);
  f[7] = sum >> 8;
  f[8] = sum & 0xFF;
  s.write(f, sizeof(f));
}

static size_t dump(SoftwareSerial& s, uint32_t ms) {
  size_t n = 0;
  const uint32_t t = millis();
  while (millis() - t < ms) {
    while (s.available()) {
      Serial.printf("%02X ", s.read());
      n++;
    }
    yield();
  }
  Serial.println(n ? "" : "(rien)");
  return n;
}

static size_t probe(uint8_t rx, uint8_t tx, const char* label) {
  SoftwareSerial s(rx, tx);
  s.begin(9600);
  Serial.printf("\n== Essai %s (RX=GPIO%u, TX=GPIO%u)\n", label, rx, tx);
  size_t n = 0;
  Serial.println("-- sans reset --");
  Serial.print("en ligne ?   -> "); sendCmd(s, 0x3F, 0); n += dump(s, 1000);
  Serial.print("etat         -> "); sendCmd(s, 0x42, 0); n += dump(s, 800);
  Serial.print("nb fichiers  -> "); sendCmd(s, 0x48, 0); n += dump(s, 800);
  Serial.println("-- avec reset --");
  Serial.print("reset        -> "); sendCmd(s, 0x0C, 0); n += dump(s, 5000);
  Serial.print("choix SD     -> "); sendCmd(s, 0x09, 2); n += dump(s, 1500);
  Serial.print("en ligne ?   -> "); sendCmd(s, 0x3F, 0); n += dump(s, 1000);
  Serial.print("version      -> "); sendCmd(s, 0x46, 0); n += dump(s, 500);
  Serial.print("etat         -> "); sendCmd(s, 0x42, 0); n += dump(s, 500);
  Serial.print("nb fichiers  -> "); sendCmd(s, 0x48, 0); n += dump(s, 500);
  delay(4000);
  Serial.print("nb fichiers (+4 s) -> "); sendCmd(s, 0x48, 0); n += dump(s, 800);
  Serial.print("etat (+4 s)        -> "); sendCmd(s, 0x42, 0); n += dump(s, 800);
  Serial.print("volume       -> "); sendCmd(s, 0x06, VOLUME); n += dump(s, 300);
  struct { const char* label; uint8_t cmd; uint16_t param; } plays[] = {
    {"dossier mp3/0001 (0x12)", 0x12, 1},
    {"piste 1 par index (0x03)", 0x03, 1},
    {"lecture/reprise (0x0D)", 0x0D, 0},
  };
  for (auto& p : plays) {
    Serial.printf("lecture %s -> ", p.label); sendCmd(s, p.cmd, p.param); n += dump(s, 1500);
    Serial.print("   etat pendant -> "); sendCmd(s, 0x42, 0); n += dump(s, 800);
    Serial.print("   piste courante -> "); sendCmd(s, 0x4C, 0); n += dump(s, 800);
    delay(2000);
    Serial.print("   stop -> "); sendCmd(s, 0x16, 0); n += dump(s, 800);
  }
  Serial.printf("octets recus : %u\n", n);
  s.end();
  return n;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nSONIA diagnostic DFPlayer");
  const size_t a = probe(PIN_DF_RX, PIN_DF_TX, "cablage prevu");
  delay(1000);
  const size_t b = probe(PIN_DF_TX, PIN_DF_RX, "fils inverses");
  Serial.printf("\nRESULTAT : prevu=%u octets, inverse=%u octets\n", a, b);
}

void loop() {}
#endif
