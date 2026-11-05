<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logo-dark.png">
    <img src="docs/logo-light.png" alt="SONIA : Sonnette Opérationnelle, Non Infinie, Améliorée" width="600">
  </picture>
</p>

# SONIA

**S**onnette **O**pérationnelle, **N**on **I**nfinie, **A**méliorée.

Une sonnette à base d'ESP8266 et de DFPlayer Mini. Un appui sur le bouton joue un MP3 depuis une carte SD. Si la personne insiste, un autre son est joué. Très rarement, la première sonnerie est remplacée par une blague. Le firmware se met à jour par Wi-Fi (OTA), propose une page web pour lancer les sons à la main et une page d'administration protégée par mot de passe pour régler le Wi-Fi et le MQTT.

## Fonctionnement

| Situation | Son joué |
|---|---|
| 1re sonnerie, ou plus de 20 s de calme depuis la fin de la précédente | `0001.mp3` |
| Nouvelle sonnerie moins de 20 s après la fin de la précédente | `0002.mp3`, à chaque fois tant que la personne insiste |
| 1re sonnerie, tirage au sort gagnant (0,2 %, au plus une fois par 24 h) | `0003.mp3` à la place de `0001.mp3` |

**« Non infinie » :** la sonnette ne peut jamais jouer en boucle.

- Un seul endroit du code lance une lecture, et il n'est appelé que sur un nouvel appui, quand rien ne joue.
- Le signal « fin de lecture » envoyé par le DFPlayer ne relance jamais de son.
- Les appuis sont ignorés pendant la lecture et pendant 1,5 s après.
- Une lecture est coupée de force au bout de 30 s.
- Maintenir le bouton enfoncé ne redéclenche rien, et l'anti-rebond est de 30 ms.

Tous les réglages (volume, délais, probabilité de la blague, broches) sont dans [`include/config.h`](include/config.h).

## Matériel

- Wemos D1 mini Pro (ESP8266, 16 Mo de flash)
- DFPlayer Mini et une carte micro SD
- Un bouton poussoir
- Une résistance de 1 kΩ
- Un condensateur électrolytique de 100 à 470 µF (10 V minimum)
- Un haut-parleur de 4 à 8 Ω, et éventuellement un ampli

## Câblage

### DFPlayer et D1 mini

| DFPlayer | D1 mini | Remarque |
|---|---|---|
| VCC (broche 1) | 5V | |
| GND (broche 7 ou 10) | G | Masse commune obligatoire |
| TX (broche 3) | D6 (GPIO12) | |
| RX (broche 2) | D7 (GPIO13) | Via la résistance de 1 kΩ |

Le condensateur se place entre VCC et GND, au plus près du DFPlayer, avec la bande « − » sur GND. Sans lui, la liaison série reçoit du bruit et le DFPlayer peut ne pas lire la carte.

### Bouton

Une patte sur **D5** (GPIO14), l'autre sur **G**. Le pull-up interne est activé, donc pas besoin de résistance.

### Son

Deux possibilités :

- **Sans ampli :** le haut-parleur (3 W maximum) se branche sur **SPK_1** et **SPK_2** du DFPlayer. Ne reliez pas SPK_2 à la masse.
- **Avec un ampli :** l'entrée de l'ampli se branche sur **DAC_L** (et/ou **DAC_R**) et sur un **GND** du DFPlayer. La masse de l'alimentation de l'ampli doit être reliée à celle de l'ESP. Avec un ampli à sorties pontées (PAM8403, PAM8406…), ne reliez jamais une sortie « − » à la masse.

Les broches D3, D4 et D8 sont évitées parce qu'elles gênent le démarrage de l'ESP8266, et D0 parce qu'elle n'a pas de pull-up.

## Carte SD

```
/mp3/0001.mp3   1re sonnerie
/mp3/0002.mp3   la personne insiste
/mp3/0003.mp3   blague
```

- **Formatez la carte avec une seule petite partition FAT16 (2 Go).** Avec une partition FAT32 de 16 Go, notre clone de DFPlayer voyait la carte mais 0 fichier. Sur macOS :
  ```
  diskutil partitionDisk /dev/diskN MBR "MS-DOS FAT16" SONIA 2G "Free Space" LIBRE R
  ```
  Vérifiez `diskutil list` avant : cette commande efface toute la carte.
- Les noms doivent commencer par 4 chiffres. Le DFPlayer choisit le fichier par son numéro, pas par l'ordre de copie.
- Sur macOS, lancez `dot_clean /Volumes/SONIA` avant d'éjecter, pour supprimer les fichiers cachés `._*`.

## Installation

Le projet utilise [PlatformIO](https://platformio.org/).

1. **Secrets du firmware :** copiez `include/secrets.example.h` en `include/secrets.h` et remplissez le Wi-Fi par défaut, le mot de passe OTA et le mot de passe de la page d'administration (8 caractères minimum).
2. **Secrets de PlatformIO (pour l'OTA) :** copiez `secrets.example.ini` en `secrets.ini` et mettez le même mot de passe OTA.
3. **Premier flash, en USB :**
   ```
   pio run -t upload
   pio device monitor
   ```

Les fichiers `include/secrets.h` et `secrets.ini` sont ignorés par git. Ne mettez jamais de vraies valeurs dans les fichiers `*.example.*`.

## Mise à jour sans fil (OTA)

Depuis le même réseau :

```
pio run -e ota -t upload
```

L'OTA est protégé par le mot de passe défini dans les secrets. Si `sonia.local` ne répond pas, passez l'adresse IP : `pio run -e ota -t upload --upload-port <IP>`.

## Page web

Ouvrez `http://sonia.local` depuis le même réseau. Trois boutons lancent les trois sons. La page applique les mêmes garde-fous que le bouton : elle répond « Occupé » si un son est en cours. Les sons lancés depuis la page ne comptent pas pour la détection d'insistance.

La page n'a pas de mot de passe : toute personne sur le réseau peut faire sonner.

## Administration

`http://sonia.local/admin`, utilisateur `admin`, mot de passe `ADMIN_PASSWORD` de `include/secrets.h`. L'authentification HTTP Digest évite d'envoyer le mot de passe en clair, et les formulaires envoyés depuis un autre site sont refusés.

La page affiche l'état de SONIA (Wi-Fi, signal, MQTT, DFPlayer, durée depuis le démarrage) et permet de régler :

- **le Wi-Fi :** nom du réseau et mot de passe ;
- **le MQTT :** activé ou non, serveur, port, utilisateur, mot de passe et topic de base.

Un mot de passe laissé vide garde sa valeur actuelle. « Enregistrer et redémarrer » sauvegarde les réglages dans la flash de l'ESP, où ils survivent aux redémarrages et aux mises à jour OTA. Les valeurs de `secrets.h` ne servent qu'au tout premier démarrage.

### Point d'accès de secours

Si SONIA ne trouve pas le Wi-Fi pendant 60 s (mauvais mot de passe, réseau changé…), elle ouvre le point d'accès **`SONIA-config`**, protégé par le mot de passe d'administration. Connectez-vous-y et corrigez les réglages sur `http://192.168.4.1/admin`. Le point d'accès se ferme dès que le Wi-Fi revient. La sonnette continue de fonctionner pendant ce temps.

## MQTT

Une fois activé dans l'administration, avec `sonia` comme topic de base :

| Topic | Sens | Contenu |
|---|---|---|
| `sonia/status` | publié, conservé | `online`, ou `offline` publié par le serveur si SONIA disparaît |
| `sonia/ring` | publié | `{"track":1,"name":"first","source":"button"}` à chaque son |
| `sonia/play` | écouté | `1`, `2` ou `3` pour lancer un son, avec les mêmes garde-fous que le bouton |

`name` vaut `first`, `insist` ou `joke`, et `source` vaut `button`, `web` ou `mqtt`.

Indiquez le serveur par son adresse IP de préférence. Si le serveur est injoignable, chaque tentative de connexion (toutes les 15 s) bloque SONIA jusqu'à 1,5 s, et une résolution DNS ajouterait encore du temps. Un appui très bref sur le bouton à ce moment-là pourrait être manqué.

## Diagnostic du DFPlayer

Si le DFPlayer ne répond pas, un firmware de diagnostic envoie des commandes brutes et affiche les réponses sur le port série. Il teste aussi le câblage avec TX et RX inversés, côté logiciel :

```
pio run -e diag -t upload
pio device monitor
```

Pour revenir au firmware normal : `pio run -t upload`.

## Pistes pour la suite

- Couper l'ampli entre deux sonneries (MOSFET ou broche MUTE), pour supprimer le bourdonnement au repos
