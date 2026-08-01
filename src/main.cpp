#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "max6675.h"
#include <Fonts/FreeSans9pt7b.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>
#include "secrets.h"

// Impostazioni della rete WiFi
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 60000; // tempo massimo di attesa del WiFi di casa all'avvio
bool wifiApMode = false; // true quando si sta usando l'Access Point locale al posto del WiFi di casa

// Impostazioni del display ST7735
//#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   27  // Chip select control pin
#define TFT_DC    5  // Data Command control pin
//#define TFT_RST   4  // Reset pin (could connect to RST pin)
#define TFT_RST  -1  // Set TFT_RST to -1 if display RESET is connected to ESP32 board RST
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
unsigned long elapsedTime = 0;
bool startChrono = false;
std::vector<float> temperatureReadings;
std::vector<float> temperatureReadings2;
const int maxReadings = 120;
const int buzzerPin = 33 ;  //  buzzer
const int touchPin = T5;  // pulsante touch
const int rele1 = 22;
const int rele2 = 17;

// Controllo potenza tramite PWM lento a 2Hz (periodo 500ms): ogni relè viene
// pilotato in modo indipendente con un duty cycle proprio, cosi' la spia al
// neon collegata resta visibilmente intermittente. CIELO (2400W) e PLATEA
// (900W) possono essere accese insieme, ma la linea/contatore condiviso
// regge al massimo MAX_COMBINED_POWER_W: se le percentuali impostate
// supererebbero il limite, viene ridotta automaticamente solo la PLATEA
// (il CIELO non viene mai toccato dal limite di potenza).
const unsigned long PWM_PERIOD_MS = 500; // 2Hz
const float RELE1_POWER_W = 2400.0; // CIELO
const float RELE2_POWER_W = 900.0;  // PLATEA
const float MAX_COMBINED_POWER_W = 2800.0;
const float ENERGY_PRICE_EUR_KWH = 0.40;
float potenza1 = 100.0; // percentuale di potenza manuale per il relè 1 (CIELO), usata a target raggiunto
float potenza2 = 100.0; // percentuale di potenza manuale per il relè 2 (PLATEA), usata a target raggiunto
bool demand1 = false;   // true se la zona 1 ha bisogno di calore (fuori dalla banda di isteresi)
bool demand2 = false;
bool rele1Status = false; // stato corrente del relè 1 (calcolato dallo scheduler)
bool rele2Status = false;

// Definizione dei pin per la termocoppia
int thermoDO = 15;
int thermoCS = 21;
int thermoCLK = 4;
int thermoCS2 = 2;
// Creazione degli oggetti MAX6675
MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);
MAX6675 thermocouple2(thermoCLK, thermoCS2, thermoDO);

WebServer server(80);
Preferences preferences;

// Ricette salvate su LittleFS come array JSON: [{"name","temp1","temp2","potenza1","potenza2"}, ...]
const char *RECIPES_FILE = "/recipes.json";
const size_t MAX_RECIPES = 20;

unsigned long lastUpdateTime = 0;
const long updateInterval = 60000; // Intervallo di aggiornamento in millisecondi (60000 ms = 1 minuto)
unsigned long startTimeRele1 = 0;
unsigned long startTimeRele2 = 0;
unsigned long activeTimeRele1 = 0;  // Tempo totale di attivazione in secondi
unsigned long activeTimeRele2 = 0;
unsigned long rele1MillisTotal = 0;  // Minuti totali accumulati per rele1
unsigned long rele2MillisTotal = 0;  // Minuti totali accumulati per rele2
int rele1MinutesTotal = 0;  // Minuti totali accumulati per rele1
int rele2MinutesTotal = 0;  // Minuti totali accumulati per rele2
bool rele1Active = false;  // Stato del rele1
bool rele2Active = false;  // Stato del rele2
float euroTotal = 0.0;
int euroTotalDecimali = 0 ;

const int ledPin = 13;     // Pin del LED interno
unsigned long lastButtonPress = 0;
int countdown = 0;
bool countingDown = false;
bool lastButtonState = HIGH;  // Stato iniziale alto (non premuto)
const unsigned long debounceDelay = 450;  // Tempo di debounce in millisecondi

unsigned long previousMillis = 0;
const long interval = 1000;  // Intervallo di 1 secondo


unsigned long lastStateChange = 0;
// Variabili di impostazione temperatura

float temperature1 = 0.0;
float temperature2 = 0.0;
float tempimpostata1 = 400; // Soglia temperatura per relè 1
float tempimpostata2 = 300; // Soglia temperatura per relè 2
float deltaT1 = 0.5; // Isteresi per relè 1
float deltaT2 = 0.5; // Isteresi per relè 2
char tempString1[10];
char tempString2[10];
char tempString3[10];
char tempString4[10];
char timerString[10];
char rele1TimeString[10];  // Stringa per il tempo attivo di rele1
char rele2TimeString[10];  // Stringa per il tempo attivo di rele2
char euroTotalString[10] ;
char euroTotalDecimaliString[10] ;
char prevTempString1[10] = "";
char prevTempString2[10] = "";
char prevTempString3[10] = "";
char prevTempString4[10] = "";
char prevrele1TimeString[10] = "";
char prevrele2TimeString[10] = "";
char prevTimerString[10] = "";
char preveuroTotalString[10] = "";

char preveuroTotalDecimaliString[10] = "";
uint16_t cieloColor =  ST7735_WHITE ;
uint16_t plateaColor =  ST7735_WHITE ;

const int threshold = 40; // Valore soglia per il touch
const int debounceTime = 200;
unsigned long lastTouchTime = 0; // Tempo dell'ultimo tocco rilevato
bool inDebounce = false;         // Indica se ci troviamo nel periodo di debounce
unsigned long currentMillis = 0;

unsigned long lastReadTime = 0;
const unsigned long readInterval = 2000; // 2000 millisecondi = 2 secondi
bool readFirstThermocouple = true;
// Variabili per la logica del pulsante
unsigned long buttonPressTime = 0;
unsigned long countdownTimer = 0; // 30 secondi
bool countdownStarted = false;

int melodies[][8] = {
  {1975, 1975, 2093, 2349, 2349, 2093, 1975, 1760}, // Melodia 1 tetris
  {1396, 1318, 1174, 1318, 1396, 1568, 1760, 1976}, // Melodia 2 pacman
  {1046, 1174, 1318, 1396, 1568, 1760, 2093, 2349},  // Melodia 3 (tre ottave più alte) - "Melodia 3"
  {523, 587, 659, 698, 784, 880, 988, 1047},         // Melodia 4 (due ottave più alte) - "Melodia 4"
  {1047, 988, 880, 784, 698, 659, 587, 523},         // Melodia 5 (due ottave più alte) - "Melodia 5"
  {523, 659, 784, 523, 659, 784, 523, 659},          // Melodia 6 (una ottava più alta) - "Melodia 6"
  {1047, 1175, 1319, 1397, 1568, 1760, 1976, 2093}, // Melodia 7 (tre ottave più alte) - "Melodia 7"
  {2093, 1976, 1760, 1568, 1397, 1319, 1175, 1047}  // Melodia 8 (tre ottave più alte) - "Melodia 8"
};

// CSS condiviso da tutte le pagine web (dark theme, leggibile da smartphone in cucina)
const char COMMON_CSS[] PROGMEM =
    "body{font-family:Arial,sans-serif;background:#1b1b1b;color:#eee;margin:0;padding:0 12px 24px}"
    "nav{background:#222;padding:10px 12px;margin:0 -12px 16px;display:flex;flex-wrap:wrap;gap:8px}"
    "nav a{color:#fff;text-decoration:none;padding:8px 12px;background:#333;border-radius:6px;font-size:14px}"
    "nav a:hover{background:#c0392b}"
    "h1{font-size:22px}"
    "label{display:block;margin-top:14px;font-size:15px}"
    "input[type=number],input[type=text]{font-size:16px;padding:8px;width:100%;max-width:220px;box-sizing:border-box;border-radius:6px;border:1px solid #444;background:#222;color:#eee;margin-top:4px}"
    "input[type=range]{width:100%;max-width:320px}"
    "button,input[type=submit]{font-size:16px;padding:10px 18px;margin-top:14px;margin-right:8px;border:none;border-radius:6px;background:#c0392b;color:#fff;cursor:pointer}"
    "button:hover,input[type=submit]:hover{background:#e74c3c}"
    ".card{background:#242424;border-radius:10px;padding:16px;margin-bottom:14px;max-width:480px}"
    ".big{font-size:34px;font-weight:bold}"
    ".status{font-size:13px;opacity:.7}"
    "table{border-collapse:collapse;width:100%;max-width:560px}"
    "td,th{padding:8px;border-bottom:1px solid #333;text-align:left;font-size:14px}";

void saveSettings();
void loadSettings();
void handleRoot();
void monitorRoot();
void handleMonitor();
void handleTemperatureChart();
void handleTemperatureData();
void handleSetTemp();
void handlePowerPage();
void handleSetPower();
void handleRecipesPage();
void handleRecipesList();
void handleRecipeSave();
void handleRecipeLoad();
void handleRecipeDelete();
void handleStatus();
void updateDisplay();
void updateRelayControl(unsigned long now);
void playBuzzer(int frequency, int duration);
void onWiFiEvent(WiFiEvent_t event);
void startAccessPoint();
String pageHeader(const String &title);
String pageFooter();

void saveSettings() {
    preferences.begin("my-app", false);
    preferences.putFloat("tempimpostata1", tempimpostata1);
    preferences.putFloat("tempimpostata2", tempimpostata2);
    preferences.putFloat("potenza1", potenza1);
    preferences.putFloat("potenza2", potenza2);
    preferences.end();
}

void loadSettings() {
    preferences.begin("my-app", true);
    tempimpostata1 = preferences.getFloat("tempimpostata1", 400); // Default 400
    tempimpostata2 = preferences.getFloat("tempimpostata2", 300); // Default 300
    potenza1 = preferences.getFloat("potenza1", 100);
    potenza2 = preferences.getFloat("potenza2", 100);
    preferences.end();
}

// Avvia l'Access Point locale mantenendo attivo anche il client WiFi (WIFI_AP_STA),
// cosi' il forno resta raggiungibile via 192.168.4.1 e intanto continua a tentare
// la riconnessione al WiFi di casa in background.
void startAccessPoint() {
    wifiApMode = true;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.print("Access Point attivo, IP: ");
    Serial.println(WiFi.softAPIP());
}

// Gestisce in modo non bloccante i cambi di stato della connessione WiFi durante
// il funzionamento: se la rete di casa cade riattiva l'AP, se torna disponibile
// lo disattiva. Non interferisce mai con lettura temperature/gestione relè.
void onWiFiEvent(WiFiEvent_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        if (wifiApMode) {
            Serial.println("WiFi di casa ripristinato: disattivo l'Access Point");
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            wifiApMode = false;
        }
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        if (!wifiApMode) {
            Serial.println("WiFi di casa perso: avvio Access Point locale");
            startAccessPoint();
        }
    }
}

void setup() {
 Serial.begin(9600);
  tft.initR(INITR_BLACKTAB);   // Inizializza il display ST7735
  tft.setRotation(0);          // Ruota il display se necessario
  tft.fillScreen(ST7735_BLACK); // Imposta lo sfondo a nero

  pinMode(buzzerPin, OUTPUT);

  // Filesystem per le ricette salvate (formatta automaticamente se assente/corrotto)
  if (!LittleFS.begin(true)) {
    Serial.println("Errore inizializzazione LittleFS");
  }

  // Carica subito le ultime impostazioni salvate: il forno deve poter
  // funzionare (relè, display, timer) anche se il WiFi non arriva mai.
  loadSettings();

  // Configurazione WiFi: prova a connettersi al WiFi di casa per un tempo
  // massimo di WIFI_CONNECT_TIMEOUT_MS. Se non ci riesce, avvia un Access
  // Point locale cosi' il forno resta comunque utilizzabile da web/OTA.
  // In seguito, se la connessione cade o torna disponibile, onWiFiEvent()
  // gestisce automaticamente il passaggio AP<->STA senza bloccare il loop.
  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("Connessione al WiFi di casa...");
  unsigned long wifiStartAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartAttempt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnesso al WiFi di casa");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi di casa non raggiunto entro il timeout: avvio Access Point locale");
    startAccessPoint();
  }
  WiFi.setAutoReconnect(true); // riconnessione automatica in background se la rete cade

  if (MDNS.begin("mio-esp32")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS attivo: http://mio-esp32.local");
  }

  // Configurazione server web
  server.on("/", HTTP_GET, monitorRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/settemp", HTTP_POST, handleSetTemp);
  server.on("/setting", HTTP_GET, handleRoot);
  server.on("/power", HTTP_GET, handlePowerPage);
  server.on("/setpower", HTTP_POST, handleSetPower);
  server.on("/recipes", HTTP_GET, handleRecipesPage);
  server.on("/api/recipes", HTTP_GET, handleRecipesList);
  server.on("/api/recipes", HTTP_POST, handleRecipeSave);
  server.on("/api/recipes/load", HTTP_POST, handleRecipeLoad);
  server.on("/api/recipes/delete", HTTP_POST, handleRecipeDelete);
  server.on("/temperature", HTTP_GET, handleMonitor);
  server.on("/temperature-chart", HTTP_GET, handleTemperatureChart);
  server.on("/temperature-data", HTTP_GET, handleTemperatureData);
  server.begin();

  // Configurazione pin relè
  pinMode(rele1, OUTPUT);
  pinMode(rele2, OUTPUT);
ArduinoOTA.setHostname("mio-esp32");  // Imposta un nome host univoco

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }
    // NOTA: se stai aggiornando SPIFFS questo sarebbe un buon momento per eseguire un SPIFFS.end()
    Serial.println("Inizia OTA: " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nFine");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progresso: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Errore[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Autenticazione fallita");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Inizio fallito");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connessione fallita");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Ricezione fallita");
    } else if (error == OTA_END_ERROR) {
      Serial.println("Fine fallita");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Pronto per OTA");

}

void loop() {
    // Gestione server web e OTA
    server.handleClient();
    ArduinoOTA.handle();

    unsigned long currentMillis = millis();

    // Lettura e gestione temperature
    if (currentMillis - lastReadTime >= readInterval) {
        if (readFirstThermocouple) {
            temperature1 = thermocouple.readCelsius();
        } else {
            temperature2 = thermocouple2.readCelsius();
        }

        readFirstThermocouple = !readFirstThermocouple;
        lastReadTime = currentMillis;
    }

    if (currentMillis - lastUpdateTime >= updateInterval) {
        temperatureReadings.push_back(temperature1);
        temperatureReadings2.push_back(temperature2);
        lastUpdateTime = currentMillis;

        Serial.print("Temperature1: ");
        Serial.println(temperature1);
        Serial.print("Temperature2: ");
        Serial.println(temperature2);
    }

    if (temperatureReadings.size() > maxReadings) {
        temperatureReadings.erase(temperatureReadings.begin());
        temperatureReadings2.erase(temperatureReadings2.begin());
    }

    // Controllo relè: isteresi per decidere se serve calore + burst-fire lento
    // per erogare la potenza impostata senza mai accendere insieme le due resistenze
    updateRelayControl(currentMillis);

    // Conversione delle temperature in stringhe
    dtostrf(temperature1, 4, 0, tempString1);
    dtostrf(temperature2, 4, 0, tempString2);
    dtostrf(tempimpostata1, 4, 0, tempString3);
    dtostrf(tempimpostata2, 4, 0, tempString4);
    dtostrf(countdown, 4, 0, timerString);
    dtostrf(rele1MinutesTotal, 4, 0, rele1TimeString);
    dtostrf(rele2MinutesTotal, 4, 0, rele2TimeString);
    dtostrf(euroTotal, 4, 2, euroTotalString);

    // Aggiornamento display se le temperature o il timer sono cambiati
    if (strcmp(tempString1, prevTempString1) != 0 || strcmp(tempString2, prevTempString2) != 0 ||
        strcmp(tempString3, prevTempString3) != 0 || strcmp(tempString4, prevTempString4) != 0 ||
        strcmp(rele1TimeString, prevrele1TimeString) != 0 || strcmp(rele2TimeString, prevrele2TimeString) != 0 ||
        strcmp(euroTotalString, preveuroTotalString) != 0 || strcmp(timerString, prevTimerString) != 0) {

        updateDisplay();

        strcpy(prevTempString1, tempString1);
        strcpy(prevTempString2, tempString2);
        strcpy(prevTempString3, tempString3);
        strcpy(prevTempString4, tempString4);
        strcpy(prevrele1TimeString, rele1TimeString);
        strcpy(prevrele2TimeString, rele2TimeString);
        strcpy(preveuroTotalString, euroTotalString);
        strcpy(prevTimerString, timerString);
    }

    // Gestione del pulsante touch
    if (touchRead(touchPin) < threshold && currentMillis - lastButtonPress >= debounceDelay) {
        lastButtonPress = currentMillis;
        countdown += 60;
        Serial.print("+60sec ");
        Serial.println(countdown);
    }

    if (countdown > 660) {
        countdown = 0;
        countingDown = false;
        Serial.println(countdown);
    }

    if (!countingDown && touchRead(touchPin) > threshold && countdown > 0 && currentMillis - lastButtonPress >= 1000) {
        Serial.println("Aspettato 3 secondi senza premere il pulsante.");
        countingDown = true;
    }

    // Gestione del conto alla rovescia
    if (countingDown) {
        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;
            if (countdown > 0) {
                Serial.println(countdown);
                countdown--;
            } else {
                int randomMelody = random(0, 8);
                for (int i = 0; i < 8; i++) {
                    playBuzzer(melodies[randomMelody][i], 200);
                }

             Serial.println("Fine del conto alla rovescia. Inizio conteggio tempo.");
            countingDown = false;
            startChrono = true;  // Inizia il cronometro
            elapsedTime = 0;     // Reset del tempo trascorso
            previousMillis = currentMillis; // Reset tempo
            }
        }
    }
// Gestione del cronometro (conteggio del tempo trascorso)
if (startChrono) {
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        elapsedTime++;  // Incrementa il tempo trascorso
        dtostrf(elapsedTime, 4, 0, timerString);  // Aggiorna timerString con il tempo trascorso
        //updateDisplay();  // Aggiorna il display con il nuovo valore di timerString
        Serial.print("Tempo trascorso: ");
        Serial.println(elapsedTime);
    }
}
   if (digitalRead(rele1) == HIGH) {  // Rele1 acceso
        if (!rele1Active) {
            rele1Active = true;
            startTimeRele1 = millis();  // Registra il momento in cui viene acceso
        }else {if (millis() - startTimeRele1 >= 30000) {
        rele1Active = false;
        rele1MillisTotal += (millis() - startTimeRele1) ;  // Aggiorna il tempo Somma i minuti totali
        rele1MinutesTotal = rele1MillisTotal / 60000;  // Aggiorna il tempo Somma i minuti totali
    }
    }
    } else {

      if (rele1Active ) {
        rele1Active = false;
        rele1MillisTotal += (millis() - startTimeRele1) ;  // Aggiorna il tempo Somma i minuti totali
        rele1MinutesTotal = rele1MillisTotal / 60000;  // Aggiorna il tempo Somma i minuti totali
    }



}

   if (digitalRead(rele2) == HIGH) {  // Rele2 acceso
        if (!rele2Active) {
            rele2Active = true;
            startTimeRele2 = millis();  // Registra il momento in cui viene acceso
        }else {if (millis() - startTimeRele2 >= 30000) {
        rele2Active = false;
        rele2MillisTotal += (millis() - startTimeRele2) ;  // Aggiorna il tempo Somma i minuti totali
        rele2MinutesTotal = rele2MillisTotal / 60000;  // Aggiorna il tempo Somma i minuti totali
    }
    }
    } else {

      if (rele2Active ) {
        rele2Active = false;
        rele2MillisTotal += (millis() - startTimeRele2) ;  // Aggiorna il tempo Somma i minuti totali
        rele2MinutesTotal = rele2MillisTotal / 60000;  // Aggiorna il tempo Somma i minuti totali
    }



}
euroTotal = ((rele2MinutesTotal / 60.0) * (RELE2_POWER_W / 1000.0) + (rele1MinutesTotal / 60.0) * (RELE1_POWER_W / 1000.0)) * ENERGY_PRICE_EUR_KWH;
euroTotalDecimali = (int)((euroTotal - (int)euroTotal) * 100);

}

// Decide se le due zone hanno bisogno di calore (isteresi di sicurezza) e calcola
// la potenza (%) da erogare a ciascuna in base alla fase di cottura. La PLATEA,
// avendo molta più inerzia termica, guida la fase (non il CIELO):
//  1) PLATEA lontana dal proprio target (>40 gradi dal setpoint PLATEA):
//     preriscaldo a spinta massima, CIELO 50% fisso e PLATEA 100% fisso;
//  2) avvicinamento (PLATEA entro 40 gradi dal proprio target, ma non ancora
//     entrambe a target): CIELO alla potenza impostata dall'utente, PLATEA 50% fisso;
//  3) mantenimento (entrambe le zone a target): ciascuna alla propria potenza
//     impostata, tranne che se il CIELO e' oltre il 70% e la PLATEA deve
//     riaccendersi, nel qual caso il CIELO viene abbassato al 50% per
//     contenere l'assorbimento combinato.
// In ogni fase, se la potenza risultante supererebbe MAX_COMBINED_POWER_W
// (limite della linea/contatore condiviso), viene ridotta solo la PLATEA.
// Ogni relè viene poi pilotato in modo indipendente (possono stare accesi
// insieme) con un PWM lento a 2Hz (PWM_PERIOD_MS), cosi' le spie al neon
// restano visibilmente intermittenti.
void updateRelayControl(unsigned long now) {
    if (temperature1 <= tempimpostata1 - deltaT1 || countdown > 0) demand1 = true;
    if (temperature1 > tempimpostata1 + deltaT1) demand1 = false;
    if (temperature1 > tempimpostata1 + 20) demand1 = false; // sicurezza: mai oltre +20 gradi dal setpoint

    if (temperature2 <= tempimpostata2 - deltaT2) demand2 = true;
    if (temperature2 > tempimpostata2 + deltaT2) demand2 = false;

    // La PLATEA ha molta più inerzia termica del CIELO: all'avvio ha priorità
    // (100% di potenza) finché non si avvicina al proprio target, mentre il
    // CIELO (che scalda molto più in fretta) resta al minimo indispensabile (50%).
    float dist2 = tempimpostata2 - temperature2;
    bool bothReached = (temperature1 >= tempimpostata1) && (temperature2 >= tempimpostata2);

    float targetPower1, targetPower2;
    if (dist2 > 40) {
        targetPower1 = 50;
        targetPower2 = 100;
    } else if (!bothReached) {
        targetPower1 = constrain(potenza1, 0.0f, 100.0f);
        targetPower2 = 50;
    } else {
        targetPower1 = constrain(potenza1, 0.0f, 100.0f);
        targetPower2 = constrain(potenza2, 0.0f, 100.0f);
        if (targetPower1 > 70 && demand2) {
            targetPower1 = 50;
        }
    }

    // Limite di potenza combinata (linea/contatore condiviso): se le percentuali
    // risultanti superano MAX_COMBINED_POWER_W, si riduce solo la PLATEA, mai il CIELO.
    float watts1 = RELE1_POWER_W * (targetPower1 / 100.0f);
    float watts2 = RELE2_POWER_W * (targetPower2 / 100.0f);
    if (watts1 + watts2 > MAX_COMBINED_POWER_W) {
        float maxWatts2 = MAX_COMBINED_POWER_W - watts1;
        if (maxWatts2 < 0) maxWatts2 = 0;
        float cappedPower2 = (maxWatts2 / RELE2_POWER_W) * 100.0f;
        if (cappedPower2 < targetPower2) targetPower2 = cappedPower2;
    }

    unsigned long phase = now % PWM_PERIOD_MS;
    unsigned long onTime1 = (unsigned long)(PWM_PERIOD_MS * (targetPower1 / 100.0f));
    unsigned long onTime2 = (unsigned long)(PWM_PERIOD_MS * (targetPower2 / 100.0f));

    rele1Status = demand1 && (phase < onTime1);
    rele2Status = demand2 && (phase < onTime2);

    digitalWrite(rele1, rele1Status ? HIGH : LOW);
    digitalWrite(rele2, rele2Status ? HIGH : LOW);

    cieloColor = !demand1 ? ST7735_WHITE : (rele1Status ? ST7735_RED : ST7735_MAGENTA);
    plateaColor = !demand2 ? ST7735_WHITE : (rele2Status ? ST7735_RED : ST7735_MAGENTA);
}

String pageHeader(const String &title) {
    String html;
    html.reserve(600);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>" + title + " - Forno Pizza</title>";
    html += "<style>" + String(COMMON_CSS) + "</style>";
    html += "</head><body>";
    html += "<nav>";
    html += "<a href='/'>Monitor</a>";
    html += "<a href='/setting'>Temperature</a>";
    html += "<a href='/power'>Potenza</a>";
    html += "<a href='/recipes'>Ricette</a>";
    html += "<a href='/temperature-chart'>Grafico</a>";
    html += "</nav>";
    html += "<h1>" + title + "</h1>";
    return html;
}

String pageFooter() {
    return "</body></html>";
}

void handleRoot() {
    String html = pageHeader("Temperature");
    html += "<form class='card' action='/settemp' method='POST'>";
    html += "<label>Setpoint CIELO (&deg;C)<input type='number' step='1' name='temp1' value='" + String(tempimpostata1, 0) + "'></label>";
    html += "<label>Setpoint PLATEA (&deg;C)<input type='number' step='1' name='temp2' value='" + String(tempimpostata2, 0) + "'></label>";
    html += "<input type='submit' value='Salva'>";
    html += "</form>";
    html += pageFooter();
    server.send(200, "text/html", html);
}

void monitorRoot() {
    String html = pageHeader("Monitor");
    html += "<div class='card'><div>CIELO</div><div class='big' id='t1'>--</div>";
    html += "<div class='status'>Setpoint <span id='s1'>--</span>&deg;C &middot; Potenza <span id='p1'>--</span>% &middot; <span id='r1'>-</span></div></div>";
    html += "<div class='card'><div>PLATEA</div><div class='big' id='t2'>--</div>";
    html += "<div class='status'>Setpoint <span id='s2'>--</span>&deg;C &middot; Potenza <span id='p2'>--</span>% &middot; <span id='r2'>-</span></div></div>";
    html += "<div class='card'><div class='status'>Costo stimato</div><div class='big'>&euro; <span id='euro'>--</span></div>";
    html += "<div class='status' id='wifiInfo'>--</div></div>";
    html += "<script>";
    html += "function refresh(){fetch('/status').then(r=>r.json()).then(d=>{";
    html += "t1.textContent=Math.round(d.t1)+'°C';t2.textContent=Math.round(d.t2)+'°C';";
    html += "s1.textContent=Math.round(d.s1);s2.textContent=Math.round(d.s2);";
    html += "p1.textContent=Math.round(d.p1);p2.textContent=Math.round(d.p2);";
    html += "r1.textContent=d.r1?'ACCESO':'spento';r2.textContent=d.r2?'ACCESO':'spento';";
    html += "euro.textContent=d.euro.toFixed(2);";
    html += "wifiInfo.textContent='WiFi: '+d.wifi+' ('+d.ip+')';";
    html += "});}";
    html += "refresh();setInterval(refresh,3000);";
    html += "</script>";
    html += pageFooter();
    server.send(200, "text/html", html);
}

void handleStatus() {
    String json;
    json.reserve(320);
    json += "{";
    json += "\"t1\":" + String(temperature1, 1) + ",";
    json += "\"t2\":" + String(temperature2, 1) + ",";
    json += "\"s1\":" + String(tempimpostata1, 0) + ",";
    json += "\"s2\":" + String(tempimpostata2, 0) + ",";
    json += "\"p1\":" + String(potenza1, 0) + ",";
    json += "\"p2\":" + String(potenza2, 0) + ",";
    json += "\"r1\":" + String(rele1Status ? "true" : "false") + ",";
    json += "\"r2\":" + String(rele2Status ? "true" : "false") + ",";
    json += "\"euro\":" + String(euroTotal, 2) + ",";
    json += "\"timer\":" + String(countdown) + ",";
    json += "\"chrono\":" + String(startChrono ? "true" : "false") + ",";
    json += "\"elapsed\":" + String(elapsedTime) + ",";
    json += "\"wifi\":\"" + String(wifiApMode ? "AP" : "STA") + "\",";
    json += "\"ip\":\"" + String(wifiApMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\"";
    json += "}";
    server.send(200, "application/json", json);
}

void handleMonitor() {
  float temperature1 = thermocouple.readCelsius();
  delay(500);
  float temperature2 = thermocouple2.readCelsius();
    String json = "{\"temperature1\": " + String(temperature1) + ", \"temperature2\": " + String(temperature2) + "}";
    server.send(200, "application/json", json);
}

void handlePowerPage() {
    String html = pageHeader("Potenza resistenze");
    html += "<p class='status'>Potenza applicata quando entrambe le zone hanno raggiunto il target (PWM a 2Hz, la spia al neon segue il duty cycle). Durante il preriscaldo e l'avvicinamento al target la potenza è gestita automaticamente: PLATEA 100% / CIELO 50% finché la PLATEA è a più di 40&deg; dal target (ha più inerzia termica), poi CIELO alla % qui impostata / PLATEA 50% fino al raggiungimento di entrambe. Se il CIELO è oltre il 70%, quando la PLATEA si riaccende il CIELO viene abbassato al 50%. La linea condivisa regge al massimo 2800W: se le percentuali impostate la supererebbero, viene ridotta automaticamente solo la PLATEA.</p>";
    html += "<form class='card' action='/setpower' method='POST'>";
    html += "<label>Potenza CIELO: <span id='v1'>" + String(potenza1, 0) + "</span>%";
    html += "<input type='range' min='0' max='100' name='potenza1' value='" + String(potenza1, 0) + "' oninput=\"v1.textContent=this.value\"></label>";
    html += "<label>Potenza PLATEA: <span id='v2'>" + String(potenza2, 0) + "</span>%";
    html += "<input type='range' min='0' max='100' name='potenza2' value='" + String(potenza2, 0) + "' oninput=\"v2.textContent=this.value\"></label>";
    html += "<input type='submit' value='Applica'>";
    html += "</form>";
    html += pageFooter();
    server.send(200, "text/html", html);
}

void handleSetPower() {
    if (server.hasArg("potenza1")) potenza1 = constrain(server.arg("potenza1").toFloat(), 0.0f, 100.0f);
    if (server.hasArg("potenza2")) potenza2 = constrain(server.arg("potenza2").toFloat(), 0.0f, 100.0f);
    saveSettings();
    server.sendHeader("Location", "/power");
    server.send(303);
}

void handleRecipesPage() {
    String html = pageHeader("Ricette");
    html += "<div class='card'>";
    html += "<label>Nome ricetta<input type='text' id='recipeName' placeholder='Es. Margherita'></label>";
    html += "<button onclick='saveRecipe()'>Salva impostazioni attuali come ricetta</button>";
    html += "</div>";
    html += "<table id='recipeTable'><thead><tr><th>Nome</th><th>Cielo</th><th>Platea</th><th>Potenza</th><th></th></tr></thead><tbody></tbody></table>";
    html += "<script>";
    html += "function loadList(){fetch('/api/recipes').then(r=>r.json()).then(list=>{";
    html += "const tb=document.querySelector('#recipeTable tbody');tb.innerHTML='';";
    html += "list.forEach(rc=>{const tr=document.createElement('tr');";
    html += "tr.innerHTML=`<td>${rc.name}</td><td>${rc.temp1}&deg;</td><td>${rc.temp2}&deg;</td><td>${rc.potenza1}/${rc.potenza2}%</td>`+";
    html += "`<td><button onclick=\"loadRecipe('${rc.name}')\">Carica</button> <button onclick=\"delRecipe('${rc.name}')\">Elimina</button></td>`;";
    html += "tb.appendChild(tr);});});}";
    html += "function saveRecipe(){const name=recipeName.value.trim();if(!name)return;";
    html += "fetch('/status').then(r=>r.json()).then(s=>fetch('/api/recipes',{method:'POST',headers:{'Content-Type':'application/json'},";
    html += "body:JSON.stringify({name:name,temp1:s.s1,temp2:s.s2,potenza1:s.p1,potenza2:s.p2})})).then(loadList);}";
    html += "function loadRecipe(name){fetch('/api/recipes/load',{method:'POST',headers:{'Content-Type':'application/json'},";
    html += "body:JSON.stringify({name:name})}).then(()=>alert('Ricetta caricata: '+name));}";
    html += "function delRecipe(name){if(!confirm('Eliminare '+name+'?'))return;";
    html += "fetch('/api/recipes/delete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:name})}).then(loadList);}";
    html += "loadList();";
    html += "</script>";
    html += pageFooter();
    server.send(200, "text/html", html);
}

void handleRecipesList() {
    File f = LittleFS.open(RECIPES_FILE, "r");
    if (!f) { server.send(200, "application/json", "[]"); return; }
    String content = f.readString();
    f.close();
    if (content.length() == 0) content = "[]";
    server.send(200, "application/json", content);
}

void handleRecipeSave() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"body mancante\"}"); return; }
    JsonDocument body;
    if (deserializeJson(body, server.arg("plain"))) {
        server.send(400, "application/json", "{\"error\":\"json non valido\"}");
        return;
    }
    String name = String((const char *)(body["name"] | ""));
    name.trim();
    if (name.length() == 0) { server.send(400, "application/json", "{\"error\":\"nome mancante\"}"); return; }

    JsonDocument doc;
    File f = LittleFS.open(RECIPES_FILE, "r");
    if (f) { deserializeJson(doc, f); f.close(); }
    if (!doc.is<JsonArray>()) doc.to<JsonArray>();
    JsonArray arr = doc.as<JsonArray>();

    JsonObject target;
    for (JsonObject r : arr) {
        if (String((const char *)(r["name"] | "")) == name) { target = r; break; }
    }
    if (target.isNull()) {
        if (arr.size() >= MAX_RECIPES) { server.send(400, "application/json", "{\"error\":\"troppe ricette salvate\"}"); return; }
        target = arr.add<JsonObject>();
    }
    target["name"] = name;
    target["temp1"] = (float)(body["temp1"] | tempimpostata1);
    target["temp2"] = (float)(body["temp2"] | tempimpostata2);
    target["potenza1"] = (float)(body["potenza1"] | potenza1);
    target["potenza2"] = (float)(body["potenza2"] | potenza2);

    File out = LittleFS.open(RECIPES_FILE, "w");
    serializeJson(doc, out);
    out.close();
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleRecipeLoad() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"body mancante\"}"); return; }
    JsonDocument body;
    if (deserializeJson(body, server.arg("plain"))) {
        server.send(400, "application/json", "{\"error\":\"json non valido\"}");
        return;
    }
    String name = String((const char *)(body["name"] | ""));

    JsonDocument doc;
    File f = LittleFS.open(RECIPES_FILE, "r");
    if (f) { deserializeJson(doc, f); f.close(); }

    for (JsonObject r : doc.as<JsonArray>()) {
        if (String((const char *)(r["name"] | "")) == name) {
            tempimpostata1 = r["temp1"] | tempimpostata1;
            tempimpostata2 = r["temp2"] | tempimpostata2;
            potenza1 = r["potenza1"] | potenza1;
            potenza2 = r["potenza2"] | potenza2;
            saveSettings();
            server.send(200, "application/json", "{\"ok\":true}");
            return;
        }
    }
    server.send(404, "application/json", "{\"error\":\"ricetta non trovata\"}");
}

void handleRecipeDelete() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"body mancante\"}"); return; }
    JsonDocument body;
    if (deserializeJson(body, server.arg("plain"))) {
        server.send(400, "application/json", "{\"error\":\"json non valido\"}");
        return;
    }
    String name = String((const char *)(body["name"] | ""));

    JsonDocument doc;
    File f = LittleFS.open(RECIPES_FILE, "r");
    if (f) { deserializeJson(doc, f); f.close(); }
    if (!doc.is<JsonArray>()) doc.to<JsonArray>();

    JsonDocument filtered;
    JsonArray outArr = filtered.to<JsonArray>();
    for (JsonObject r : doc.as<JsonArray>()) {
        if (String((const char *)(r["name"] | "")) != name) outArr.add(r);
    }

    File out = LittleFS.open(RECIPES_FILE, "w");
    serializeJson(filtered, out);
    out.close();
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleTemperatureChart() {
    String html = pageHeader("Grafico Temperature");
    html += "<canvas id='c' style='width:100%;max-width:640px;height:300px;background:#111;border-radius:8px'></canvas>";
    html += "<script>";
    html += "const cv=document.getElementById('c');function resize(){cv.width=cv.clientWidth;cv.height=cv.clientHeight;}window.addEventListener('resize',resize);resize();";
    html += "function draw(d){const ctx=cv.getContext('2d');const w=cv.width,h=cv.height;ctx.clearRect(0,0,w,h);";
    html += "const max=500,pad=30;";
    html += "ctx.strokeStyle='#444';ctx.beginPath();ctx.moveTo(pad,0);ctx.lineTo(pad,h-pad);ctx.lineTo(w,h-pad);ctx.stroke();";
    html += "ctx.fillStyle='#888';ctx.font='11px sans-serif';";
    html += "for(let g=0;g<=max;g+=100){const y=h-pad-(g/max)*(h-pad);ctx.fillText(g,2,y+4);ctx.strokeStyle='#222';ctx.beginPath();ctx.moveTo(pad,y);ctx.lineTo(w,y);ctx.stroke();}";
    html += "function line(arr,color){if(arr.length<2)return;ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();";
    html += "arr.forEach((v,i)=>{const x=pad+(i/(arr.length-1))*(w-pad);const y=h-pad-(Math.min(v,max)/max)*(h-pad);i?ctx.lineTo(x,y):ctx.moveTo(x,y);});ctx.stroke();}";
    html += "line(d.temperatures1,'#e74c3c');line(d.temperatures2,'#3498db');";
    html += "ctx.fillStyle='#e74c3c';ctx.fillText('CIELO',w-70,14);ctx.fillStyle='#3498db';ctx.fillText('PLATEA',w-70,28);}";
    html += "function update(){fetch('/temperature-data').then(r=>r.json()).then(draw);}";
    html += "update();setInterval(update,10000);";
    html += "</script>";
    html += "<p><a href='/'><button>Torna al Monitor</button></a></p>";
    html += pageFooter();
    server.send(200, "text/html", html);
}


void handleTemperatureData() {
    String json = "{\"labels\": [";
    String temperatures1 = "\"temperatures1\": [";
    String temperatures2 = "\"temperatures2\": [";
    for (size_t i = 0; i < temperatureReadings.size(); ++i) {
        if (i > 0) {
            json += ",";
            temperatures1 += ",";
            temperatures2 += ",";
        }
        json += "\"" + String(i) + "\"";
        temperatures1 += String(temperatureReadings[i]);
        temperatures2 += String(temperatureReadings2[i]);
    }
    json += "], " + temperatures1 + "], " + temperatures2 + "]}";

    server.send(200, "application/json", json);
}


void handleSetTemp() {
    if (server.hasArg("temp1")) tempimpostata1 = server.arg("temp1").toFloat();
    if (server.hasArg("temp2")) tempimpostata2 = server.arg("temp2").toFloat();

    // Salva le impostazioni nella EEPROM
    saveSettings();

    server.sendHeader("Location", "/setting");
    server.send(303);
}
void updateDisplay() {
    tft.fillScreen(ST7735_BLACK);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextSize(0.5);
    tft.setTextColor(ST7735_YELLOW);
    tft.setCursor(12, 15);
    tft.println("PIZZERIA");
    tft.setCursor(12, 35);
    tft.println("da SIMONE");
      //  uint16_t cieloColor = digitalRead(rele1) == LOW ? ST7735_WHITE :
            //           (rele2Priority = true ? ST7735_ORANGE : ST7735_RED);
    tft.setTextColor(cieloColor);
    tft.setCursor(0, 65);
    tft.print("CIELO : ");
    tft.setCursor(80, 65);
    tft.println(tempString1);
    tft.setFont(); // Resetta al font di default per altre informazioni
    tft.setTextSize(1);
    tft.setCursor(118, 50);
    tft.print("o");
    tft.setTextColor(ST7735_BLUE);
    tft.setCursor(85, 73);
    //tft.print("SET:");
    tft.println(tempString3);
    tft.setFont(&FreeSans9pt7b);
    //uint16_t plateaColor = digitalRead(rele2) == LOW ? ST7735_WHITE :
    //                   (rele1Priority = true ? ST7735_MAGENTA : ST7735_RED);
    tft.setTextColor(plateaColor);
    tft.setCursor(0, 100);
    tft.print("PLATEA: ");
    tft.setCursor(80, 100);
    tft.println(tempString2);
    tft.setFont(); // Resetta al font di default per altre informazioni
    tft.setTextSize(1);
    tft.setCursor(118, 85);
    tft.print("o");
    tft.setTextColor(ST7735_BLUE);
    tft.setCursor(85, 108);
   // tft.print("SET:");
    tft.println(tempString4);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(ST7735_GREEN);
    tft.setCursor(0, 135);
    tft.print("TIMER: ");
    tft.setCursor(80, 135);
    tft.println(timerString);
    tft.setFont(); // Resetta al font di default per altre informazioni
    tft.setTextSize(1);
    tft.setCursor(118, 128);
    tft.print("S");
    //tft.setTextColor(ST7735_BLUE);
    //tft.setCursor(130, 85);
   // tft.print("SET:");
   // tft.println(timerString);
    tft.setTextColor(ST7735_CYAN);
    tft.setCursor(5, 140);
    //tft.print("Rele1");
    tft.print(rele1TimeString);
    tft.print(rele2TimeString);
    //tft.setTextColor(ST7735_CYAN);
    tft.setCursor(65, 140);
    tft.print("EURO: ");
    tft.print(euroTotalString);

    tft.setTextColor(ST7735_CYAN);
    tft.setCursor(5, 150);
    tft.print(wifiApMode ? "AP: " : "IP: ");
    tft.println(wifiApMode ? WiFi.softAPIP() : WiFi.localIP());
}
void playBuzzer(int frequency, int duration) {
  // Suona per il 90% della durata cosi' le note restano distinguibili invece
  // di suonare legate; il 10% restante e' un breve silenzio tra una nota e l'altra.
  tone(buzzerPin, frequency, (duration * 9) / 10);
  delay(duration);
  noTone(buzzerPin);
}
