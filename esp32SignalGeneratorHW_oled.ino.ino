#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==== CONFIG RETE ====
const char* ssid = "OOZTEST";
const char* password = "diegodiego!";

// ==== DISPLAY OLED ====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==== GENERATORE ====
#define DAC_PIN 25
#define TABLE_MAX 256
hw_timer_t* timer = nullptr;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

Preferences prefs;
WebServer server(80);

// ==== VARIABILI PARAMETRI ====
String waveType = "sine";
int freq = 50;
int tableSize = 128;
float calibrationFactor = 1.0;

uint8_t sineTable[TABLE_MAX];
uint8_t triTable[TABLE_MAX];
uint8_t sawTable[TABLE_MAX];

volatile int waveIndex = 0;

// ==== GENERA TABELLE ====
void generateTables() {
  for (int i = 0; i < TABLE_MAX; i++) {
    sineTable[i] = (uint8_t)((sin(2 * PI * i / TABLE_MAX) * 127.5) + 127.5);
    triTable[i]  = (i < TABLE_MAX / 2)
                     ? map(i, 0, TABLE_MAX / 2 - 1, 0, 255)
                     : map(i, TABLE_MAX / 2, TABLE_MAX - 1, 255, 0);
    sawTable[i]  = map(i, 0, TABLE_MAX - 1, 0, 255);
  }
}

// ==== ISR OUTPUT ====
void IRAM_ATTR onTimer() {
  uint8_t value = 0;
  int idx = (waveIndex * (TABLE_MAX / tableSize)) % TABLE_MAX;

  if (waveType == "sine") {
    value = sineTable[idx];
  } else if (waveType == "triangle") {
    value = triTable[idx];
  } else if (waveType == "saw") {
    value = sawTable[idx];
  } else if (waveType == "square") {
    value = (waveIndex < tableSize / 2) ? 255 : 0;
  }

  dacWrite(DAC_PIN, value);

  waveIndex++;
  if (waveIndex >= tableSize) waveIndex = 0;
}

// ==== AGGIORNA TIMER IN MODO SICURO ====
void safeUpdateTimer() {
  if (timer) {
    timerAlarmDisable(timer);
    timerDetachInterrupt(timer);
    timerEnd(timer);
    timer = nullptr;
  }

  float adjFreq = freq * calibrationFactor;
  if (adjFreq < 1) adjFreq = 1;
  if (adjFreq > 100) adjFreq = 100;

  float period_us = 1000000.0 / (adjFreq * tableSize);
  uint64_t ticks = (uint64_t)period_us;
  if (ticks < 1) ticks = 1;

  timer = timerBegin(0, 80, true); // prescaler 80 = 1 tick = 1 µs
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, ticks, true);
  timerAlarmEnable(timer);
}

// ==== SALVATAGGIO ====
void saveSettings() {
  prefs.begin("genwave", false);
  prefs.putInt("freq", freq);
  prefs.putString("wave", waveType);
  prefs.putInt("res", tableSize);
  prefs.putFloat("cal", calibrationFactor);
  prefs.end();
}

void loadSettings() {
  prefs.begin("genwave", true);
  freq = prefs.getInt("freq", 50);
  waveType = prefs.getString("wave", "sine");
  tableSize = prefs.getInt("res", 128);
  calibrationFactor = prefs.getFloat("cal", 1.0);
  prefs.end();
}

// ==== INTERFACCIA WEB ====
void handleRoot() {
  String html = "<html><body style='font-family:sans-serif'>";
  html += "<h2>Generatore di onde ESP32</h2>";
  html += "By Diego Diegooz Pellacani";
  html += "-------------------------------";
  html += "<form action='/set' method='GET'>";
  html += "Onda: <select name='wave'>";
  html += "<option" + String(waveType=="sine"?" selected":"") + ">sine</option>";
  html += "<option" + String(waveType=="triangle"?" selected":"") + ">triangle</option>";
  html += "<option" + String(waveType=="saw"?" selected":"") + ">saw</option>";
  html += "<option" + String(waveType=="square"?" selected":"") + ">square</option>";
  html += "</select><br>";
  html += "Frequenza (Hz, max 100): <input type='number' name='freq' value='" + String(freq) + "'><br>";
  html += "Risoluzione: <select name='res'>";
  for (int r : {32,64,128,256}) {
    html += "<option";
    if (tableSize==r) html += " selected";
    html += ">" + String(r) + "</option>";
  }
  html += "</select><br>";
  html += "<input type='submit' value='Applica'>";
  html += "</form><br>";
  html += "<form action='/calib' method='GET'>";
  html += "Fattore calibrazione: <input type='text' name='cal' value='" + String(calibrationFactor,3) + "'><br>";
  html += "<input type='submit' value='Salva calibrazione'>";
  html += "</form><br>";
  html += "<form action='/reset' method='GET'>";
  html += "<input type='submit' value='Riavvia ESP32'>";
  html += "</form>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleSet() {
  if (server.hasArg("wave")) waveType = server.arg("wave");
  if (server.hasArg("freq")) freq = server.arg("freq").toInt();
  if (server.hasArg("res")) tableSize = server.arg("res").toInt();
  saveSettings();
  safeUpdateTimer();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleCalib() {
  if (server.hasArg("cal")) calibrationFactor = server.arg("cal").toFloat();
  saveSettings();
  safeUpdateTimer();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleReset() {
  server.send(200, "text/html", "<html><body>Riavvio...</body></html>");
  delay(500);
  ESP.restart();
}

// ==== OLED ====
void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Generatore ESP32");

  display.setCursor(0,12);
  display.print("IP: ");
  display.println(WiFi.localIP());

  display.setCursor(0,24);
  display.print("Onda: ");
  display.println(waveType);

  display.setCursor(0,36);
  display.print("F: ");
  display.print(freq);
  display.println(" Hz");

  display.setCursor(0,48);
  display.print("Res: ");
  display.print(tableSize);
  display.print(" Cal:");
  display.print(calibrationFactor,2);

  display.display();
}

// ==== SETUP ====
void setup() {
  Serial.begin(115200);

  generateTables();
  loadSettings();

  WiFi.begin(ssid, password);
  Serial.print("Connessione WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connesso!");
  Serial.println(WiFi.localIP());

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED non trovato");
  }

  updateOLED();

  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/calib", handleCalib);
  server.on("/reset", handleReset);
  server.begin();

  safeUpdateTimer();
}

// ==== LOOP ====
unsigned long lastOLED = 0;
void loop() {
  server.handleClient();
  if (millis() - lastOLED > 1000) {
    updateOLED();
    lastOLED = millis();
  }
}
