#ifndef WEBSERVER_MANAGER_H
#define WEBSERVER_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
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
    ::WebServer* server;
    SystemConfig* sysConfig;

    bool apMode;
    bool wifiConnected;
    unsigned long lastReconnectAttempt;

    // Callbacks
    void (*onSignalCaptured)(const RFSignal*);
    void (*onSignalTransmit)(const char* deviceId, uint8_t signalIndex);

    // Señal temporal para captura
    RFSignal* tempCapturedSignal;
    bool captureInProgress;

    // Configuración de rutas
    void setupRoutes();

    // Handlers de páginas
    void handleRoot();
    void handleNotFound();

    // API REST
    void handleGetStatus();
    void handleGetConfig();
    void handleSaveConfig();
    void handleGetDevices();
    void handleAddDevice();
    void handleUpdateDevice();
    void handleDeleteDevice();
    void handleTransmitSignal();
    void handleStartCapture();
    void handleStopCapture();
    void handleGetCapture();
    void handleSaveSignal();
    void handleDeleteSignal();
    void handleTestSignal();
    void handleUpdateSignalRepeat();
    void handleUpdateSignalInvert();
    void handleSetFrequency();
    void handleScanFrequency();
    void handleIdentifySignal();
    void handleDecodeAOK();
    void handleBackup();
    void handleRestore();
    void handleWiFiScan();
    void handleWiFiConnect();
    void handleMqttRediscover();
    void handleReboot();
    void handleFactoryReset();

    // Helpers
    String getContentType(const String& filename);
    void sendJsonResponse(int code, const String& json);
    void sendJsonError(int code, const String& message);
    void handleCORS();
    bool checkAuth();  // Verificar autenticación
};

// Instancia global
extern WebServerManager webServer;

#endif // WEBSERVER_MANAGER_H
