// Secrets : include/secrets.h est ignoré par git (modèle : secrets.example.h).
#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copier include/secrets.example.h en include/secrets.h et le remplir (voir README)"
#endif

#ifndef ADMIN_PASSWORD
#error "ADMIN_PASSWORD manquant dans include/secrets.h (voir secrets.example.h)"
#endif
