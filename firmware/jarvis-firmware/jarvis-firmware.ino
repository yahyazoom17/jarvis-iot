#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Audio.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =========================
// WiFi configuration
// =========================
const char* WIFI_SSID = "Yahya";
const char* WIFI_PASSWORD = "taj@1727";

// =========================
// FastAPI WebSocket server
// =========================
// IMPORTANT:
// Use your computer's LAN IP, NOT localhost.
//
// Example:
// ws://192.168.1.100:8000/ws
//
const char* WS_HOST = "192.168.0.103";
const char* SERVER_HOST = "http://192.168.0.103:8000";
const uint16_t WS_PORT = 8000;
const char* WS_PATH = "/ws";

WebSocketsClient webSocket;

// Send JSON every 5 seconds
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 3000;

// Push Button
#define BUTTON_PIN 2

bool isRecording = false;

// ============================================================
// INMP441 -> ESP32-S3 GPIO
// ============================================================

#define MIC_I2S_SD   16
#define MIC_I2S_SCK  17
#define MIC_I2S_WS   18
#define SAMPLE_RATE 16000
#define SAMPLE_BITS 16
#define CHANNELS 1

I2SClass I2S;

// =========================
// Audio buffer
// =========================

#define BUFFER_SAMPLES 512
int32_t rawBuffer[BUFFER_SAMPLES];
int16_t pcmBuffer[BUFFER_SAMPLES];

// ============================================================
// ESP32-S3 -> MAX98357A I2S pins
// ============================================================

#define I2S_BCLK 12
#define I2S_LRC  13
#define I2S_DOUT 11

Audio audio;

// OLED PIN CONFIG

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_SDA 8
#define OLED_SCL 9

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    -1
);

void resetDisplay() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(1, 20);
    display.println("JARVIS v2.0");
    display.display();
    delay(3000);
}

// ============================================================
// Music/Song streaming
// ============================================================

enum AudioState {
    AUDIO_IDLE,
    AUDIO_PLAYING_AI,
    AUDIO_PLAYING_TRACK
};

AudioState audioState = AUDIO_IDLE;
String pendingTrackUrl;

void playAudioUrl(const String& url) {

    Serial.println();
    Serial.println("================================");
    Serial.println("Starting audio");
    Serial.println("URL:");
    Serial.println(url);
    Serial.println("================================");

    // stop previous stream
    audio.stopSong();

    // Start new stream
    audio.connecttohost(url.c_str());
}

// This callback handles smooth transiton from AI voice to audio track
void audio_eof_mp3(const char *info) {
    Serial.println("[AUDIO] MP3 finished");

    if (audioState == AUDIO_PLAYING_AI) {
        Serial.println("[AUDIO] AI lead finished");
        Serial.println("[AUDIO] Starting track...");

        audioState = AUDIO_PLAYING_TRACK;
        playAudioUrl(pendingTrackUrl);
    }
    else if (audioState == AUDIO_PLAYING_TRACK) {
        Serial.println("[AUDIO] Track finished");

        audioState = AUDIO_IDLE;
    }
}

// =========================
// Setup microphone
// =========================

void setupMic()
{
    I2S.setPins(
        MIC_I2S_SCK,
        MIC_I2S_WS,
        -1,
        MIC_I2S_SD,
        -1
    );

    bool ok = I2S.begin(
        I2S_MODE_STD,
        SAMPLE_RATE,
        I2S_DATA_BIT_WIDTH_32BIT,
        I2S_SLOT_MODE_MONO,
        I2S_STD_SLOT_LEFT
    );

    if (!ok) {
        Serial.println(
            "[I2S] Failed to initialize"
        );

        while (true) {
            delay(1000);
        }
    }

    Serial.println(
        "[I2S] INMP441 initialized"
    );
}

// ============================================================
// Send audio_start
// ============================================================

void sendAudioStart()
{
    if (!webSocket.isConnected()) {
        return;
    }

    JsonDocument doc;

    doc["type"] = "audio_start";
    doc["sample_rate"] = SAMPLE_RATE;
    doc["channels"] = CHANNELS;
    doc["bits"] = SAMPLE_BITS;
    doc["encoding"] = "pcm_s16le";

    String json;
    serializeJson(doc, json);

    webSocket.sendTXT(json);

    Serial.println("[WS] >>> AUDIO START");
}


// ============================================================
// Send audio_end
// ============================================================

void sendAudioEnd()
{
    if (!webSocket.isConnected()) {
        return;
    }

    JsonDocument doc;

    doc["type"] = "audio_end";

    String json;
    serializeJson(doc, json);

    webSocket.sendTXT(json);

    Serial.println("[WS] >>> AUDIO END");
}


// ============================================================
// Start recording
// ============================================================

void startRecording()
{
    if (isRecording) {
        return;
    }


    if (!webSocket.isConnected()) {
        Serial.println("[MIC] WebSocket not connected");
        return;
    }

    Serial.println();
    Serial.println("==============================");
    Serial.println("[MIC] RECORDING STARTED");
    Serial.println("==============================");

    sendAudioStart();

    isRecording = true;
}


// ============================================================
// Stop recording
// ============================================================

void stopRecording()
{
    if (!isRecording) {
        return;
    }

    Serial.println();
    Serial.println("==============================");
    Serial.println("[MIC] RECORDING STOPPED");
    Serial.println("==============================");

    sendAudioEnd();

    isRecording = false;
}


// ============================================================
// Handle push button
// ============================================================

void handleButton()
{
    // INPUT_PULLUP:
    //
    // Not pressed = HIGH
    // Pressed     = LOW

    bool buttonPressed =
        digitalRead(BUTTON_PIN) == LOW;


    // Button pressed
    if (buttonPressed && !isRecording) {

        startRecording();
    }


    // Button released
    if (!buttonPressed && isRecording) {

        stopRecording();
    }
}

// =========================
// Send microphone audio
// =========================

void sendMicrophoneAudio()
{
    size_t bytesRead = I2S.readBytes(
        (char*)rawBuffer,
        sizeof(rawBuffer)
    );

    if (bytesRead == 0) {
        return;
    }

    size_t samplesRead = bytesRead / sizeof(int32_t);

    // Convert 32-bit I2S samples
    // to 16-bit PCM

    for (size_t i = 0; i < samplesRead; i++) {
        pcmBuffer[i] = (int16_t)(rawBuffer[i] >> 14);
    }

    size_t pcmBytes = samplesRead * sizeof(int16_t);

    if (webSocket.isConnected()) {
        webSocket.sendBIN((uint8_t*)pcmBuffer, pcmBytes);
    }
}

// =========================
// Send JSON to FastAPI
// =========================
void sendJson() {

    // Create JSON document
    JsonDocument doc;

    doc["device_id"] = "jarvis-#001";
    doc["health"] = "ok";
    doc["wifi"] = WiFi.RSSI();
    doc["status"] = "online";
    doc["type"] = "handshake";
    doc["message"] = "Hello! this is Jarvis-ESP32";

    // Example nested object
    JsonObject location = doc["location"].to<JsonObject>();
    location["lat"] = 12.9716;
    location["lng"] = 77.5946;

    // Convert JSON to String
    String json;

    serializeJson(doc, json);

    Serial.print("[WS] Sending: ");
    Serial.println(json);

    // Send JSON over WebSocket
    webSocket.sendTXT(json);
}

// =========================
// WebSocket event handler
// =========================
void webSocketEvent(
    WStype_t type,
    uint8_t* payload,
    size_t length
) {
    switch (type) {

        case WStype_DISCONNECTED:
            Serial.println("[WS] Disconnected");
            break;

        case WStype_CONNECTED: {
            Serial.println("[WS] Connected to Jarvis Server");
            // Send an initial message
            // sendJson();
            break;
        }

        case WStype_TEXT: {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload, length);

            if (error) {
                Serial.print("JSON error: ");
                Serial.println(error.c_str());
                return;
            }
            const char* status = doc["status"];
            Serial.print("Status: ");
            Serial.println(status);

            const char* service = doc["service"];
            // Logic for Music/Song player service
            if(strcmp(service, "audio") == 0) {
                Serial.println("----- AUDIO SERVICE STARTED -----");
                const char* audioUrl = doc["audio_url"];
                const char* audioTitle = doc["audio_title"];
                if (audioUrl == nullptr) {
                Serial.println("[JSON] No Audio URL found");
                return;
                }
                Serial.print("NOW PLAYING: ");
                Serial.println(audioTitle);
                playAudioUrl(audioUrl);
                break;
            }
            // Logic for Jarvis AI service
            else if (strcmp(service, "jarvis_response") == 0) {
                Serial.println("----- JARVIS SERVICE STARTED -----");
                if (doc["is_audio"] == true) {
                    display.clearDisplay();
                    display.setCursor(10, 20);
                    display.setTextSize(1);
                    display.setTextColor(SSD1306_WHITE);
                    display.println("Playing song on JARVIS speaker...");
                    display.display();
                }
                const char* responseUrl = doc["response_url"];
                const char* responseText = doc["response_text"];
                if (responseUrl == nullptr) {
                Serial.println("[JSON] No response URL found");
                return;
                }
                Serial.print("JARVIS RESPONSE: ");
                Serial.println(responseText);
                String audioUrl = String(SERVER_HOST) + responseUrl;
                playAudioUrl(audioUrl);
                break;
            }
            else if (strcmp(service, "text_stream") == 0) {
                display.clearDisplay();
                display.setCursor(10, 20);
                display.setTextSize(1);
                display.setTextColor(SSD1306_WHITE);
                Serial.println("----- STREAMING RESPONSE -----");
                const char* responseWord = doc["text"];
                if (responseWord == nullptr) {
                Serial.println("[JSON] No text found");
                return;
                }
                 // Redraw OLED
                display.println(responseWord);
                display.display();
                break;
            }
            resetDisplay();
            break;
        }
        case WStype_BIN:
            Serial.println("[WS] Received binary data");
            break;
        case WStype_ERROR:
            Serial.println("[WS] Error");
            break;
        default:
            break;
    }
}


// =========================
// Connect to WiFi
// =========================
void connectWiFi() {

    Serial.print("Connecting to WiFi");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi connected!");

    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());
}

// =========================
// Arduino setup
// =========================
void setup() {

    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("Starting ESP32 WebSocket client...");

    // OLED Setup
    Wire.begin(OLED_SDA, OLED_SCL);

    // Push button setup
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Setup MIC
    setupMic();

    // Audio pin setup
    audio.setPinout(
        I2S_BCLK,
        I2S_LRC,
        I2S_DOUT
    );

    // set speaker volume
    audio.setVolume(19);

    // Connect WiFi
    connectWiFi();

    // Configure WebSocket
    webSocket.begin(
        WS_HOST,
        WS_PORT,
        WS_PATH
    );

    // Register event handler
    webSocket.onEvent(webSocketEvent);

    // Automatically reconnect
    webSocket.setReconnectInterval(5000);

    Serial.println("System ready");

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("SSD1306 not found");
        while (true);
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(10, 20);
    display.println("JARVIS v2.0");
    display.display();
    delay(3000);
}

// =========================
// Arduino loop
// =========================
void loop() {
    // Keep WebSocket connection alive
    webSocket.loop();
    // Keep audio alive
    audio.loop();
    // Check push-to-talk button 
    handleButton();
    // Send microphone data ONLY while button is held 
    if (isRecording && webSocket.isConnected()) { 
        sendMicrophoneAudio(); 
    }
}