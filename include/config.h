#pragma once
#include <Arduino.h>

// Nom réseau (Wi-Fi, mDNS sonia.local, OTA).
constexpr const char* DEVICE_NAME = "sonia";

// --- Brochage (Wemos D1 mini Pro) ---
// Bouton : une patte sur PIN_BUTTON, l'autre sur GND (pull-up interne activé).
constexpr uint8_t PIN_BUTTON = D5;  // GPIO14
// DFPlayer : liaison série logicielle, 9600 bauds.
constexpr uint8_t PIN_DF_RX = D6;   // GPIO12 <- TX du DFPlayer
constexpr uint8_t PIN_DF_TX = D7;   // GPIO13 -> RX du DFPlayer (via résistance 1 kΩ)

// --- Pistes : fichiers /mp3/000N.mp3 sur la carte SD ---
constexpr uint16_t TRACK_FIRST  = 1;  // /mp3/0001.mp3 : 1re sonnerie
constexpr uint16_t TRACK_INSIST = 2;  // /mp3/0002.mp3 : la personne insiste
constexpr uint16_t TRACK_JOKE   = 3;  // /mp3/0003.mp3 : blague, très rare, à la place de la 1re

constexpr uint8_t VOLUME = 30;  // 0 à 30

// --- Temporisations ---
constexpr uint32_t DEBOUNCE_MS   = 30;     // anti-rebond du bouton
constexpr uint32_t COOLDOWN_MS   = 1500;   // appuis ignorés après la fin d'une lecture
constexpr uint32_t MAX_PLAY_MS   = 30000;  // sécurité : arrêt forcé au-delà
// Une sonnerie moins de INSIST_WINDOW_MS après la fin de la précédente = insistance.
// Fenêtre glissante : après ce délai sans sonnerie, on repart sur la 1re.
constexpr uint32_t INSIST_WINDOW_MS = 20000;

// --- Blague ---
// Tirée uniquement à la place d'une 1re sonnerie : 1 chance sur JOKE_ODDS (500 -> 0,2 %).
constexpr uint32_t JOKE_ODDS       = 500;
constexpr uint32_t JOKE_MIN_GAP_MS = 24UL * 3600UL * 1000UL;  // au plus une fois par 24 h
