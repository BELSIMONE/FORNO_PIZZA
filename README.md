# Forno Pizza ESP32

Firmware per smartizzare un forno da pizza a resistenze elettriche (cielo + platea),
basato su ESP32, con controllo temperatura, timer di cottura, interfaccia web,
ricette salvabili e aggiornamento firmware via OTA.

## Hardware

- **ESP32** (Dev Module)
- Display **TFT ST7735** (SPI) per temperature, timer, costo stimato e IP
- 2x termocoppia tipo K con moduli **MAX6675** (CIELO e PLATEA), sullo stesso bus SPI software,
  lette a rotazione
- 2x relè allo stato solido (SSR) che pilotano le resistenze CIELO e PLATEA
- Pulsante **touch capacitivo** (pin `T5`) per impostare un timer di cottura
- Buzzer per segnalazioni acustiche a fine timer

### Pin utilizzati

| Funzione        | Pin  |
|-----------------|------|
| TFT MOSI        | 23   |
| TFT SCLK        | 18   |
| TFT CS          | 27   |
| TFT DC          | 5    |
| Buzzer          | 33   |
| Touch (timer)   | T5   |
| Relè CIELO      | 22   |
| Relè PLATEA     | 17   |
| Termocoppie DO  | 15   |
| Termocoppie CLK | 4    |
| Termocoppia CS1 (cielo)  | 21 |
| Termocoppia CS2 (platea) | 2  |

## Funzionalità

- **Controllo temperatura a isteresi** per entrambe le zone, con soglia di sicurezza
  (spegnimento forzato oltre +20°C dal setpoint)
- **Potenza regolabile (0-100%) a burst-fire lento**: le due resistenze condividono la
  stessa linea elettrica e non vengono mai accese insieme. Ognuna riceve una "fetta" di
  tempo proporzionale alla potenza impostata dentro un periodo lento (6s di default),
  cosi' le spie al neon collegate ai relè restano visibilmente intermittenti invece di
  sfarfallare a frequenza impercettibile. Regolabile da `/power`.
- **Ricette salvabili**: nome + temperature + potenza delle due zone, salvate su
  filesystem (LittleFS) e richiamabili dall'interfaccia web (`/recipes`)
- **Timer di cottura** avviabile dal pulsante touch (+60s per tocco), con conto alla
  rovescia e melodia casuale di fine cottura
- **Stima costo elettrico** in base ai minuti di accensione reale delle resistenze
- **Interfaccia web** (dark theme, ottimizzata per smartphone):
  - `/` Monitor: temperature live, stato relè, costo stimato
  - `/setting` Impostazione setpoint di temperatura
  - `/power` Impostazione potenza (%) delle due resistenze
  - `/recipes` Gestione ricette (salva, carica, elimina)
  - `/temperature-chart` Grafico storico temperature (canvas nativo, nessuna
    dipendenza da CDN esterni: funziona anche offline in modalità Access Point)
- **Fallback WiFi → Access Point**: se il WiFi di casa non risponde entro 60s
  all'avvio, il forno apre un proprio Access Point (`FornoPizza`, vedi
  `include/secrets.h`) restando comunque utilizzabile via web/OTA. Se la rete di
  casa cade durante il funzionamento l'Access Point si riattiva automaticamente
  (e si disattiva da solo appena la rete torna disponibile), senza mai bloccare
  la lettura delle temperature o il controllo dei relè.
- **OTA** (`ArduinoOTA`) per aggiornare il firmware via WiFi senza cavo USB
- **mDNS**: il forno è raggiungibile anche via `http://mio-esp32.local`

## Struttura del progetto

```
platformio.ini       Configurazione PlatformIO (board, librerie, ambienti di upload)
src/main.cpp          Firmware
include/secrets.h      Credenziali WiFi/AP (NON versionato, vedi sotto)
forno_pizza/            Archivio storico delle evoluzioni Arduino IDE del progetto
```

## Setup

### Credenziali

Crea `include/secrets.h` (escluso da git) con:

```cpp
#pragma once
static const char *WIFI_SSID = "...";
static const char *WIFI_PASSWORD = "...";
static const char *AP_SSID = "FornoPizza";
static const char *AP_PASSWORD = "...";  // almeno 8 caratteri
```

### Compilazione e primo flash (via USB)

```
pio run -e usb -t upload
```

### Aggiornamenti successivi (via OTA)

Il forno deve essere online. L'IP/hostname di destinazione è configurato in
`platformio.ini` nell'ambiente `[env:ota]`:

```
pio run -e ota -t upload
```

## Note di sicurezza elettrica

Il firmware presume che le due resistenze (CIELO e PLATEA) condividano la stessa
linea/interruttore e non possano essere alimentate insieme a piena potenza: per
questo il controllo a burst-fire garantisce mutua esclusione (mai entrambi i relè
accesi nello stesso istante). Se l'impianto elettrico del forno cambia (es. linee
indipendenti), questo vincolo va rivisto nella funzione `updateRelayControl()` in
`src/main.cpp`.
