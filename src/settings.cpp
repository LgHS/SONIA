#include "settings.h"
#include <EEPROM.h>
#include "config.h"
#include "credentials.h"

Settings settings;

namespace {

constexpr uint32_t MAGIC = 0x534F4E31;  // "SON1" : à changer si la structure change

struct Stored {
  uint32_t magic;
  Settings data;
  uint32_t checksum;
};

uint32_t checksumOf(const Settings& s) {
  uint32_t h = 2166136261u;  // FNV-1a
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < sizeof(s); i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

void setDefaults() {
  memset(&settings, 0, sizeof(settings));
  copyField(settings.wifiSsid, WIFI_SSID);
  copyField(settings.wifiPassword, WIFI_PASSWORD);
  settings.mqttEnabled = false;
  settings.mqttPort = MQTT_DEFAULT_PORT;
  copyField(settings.mqttTopic, MQTT_DEFAULT_TOPIC);
}

}  // namespace

void loadSettings() {
  Stored stored;
  EEPROM.begin(sizeof(Stored));
  EEPROM.get(0, stored);
  EEPROM.end();
  if (stored.magic != MAGIC || stored.checksum != checksumOf(stored.data)) {
    setDefaults();
    return;
  }
  settings = stored.data;
}

bool saveSettings() {
  Stored stored{MAGIC, settings, checksumOf(settings)};
  EEPROM.begin(sizeof(Stored));
  EEPROM.put(0, stored);
  const bool ok = EEPROM.commit();
  EEPROM.end();
  return ok;
}
