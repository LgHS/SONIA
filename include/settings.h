// Réglages modifiables depuis la page d'administration, enregistrés en flash.
#pragma once
#include <Arduino.h>

struct Settings {
  char wifiSsid[33];
  char wifiPassword[65];
  bool mqttEnabled;
  char mqttHost[65];
  uint16_t mqttPort;
  char mqttUser[33];
  char mqttPassword[65];
  char mqttTopic[65];
};

extern Settings settings;

// Charge les réglages enregistrés, ou les valeurs par défaut (secrets.h) s'il n'y en a pas.
void loadSettings();
bool saveSettings();

// Copie une chaîne dans un champ de taille fixe, toujours terminée par '\0'.
template <size_t N>
void copyField(char (&dst)[N], const char* src) {
  strncpy(dst, src, N - 1);
  dst[N - 1] = '\0';
}
