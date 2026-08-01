#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <max6675.h>

const char *ssid = "IlTuoSSID";
const char *password = "LaTuaPassword";

const int pinSCK1 = 19;
const int pinCS1 = 18;
const int pinMISO1 = 5;

const int pinSCK2 = 25;
const int pinCS2 = 33;
const int pinMISO2 = 32;

const int pinRelay1 = 2;
const int pinRelay2 = 26;

double temperatura1 = 0;
double temperatura2 = 0;
double temperaturaDesiderata1 = 25.0;
double temperaturaDesiderata2 = 25.0;
double deltaT = 5.0;

MAX6675 termocoppia1(pinSCK1, pinCS1, pinMISO1);
MAX6675 termocoppia2(pinSCK2, pinCS2, pinMISO2);

AsyncWebServer server(80);

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connessione in corso...");
  }
  Serial.println("Connesso a WiFi");
  Serial.print("Indirizzo IP: ");
  Serial.println(WiFi.localIP());

  pinMode(pinRelay1, OUTPUT);
  pinMode(pinRelay2, OUTPUT);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/setTemps", HTTP_POST, handleSetTemps);
  server.begin();
}

void loop() {
  temperatura1 = termocoppia1.readCelsius();
  temperatura2 = termocoppia2.readCelsius();

  controllaRelay(temperatura1, pinRelay1, temperaturaDesiderata1, deltaT);
  controllaRelay(temperatura2, pinRelay2, temperaturaDesiderata2, deltaT);

  delay(1000);
}

void handleRoot(AsyncWebServerRequest *request) {
  String paginaWeb = "<html><body>";
  paginaWeb += "<h1>Controlla le temperature</h1>";
  paginaWeb += "<p>Temperatura 1: " + String(temperatura1) + " &deg;C</p>";
  paginaWeb += "<p>Temperatura 2: " + String(temperatura2) + " &deg;C</p>";
  paginaWeb += "<form action='/setTemps' method='post'>";
  paginaWeb += "<label>Imposta temperatura desiderata 1: </label>";
  paginaWeb += "<input type='number' step='0.1' name='tempDes1' value='" + String(temperaturaDesiderata1) + "'><br>";
  paginaWeb += "<label>Imposta temperatura desiderata 2: </label>";
  paginaWeb += "<input type='number' step='0.1' name='tempDes2' value='" + String(temperaturaDesiderata2) + "'><br>";
  paginaWeb += "<label>Imposta Delta T: </label>";
  paginaWeb += "<input type='number' step='0.1' name='deltaT' value='" + String(deltaT) + "'><br>";
  paginaWeb += "<input type='submit' value='Imposta'>";
  paginaWeb += "</form>";
  paginaWeb += "</body></html>";
  request->send(200, "text/html", paginaWeb);
}

void handleSetTemps(AsyncWebServerRequest *request) {
  if (request->hasParam("tempDes1")) {
    temperaturaDesiderata1 = request->getParam("tempDes1")->value().toDouble();
  }

  if (request->hasParam("tempDes2")) {
    temperaturaDesiderata2 = request->getParam("tempDes2")->value().toDouble();
  }

  if (request->hasParam("deltaT")) {
    deltaT = request->getParam("deltaT")->value().toDouble();
  }

  request->send(200, "text/plain", "Temperature impostate");
}

void controllaRelay(double temperatura, int pinRelay, double temperaturaDesiderata, double deltaT) {
  if (temperatura < (temperaturaDesiderata - deltaT)) {
    digitalWrite(pinRelay, HIGH);
  } else if (temperatura > (temperaturaDesiderata + deltaT)) {
    digitalWrite(pinRelay, LOW);
  }
}
