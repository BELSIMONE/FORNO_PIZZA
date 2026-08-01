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
#include <vector>

// Impostazioni della rete WiFi
const char *ssid = "myhome";
const char *password = "REDACTED_WIFI_PASSWORD";

// Impostazioni del display ST7735
//#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   15  // Chip select control pin
#define TFT_DC    5  // Data Command control pin
//#define TFT_RST   4  // Reset pin (could connect to RST pin)
#define TFT_RST  -1  // Set TFT_RST to -1 if display RESET is connected to ESP32 board RST
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

std::vector<float> temperatureReadings;
std::vector<float> temperatureReadings2;
const int maxReadings = 60;

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

unsigned long lastUpdateTime = 0;
const long updateInterval = 60000; // Intervallo di aggiornamento in millisecondi (60000 ms = 1 minuto)


// Variabili di impostazione temperatura

float temperature1 = 0.0;
float temperature2 = 0.0;
float tempimpostata1 = 400; // Soglia temperatura per relè 1
float tempimpostata2 = 300; // Soglia temperatura per relè 2
float deltaT1 = 2.0; // Isteresi per relè 1
float deltaT2 = 2.0; // Isteresi per relè 2
char tempString1[10];
char tempString2[10];
char tempString3[10];
char tempString4[10];
char timerString[10];

char prevTempString1[10] = "";
char prevTempString2[10] = "";
char prevTempString3[10] = "";
char prevTempString4[10] = "";
char prevTimerString[10] = "";

const int touchPin = T0;  // Sostituisci con il pin touch che vuoi utilizzare
const int threshold = 40; // Valore soglia per il touch
const int debounceTime = 200;
unsigned long lastTouchTime = 0; // Tempo dell'ultimo tocco rilevato
bool inDebounce = false;         // Indica se ci troviamo nel periodo di debounce

const int buzzerPin = 27;  // Sostituisci con il pin del buzzer

// Variabili per la logica del pulsante
unsigned long buttonPressTime = 0;
unsigned long countdownTimer = 0; // 30 secondi
bool countdownStarted = false;

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
  pinMode(buttonPin, INPUT_PULLUP); // Pulsante con resistenza di pull-up
  pinMode(buzzerPin, OUTPUT); 

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
  server.on("/", HTTP_GET, monitorRoot);
  server.on("/settemp", HTTP_POST, handleSetTemp);
  server.on("/setting", HTTP_GET, handleRoot);
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
  // Gestione server web
  server.handleClient();
 ArduinoOTA.handle(); 
  // Lettura e gestione temperature
      unsigned long currentMillis = millis();
    
  
  float temperature1 = thermocouple.readCelsius();
  delay(2000);
  float temperature2 = thermocouple2.readCelsius();
  if (currentMillis - lastUpdateTime >= updateInterval) { 
    temperatureReadings.push_back(temperature1);
    temperatureReadings2.push_back(temperature2);
    lastUpdateTime = currentMillis;
    Serial.print("Temperature1: ");
    Serial.println(temperature1);
    Serial.print("Temperature2: ");
    Serial.println(temperature2);}   
  if (temperatureReadings.size() > maxReadings) {
        temperatureReadings.erase(temperatureReadings.begin());
        temperatureReadings2.erase(temperatureReadings2.begin());
    }
  
 // Serial.println(temperature1);
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
  dtostrf(countdownTimer, 4, 0, timerString);
  
  
  if (strcmp(tempString1, prevTempString1) != 0 || strcmp(tempString2, prevTempString2) != 0 ||
      strcmp(tempString3, prevTempString3) != 0 || strcmp(tempString4, prevTempString4) != 0 ||
      strcmp(timerString, prevTimerString) != 0){
    // Aggiorna il display
    updateDisplay();

    // Aggiorna le variabili di controllo con i nuovi valori
    strcpy(prevTempString1, tempString1);
    strcpy(prevTempString2, tempString2);
    strcpy(prevTempString3, tempString3);
    strcpy(prevTempString4, tempString4);
    strcpy(prevTimerString, timerString);
  }
    unsigned long currentMillis = millis();

    // Rileva il tocco solo se non ci troviamo nel periodo di debounce
    if (!inDebounce && touchRead(touchPin) < threshold) {
        lastTouchTime = currentMillis;
        inDebounce = true;
        countdownTimer += 30; // Aggiungi 30 secondi al timer
        buttonPressTime = millis();
      //  countdownStarted = true;
    }

    // Controlla se il periodo di debounce è terminato
    if (inDebounce && currentMillis - lastTouchTime > debounceTime) {
        inDebounce = false; // Resetta lo stato di debounce
    }


  // Gestione del conto alla rovescia
  if (millis() - buttonPressTime > 3000) { // 3 secondi dall'ultima pressione
    countdownStarted = true;
    buttonPressTime = millis(); // Resetta il timer
  }
    if (countdownStarted && countdownTimer > 0) {
      countdownTimer--;
    } else {
      playBuzzer(440, 2000); // Suona il buzzer a 440 Hz per 2 secondi
      countdownStarted = false;
    }

if (countdownStarted && countdownTimer > 0) {
if (millis() - countdownTimer > 3000) {

  if (countdownStarted && (currentMillis - previousMillis >= 1000)) {
    previousMillis = currentMillis;
    
    if (countdownTimer > 0) {
      Serial.println(countdownTimer);
      countdownTimer--;
    } else {
      Serial.println("fine");
      // Qui puoi aggiungere ulteriori azioni da eseguire quando il conto alla rovescia è terminato
      // Ad esempio, puoi attivare un relè o eseguire altre operazioni.
      // Assicurati di aggiornare il countdown se desideri ripetere il conto alla rovescia.
    }
  }


}
void handleRoot() {
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>";
    html += "<style>";
    html += "body { font-size: 16px; }";
    html += "button { font-size: 16px; padding: 10px; }";
    html += "</style>";
    html += "</head><body>";
    html += "<h1>Impostazioni Temperatura</h1>";
    html += "<form action='/settemp' method='POST'>";
    html += "Temperatura Impostata 1: <input type='number' name='temp1' value='" + String(tempimpostata1) + "'><br>";
    html += "Temperatura Impostata 2: <input type='number' name='temp2' value='" + String(tempimpostata2) + "'><br>";
    html += "<input type='submit' value='Salva'>";
    html += "</form>";

    // Aggiungi qui il pulsante per la pagina di monitoraggio
    html += "<p><a href='/'><button>Torna alla Pagina di Monitoraggio</button></a></p>";

    html += "</body></html>";
    server.send(200, "text/html", html);
}

void monitorRoot() {
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>";
    html += "<style>";
    html += "body { font-size: 16px; }";
    html += "button { font-size: 16px; padding: 10px; }";
    html += "</style>";
    html += "<script>";
    html += "function updateTemperatures() {";
    html += "  var xhttp = new XMLHttpRequest();";
    html += "  xhttp.onreadystatechange = function() {";
    html += "    if (this.readyState == 4 && this.status == 200) {";
    html += "      var data = JSON.parse(this.responseText);";
    html += "      document.getElementById('temp1').innerHTML = Math.round(data.temperature1);";
    html += "      document.getElementById('temp2').innerHTML = Math.round(data.temperature2);";
    html += "    }";
    html += "  };";
    html += "  xhttp.open(\"GET\", \"/temperature\", true);";
    html += "  xhttp.send();";
    html += "}";
    html += "setInterval(updateTemperatures, 5000);"; // Aggiorna ogni 5 secondi
    html += "</script>";
    html += "</head><body>";
    html += "<h1>MONITOR Temperature</h1>";
    html += "<p>Temperatura CIELO : <span id='temp1'></span> °C</p>";
    html += "<p>Temperatura PLATEA: <span id='temp2'></span> °C</p>";
    
    html += "<p><a href='/setting'><button>Impostazioni Temperature</button></a></p>";
    html += "<p><a href='/temperature-chart'><button>Grafico Temperatura</button></a></p>";
    html += "</body></html>";
    server.sendHeader("Content-Type", "text/html; charset=utf-8");
    server.send(200, "text/html", html);
}
void handleMonitor() {
  float temperature1 = thermocouple.readCelsius();
  delay(500);
  float temperature2 = thermocouple2.readCelsius();
    String json = "{\"temperature1\": " + String(temperature1) + ", \"temperature2\": " + String(temperature2) + "}";
    server.send(200, "application/json", json);
}
void handleTemperatureChart() {
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
    html += "<style>";
    html += "body { font-size: 16px; }";
    html += "button { font-size: 16px; padding: 10px; }";
    html += "canvas { max-width: 100%; height: auto; }"; // Rendi il canvas reattivo
    html += "</style>";
    html += "</head><body>";
    html += "<h1>Grafico Temperatura</h1>";

    // Contenitore del canvas senza dimensioni fisse
    html += "<div>";
    html += "<canvas id='tempChart'></canvas>";
    html += "</div>";

    html += "<script>";
    html += "var ctx = document.getElementById('tempChart').getContext('2d');";
    html += "var tempChart = new Chart(ctx, {";
    html += "  type: 'line',";
    html += "  data: {";
    html += "    labels: [],";
    html += "    datasets: [";
    html += "      { label: 'CIELO', data: [], backgroundColor: 'rgba(255, 99, 132, 0.2)', borderColor: 'rgba(255, 99, 132, 1)', borderWidth: 1 },";
    html += "      { label: 'PLATEA', data: [], backgroundColor: 'rgba(54, 162, 235, 0.2)', borderColor: 'rgba(54, 162, 235, 1)', borderWidth: 1 }";
    html += "    ]";
    html += "  },";
    html += "  options: {";
    html += "    scales: {";
    html += "      y: {";
    html += "        beginAtZero: true,";
    html += "        min: 0,";
    html += "        max: 500,";
    html += "        title: {";
    html += "          display: true,";
    html += "          text: 'Gradi'";
    html += "        }";
    html += "      },";
    html += "      x: {";
    html += "        title: {";
    html += "          display: true,";
    html += "          text: 'Minuti'";
    html += "        }";
    html += "      }";
    html += "    }";
    html += "  }";
    html += "});";
    html += "function updateChart() {";
    html += "  fetch('/temperature-data').then(response => response.json()).then(data => {";
    html += "    tempChart.data.labels = data.labels;";
    html += "    tempChart.data.datasets[0].data = data.temperatures1;"; // Temperatures1 per CIELO
    html += "    tempChart.data.datasets[1].data = data.temperatures2;"; // Temperatures2 per PLATEA
    html += "    tempChart.update();";
    html += "  });";
    html += "}";
    html += "setInterval(updateChart, 10000);"; // Aggiorna ogni 10 secondi
    html += "</script>";
    html += "<p><a href='/'><button>Torna alla Pagina Principale</button></a></p>";
    html += "</body></html>";

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

    server.sendHeader("Location", "/");
    server.send(303);
}
void updateDisplay() {
    tft.fillScreen(ST7735_BLACK);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextSize(0.5);
    tft.setTextColor(ST7735_YELLOW);

   
    tft.setCursor(12, 15);
    tft.println("SIMON'S OVEN");
    uint16_t cieloColor = digitalRead(rele1) == HIGH ? ST7735_RED : ST7735_WHITE;
    tft.setTextColor(cieloColor);
    tft.setCursor(0, 45);
    tft.print("CIELO : "); 
    tft.setCursor(80, 45);
    tft.println(tempString1);
    tft.setFont(); // Resetta al font di default per altre informazioni
    tft.setTextSize(1); 
    tft.setCursor(118, 30);
    tft.print("o");
    tft.setTextColor(ST7735_BLUE);
    tft.setCursor(130, 35);
    //tft.print("SET:"); 
    tft.println(tempString3);
    
    tft.setFont(&FreeSans9pt7b);
    uint16_t plateaColor = digitalRead(rele2) == HIGH ? ST7735_RED : ST7735_WHITE;
    tft.setTextColor(plateaColor);
    tft.setCursor(0, 70);
    tft.print("PLATEA: ");
    tft.setCursor(80, 70); 
    tft.println(tempString2);
    tft.setFont(); // Resetta al font di default per altre informazioni
    tft.setTextSize(1);
    tft.setCursor(118, 55);
    tft.print("o"); 
    tft.setTextColor(ST7735_BLUE);
    tft.setCursor(130, 60);
   // tft.print("SET:"); 
    tft.println(tempString4);
    
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(ST7735_GREEN);
    tft.setCursor(0, 95);
    tft.print("TIMER: ");
    tft.setCursor(80, 95); 
    tft.println(timerString);
    tft.setFont(); // Resetta al font di default per altre informazioni
    tft.setTextSize(1);
    tft.setCursor(118, 88);
    tft.print("S"); 
    //tft.setTextColor(ST7735_BLUE);
    //tft.setCursor(130, 85);
   // tft.print("SET:"); 
   // tft.println(timerString);

    tft.setTextColor(ST7735_CYAN);
    tft.setCursor(25, 115);
    tft.print("IP: "); 
    tft.println(WiFi.localIP());
}
void playBuzzer(int frequency, int duration) {
  tone(buzzerPin, frequency, duration);
}
