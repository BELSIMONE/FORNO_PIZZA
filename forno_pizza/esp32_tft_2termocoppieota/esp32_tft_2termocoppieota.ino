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
// Impostazioni della rete WiFi
const char *ssid = "myhome";
const char *password = "REDACTED_WIFI_PASSWORD";

// Impostazioni del display ST7735
//#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   15  // Chip select control pin
#define TFT_DC    5  // Data Command control pin
#define TFT_RST   4  // Reset pin (could connect to RST pin)
//#define TFT_RST  -1  // Set TFT_RST to -1 if display RESET is connected to ESP32 board RST
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// Pin per i relè e la termocoppia (aggiorna per ESP32)
const int rele1 = 12; 
const int rele2 = 14;
//const int ledPin = 2; // Pin del LED interno su ESP32

// Definizione dei pin per la termocoppia
int thermoDO = 32; 
int thermoCS = 33; 
int thermoCLK = 26; 
int thermoCS2 = 25; 
// Creazione degli oggetti MAX6675
MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);
MAX6675 thermocouple2(thermoCLK, thermoCS2, thermoDO);

WebServer server(80);
Preferences preferences;



// Variabili di impostazione temperatura
float tempimpostata1 = 400; // Soglia temperatura per relè 1
float tempimpostata2 = 300; // Soglia temperatura per relè 2
float deltaT1 = 1.0; // Isteresi per relè 1
float deltaT2 = 1.0; // Isteresi per relè 2
  char tempString1[10];
char tempString2[10];
char tempString3[10];
char tempString4[10];

char prevTempString1[10] = "";
char prevTempString2[10] = "";
char prevTempString3[10] = "";
char prevTempString4[10] = "";

// Resto del codice...

void saveSettings() {
    preferences.begin("my-app", false);
    preferences.putFloat("tempimpostata1", tempimpostata1);
    preferences.putFloat("tempimpostata2", tempimpostata2);
    preferences.end();
}

void loadSettings() {
    preferences.begin("my-app", true);
    tempimpostata1 = preferences.getFloat("tempimpostata1", 400); // Default 400
    tempimpostata2 = preferences.getFloat("tempimpostata2", 300); // Default 300
    preferences.end();
}

void setup() {
 Serial.begin(9600);
  tft.initR(INITR_BLACKTAB);   // Inizializza il display ST7735
  tft.setRotation(1);          // Ruota il display se necessario
  tft.fillScreen(ST7735_BLACK); // Imposta lo sfondo a nero
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


  // Configurazione WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  //digitalWrite(ledPin, HIGH);
  Serial.println(WiFi.localIP());
  loadSettings();

  // Configurazione server web
  server.on("/", HTTP_GET, handleRoot);
  server.on("/settemp", HTTP_POST, handleSetTemp);
  server.begin();

  // Configurazione pin relè
  pinMode(rele1, OUTPUT);
  pinMode(rele2, OUTPUT);
}

void loop() {
  // Gestione server web
  server.handleClient();
 ArduinoOTA.handle(); 
  // Lettura e gestione temperature
  float temperature1 = thermocouple.readCelsius();
  float temperature2 = thermocouple2.readCelsius();

  // Controllo e attivazione del relè 1 con isteresi
    if (temperature1 < tempimpostata1 - deltaT1) {
        digitalWrite(rele1, HIGH); // Accende il relè 1
    } else if (temperature1 > tempimpostata1 + deltaT1) {
        digitalWrite(rele1, LOW); // Spegne il relè 1
    }

    // Controllo e attivazione del relè 2 con isteresi
    if (temperature2 < tempimpostata2 - deltaT2) {
        digitalWrite(rele2, HIGH); // Accende il relè 2
    } else if (temperature2 > tempimpostata2 + deltaT2) {
        digitalWrite(rele2, LOW); // Spegne il relè 2
    }
  dtostrf(temperature1, 4, 0, tempString1);
  dtostrf(temperature2, 4, 0, tempString2);
  dtostrf(tempimpostata1, 4, 0, tempString3);
  dtostrf(tempimpostata2, 4, 0, tempString4);
  
   if (strcmp(tempString1, prevTempString1) != 0 || strcmp(tempString2, prevTempString2) != 0 ||
      strcmp(tempString3, prevTempString3) != 0 || strcmp(tempString4, prevTempString4) != 0) {
    // Aggiorna il display
    strcpy(prevTempString1, tempString1);
    strcpy(prevTempString2, tempString2);
    strcpy(prevTempString3, tempString3);
    strcpy(prevTempString4, tempString4);
  

updateDisplay();

  delay(1000);
}
}

void handleRoot() {
    String html = "<html><head>";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>";
    html += "</head><body>";
    html += "<h1>Impostazioni Temperatura</h1>";
    html += "<form action='/settemp' method='POST'>";
    html += "Temperatura Impostata 1: <input type='number' name='temp1' value='" + String(tempimpostata1) + "'><br>";
    html += "Temperatura Impostata 2: <input type='number' name='temp2' value='" + String(tempimpostata2) + "'><br>";
    html += "<input type='submit' value='Salva'>";
    html += "</form>";
    html += "</body></html>";
    server.send(200, "text/html", html);

}
void handleSetTemp() {
    if (server.hasArg("temp1")) tempimpostata1 = server.arg("temp1").toFloat();
    if (server.hasArg("temp2")) tempimpostata2 = server.arg("temp2").toFloat();

    // Salva le impostazioni nella EEPROM
    saveSettings();

    server.sendHeader("Location", "/");
    server.send(303);
}
void updateDisplay() {
    tft.fillScreen(ST7735_BLACK);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextSize(0.5);
    tft.setTextColor(ST7735_GREEN);

   
    tft.setCursor(10, 15);
    tft.println("SIMON'S OVEN");
    tft.setTextColor(ST7735_RED);
    tft.setCursor(0, 45);
    tft.print("CIELO : "); 
    tft.setCursor(80, 45);
    tft.println(tempString3);
    tft.setFont(); // Resetta al font di default per altre informazioni
    tft.setTextSize(1); 
    tft.setCursor(118, 30);
    tft.print("o");
    tft.setTextColor(ST7735_BLUE);
    tft.setCursor(130, 35);
    //tft.print("SET:"); 
    tft.println(tempString3);
    
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(ST7735_RED);
    tft.setCursor(0, 70);
    tft.print("PLATEA: ");
    tft.setCursor(80, 70); 
    tft.println(tempString4);
    tft.setFont(); // Resetta al font di default per altre informazioni
    tft.setTextSize(1);
    tft.setCursor(118, 55);
    tft.print("o"); 
    tft.setTextColor(ST7735_BLUE);
    tft.setCursor(130, 60);
   // tft.print("SET:"); 
    tft.println(tempString4);
    
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(ST7735_ORANGE);
    tft.setCursor(0, 95);
    tft.print("TIMER: ");
    tft.setCursor(80, 95); 
    tft.println(tempString4);
    tft.setFont(); // Resetta al font di default per altre informazioni
    tft.setTextSize(1);
    tft.setCursor(118, 88);
    tft.print("S"); 
    tft.setTextColor(ST7735_BLUE);
    tft.setCursor(130, 85);
   // tft.print("SET:"); 
    tft.println(tempString4);

    tft.setTextColor(ST7735_CYAN);
    tft.setCursor(25, 115);
    tft.print("IP: "); 
    tft.println(WiFi.localIP());
}
