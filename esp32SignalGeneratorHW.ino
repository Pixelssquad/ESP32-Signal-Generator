#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

//credenziali della vostra rete wifi, l'ip assegnato lo vedrete sul serial monitor
const char* ssid = "xxxxxxxxxxxxxx";   
const char* password = "xxxxxxxxxxxxxxxxxxxxxxxxxx";

AsyncWebServer server(80);
Preferences prefs;

String waveType = "sine";
int freq = 100;        // Hz
int waveDelay = 0;     // ms
int tableSize = 256;   // Risoluzione campioni

const int dacPin = 25;

#define TABLE_MAX 256
uint8_t sineTable[TABLE_MAX];
uint8_t triTable[TABLE_MAX];
uint8_t sawTable[TABLE_MAX];

hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

volatile int idx = 0;

void initTables() {
  for (int i = 0; i < TABLE_MAX; i++) {
    sineTable[i] = (uint8_t)(127.5 + 127.5 * sin(2 * PI * i / TABLE_MAX));

    if (i < TABLE_MAX / 2) {
      triTable[i] = (uint8_t)(i * 255.0 / (TABLE_MAX / 2));
    } else {
      triTable[i] = (uint8_t)(255 - ((i - TABLE_MAX / 2) * 255.0 / (TABLE_MAX / 2)));
    }

    sawTable[i] = (uint8_t)(i * 255.0 / TABLE_MAX);
  }
}

void IRAM_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);

  if (waveType == "sine") {
    dacWrite(dacPin, sineTable[idx * (TABLE_MAX / tableSize)]);
  } else if (waveType == "triangle") {
    dacWrite(dacPin, triTable[idx * (TABLE_MAX / tableSize)]);
  } else if (waveType == "saw") {
    dacWrite(dacPin, sawTable[idx * (TABLE_MAX / tableSize)]);
  } else if (waveType == "square") {
    dacWrite(dacPin, (idx < tableSize / 2) ? 255 : 0);
  }

  idx = (idx + 1) % tableSize;

  portEXIT_CRITICAL_ISR(&timerMux);
}

void saveSettings() {
  prefs.begin("signalgen", false);
  prefs.putString("waveType", waveType);
  prefs.putInt("freq", freq);
  prefs.putInt("waveDelay", waveDelay);
  prefs.putInt("tableSize", tableSize);
  prefs.end();
}

void loadSettings() {
  prefs.begin("signalgen", true);
  waveType = prefs.getString("waveType", "sine");
  freq = prefs.getInt("freq", 100);
  waveDelay = prefs.getInt("waveDelay", 0);
  tableSize = prefs.getInt("tableSize", 256);
  prefs.end();
}

void updateTimer() {
  if (timer) {
    timerEnd(timer);
  }

  timer = timerBegin(0, 80, true); // 1 tick = 1 µs
  timerAttachInterrupt(timer, &onTimer, true);

  float samplePeriodUs = 1000000.0 / (freq * tableSize);
  timerAlarmWrite(timer, (uint64_t)samplePeriodUs, true);
  timerAlarmEnable(timer);

  Serial.printf("Timer aggiornato: freq=%d Hz, tableSize=%d, periodo=%.2f us\n",
                freq, tableSize, samplePeriodUs);
}

void safeUpdateTimer() {
  portENTER_CRITICAL(&timerMux);
  if (timer) timerAlarmDisable(timer);
  portEXIT_CRITICAL(&timerMux);

  updateTimer();

  portENTER_CRITICAL(&timerMux);
  if (timer) timerAlarmEnable(timer);
  portEXIT_CRITICAL(&timerMux);
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Generatore di Segnale ESP32</title>
</head>
<body>
  <h2>Generatore di Segnale ESP32</h2>
  <h3>By Diego "Diegooz" Pellacani</h3>
  <p><a target="_blank" rel="noopener noreferrer" href="https://www.youtube.com/@PixelsSquad">My YouTube Channel</a> &nbsp;- &nbsp;<a target="_blank" rel="noopener noreferrer" href="https://linktr.ee/ps_diegooz">My Socials/Contacts</a></p>
  <hr>
  <p>Onda attuale: <b>%WAVE%</b></p>
  <p>Frequenza impostata: <b>%FREQ%</b> Hz</p>
  <p>Delay tra cicli: <b>%DELAY%</b> ms</p>
  <p>Risoluzione: <b>%TABLE%</b> campioni</p>
  <br>
  <button onclick="location.href='/setWave?type=sine'">Seno</button>
  <button onclick="location.href='/setWave?type=triangle'">Triangolare</button>
  <button onclick="location.href='/setWave?type=square'">Quadra</button>
  <button onclick="location.href='/setWave?type=saw'">Dente di sega</button>
  <br><br>
  <form action="/setFreq" method="get">
    Frequenza (max 100 Hz): <input type="number" name="value" min="1" max="100" value="%FREQ%">
    <input type="submit" value="Imposta">
  </form>

  <form action="/setTable" method="get">
    Risoluzione:
    <select name="value">
      <option value="32" %S32%>32</option>
      <option value="64" %S64%>64</option>
      <option value="128" %S128%>128</option>
      <option value="256" %S256%>256</option>
    </select>
    <input type="submit" value="Imposta">
  </form>
  <br><br>
  <button style="background:red;color:white;padding:10px" onclick="location.href='/reset'">Riavvia ESP32</button>
</body>
</html>
)rawliteral";

String processor(const String& var) {
  if (var == "WAVE") return waveType;
  if (var == "FREQ") return String(freq);
  if (var == "DELAY") return String(waveDelay);
  if (var == "TABLE") return String(tableSize);
  if (var == "S32") return (tableSize==32) ? "selected" : "";
  if (var == "S64") return (tableSize==64) ? "selected" : "";
  if (var == "S128") return (tableSize==128) ? "selected" : "";
  if (var == "S256") return (tableSize==256) ? "selected" : "";
  return String();
}

void setup() {
  Serial.begin(115200);
  initTables();
  loadSettings();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnesso! IP: " + WiFi.localIP().toString());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  server.on("/setWave", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("type")) {
      waveType = request->getParam("type")->value();
      saveSettings();
    }
    request->redirect("/");
  });

  server.on("/setFreq", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("value")) {
      int val = request->getParam("value")->value().toInt();
      if (val > 0 && val <= 100) {
        freq = val;
        saveSettings();
        safeUpdateTimer();
      }
    }
    request->redirect("/");
  });

  server.on("/setDelay", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("value")) {
      waveDelay = request->getParam("value")->value().toInt();
      saveSettings();
    }
    request->redirect("/");
  });

  server.on("/setTable", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("value")) {
      int val = request->getParam("value")->value().toInt();
      if (val==32 || val==64 || val==128 || val==256) {
        tableSize = val;
        saveSettings();
        safeUpdateTimer();
      }
    }
    request->redirect("/");
  });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Riavvio ESP32...");
    delay(500);
    ESP.restart();
  });

  server.begin();
  safeUpdateTimer();
}

void loop() {
  if (waveDelay > 0 && idx == 0) {
    delay(waveDelay);
  }
}



 // <form action="/setDelay" method="get">
 //   Delay (ms): <input type="number" name="value" min="0" value="%DELAY%">
 //   <input type="submit" value="Imposta">
 // </form>

