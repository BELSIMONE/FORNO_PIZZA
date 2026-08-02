# Forno Pizza ESP32

Firmware per smartizzare un forno da pizza a resistenze elettriche (cielo + platea),
basato su ESP32, con controllo temperatura, timer di cottura, interfaccia web,
ricette salvabili e aggiornamento firmware via OTA.

## Hardware

- **ESP32** (Dev Module)
- Display **TFT ST7735** (SPI) per temperature, timer, costo stimato e IP
- 2x termocoppia tipo K con moduli **MAX6675** (CIELO e PLATEA), sullo stesso bus SPI software,
  lette a rotazione
- 2x relè allo stato solido (SSR) che pilotano le resistenze CIELO (2400 W) e PLATEA (900 W)
- Pulsante **touch capacitivo** (pin `T9` / GPIO32) per impostare un timer di cottura
- Buzzer per segnalazioni acustiche a fine timer

### Pin utilizzati

| Funzione        | Pin  |
|-----------------|------|
| TFT MOSI        | 23   |
| TFT SCLK        | 18   |
| TFT CS          | 27   |
| TFT DC          | 5    |
| Buzzer          | 33   |
| Touch (timer)   | T9 (GPIO32) |
| Relè CIELO      | 22   |
| Relè PLATEA     | 17   |
| Termocoppie DO  | 15   |
| Termocoppie CLK | 4    |
| Termocoppia CS1 (cielo)  | 21 |
| Termocoppia CS2 (platea) | 2  |

## Funzionalità

- **Controllo temperatura a isteresi** per entrambe le zone, con soglia di sicurezza
  (spegnimento forzato oltre +20°C dal setpoint)
- **Potenza regolabile (0-100%) a PWM lento (2Hz)**, applicata in tre fasi in base
  a quanto manca al target della PLATEA (ha molta più inerzia termica del CIELO,
  quindi guida la fase e ha priorità iniziale):
  1. **Preriscaldo** (PLATEA oltre 40°C dal proprio target): PLATEA fissa al
     100%, CIELO fisso al 50%
  2. **Avvicinamento** (PLATEA entro 40°C dal proprio target, ma non ancora
     entrambe le zone a target): CIELO alla potenza impostata dall'utente,
     PLATEA fissa al 50%
  3. **Mantenimento** (entrambe le zone a target): ciascuna alla propria potenza
     impostata; se il CIELO è oltre il 70% e la PLATEA deve riaccendersi, il
     CIELO viene abbassato al 50% per contenere l'assorbimento combinato

  In ogni fase, se la potenza risultante (CIELO 2400W + PLATEA 900W) supererebbe
  il limite della linea condivisa (2800W), viene ridotta automaticamente solo
  la PLATEA, mai il CIELO.

  Le due resistenze possono essere accese contemporaneamente. Ogni relè è
  pilotato in modo indipendente con un duty cycle proprio su un periodo di
  500ms (2Hz), cosi' la spia al neon collegata resta visibilmente intermittente
  invece di sfarfallare. Potenza regolabile da `/power`.
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

Le due resistenze (CIELO 2400W, PLATEA 900W) possono essere accese
contemporaneamente, ma la linea/contatore condiviso regge al massimo
`MAX_COMBINED_POWER_W` (2800W di default): se le percentuali impostate
supererebbero il limite, il firmware riduce automaticamente solo la PLATEA,
mai il CIELO. In fase di mantenimento, se il CIELO è impostato oltre il 70% e
la PLATEA deve riaccendersi, il CIELO viene inoltre abbassato temporaneamente
al 50% per contenere l'assorbimento combinato (vedi `updateRelayControl()` in
`src/main.cpp`). Se l'impianto elettrico o le potenze delle resistenze
cambiano, questi valori vanno aggiornati di conseguenza.
