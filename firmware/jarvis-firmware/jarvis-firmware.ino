/*
  JARVIS ESP32-S3 — stable push-to-talk audio streaming
  [FIXED: Added FreeRTOS Queue to prevent choppy audio / dropped frames]
*/

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Audio.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// ---------- Wi-Fi / server ----------
const char* WIFI_SSID = "Yahya";
const char* WIFI_PASSWORD = "taj@1727";

// Set this to the LAN IP of the computer running FastAPI.
const char* WS_HOST = "192.168.0.111";
const uint16_t WS_PORT = 8000;
const char* WS_PATH = "/ws";
const char* SERVER_BASE_URL = "http://192.168.0.111:8000"; // Change to https://yahyazzzz-jarvis-iot.hf.space

// ---------- Pins ----------
static constexpr int MIC_I2S_SD = 16;
static constexpr int MIC_I2S_SCK = 17;
static constexpr int MIC_I2S_WS = 18;

static constexpr int SPK_I2S_BCLK = 12;
static constexpr int SPK_I2S_LRC = 13;
static constexpr int SPK_I2S_DOUT = 11;

static constexpr int BUTTON_PIN = 2;  // button connects GPIO2 to GND
static constexpr int OLED_SDA = 8;
static constexpr int OLED_SCL = 9;

static constexpr uint32_t SAMPLE_RATE = 16000;
static constexpr size_t SAMPLES_PER_CHUNK = 1024; // 2048 bytes PCM per frame
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 35;

// ---------- FreeRTOS Queue ----------
// 20 chunks buffers ~320ms of audio, safely riding out Wi-Fi stutters
static constexpr size_t QUEUE_MAX_CHUNKS = 10; 
QueueHandle_t audioQueue;

WebSocketsClient webSocket;
I2SClass micI2S;
Audio audio;
Adafruit_SSD1306 display(128, 64, &Wire, -1);

enum class AudioState : uint8_t { IDLE, PLAYING_AI, PLAYING_TRACK };
AudioState audioState = AudioState::IDLE;
String pendingTrackUrl;

volatile bool wsConnected = false;
volatile bool isRecording = false;
bool oledReady = false;
bool lastRawButton = HIGH;
bool stableButton = HIGH;
uint32_t lastButtonChangeMs = 0;
uint32_t lastStatusMs = 0;

// ---------- Display ----------
void showMessage(const String& line1, const String& line2 = "") {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(line1);
  if (line2.length()) display.println(line2);
  display.display();
}

// ---------- WebSocket JSON ----------
void sendControlMessage(const char* type) {
  if (!wsConnected) return;

  JsonDocument doc;
  doc["type"] = type;

  if (strcmp(type, "audio_start") == 0) {
    doc["sample_rate"] = SAMPLE_RATE;
    doc["channels"] = 1;
    doc["bits"] = 16;
    doc["encoding"] = "pcm_s16le";
  }

  String json;
  serializeJson(doc, json);
  webSocket.sendTXT(json);
}

void sendHandshake() {
  if (!wsConnected) return;
  JsonDocument doc;
  doc["type"] = "handshake";
  doc["device_id"] = "jarvis-esp32-s3";
  doc["status"] = "online";
  String json;
  serializeJson(doc, json);
  webSocket.sendTXT(json);
}

// ---------- Microphone & FreeRTOS Task ----------
bool setupMic() {
  micI2S.setPins(MIC_I2S_SCK, MIC_I2S_WS, -1, MIC_I2S_SD, -1);
  return micI2S.begin(
    I2S_MODE_STD,
    SAMPLE_RATE,
    I2S_DATA_BIT_WIDTH_32BIT,
    I2S_SLOT_MODE_MONO,
    I2S_STD_SLOT_LEFT
  );
}

// Dedicated background task to read mic seamlessly
void micTask(void *pvParameters) {
  int32_t rawSamples[SAMPLES_PER_CHUNK];
  int16_t pcmSamples[SAMPLES_PER_CHUNK];
  
  while (true) {
    if (!isRecording) {
      vTaskDelay(pdMS_TO_TICKS(20)); // Rest when idle
      continue;
    }
    
    // Blocks safely until requested data is ready, isolated from Wi-Fi lag
    size_t bytesRead = micI2S.readBytes(
      reinterpret_cast<char*>(rawSamples),
      sizeof(rawSamples)
    );

    if (bytesRead == sizeof(rawSamples)) {
      for (size_t i = 0; i < SAMPLES_PER_CHUNK; ++i) {
        // INMP441 emits 24-bit data. Shift right by 14 adds digital gain.
        int32_t sample = rawSamples[i] >> 14;
        
        // Prevent audio distortion/clipping
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        pcmSamples[i] = static_cast<int16_t>(sample);
      }
      
      // Push to queue, wait up to 10ms if queue is temporarily full
      xQueueSend(audioQueue, pcmSamples, pdMS_TO_TICKS(10));
    } else {
      vTaskDelay(pdMS_TO_TICKS(1)); 
    }
  }
}

void startRecording() {
  if (isRecording) return;
  if (!wsConnected) {
    Serial.println("[MIC] Cannot record: WebSocket is disconnected");
    showMessage("JARVIS", "Server disconnected");
    return;
  }

  isRecording = true;
  sendControlMessage("audio_start");
  Serial.println("[MIC] RECORDING STARTED");
  showMessage("JARVIS", "Listening...");
}

void stopRecording() {
  if (!isRecording) return;
  
  // 1. Tell the background micTask to stop recording new audio
  isRecording = false;

  // 2. Wait just a moment to let micTask finish its current I2S read
  // and push the very last chunk of audio into the queue.
  delay(30); 

  // 3. CRITICAL FIX: Drain all remaining audio chunks from the queue 
  // and send them to the server BEFORE we send the "audio_end" signal.
  int16_t pcmSamples[SAMPLES_PER_CHUNK];
  while (xQueueReceive(audioQueue, pcmSamples, 0) == pdTRUE) {
    webSocket.sendBIN(
      reinterpret_cast<uint8_t*>(pcmSamples),
      sizeof(pcmSamples)
    );
  }

  // 4. Now that the queue is safely sent, tell the server we are done.
  sendControlMessage("audio_end");
  Serial.println("[MIC] RECORDING STOPPED (Queue flushed)");
  showMessage("JARVIS", "Audio sent");
}

void streamMicrophoneChunk() {
  if (!isRecording || !wsConnected) {
    // Keep the queue clean of stale data when we are NOT recording.
    // We don't have to worry about losing good audio here anymore, 
    // because stopRecording() already safely sent it.
    int16_t dummy[SAMPLES_PER_CHUNK];
    while (uxQueueMessagesWaiting(audioQueue) > 0) {
      xQueueReceive(audioQueue, dummy, 0);
    }
    return;
  }

  int16_t pcmSamples[SAMPLES_PER_CHUNK];
  
  // During normal recording, send chunks as fast as the network allows
  while (xQueueReceive(audioQueue, pcmSamples, 0) == pdTRUE) {
    webSocket.sendBIN(
      reinterpret_cast<uint8_t*>(pcmSamples),
      sizeof(pcmSamples)
    );
  }
}

// ---------- Audio playback ----------
void playAudioUrl(const String& url) {
  Serial.printf("[AUDIO] Starting URL: %s\n", url.c_str());
  audio.stopSong();
  audio.connecttohost(url.c_str());
}

void audio_eof_mp3(const char* info) {
  Serial.println("[AUDIO] Stream finished");
  if (audioState == AudioState::PLAYING_AI && pendingTrackUrl.length()) {
    audioState = AudioState::PLAYING_TRACK;
    playAudioUrl(pendingTrackUrl);
  } else {
    audioState = AudioState::IDLE;
  }
}

// ---------- Incoming WebSocket messages ----------
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      wsConnected = false;
      if (isRecording) {
        isRecording = false;
        Serial.println("[WS] Disconnected during recording; recording stopped");
      }
      Serial.println("[WS] Disconnected");
      showMessage("JARVIS", "Disconnected");
      break;

    case WStype_CONNECTED:
      wsConnected = true;
      Serial.println("[WS] Connected to JARVIS server");
      showMessage("JARVIS", "Server connected");
      sendHandshake();
      break;

    case WStype_TEXT: {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, payload, length);
      if (err) return;

      const char* status = doc["status"] | "";
      const char* service = doc["service"] | "";

      if (strcmp(service, "audio") == 0) {
        const char* url = doc["audio_url"] | "";
        if (*url) playAudioUrl(String(url));
      } else if (strcmp(service, "jarvis_response") == 0) {
        const char* responseUrl = doc["response_url"] | "";
        if (*responseUrl) {
          String fullUrl = responseUrl;
          if (!fullUrl.startsWith("http")) {
            fullUrl = String(SERVER_BASE_URL) + responseUrl;
          }
          playAudioUrl(fullUrl);
        }
        const char* responseText = doc["response_text"] | "";
        showMessage("JARVIS", String(responseText).substring(0, 20));
      } else if (strcmp(service, "text_stream") == 0) {
        const char* responseText = doc["text"] | "";
        showMessage("JARVIS", String(responseText).substring(0, 40));
      } else if (strcmp(status, "audio_saved") == 0) {
        Serial.printf("[WS] Audio saved: %lu bytes\n", (unsigned long)(doc["bytes"] | 0));
      } else if (strcmp(status, "error") == 0) {
        const char* errorText = doc["message"] | "Server error";
        Serial.printf("[SERVER] %s\n", errorText);
      }
      break;
    }
    default: break;
  }
}

// ---------- Wi-Fi ----------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Reduce latency
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("[WiFi] Connecting");
  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
    if (millis() - started > 30000) ESP.restart();
  }
  Serial.printf("\n[WiFi] Connected. ESP32 IP: %s\n", WiFi.localIP().toString().c_str());
}

// ---------- Setup / loop ----------
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);
  oledReady = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (oledReady) showMessage("JARVIS", "Booting...");

  // Initialize FreeRTOS Queue for buffering mic audio safely
  audioQueue = xQueueCreate(QUEUE_MAX_CHUNKS, SAMPLES_PER_CHUNK * sizeof(int16_t));

  if (!setupMic()) {
    Serial.println("[I2S] Microphone initialization failed");
    showMessage("JARVIS", "Mic init failed");
    while (true) delay(1000);
  }
  
  // Pin task to Core 0 (PRO_CORE) to balance CPU load with Arduino on Core 1
  xTaskCreatePinnedToCore(micTask, "MicTask", 16384, NULL, 2, NULL, 0);

  audio.setPinout(SPK_I2S_BCLK, SPK_I2S_LRC, SPK_I2S_DOUT);
  audio.setVolume(20);

  connectWiFi();

  webSocket.begin(WS_HOST, WS_PORT, WS_PATH);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(10000);
  // webSocket.enableHeartbeat(15000, 3000, 2);

  Serial.println("[SYSTEM] Ready. Hold button to record.");
  showMessage("JARVIS", "Ready - hold btn");
}

void loop() {
  webSocket.loop();
  audio.loop();

  bool rawButton = digitalRead(BUTTON_PIN);
  if (rawButton != lastRawButton) {
    lastRawButton = rawButton;
    lastButtonChangeMs = millis();
  }

  if ((millis() - lastButtonChangeMs) >= BUTTON_DEBOUNCE_MS && rawButton != stableButton) {
    stableButton = rawButton;
    if (stableButton == LOW) startRecording();
    else stopRecording();
  }

  // Drains the queue created by the micTask
  streamMicrophoneChunk();

  if (millis() - lastStatusMs >= 5000) {
    lastStatusMs = millis();
    Serial.printf("[SYSTEM] heap=%u ws=%s recording=%s\n",
                  ESP.getFreeHeap(),
                  wsConnected ? "connected" : "disconnected",
                  isRecording ? "yes" : "no");
  }
  delay(1);
}