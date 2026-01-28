#include "WebServerManager.h"
#include <LittleFS.h>
#include <ElegantOTA.h>
#include "SomfyRTS.h"
#include "DooyaBidir.h"
#include "AOK_Protocol.h"
#include "MQTTClient.h"

WebServerManager webServer;

WebServerManager::WebServerManager() {
    server = nullptr;
    tempCapturedSignal = nullptr;
    apMode = false;
    wifiConnected = false;
    lastReconnectAttempt = 0;
    onSignalCaptured = nullptr;
    onSignalTransmit = nullptr;
    captureInProgress = false;
    sysConfig = nullptr;
}

bool WebServerManager::begin(SystemConfig* config) {
    sysConfig = config;
    Serial.println("[Web] Iniciando servidor web...");

    // Crear instancias dinamicamente
    server = new ::WebServer(80);
    tempCapturedSignal = new RFSignal();

    if (config->wifi_configured && strlen(config->wifi_ssid) > 0) {
        if (connectWiFi(config->wifi_ssid, config->wifi_password)) {
            Serial.println("[Web] Conectado a WiFi");
        } else {
            Serial.println("[Web] No se pudo conectar a WiFi, iniciando AP...");
            startAP();
        }
    } else {
        startAP();
    }

    setupRoutes();

    // Initialize ElegantOTA BEFORE server->begin()
    ElegantOTA.begin(server);

    server->begin();
    Serial.printf("[Web] Servidor iniciado en http://%s\n", getIPAddress().c_str());
    Serial.printf("[Web] OTA disponible en http://%s/update\n", getIPAddress().c_str());

    return true;
}

void WebServerManager::stop() {
    if (server) server->stop();
}

bool WebServerManager::startAP() {
    Serial.println("[Web] Iniciando modo AP...");

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    apMode = true;
    Serial.printf("[Web] AP iniciado: %s - IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    return true;
}

bool WebServerManager::connectWiFi(const char* ssid, const char* password) {
    // Si ya está conectado al mismo SSID, no reconectar
    if (WiFi.status() == WL_CONNECTED) {
        String currentSSID = WiFi.SSID();
        if (currentSSID == ssid) {
            wifiConnected = true;
            Serial.printf("[Web] Ya conectado a %s, IP: %s\n", ssid, WiFi.localIP().toString().c_str());
            return true;
        }
    }

    Serial.printf("[Web] Conectando a WiFi: %s...\n", ssid);

    // No cambiar modo ni reconectar si ya está conectado
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.mode(WIFI_STA);
        // Configurar hostname si está disponible
        if (sysConfig && strlen(sysConfig->device_name) > 0) {
            WiFi.setHostname(sysConfig->device_name);
        }
        WiFi.begin(ssid, password);
    }

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < 15000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.printf("[Web] WiFi conectado! IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }

    wifiConnected = false;
    Serial.println("[Web] Error al conectar a WiFi");
    return false;
}

bool WebServerManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool WebServerManager::isAPMode() {
    return apMode;
}

String WebServerManager::getIPAddress() {
    if (isConnected()) {
        return WiFi.localIP().toString();
    }
    return WiFi.softAPIP().toString();
}

String WebServerManager::getSSID() {
    if (isConnected()) {
        return WiFi.SSID();
    }
    return AP_SSID;
}

int WebServerManager::getRSSI() {
    if (isConnected()) {
        return WiFi.RSSI();
    }
    return 0;
}

void WebServerManager::loop() {
    static unsigned long wifiLostTime = 0;
    static unsigned long apModeStartTime = 0;
    static unsigned long lastReconnectTry = 0;
    static uint8_t reconnectAttempts = 0;

    if (!server || !sysConfig) return;

    server->handleClient();
    ElegantOTA.loop();

    bool connected = isConnected();

    // Si perdió WiFi
    if (sysConfig->wifi_configured && !connected) {
        if (wifiLostTime == 0) {
            wifiLostTime = millis();
            reconnectAttempts = 0;
            Serial.println("[Web] WiFi perdido, iniciando reconexión...");
        }

        unsigned long timeSinceLost = millis() - wifiLostTime;
        unsigned long reconnectInterval;

        // Reconexión agresiva: más frecuente al inicio
        if (timeSinceLost < 30000) {
            reconnectInterval = 5000;   // Primeros 30s: cada 5 segundos
        } else if (timeSinceLost < 120000) {
            reconnectInterval = 15000;  // 30s-2min: cada 15 segundos
        } else {
            reconnectInterval = 30000;  // Después: cada 30 segundos
        }

        // Si no está en modo AP y pasaron 2 minutos, encender AP
        if (!apMode && timeSinceLost > 120000) {
            Serial.println("[Web] 2 min sin WiFi, encendiendo AP de respaldo...");
            startAP();
            apModeStartTime = millis();
        }

        // Intentar reconectar según el intervalo calculado
        if (millis() - lastReconnectTry > reconnectInterval) {
            lastReconnectTry = millis();
            reconnectAttempts++;
            Serial.printf("[Web] Intento reconexión #%d a %s...\n", reconnectAttempts, sysConfig->wifi_ssid);

            // Forzar reconexión
            WiFi.disconnect(false);
            delay(100);
            WiFi.setHostname(sysConfig->device_name);
            WiFi.begin(sysConfig->wifi_ssid, sysConfig->wifi_password);
        }

        // Si lleva 30 minutos en modo AP sin reconectar, reiniciar
        if (apMode && (millis() - apModeStartTime > 1800000)) {
            Serial.println("[Web] 30 min sin WiFi, reiniciando sistema...");
            delay(1000);
            ESP.restart();
        }
    }

    // Si reconectó WiFi
    if (connected) {
        if (wifiLostTime > 0) {
            unsigned long downtime = (millis() - wifiLostTime) / 1000;
            Serial.printf("[Web] WiFi reconectado tras %lu segundos (%d intentos)\n", downtime, reconnectAttempts);
        }
        wifiLostTime = 0;
        apModeStartTime = 0;
        reconnectAttempts = 0;

        // Si está en modo AP, apagarlo
        if (apMode) {
            Serial.println("[Web] Apagando AP (WiFi estable)...");
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            apMode = false;
        }
    }

    if (captureInProgress && rfModule.isConnected() && !rfModule.isCapturing()) {
        captureInProgress = false;
    }
}

void WebServerManager::setSignalCapturedCallback(void (*callback)(const RFSignal*)) {
    onSignalCaptured = callback;
}

void WebServerManager::setSignalTransmitCallback(void (*callback)(const char* deviceId, uint8_t signalIndex)) {
    onSignalTransmit = callback;
}

void WebServerManager::handleCORS() {
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    server->sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

bool WebServerManager::checkAuth() {
    // Verificar si hay credenciales de autenticación básica
    if (!server->authenticate(WEB_AUTH_USER, WEB_AUTH_PASSWORD)) {
        server->requestAuthentication(BASIC_AUTH, "RF Controller", "Acceso denegado");
        return false;
    }
    return true;
}

void WebServerManager::setupRoutes() {
    // Handler for CORS preflight OPTIONS requests
    server->on("/api/config", HTTP_OPTIONS, [this]() { handleCORS(); server->send(204); });
    server->on("/api/devices", HTTP_OPTIONS, [this]() { handleCORS(); server->send(204); });
    server->on("/api/devices/update", HTTP_OPTIONS, [this]() { handleCORS(); server->send(204); });
    server->on("/api/rf/signal/save", HTTP_OPTIONS, [this]() { handleCORS(); server->send(204); });
    server->on("/api/rf/signal/delete", HTTP_OPTIONS, [this]() { handleCORS(); server->send(204); });
    server->on("/api/rf/test", HTTP_OPTIONS, [this]() { handleCORS(); server->send(204); });
    server->on("/api/signal/repeat", HTTP_OPTIONS, [this]() { handleCORS(); server->send(204); });
    server->on("/api/signal/invert", HTTP_OPTIONS, [this]() { handleCORS(); server->send(204); });
    server->on("/api/restore", HTTP_OPTIONS, [this]() { handleCORS(); server->send(204); });
    server->on("/api/wifi/connect", HTTP_OPTIONS, [this]() { handleCORS(); server->send(204); });

    server->on("/", HTTP_GET, [this]() { handleRoot(); });
    server->on("/api/status", HTTP_GET, [this]() { handleGetStatus(); });
    server->on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
    server->on("/api/config", HTTP_POST, [this]() { handleSaveConfig(); });
    server->on("/api/devices", HTTP_GET, [this]() { handleGetDevices(); });
    server->on("/api/devices", HTTP_POST, [this]() { handleAddDevice(); });
    server->on("/api/devices/update", HTTP_POST, [this]() { handleUpdateDevice(); });
    server->on("/api/devices/delete", HTTP_GET, [this]() { handleDeleteDevice(); });
    server->on("/api/rf/transmit", HTTP_GET, [this]() { handleTransmitSignal(); });
    server->on("/api/rf/capture/start", HTTP_GET, [this]() { handleStartCapture(); });
    server->on("/api/rf/capture/stop", HTTP_GET, [this]() { handleStopCapture(); });
    server->on("/api/rf/capture/get", HTTP_GET, [this]() { handleGetCapture(); });
    server->on("/api/rf/signal/save", HTTP_POST, [this]() { handleSaveSignal(); });
    server->on("/api/rf/signal/delete", HTTP_POST, [this]() { handleDeleteSignal(); });
    server->on("/api/rf/test", HTTP_POST, [this]() { handleTestSignal(); });
    server->on("/api/signal/repeat", HTTP_POST, [this]() { handleUpdateSignalRepeat(); });
    server->on("/api/signal/invert", HTTP_POST, [this]() { handleUpdateSignalInvert(); });
    server->on("/api/rf/frequency", HTTP_GET, [this]() { handleSetFrequency(); });
    server->on("/api/rf/scan", HTTP_GET, [this]() { handleScanFrequency(); });
    server->on("/api/rf/identify", HTTP_GET, [this]() { handleIdentifySignal(); });
    server->on("/api/rf/decode-aok", HTTP_POST, [this]() { handleDecodeAOK(); });
    server->on("/api/backup", HTTP_GET, [this]() { handleBackup(); });
    server->on("/api/restore", HTTP_POST, [this]() { handleRestore(); });
    server->on("/api/wifi/scan", HTTP_GET, [this]() { handleWiFiScan(); });
    server->on("/api/wifi/connect", HTTP_POST, [this]() { handleWiFiConnect(); });
    server->on("/api/mqtt/rediscover", HTTP_POST, [this]() { handleMqttRediscover(); });
    server->on("/api/reboot", HTTP_GET, [this]() { handleReboot(); });
    server->on("/api/factory-reset", HTTP_GET, [this]() { handleFactoryReset(); });
    server->onNotFound([this]() { handleNotFound(); });
}

void WebServerManager::handleRoot() {
    if (!checkAuth()) return;

    if (LittleFS.exists("/index.html")) {
        File file = LittleFS.open("/index.html", "r");
        server->streamFile(file, "text/html");
        file.close();
    } else {
        String html = "<!DOCTYPE html><html><head><title>RF Controller</title>";
        html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
        html += "</head><body><h1>RF Controller</h1>";
        html += "<p>Archivos web no encontrados. Suba los archivos al filesystem.</p>";
        html += "<p>IP: " + getIPAddress() + "</p>";
        html += "</body></html>";
        server->send(200, "text/html", html);
    }
}

void WebServerManager::handleNotFound() {
    // Handle CORS preflight for any unhandled OPTIONS request
    if (server->method() == HTTP_OPTIONS) {
        handleCORS();
        server->send(204);
        return;
    }

    // Requiere autenticación para archivos estáticos
    if (!checkAuth()) return;

    String path = server->uri();
    if (LittleFS.exists(path)) {
        File file = LittleFS.open(path, "r");
        server->streamFile(file, getContentType(path));
        file.close();
        return;
    }
    server->send(404, "text/plain", "Not found");
}

void WebServerManager::handleGetStatus() {
    handleCORS();

    StaticJsonDocument<512> doc;
    doc["wifi_connected"] = isConnected();
    doc["wifi_ssid"] = getSSID();
    doc["ap_mode"] = apMode;
    doc["ip"] = getIPAddress();
    doc["rssi"] = getRSSI();

    bool rfConnected = rfModule.isConnected();
    doc["rf_connected"] = rfConnected;
    doc["rf_frequency"] = rfConnected ? round(rfModule.getFrequency() * 100) / 100.0 : 0;
    doc["rf_capturing"] = rfConnected ? rfModule.isCapturing() : false;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
    doc["ota_url"] = "http://" + getIPAddress() + "/update";
    doc["version"] = FIRMWARE_VERSION;

    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void WebServerManager::handleGetConfig() {
    handleCORS();

    StaticJsonDocument<1024> doc;
    doc["wifi_ssid"] = sysConfig->wifi_ssid;
    doc["wifi_configured"] = sysConfig->wifi_configured;
    doc["mqtt_enabled"] = sysConfig->mqtt_enabled;
    doc["mqtt_server"] = sysConfig->mqtt_server;
    doc["mqtt_port"] = sysConfig->mqtt_port;
    doc["mqtt_user"] = sysConfig->mqtt_user;
    doc["mqtt_discovery"] = sysConfig->mqtt_discovery;
    doc["ntp_server"] = sysConfig->ntp_server;
    doc["timezone"] = sysConfig->timezone;
    doc["device_name"] = sysConfig->device_name;
    doc["default_frequency"] = sysConfig->default_frequency;

    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void WebServerManager::handleSaveConfig() {
    handleCORS();
    if (!checkAuth()) return;

    if (!server->hasArg("plain")) {
        sendJsonError(400, "No data received");
        return;
    }

    String body = server->arg("plain");
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        sendJsonError(400, "Invalid JSON");
        return;
    }

    if (doc.containsKey("wifi_ssid")) {
        strlcpy(sysConfig->wifi_ssid, doc["wifi_ssid"] | "", sizeof(sysConfig->wifi_ssid));
    }
    if (doc.containsKey("wifi_password")) {
        strlcpy(sysConfig->wifi_password, doc["wifi_password"] | "", sizeof(sysConfig->wifi_password));
    }
    if (doc.containsKey("wifi_ssid") || doc.containsKey("wifi_password")) {
        sysConfig->wifi_configured = strlen(sysConfig->wifi_ssid) > 0;
    }

    bool mqttChanged = false;
    if (doc.containsKey("mqtt_enabled")) {
        mqttChanged = true;
        sysConfig->mqtt_enabled = doc["mqtt_enabled"];
    }
    if (doc.containsKey("mqtt_server")) {
        mqttChanged = true;
        strlcpy(sysConfig->mqtt_server, doc["mqtt_server"] | "", sizeof(sysConfig->mqtt_server));
    }
    if (doc.containsKey("mqtt_port")) {
        mqttChanged = true;
        sysConfig->mqtt_port = doc["mqtt_port"];
    }
    if (doc.containsKey("mqtt_user")) {
        mqttChanged = true;
        strlcpy(sysConfig->mqtt_user, doc["mqtt_user"] | "", sizeof(sysConfig->mqtt_user));
    }
    if (doc.containsKey("mqtt_password")) {
        mqttChanged = true;
        strlcpy(sysConfig->mqtt_password, doc["mqtt_password"] | "", sizeof(sysConfig->mqtt_password));
    }
    if (doc.containsKey("mqtt_client_id")) {
        mqttChanged = true;
        strlcpy(sysConfig->mqtt_client_id, doc["mqtt_client_id"] | "", sizeof(sysConfig->mqtt_client_id));
    }
    if (doc.containsKey("mqtt_discovery")) {
        mqttChanged = true;
        sysConfig->mqtt_discovery = doc["mqtt_discovery"];
    }

    if (doc.containsKey("ntp_server")) {
        strlcpy(sysConfig->ntp_server, doc["ntp_server"] | "", sizeof(sysConfig->ntp_server));
    }
    if (doc.containsKey("timezone")) {
        strlcpy(sysConfig->timezone, doc["timezone"] | "", sizeof(sysConfig->timezone));
    }
    if (doc.containsKey("device_name")) {
        strlcpy(sysConfig->device_name, doc["device_name"] | "", sizeof(sysConfig->device_name));
    }
    if (doc.containsKey("default_frequency")) {
        sysConfig->default_frequency = doc["default_frequency"];
    }

    if (storage.saveConfig(sysConfig)) {
        // Si cambió la configuración MQTT, reconectar
        if (mqttChanged) {
            Serial.println("[Web] Config MQTT cambiada, reconectando...");
            mqttClient.stop();
            if (sysConfig->mqtt_enabled && strlen(sysConfig->mqtt_server) > 0) {
                mqttClient.begin(sysConfig);
            }
        }
        sendJsonResponse(200, "{\"success\":true,\"message\":\"Configuracion guardada\"}");
    } else {
        sendJsonError(500, "Error al guardar configuracion");
    }
}

void WebServerManager::handleGetDevices() {
    handleCORS();

    // Leer archivo JSON directamente para evitar cargar todo en RAM
    if (!LittleFS.exists(DEVICES_FILE)) {
        sendJsonResponse(200, "[]");
        return;
    }

    File file = LittleFS.open(DEVICES_FILE, "r");
    if (!file) {
        sendJsonResponse(200, "[]");
        return;
    }

    // Enviar el contenido del archivo directamente
    String content = file.readString();
    file.close();

    if (content.length() == 0) {
        sendJsonResponse(200, "[]");
        return;
    }

    sendJsonResponse(200, content);
}

void WebServerManager::handleAddDevice() {
    handleCORS();
    if (!checkAuth()) return;

    if (!server->hasArg("plain")) {
        sendJsonError(400, "No data received");
        return;
    }

    String body = server->arg("plain");
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        sendJsonError(400, "Invalid JSON");
        return;
    }

    SavedDevice device;
    memset(&device, 0, sizeof(device));

    String uuid = storage.generateUUID();
    strlcpy(device.id, uuid.c_str(), sizeof(device.id));
    strlcpy(device.name, doc["name"] | "Nuevo dispositivo", sizeof(device.name));
    device.type = (DeviceType)(doc["type"].as<int>());
    strlcpy(device.room, doc["room"] | "", sizeof(device.room));
    device.enabled = true;

    if (device.type == DEVICE_CURTAIN_SOMFY) {
        device.somfy.address = doc["somfy_address"] | 0;
        device.somfy.rollingCode = doc["somfy_rolling_code"] | 0;
    }

    if (device.type == DEVICE_CURTAIN_DOOYA_BIDIR) {
        device.dooyaBidir.deviceId = doc["dooya_device_id"] | 0;
        device.dooyaBidir.unitCode = doc["dooya_unit_code"] | 1;
    }

    if (device.type == DEVICE_CURTAIN_AOK) {
        device.aok.remoteId = doc["aok_remote_id"] | 0;
        // Channel 0 is valid (group control), so check if key exists before defaulting
        device.aok.channel = doc.containsKey("aok_channel") ? (uint8_t)doc["aok_channel"].as<int>() : 1;
    }

    if (storage.addDevice(&device)) {
        StaticJsonDocument<256> response;
        response["success"] = true;
        response["id"] = device.id;
        response["message"] = "Dispositivo agregado";
        String responseStr;
        serializeJson(response, responseStr);
        sendJsonResponse(200, responseStr);
    } else {
        sendJsonError(500, "Error al agregar dispositivo");
    }
}

void WebServerManager::handleUpdateDevice() {
    handleCORS();
    if (!checkAuth()) return;

    if (!server->hasArg("plain")) {
        sendJsonError(400, "No data received");
        return;
    }

    String body = server->arg("plain");
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        sendJsonError(400, "Invalid JSON");
        return;
    }

    const char* id = doc["id"] | "";
    if (strlen(id) == 0) {
        sendJsonError(400, "Device ID required");
        return;
    }

    SavedDevice device;
    if (!storage.getDevice(id, &device)) {
        sendJsonError(404, "Device not found");
        return;
    }

    if (doc.containsKey("name")) {
        strlcpy(device.name, doc["name"], sizeof(device.name));
    }
    if (doc.containsKey("type")) device.type = (DeviceType)(doc["type"].as<int>());
    if (doc.containsKey("room")) {
        strlcpy(device.room, doc["room"], sizeof(device.room));
    }
    if (doc.containsKey("enabled")) device.enabled = doc["enabled"];

    if (doc.containsKey("somfy_address")) device.somfy.address = doc["somfy_address"];
    if (doc.containsKey("somfy_rolling_code")) device.somfy.rollingCode = doc["somfy_rolling_code"];
    if (doc.containsKey("dooya_device_id")) device.dooyaBidir.deviceId = doc["dooya_device_id"];
    if (doc.containsKey("dooya_unit_code")) device.dooyaBidir.unitCode = doc["dooya_unit_code"];
    if (doc.containsKey("aok_remote_id")) device.aok.remoteId = doc["aok_remote_id"];
    if (doc.containsKey("aok_channel")) device.aok.channel = doc["aok_channel"];

    if (storage.updateDevice(id, &device)) {
        sendJsonResponse(200, "{\"success\":true,\"message\":\"Dispositivo actualizado\"}");
    } else {
        sendJsonError(500, "Error al actualizar dispositivo");
    }
}

void WebServerManager::handleDeleteDevice() {
    handleCORS();
    if (!checkAuth()) return;

    String id = server->arg("id");
    if (id.length() == 0) {
        sendJsonError(400, "Device ID required");
        return;
    }

    if (storage.deleteDevice(id.c_str())) {
        sendJsonResponse(200, "{\"success\":true,\"message\":\"Dispositivo eliminado\"}");
    } else {
        sendJsonError(500, "Error al eliminar dispositivo");
    }
}

void WebServerManager::handleTransmitSignal() {
    handleCORS();

    String deviceId = server->arg("id");
    int signalIndex = server->arg("signal").toInt();

    Serial.printf("[Web] Transmit request: device=%s, signal=%d\n", deviceId.c_str(), signalIndex);

    if (deviceId.length() == 0) {
        sendJsonError(400, "Device ID required");
        return;
    }

    SavedDevice device;
    if (!storage.getDevice(deviceId.c_str(), &device)) {
        Serial.printf("[Web] Device not found: %s\n", deviceId.c_str());
        sendJsonError(404, "Device not found");
        return;
    }

    Serial.printf("[Web] Device found: %s, type=%d, signalCount=%d\n",
                  device.name, device.type, device.signalCount);

    // Somfy RTS
    if (device.type == DEVICE_CURTAIN_SOMFY) {
        // Verificar que tenga dirección configurada
        if (device.somfy.address == 0) {
            sendJsonError(400, "Direccion Somfy no configurada. Elimina y crea el dispositivo con una direccion valida.");
            return;
        }

        uint8_t cmd = SOMFY_CMD_MY;
        if (signalIndex == 0) cmd = SOMFY_CMD_UP;
        else if (signalIndex == 1) cmd = SOMFY_CMD_DOWN;
        else if (signalIndex == 2) cmd = SOMFY_CMD_MY;
        else if (signalIndex == 3) cmd = SOMFY_CMD_PROG;

        somfyRTS.setRemote(&device.somfy);
        bool success = somfyRTS.sendCommand(cmd);

        if (success) {
            device.somfy.rollingCode++;
            storage.updateSomfyRollingCode(deviceId.c_str(), device.somfy.rollingCode);
            sendJsonResponse(200, "{\"success\":true,\"message\":\"Comando Somfy enviado\"}");
        } else {
            sendJsonError(500, "Error al enviar comando Somfy");
        }
        return;
    }

    // Dooya Bidir
    if (device.type == DEVICE_CURTAIN_DOOYA_BIDIR) {
        // Verificar que tenga Device ID configurado
        if (device.dooyaBidir.deviceId == 0) {
            sendJsonError(400, "Device ID no configurado. Elimina y crea el dispositivo con un ID valido.");
            return;
        }

        uint8_t cmd = DOOYA_BIDIR_CMD_STOP;
        if (signalIndex == 0) cmd = DOOYA_BIDIR_CMD_UP;
        else if (signalIndex == 1) cmd = DOOYA_BIDIR_CMD_DOWN;
        else if (signalIndex == 2) cmd = DOOYA_BIDIR_CMD_STOP;
        else if (signalIndex == 3) cmd = DOOYA_BIDIR_CMD_PROG;

        dooyaBidir.setRemote(&device.dooyaBidir);
        bool success = dooyaBidir.sendCommand(cmd);

        if (success) {
            sendJsonResponse(200, "{\"success\":true,\"message\":\"Comando Dooya enviado\"}");
        } else {
            sendJsonError(500, "Error al enviar comando Dooya");
        }
        return;
    }

    // A-OK AC114 (protocolo específico)
    if (device.type == DEVICE_CURTAIN_AOK) {
        Serial.println("[Web] Enviando comando A-OK");
        if (device.aok.remoteId == 0) {
            sendJsonError(400, "Remote ID A-OK no configurado. Elimina y crea el dispositivo con un ID valido.");
            return;
        }

        aokProtocol.setRemoteId(device.aok.remoteId);
        aokProtocol.setChannel(device.aok.channel);

        uint8_t cmd = AOK_CMD_STOP;
        if (signalIndex == 0) cmd = AOK_CMD_UP;
        else if (signalIndex == 1) cmd = AOK_CMD_DOWN;
        else if (signalIndex == 2) cmd = AOK_CMD_STOP;
        else if (signalIndex == 3) cmd = AOK_CMD_PROGRAM;

        bool success = aokProtocol.sendCommand(cmd);

        if (success) {
            sendJsonResponse(200, "{\"success\":true,\"message\":\"Comando A-OK enviado\"}");
        } else {
            sendJsonError(500, "Error al enviar comando A-OK");
        }
        return;
    }

    // Generic signals
    if (signalIndex < 0 || signalIndex >= 4) {
        sendJsonError(400, "Invalid signal index");
        return;
    }

    // Verificar que la señal exista y sea válida
    if (device.signals[signalIndex].length == 0 || !device.signals[signalIndex].valid) {
        Serial.printf("[Web] Signal %d: length=%d, valid=%d\n",
                      signalIndex, device.signals[signalIndex].length,
                      device.signals[signalIndex].valid);
        sendJsonError(404, "Senal no encontrada o invalida");
        return;
    }

    // Verificar que CC1101 esté conectado
    if (!rfModule.isConnected()) {
        Serial.println("[Web] CC1101 no conectado, intentando reiniciar...");
        rfModule.begin();
        if (!rfModule.isConnected()) {
            sendJsonError(500, "CC1101 no disponible");
            return;
        }
    }

    // Configurar frecuencia y modulación de la señal guardada
    rfModule.setFrequency(device.signals[signalIndex].frequency);
    rfModule.setModulation(device.signals[signalIndex].modulation);

    // Use signal's repeatCount, default to RF_REPEAT_TRANSMIT if not set
    int repeats = device.signals[signalIndex].repeatCount > 0 ?
                  device.signals[signalIndex].repeatCount : RF_REPEAT_TRANSMIT;

    Serial.printf("[Web] Transmitiendo señal %d: freq=%.2f, mod=%d, len=%d, repeats=%d\n",
                  signalIndex, device.signals[signalIndex].frequency,
                  device.signals[signalIndex].modulation,
                  device.signals[signalIndex].length, repeats);

    if (rfModule.transmitRaw(device.signals[signalIndex].data, device.signals[signalIndex].length, repeats, device.signals[signalIndex].inverted)) {
        if (onSignalTransmit) {
            onSignalTransmit(deviceId.c_str(), signalIndex);
        }
        sendJsonResponse(200, "{\"success\":true,\"message\":\"Senal transmitida\"}");
    } else {
        sendJsonError(500, "Error al transmitir senal");
    }
}

void WebServerManager::handleStartCapture() {
    handleCORS();

    float frequency = server->arg("frequency").toFloat();
    if (frequency <= 0) {
        frequency = sysConfig->default_frequency;
    }

    int modulation = server->arg("modulation").toInt();
    // Si no se especifica o es inválido, usar ASK/OOK (2)
    if (modulation < 0 || modulation > 4) {
        modulation = 2;
    }

    rfModule.setFrequency(frequency);
    rfModule.setModulation(modulation);

    Serial.printf("[Web] Iniciando captura: freq=%.2f MHz, mod=%d\n", frequency, modulation);

    if (rfModule.startCapture()) {
        captureInProgress = true;
        StaticJsonDocument<128> doc;
        doc["success"] = true;
        doc["frequency"] = frequency;
        doc["modulation"] = modulation;
        doc["message"] = "Captura iniciada";
        String response;
        serializeJson(doc, response);
        sendJsonResponse(200, response);
    } else {
        sendJsonError(500, "Error al iniciar captura");
    }
}

void WebServerManager::handleStopCapture() {
    handleCORS();

    rfModule.stopCapture();
    captureInProgress = false;
    sendJsonResponse(200, "{\"success\":true,\"message\":\"Captura detenida\"}");
}

void WebServerManager::handleGetCapture() {
    handleCORS();

    unsigned long timeout = server->arg("timeout").toInt();
    if (timeout <= 0) timeout = 10000;

    unsigned long startTime = millis();
    RFSignal signal;

    while (millis() - startTime < timeout) {
        if (rfModule.captureSignal(&signal, timeout - (millis() - startTime))) {
            // Guardar en tempCapturedSignal para decode-aok
            if (tempCapturedSignal) {
                memcpy(tempCapturedSignal, &signal, sizeof(RFSignal));
                Serial.printf("[Web] Señal guardada en tempCapturedSignal: %d bytes\n", signal.length);
            }

            DynamicJsonDocument doc(2048);  // Use heap for large signal data
            doc["success"] = true;
            doc["valid"] = true;
            doc["frequency"] = round(signal.frequency * 100) / 100.0;  // Round to 2 decimals
            doc["length"] = signal.length;
            doc["modulation"] = signal.modulation;
            doc["repeatCount"] = RF_REPEAT_TRANSMIT;  // Default repeat count

            // Incluir todos los datos capturados
            String hexData = "";
            hexData.reserve(signal.length * 2 + 1);
            for (uint16_t i = 0; i < signal.length; i++) {
                if (signal.data[i] < 16) hexData += "0";
                hexData += String(signal.data[i], HEX);
            }
            doc["data"] = hexData;

            String response;
            serializeJson(doc, response);
            sendJsonResponse(200, response);
            return;
        }
        // Si captureSignal retorna false, salir del loop
        break;
    }

    StaticJsonDocument<128> doc;
    doc["success"] = false;
    doc["valid"] = false;
    doc["message"] = "No signal detected";
    String response;
    serializeJson(doc, response);
    sendJsonResponse(408, response);
}

void WebServerManager::handleSaveSignal() {
    handleCORS();

    if (!server->hasArg("plain")) {
        sendJsonError(400, "No data received");
        return;
    }

    String body = server->arg("plain");
    DynamicJsonDocument doc(2048);  // Use heap instead of stack
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        Serial.printf("[Web] Save signal JSON error: %s\n", error.c_str());
        sendJsonError(400, "Invalid JSON");
        return;
    }

    const char* deviceId = doc["deviceId"] | "";
    int signalIndexInt = doc["signalIndex"] | -1;  // Use int to detect missing value
    const char* signalName = doc["signalName"] | "Signal";

    Serial.printf("[Web] Save signal: deviceId=%s, index=%d, name=%s\n",
                  deviceId, signalIndexInt, signalName);

    if (strlen(deviceId) == 0) {
        sendJsonError(400, "Device ID required");
        return;
    }

    if (signalIndexInt < 0 || signalIndexInt > 3) {
        Serial.printf("[Web] Invalid signal index: %d\n", signalIndexInt);
        sendJsonError(400, "Invalid signal index");
        return;
    }

    uint8_t signalIndex = (uint8_t)signalIndexInt;

    RFSignal signal;
    memset(&signal, 0, sizeof(signal));

    signal.valid = true;  // Mark signal as valid!
    signal.frequency = doc["frequency"] | 433.92f;
    signal.modulation = doc["modulation"] | 2;
    signal.repeatCount = doc["repeatCount"] | RF_REPEAT_TRANSMIT;

    // Clamp repeat count between 1-20
    if (signal.repeatCount < 1) signal.repeatCount = 1;
    if (signal.repeatCount > 20) signal.repeatCount = 20;

    String hexData = doc["data"] | "";
    signal.length = hexData.length() / 2;
    if (signal.length > RF_MAX_SIGNAL_LENGTH) {
        signal.length = RF_MAX_SIGNAL_LENGTH;
    }

    for (uint16_t i = 0; i < signal.length; i++) {
        String byteStr = hexData.substring(i * 2, i * 2 + 2);
        signal.data[i] = strtol(byteStr.c_str(), NULL, 16);
    }

    Serial.printf("[Web] Saving signal: valid=%d, freq=%.2f, mod=%d, len=%d, repeat=%d\n",
                  signal.valid, signal.frequency, signal.modulation, signal.length, signal.repeatCount);

    if (storage.saveSignalToDevice(deviceId, signalIndex, &signal, signalName)) {
        sendJsonResponse(200, "{\"success\":true,\"message\":\"Senal guardada\"}");
    } else {
        sendJsonError(500, "Error al guardar senal");
    }
}

void WebServerManager::handleDeleteSignal() {
    handleCORS();

    if (!server->hasArg("plain")) {
        sendJsonError(400, "No data received");
        return;
    }

    String body = server->arg("plain");
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        sendJsonError(400, "Invalid JSON");
        return;
    }

    const char* deviceId = doc["deviceId"] | "";
    int signalIndex = doc["signalIndex"] | -1;

    Serial.printf("[Web] Delete signal: device=%s, index=%d\n", deviceId, signalIndex);

    if (strlen(deviceId) == 0 || signalIndex < 0 || signalIndex > 3) {
        sendJsonError(400, "Invalid device ID or signal index");
        return;
    }

    if (storage.deleteSignalFromDevice(deviceId, signalIndex)) {
        sendJsonResponse(200, "{\"success\":true,\"message\":\"Senal eliminada\"}");
    } else {
        sendJsonError(500, "Error al eliminar senal");
    }
}

void WebServerManager::handleTestSignal() {
    handleCORS();
    Serial.println("[Web] handleTestSignal called");

    if (!server->hasArg("plain")) {
        Serial.println("[Web] No data received in test signal");
        sendJsonError(400, "No data received");
        return;
    }

    String body = server->arg("plain");
    Serial.printf("[Web] Test signal body length: %d\n", body.length());

    DynamicJsonDocument doc(2048);  // Use heap instead of stack
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        Serial.printf("[Web] Test signal JSON error: %s\n", error.c_str());
        sendJsonError(400, "Invalid JSON");
        return;
    }

    String hexData = doc["data"] | "";
    float frequency = doc["frequency"] | 433.92f;
    int modulation = doc["modulation"] | 2;
    int repeatCount = doc["repeatCount"] | 3;  // Default 3 for test

    // Clamp repeat count
    if (repeatCount < 1) repeatCount = 1;
    if (repeatCount > 20) repeatCount = 20;

    Serial.printf("[Web] Test signal: freq=%.2f, mod=%d, data_len=%d, repeat=%d\n", frequency, modulation, hexData.length(), repeatCount);

    if (hexData.length() < 4) {
        sendJsonError(400, "No signal data");
        return;
    }

    // Convert hex to bytes
    uint16_t length = hexData.length() / 2;
    if (length > RF_MAX_SIGNAL_LENGTH) {
        length = RF_MAX_SIGNAL_LENGTH;
    }

    uint8_t* signalData = new uint8_t[length];
    for (uint16_t i = 0; i < length; i++) {
        String byteStr = hexData.substring(i * 2, i * 2 + 2);
        signalData[i] = strtol(byteStr.c_str(), NULL, 16);
    }

    // Set frequency and modulation
    Serial.printf("[Web] Setting freq=%.2f, mod=%d\n", frequency, modulation);
    rfModule.setFrequency(frequency);
    rfModule.setModulation(modulation);

    // Transmit
    Serial.printf("[Web] Transmitting %d bytes, %d times...\n", length, repeatCount);
    bool success = rfModule.transmitRaw(signalData, length, repeatCount);
    delete[] signalData;

    Serial.printf("[Web] Transmit result: %s\n", success ? "OK" : "FAILED");

    if (success) {
        sendJsonResponse(200, "{\"success\":true,\"message\":\"Senal de prueba transmitida\"}");
    } else {
        sendJsonError(500, "Error al transmitir");
    }
}

void WebServerManager::handleUpdateSignalRepeat() {
    handleCORS();

    if (!server->hasArg("plain")) {
        sendJsonError(400, "No data received");
        return;
    }

    String body = server->arg("plain");
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        sendJsonError(400, "Invalid JSON");
        return;
    }

    const char* deviceId = doc["deviceId"] | "";
    int signalIndex = doc["signalIndex"] | -1;
    int repeatCount = doc["repeatCount"] | 5;

    if (strlen(deviceId) == 0 || signalIndex < 0 || signalIndex > 3) {
        sendJsonError(400, "Invalid device ID or signal index");
        return;
    }

    // Clamp repeat count
    if (repeatCount < 1) repeatCount = 1;
    if (repeatCount > 20) repeatCount = 20;

    // Update repeat count in storage
    if (storage.updateSignalRepeatCount(deviceId, signalIndex, repeatCount)) {
        Serial.printf("[Web] Signal repeat updated: device=%s, signal=%d, repeat=%d\n",
                      deviceId, signalIndex, repeatCount);
        sendJsonResponse(200, "{\"success\":true}");
    } else {
        sendJsonError(500, "Error updating repeat count");
    }
}

void WebServerManager::handleUpdateSignalInvert() {
    handleCORS();

    if (!server->hasArg("plain")) {
        sendJsonError(400, "No data received");
        return;
    }

    String body = server->arg("plain");
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        sendJsonError(400, "Invalid JSON");
        return;
    }

    const char* deviceId = doc["deviceId"] | "";
    int signalIndex = doc["signalIndex"] | -1;
    bool inverted = doc["inverted"] | false;

    if (strlen(deviceId) == 0 || signalIndex < 0 || signalIndex > 3) {
        sendJsonError(400, "Invalid device ID or signal index");
        return;
    }

    // Update inverted flag in storage
    if (storage.updateSignalInverted(deviceId, signalIndex, inverted)) {
        Serial.printf("[Web] Signal invert updated: device=%s, signal=%d, inverted=%s\n",
                      deviceId, signalIndex, inverted ? "YES" : "NO");
        sendJsonResponse(200, "{\"success\":true}");
    } else {
        sendJsonError(500, "Error updating inverted flag");
    }
}

void WebServerManager::handleSetFrequency() {
    handleCORS();

    float frequency = server->arg("freq").toFloat();
    if (frequency <= 0) {
        sendJsonError(400, "Invalid frequency");
        return;
    }

    rfModule.setFrequency(frequency);

    StaticJsonDocument<128> doc;
    doc["success"] = true;
    doc["frequency"] = frequency;
    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void WebServerManager::handleScanFrequency() {
    handleCORS();

    float commonFreqs[] = {433.92, 315.0, 868.0, 433.42};
    float detectedFreq = rfModule.scanForSignal(commonFreqs, 4);

    StaticJsonDocument<128> doc;
    doc["success"] = detectedFreq > 0;
    doc["frequency"] = detectedFreq;
    doc["message"] = detectedFreq > 0 ? "Frecuencia detectada" : "No se detecto senal";

    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void WebServerManager::handleIdentifySignal() {
    handleCORS();

    Serial.println("[Web] Iniciando identificación de señal...");

    // Lista completa de frecuencias comunes a escanear
    float frequencies[] = {
        300.00,   // Frecuencia baja
        303.87,   // Garajes USA
        310.00,   // Algunos controles
        315.00,   // USA/Asia estándar
        390.00,   // Algunos garajes
        418.00,   // Alarmas
        433.00,   // Europa base
        433.42,   // Variante común
        433.92,   // Europa/Latam estándar (más común)
        434.00,   // Variante común
        868.00,   // Europa alta
        915.00    // USA ISM
    };
    int freqCount = 12;

    // Modulaciones a probar (ASK/OOK primero porque es la más común)
    int modulations[] = {2, 0, 1};  // ASK/OOK, 2-FSK, GFSK
    const char* modNames[] = {"ASK/OOK", "2-FSK", "GFSK"};
    int modCount = 3;

    DynamicJsonDocument doc(2048);
    doc["success"] = false;

    float detectedFreq = 0;
    int detectedMod = 2;
    int maxRSSI = -120;
    RFSignal signal;
    bool signalCaptured = false;

    // Fase 1: Escanear TODAS las frecuencias con ASK/OOK primero (más común)
    Serial.println("[Web] Fase 1: Escaneando todas las frecuencias...");

    for (int m = 0; m < modCount && detectedFreq == 0; m++) {
        rfModule.setModulation(modulations[m]);
        Serial.printf("[Web] Probando modulación: %s\n", modNames[m]);

        for (int f = 0; f < freqCount; f++) {
            rfModule.setFrequency(frequencies[f]);
            ELECHOUSE_cc1101.SetRx();

            Serial.printf("[Web] Escaneando %.2f MHz (%d/%d)...\n", frequencies[f], f + 1, freqCount);

            // Escanear por 1 segundo en cada frecuencia
            unsigned long scanStart = millis();
            while (millis() - scanStart < 1000) {
                int rssi = rfModule.getRSSI();
                if (rssi > maxRSSI && rssi > -55) {
                    maxRSSI = rssi;
                    detectedFreq = frequencies[f];
                    detectedMod = modulations[m];
                    Serial.printf("[Web] *** SEÑAL DETECTADA: %.2f MHz, %s, RSSI: %d ***\n",
                                  frequencies[f], modNames[m], rssi);
                }
                delay(15);
            }
        }
    }

    // Fase 2: Si encontramos señal, intentar capturarla
    if (detectedFreq > 0) {
        Serial.printf("[Web] Fase 2: Capturando en %.2f MHz, %s...\n",
                      detectedFreq, modNames[detectedMod == 2 ? 0 : (detectedMod == 0 ? 1 : 2)]);

        rfModule.setFrequency(detectedFreq);
        rfModule.setModulation(detectedMod);

        if (rfModule.captureSignal(&signal, 10000)) {
            signalCaptured = true;
            Serial.println("[Web] Señal capturada exitosamente");
        }
    }

    // Construir respuesta
    if (signalCaptured && signal.valid) {
        doc["success"] = true;
        doc["frequency"] = round(signal.frequency * 100) / 100.0;
        doc["modulation"] = signal.modulation;
        doc["rssi"] = maxRSSI;
        doc["length"] = signal.length;

        // Nombres de modulación
        const char* modName = "Desconocida";
        switch (signal.modulation) {
            case 0: modName = "2-FSK"; break;
            case 1: modName = "GFSK"; break;
            case 2: modName = "ASK/OOK"; break;
            case 3: modName = "4-FSK"; break;
            case 4: modName = "MSK"; break;
        }
        doc["modulation_name"] = modName;

        // Detectar protocolo
        RFProtocol protocol = rfModule.detectProtocol(&signal);
        doc["protocol"] = rfModule.getProtocolName(protocol);
        doc["protocol_id"] = (int)protocol;

        // Incluir análisis
        String analysis = rfModule.analyzeSignal(&signal);
        doc["analysis"] = analysis;

        // Datos de la señal para poder usarla
        String hexData = "";
        hexData.reserve(signal.length * 2 + 1);
        for (uint16_t i = 0; i < signal.length; i++) {
            if (signal.data[i] < 16) hexData += "0";
            hexData += String(signal.data[i], HEX);
        }
        doc["data"] = hexData;

        // Recomendaciones
        doc["recommendations"] = rfModule.getRecommendedSettings(&signal);

        doc["message"] = "Señal identificada correctamente";
    } else if (detectedFreq > 0) {
        doc["success"] = false;
        doc["frequency"] = detectedFreq;
        doc["rssi"] = maxRSSI;
        doc["message"] = "Se detectó actividad RF pero no se pudo capturar la señal. Intente mantener presionado el botón del control.";
    } else {
        doc["success"] = false;
        doc["message"] = "No se detectó ninguna señal RF. Asegúrese de presionar el botón del control cerca del receptor.";
    }

    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void WebServerManager::handleDecodeAOK() {
    handleCORS();
    if (!checkAuth()) return;

    Serial.println("[Web] Decodificando señal A-OK...");
    Serial.flush();

    // Debug: mostrar estado de tempCapturedSignal
    Serial.printf("[Web] tempCapturedSignal=%p\n", tempCapturedSignal);
    if (tempCapturedSignal) {
        Serial.printf("[Web] valid=%d, length=%d\n", tempCapturedSignal->valid, tempCapturedSignal->length);
    }
    Serial.flush();

    // Check if we have a captured signal
    if (!tempCapturedSignal || !tempCapturedSignal->valid || tempCapturedSignal->length < 20) {
        Serial.println("[Web] ERROR: No hay señal válida en tempCapturedSignal");
        sendJsonError(400, "No hay señal capturada. Primero capture una señal.");
        return;
    }

    Serial.printf("[Web] >>> Llamando learnFromCapture: data=%p, len=%d <<<\n",
                  tempCapturedSignal->data, tempCapturedSignal->length);
    Serial.flush();

    // Try to decode as A-OK
    bool success = aokProtocol.learnFromCapture(tempCapturedSignal->data, tempCapturedSignal->length);

    Serial.printf("[Web] >>> learnFromCapture retorno: %s <<<\n", success ? "true" : "false");
    Serial.flush();

    DynamicJsonDocument doc(512);

    if (success) {
        uint32_t extractedId = aokProtocol.getRemoteId();
        uint8_t extractedChannel = aokProtocol.getChannel();

        doc["success"] = true;
        doc["protocol"] = "A-OK AC114";
        doc["remote_id"] = extractedId;

        // Format as hex string
        char hexId[7];
        snprintf(hexId, sizeof(hexId), "%06X", extractedId);
        doc["remote_id_hex"] = hexId;

        doc["channel"] = extractedChannel;
        doc["message"] = "Señal A-OK decodificada correctamente!";

        Serial.printf("[Web] A-OK decodificado: ID=0x%06X, Canal=%d\n", extractedId, extractedChannel);
    } else {
        doc["success"] = false;
        doc["message"] = "No se pudo decodificar como señal A-OK. Puede ser otro protocolo.";
    }

    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void WebServerManager::handleBackup() {
    handleCORS();

    String backup = storage.createBackup();
    server->sendHeader("Content-Disposition", "attachment; filename=rf_controller_backup.json");
    server->send(200, "application/json", backup);
}

void WebServerManager::handleRestore() {
    handleCORS();
    if (!checkAuth()) return;

    if (!server->hasArg("plain")) {
        sendJsonError(400, "No data received");
        return;
    }

    String body = server->arg("plain");

    if (storage.restoreBackup(body)) {
        sendJsonResponse(200, "{\"success\":true,\"message\":\"Backup restaurado. Reiniciando...\"}");
        delay(1000);
        ESP.restart();
    } else {
        sendJsonError(500, "Error al restaurar backup");
    }
}

void WebServerManager::handleWiFiScan() {
    handleCORS();

    Serial.println("[WiFi] Iniciando escaneo de redes...");

    // Limpiar escaneos previos
    WiFi.scanDelete();

    // Desconectar STA si estaba conectado (para mejor escaneo)
    WiFi.disconnect(false);
    delay(100);

    // Asegurar modo AP+STA
    WiFi.mode(WIFI_AP_STA);
    delay(100);

    // Escaneo SINCRONO (bloquea pero es más confiable)
    Serial.println("[WiFi] Escaneando...");
    int n = WiFi.scanNetworks(false, false, false, 300);  // sync, no hidden, no passive, 300ms por canal

    Serial.printf("[WiFi] Escaneo completado: %d redes\n", n);

    DynamicJsonDocument doc(2048);
    JsonArray networks = doc.createNestedArray("networks");

    if (n > 0) {
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() > 0) {
                JsonObject network = networks.createNestedObject();
                network["ssid"] = ssid;
                network["rssi"] = WiFi.RSSI(i);
                network["encrypted"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
                Serial.printf("[WiFi]   - %s (%d dBm)\n", ssid.c_str(), WiFi.RSSI(i));
            }
        }
    } else if (n == 0) {
        Serial.println("[WiFi] No se encontraron redes");
    } else {
        Serial.printf("[WiFi] Error en escaneo: %d\n", n);
    }

    WiFi.scanDelete();

    String response;
    serializeJson(doc, response);
    sendJsonResponse(200, response);
}

void WebServerManager::handleWiFiConnect() {
    handleCORS();
    if (!checkAuth()) return;

    if (!server->hasArg("plain")) {
        sendJsonError(400, "No data received");
        return;
    }

    String body = server->arg("plain");
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        sendJsonError(400, "Invalid JSON");
        return;
    }

    const char* ssid = doc["ssid"] | "";
    const char* password = doc["password"] | "";

    if (strlen(ssid) == 0) {
        sendJsonError(400, "SSID required");
        return;
    }

    strlcpy(sysConfig->wifi_ssid, ssid, sizeof(sysConfig->wifi_ssid));
    strlcpy(sysConfig->wifi_password, password, sizeof(sysConfig->wifi_password));
    sysConfig->wifi_configured = true;
    storage.saveConfig(sysConfig);

    sendJsonResponse(200, "{\"success\":true,\"message\":\"Conectando a WiFi... Reiniciando...\"}");

    delay(1000);
    ESP.restart();
}

void WebServerManager::handleMqttRediscover() {
    handleCORS();

    if (!mqttClient.isConnected()) {
        sendJsonError(400, "MQTT no conectado");
        return;
    }

    mqttClient.publishDiscovery();
    sendJsonResponse(200, "{\"success\":true,\"message\":\"Discovery publicado\"}");
}

void WebServerManager::handleReboot() {
    handleCORS();
    if (!checkAuth()) return;

    sendJsonResponse(200, "{\"success\":true,\"message\":\"Reiniciando...\"}");
    delay(1000);
    ESP.restart();
}

void WebServerManager::handleFactoryReset() {
    handleCORS();
    if (!checkAuth()) return;

    // Solo borrar datos de usuario (config.json y devices.json)
    // NO formatear todo el sistema de archivos para preservar archivos web
    storage.clearUserData();
    sendJsonResponse(200, "{\"success\":true,\"message\":\"Configuracion borrada. Reiniciando...\"}");
    delay(1000);
    ESP.restart();
}

String WebServerManager::getContentType(const String& filename) {
    if (filename.endsWith(".html")) return "text/html";
    if (filename.endsWith(".css")) return "text/css";
    if (filename.endsWith(".js")) return "application/javascript";
    if (filename.endsWith(".json")) return "application/json";
    if (filename.endsWith(".png")) return "image/png";
    if (filename.endsWith(".ico")) return "image/x-icon";
    if (filename.endsWith(".svg")) return "image/svg+xml";
    return "text/plain";
}

void WebServerManager::sendJsonResponse(int code, const String& json) {
    handleCORS();
    server->send(code, "application/json", json);
}

void WebServerManager::sendJsonError(int code, const String& message) {
    handleCORS();
    String json = "{\"success\":false,\"error\":\"" + message + "\"}";
    server->send(code, "application/json", json);
}
