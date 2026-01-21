#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include "config.h"
#include "Storage.h"
#include "CC1101_RF.h"

class WebServerManager {
public:
    WebServerManager();

    // Inicialización
    bool begin(SystemConfig* config);
    void stop();

    // WiFi
    bool startAP();
    bool connectWiFi(const char* ssid, const char* password);
    bool isConnected();
    bool isAPMode();
    String getIPAddress();
    String getSSID();
    int getRSSI();

    // Estado
    void loop();

    // Callbacks para señales RF
    void setSignalCapturedCallback(void (*callback)(const RFSignal*));
    void setSignalTransmitCallback(void (*callback)(const char* deviceId, uint8_t signalIndex));

private:
    AsyncWebServer server;
    AsyncWebSocket ws;
    SystemConfig* sysConfig;

    bool apMode;
    bool wifiConnected;
    unsigned long lastReconnectAttempt;

    // Callbacks
    void (*onSignalCaptured)(const RFSignal*);
    void (*onSignalTransmit)(const char* deviceId, uint8_t signalIndex);

    // Señal temporal para captura
    RFSignal tempCapturedSignal;
    bool captureInProgress;

    // Configuración de rutas
    void setupRoutes();
    void setupWebSocket();

    // Handlers de páginas
    void handleRoot(AsyncWebServerRequest* request);
    void handleNotFound(AsyncWebServerRequest* request);

    // API REST
    void handleGetStatus(AsyncWebServerRequest* request);
    void handleGetConfig(AsyncWebServerRequest* request);
    void handleSaveConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handleGetDevices(AsyncWebServerRequest* request);
    void handleAddDevice(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handleUpdateDevice(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handleDeleteDevice(AsyncWebServerRequest* request);
    void handleTransmitSignal(AsyncWebServerRequest* request);
    void handleStartCapture(AsyncWebServerRequest* request);
    void handleStopCapture(AsyncWebServerRequest* request);
    void handleGetCapture(AsyncWebServerRequest* request);
    void handleSaveSignal(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handleSetFrequency(AsyncWebServerRequest* request);
    void handleScanFrequency(AsyncWebServerRequest* request);
    void handleBackup(AsyncWebServerRequest* request);
    void handleRestore(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handleWiFiScan(AsyncWebServerRequest* request);
    void handleWiFiConnect(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handleReboot(AsyncWebServerRequest* request);
    void handleFactoryReset(AsyncWebServerRequest* request);

    // WebSocket
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg, uint8_t* data, size_t len);
    void broadcastStatus();
    void broadcastCapturedSignal(const RFSignal* signal);

    // Helpers
    String getContentType(const String& filename);
    void sendJsonResponse(AsyncWebServerRequest* request, int code, const String& json);
    void sendJsonError(AsyncWebServerRequest* request, int code, const String& message);
};

// Instancia global
extern WebServerManager webServer;

#endif // WEBSERVER_H
