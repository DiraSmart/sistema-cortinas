#include "WebServer.h"

WebServerManager webServer;

WebServerManager::WebServerManager() : server(80), ws("/ws") {
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

    // Intentar conectar a WiFi si está configurado
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

    // Configurar rutas y WebSocket
    setupRoutes();
    setupWebSocket();

    // Iniciar servidor
    server.begin();
    Serial.printf("[Web] Servidor iniciado en http://%s\n", getIPAddress().c_str());

    return true;
}

void WebServerManager::stop() {
    server.end();
    ws.closeAll();
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
    Serial.printf("[Web] Conectando a WiFi: %s...\n", ssid);

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid, password);

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
    ws.cleanupClients();

    // Reconectar WiFi si se perdió la conexión
    if (sysConfig->wifi_configured && !isConnected() && !apMode) {
        if (millis() - lastReconnectAttempt > 30000) {
            lastReconnectAttempt = millis();
            Serial.println("[Web] Intentando reconectar WiFi...");
            connectWiFi(sysConfig->wifi_ssid, sysConfig->wifi_password);
        }
    }

    // Verificar estado de captura
    if (captureInProgress && !rfModule.isCapturing()) {
        captureInProgress = false;
    }
}

void WebServerManager::setSignalCapturedCallback(void (*callback)(const RFSignal*)) {
    onSignalCaptured = callback;
}

void WebServerManager::setSignalTransmitCallback(void (*callback)(const char* deviceId, uint8_t signalIndex)) {
    onSignalTransmit = callback;
}

void WebServerManager::setupRoutes() {
    // Servir archivos estáticos desde LittleFS
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // Página principal
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleRoot(request);
    });

    // ========== API REST ==========

    // Estado del sistema
    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetStatus(request);
    });

    // Configuración
    server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetConfig(request);
    });

    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* request) {},
        NULL, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        handleSaveConfig(request, data, len);
    });

    // Dispositivos
    server.on("/api/devices", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetDevices(request);
    });

    server.on("/api/devices", HTTP_POST, [](AsyncWebServerRequest* request) {},
        NULL, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        handleAddDevice(request, data, len);
    });

    server.on("/api/devices/update", HTTP_POST, [](AsyncWebServerRequest* request) {},
        NULL, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        handleUpdateDevice(request, data, len);
    });

    server.on("/api/devices/delete", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleDeleteDevice(request);
    });

    // Señales RF
    server.on("/api/rf/transmit", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleTransmitSignal(request);
    });

    server.on("/api/rf/capture/start", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleStartCapture(request);
    });

    server.on("/api/rf/capture/stop", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleStopCapture(request);
    });

    server.on("/api/rf/capture/get", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetCapture(request);
    });

    server.on("/api/rf/signal/save", HTTP_POST, [](AsyncWebServerRequest* request) {},
        NULL, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        handleSaveSignal(request, data, len);
    });

    server.on("/api/rf/frequency", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleSetFrequency(request);
    });

    server.on("/api/rf/scan", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleScanFrequency(request);
    });

    // Backup/Restore
    server.on("/api/backup", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleBackup(request);
    });

    server.on("/api/restore", HTTP_POST, [](AsyncWebServerRequest* request) {},
        NULL, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        handleRestore(request, data, len);
    });

    // WiFi
    server.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleWiFiScan(request);
    });

    server.on("/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest* request) {},
        NULL, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        handleWiFiConnect(request, data, len);
    });

    // Sistema
    server.on("/api/reboot", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleReboot(request);
    });

    server.on("/api/factory-reset", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleFactoryReset(request);
    });

    // 404
    server.onNotFound([this](AsyncWebServerRequest* request) {
        handleNotFound(request);
    });
}

void WebServerManager::setupWebSocket() {
    ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
        onWsEvent(server, client, type, arg, data, len);
    });

    server.addHandler(&ws);
}

void WebServerManager::handleRoot(AsyncWebServerRequest* request) {
    if (LittleFS.exists("/index.html")) {
        request->send(LittleFS, "/index.html", "text/html");
    } else {
        // Página de emergencia si no hay archivos
        String html = "<!DOCTYPE html><html><head><title>RF Controller</title>";
        html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
        html += "</head><body><h1>RF Controller</h1>";
        html += "<p>Archivos web no encontrados. Suba los archivos a LittleFS.</p>";
        html += "<p>IP: " + getIPAddress() + "</p>";
        html += "</body></html>";
        request->send(200, "text/html", html);
    }
}

void WebServerManager::handleNotFound(AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "Not found");
}

void WebServerManager::handleGetStatus(AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(1024);

    doc["wifi_connected"] = isConnected();
    doc["wifi_ssid"] = getSSID();
    doc["wifi_rssi"] = getRSSI();
    doc["ip"] = getIPAddress();
    doc["ap_mode"] = apMode;
    doc["rf_connected"] = rfModule.isConnected();
    doc["rf_frequency"] = rfModule.getFrequency();
    doc["rf_rssi"] = rfModule.getRSSI();
    doc["capturing"] = rfModule.isCapturing();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;

    String response;
    serializeJson(doc, response);
    sendJsonResponse(request, 200, response);
}

void WebServerManager::handleGetConfig(AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(2048);

    doc["wifi_ssid"] = sysConfig->wifi_ssid;
    doc["wifi_configured"] = sysConfig->wifi_configured;
    doc["mqtt_server"] = sysConfig->mqtt_server;
    doc["mqtt_port"] = sysConfig->mqtt_port;
    doc["mqtt_user"] = sysConfig->mqtt_user;
    doc["mqtt_enabled"] = sysConfig->mqtt_enabled;
    doc["mqtt_discovery"] = sysConfig->mqtt_discovery;
    doc["mqtt_client_id"] = sysConfig->mqtt_client_id;
    doc["timezone"] = sysConfig->timezone;
    doc["ntp_server"] = sysConfig->ntp_server;
    doc["utc_offset"] = sysConfig->utc_offset;
    doc["dst_enabled"] = sysConfig->dst_enabled;
    doc["default_frequency"] = sysConfig->default_frequency;
    doc["default_modulation"] = sysConfig->default_modulation;
    doc["device_name"] = sysConfig->device_name;
    doc["auto_detect_enabled"] = sysConfig->auto_detect_enabled;

    // Lista de frecuencias disponibles
    JsonArray freqs = doc.createNestedArray("frequencies");
    for (int i = 0; i < RF_FREQUENCIES_COUNT; i++) {
        freqs.add(RF_FREQUENCIES[i]);
    }

    String response;
    serializeJson(doc, response);
    sendJsonResponse(request, 200, response);
}

void WebServerManager::handleSaveConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, (char*)data, len);

    if (error) {
        sendJsonError(request, 400, "JSON inválido");
        return;
    }

    // Actualizar configuración
    if (doc.containsKey("wifi_ssid")) {
        strncpy(sysConfig->wifi_ssid, doc["wifi_ssid"], 63);
    }
    if (doc.containsKey("wifi_password")) {
        strncpy(sysConfig->wifi_password, doc["wifi_password"], 63);
    }
    if (doc.containsKey("mqtt_server")) {
        strncpy(sysConfig->mqtt_server, doc["mqtt_server"], 63);
    }
    if (doc.containsKey("mqtt_port")) {
        sysConfig->mqtt_port = doc["mqtt_port"];
    }
    if (doc.containsKey("mqtt_user")) {
        strncpy(sysConfig->mqtt_user, doc["mqtt_user"], 31);
    }
    if (doc.containsKey("mqtt_password")) {
        strncpy(sysConfig->mqtt_password, doc["mqtt_password"], 63);
    }
    if (doc.containsKey("mqtt_client_id")) {
        strncpy(sysConfig->mqtt_client_id, doc["mqtt_client_id"], 31);
    }
    if (doc.containsKey("mqtt_enabled")) {
        sysConfig->mqtt_enabled = doc["mqtt_enabled"];
    }
    if (doc.containsKey("mqtt_discovery")) {
        sysConfig->mqtt_discovery = doc["mqtt_discovery"];
    }
    if (doc.containsKey("timezone")) {
        strncpy(sysConfig->timezone, doc["timezone"], 63);
    }
    if (doc.containsKey("ntp_server")) {
        strncpy(sysConfig->ntp_server, doc["ntp_server"], 63);
    }
    if (doc.containsKey("utc_offset")) {
        sysConfig->utc_offset = doc["utc_offset"];
    }
    if (doc.containsKey("dst_enabled")) {
        sysConfig->dst_enabled = doc["dst_enabled"];
    }
    if (doc.containsKey("default_frequency")) {
        sysConfig->default_frequency = doc["default_frequency"];
        rfModule.setFrequency(sysConfig->default_frequency);
    }
    if (doc.containsKey("default_modulation")) {
        sysConfig->default_modulation = doc["default_modulation"];
        rfModule.setModulation(sysConfig->default_modulation);
    }
    if (doc.containsKey("device_name")) {
        strncpy(sysConfig->device_name, doc["device_name"], 31);
    }
    if (doc.containsKey("auto_detect_enabled")) {
        sysConfig->auto_detect_enabled = doc["auto_detect_enabled"];
    }

    // Guardar
    if (storage.saveConfig(sysConfig)) {
        sendJsonResponse(request, 200, "{\"success\":true}");
    } else {
        sendJsonError(request, 500, "Error al guardar configuración");
    }
}

void WebServerManager::handleGetDevices(AsyncWebServerRequest* request) {
    SavedDevice devices[MAX_DEVICES];
    uint8_t count = 0;

    storage.loadDevices(devices, &count);

    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    JsonArray arr = doc.to<JsonArray>();

    for (uint8_t i = 0; i < count; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["id"] = devices[i].id;
        obj["name"] = devices[i].name;
        obj["type"] = (int)devices[i].type;
        obj["signalCount"] = devices[i].signalCount;
        obj["enabled"] = devices[i].enabled;
        obj["room"] = devices[i].room;

        JsonArray signals = obj.createNestedArray("signals");
        for (uint8_t j = 0; j < devices[i].signalCount; j++) {
            JsonObject sig = signals.createNestedObject();
            sig["index"] = j;
            sig["name"] = devices[i].signalNames[j];
            sig["valid"] = devices[i].signals[j].valid;
            sig["frequency"] = devices[i].signals[j].frequency;
        }
    }

    String response;
    serializeJson(doc, response);
    sendJsonResponse(request, 200, response);
}

void WebServerManager::handleAddDevice(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, (char*)data, len);

    if (error) {
        sendJsonError(request, 400, "JSON inválido");
        return;
    }

    SavedDevice device;
    memset(&device, 0, sizeof(SavedDevice));

    String uuid = storage.generateUUID();
    strncpy(device.id, uuid.c_str(), 36);
    strncpy(device.name, doc["name"] | "Nuevo dispositivo", 63);
    device.type = (DeviceType)(doc["type"] | DEVICE_UNKNOWN);
    strncpy(device.room, doc["room"] | "", 31);
    device.enabled = true;
    device.signalCount = 0;
    device.createdAt = millis();
    device.lastUsed = 0;

    if (storage.addDevice(&device)) {
        DynamicJsonDocument response(256);
        response["success"] = true;
        response["id"] = device.id;

        String responseStr;
        serializeJson(response, responseStr);
        sendJsonResponse(request, 200, responseStr);
    } else {
        sendJsonError(request, 500, "Error al guardar dispositivo");
    }
}

void WebServerManager::handleUpdateDevice(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, (char*)data, len);

    if (error || !doc.containsKey("id")) {
        sendJsonError(request, 400, "JSON inválido o falta ID");
        return;
    }

    SavedDevice device;
    if (!storage.getDevice(doc["id"], &device)) {
        sendJsonError(request, 404, "Dispositivo no encontrado");
        return;
    }

    if (doc.containsKey("name")) {
        strncpy(device.name, doc["name"], 63);
    }
    if (doc.containsKey("type")) {
        device.type = (DeviceType)(int)doc["type"];
    }
    if (doc.containsKey("room")) {
        strncpy(device.room, doc["room"], 31);
    }
    if (doc.containsKey("enabled")) {
        device.enabled = doc["enabled"];
    }

    if (storage.updateDevice(doc["id"], &device)) {
        sendJsonResponse(request, 200, "{\"success\":true}");
    } else {
        sendJsonError(request, 500, "Error al actualizar dispositivo");
    }
}

void WebServerManager::handleDeleteDevice(AsyncWebServerRequest* request) {
    if (!request->hasParam("id")) {
        sendJsonError(request, 400, "Falta parámetro 'id'");
        return;
    }

    String id = request->getParam("id")->value();

    if (storage.deleteDevice(id.c_str())) {
        sendJsonResponse(request, 200, "{\"success\":true}");
    } else {
        sendJsonError(request, 404, "Dispositivo no encontrado");
    }
}

void WebServerManager::handleTransmitSignal(AsyncWebServerRequest* request) {
    if (!request->hasParam("id") || !request->hasParam("signal")) {
        sendJsonError(request, 400, "Faltan parámetros 'id' y/o 'signal'");
        return;
    }

    String deviceId = request->getParam("id")->value();
    int signalIndex = request->getParam("signal")->value().toInt();

    SavedDevice device;
    if (!storage.getDevice(deviceId.c_str(), &device)) {
        sendJsonError(request, 404, "Dispositivo no encontrado");
        return;
    }

    if (signalIndex < 0 || signalIndex >= device.signalCount) {
        sendJsonError(request, 400, "Índice de señal inválido");
        return;
    }

    if (!device.signals[signalIndex].valid) {
        sendJsonError(request, 400, "Señal no válida");
        return;
    }

    // Transmitir señal
    rfModule.setFrequency(device.signals[signalIndex].frequency);
    rfModule.setModulation(device.signals[signalIndex].modulation);

    if (rfModule.transmitSignal(&device.signals[signalIndex])) {
        // Actualizar último uso
        device.lastUsed = millis();
        storage.updateDevice(deviceId.c_str(), &device);

        sendJsonResponse(request, 200, "{\"success\":true}");

        // Notificar por WebSocket
        broadcastStatus();
    } else {
        sendJsonError(request, 500, "Error al transmitir señal");
    }
}

void WebServerManager::handleStartCapture(AsyncWebServerRequest* request) {
    float frequency = sysConfig->default_frequency;
    int modulation = sysConfig->default_modulation;
    bool autoDetect = false;

    if (request->hasParam("frequency")) {
        frequency = request->getParam("frequency")->value().toFloat();
    }
    if (request->hasParam("modulation")) {
        modulation = request->getParam("modulation")->value().toInt();
    }
    if (request->hasParam("auto")) {
        autoDetect = request->getParam("auto")->value() == "true";
    }

    rfModule.setFrequency(frequency);
    rfModule.setModulation(modulation);

    memset(&tempCapturedSignal, 0, sizeof(RFSignal));
    captureInProgress = true;

    // Iniciar captura en background
    if (autoDetect) {
        // Escanear frecuencias primero
        float detected = rfModule.scanForSignal((float*)RF_FREQUENCIES, RF_FREQUENCIES_COUNT, 5000);
        if (detected > 0) {
            rfModule.setFrequency(detected);
        }
    }

    if (rfModule.startCapture()) {
        DynamicJsonDocument doc(256);
        doc["success"] = true;
        doc["frequency"] = rfModule.getFrequency();
        doc["modulation"] = rfModule.getModulation();

        String response;
        serializeJson(doc, response);
        sendJsonResponse(request, 200, response);
    } else {
        captureInProgress = false;
        sendJsonError(request, 500, "Error al iniciar captura");
    }
}

void WebServerManager::handleStopCapture(AsyncWebServerRequest* request) {
    rfModule.stopCapture();
    captureInProgress = false;
    sendJsonResponse(request, 200, "{\"success\":true}");
}

void WebServerManager::handleGetCapture(AsyncWebServerRequest* request) {
    unsigned long timeout = 10000;
    if (request->hasParam("timeout")) {
        timeout = request->getParam("timeout")->value().toInt();
    }

    // Si hay captura en progreso, intentar obtener resultado
    if (captureInProgress || rfModule.isCapturing()) {
        if (rfModule.captureSignal(&tempCapturedSignal, timeout)) {
            captureInProgress = false;

            DynamicJsonDocument doc(2048);
            doc["success"] = true;
            doc["valid"] = tempCapturedSignal.valid;
            doc["length"] = tempCapturedSignal.length;
            doc["frequency"] = tempCapturedSignal.frequency;
            doc["modulation"] = tempCapturedSignal.modulation;

            // Datos en hex
            String dataHex = "";
            for (uint16_t i = 0; i < tempCapturedSignal.length; i++) {
                char hex[3];
                sprintf(hex, "%02X", tempCapturedSignal.data[i]);
                dataHex += hex;
            }
            doc["data"] = dataHex;

            // Análisis
            doc["analysis"] = rfModule.analyzeSignal(&tempCapturedSignal);
            doc["recommendations"] = rfModule.getRecommendedSettings(&tempCapturedSignal);

            String response;
            serializeJson(doc, response);
            sendJsonResponse(request, 200, response);

            // Notificar por WebSocket
            broadcastCapturedSignal(&tempCapturedSignal);
            return;
        }
    }

    DynamicJsonDocument doc(256);
    doc["success"] = false;
    doc["capturing"] = rfModule.isCapturing();
    doc["message"] = "No hay señal capturada";

    String response;
    serializeJson(doc, response);
    sendJsonResponse(request, 200, response);
}

void WebServerManager::handleSaveSignal(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, (char*)data, len);

    if (error) {
        sendJsonError(request, 400, "JSON inválido");
        return;
    }

    if (!doc.containsKey("deviceId") || !doc.containsKey("signalIndex") || !doc.containsKey("signalName")) {
        sendJsonError(request, 400, "Faltan campos requeridos");
        return;
    }

    String deviceId = doc["deviceId"];
    int signalIndex = doc["signalIndex"];
    String signalName = doc["signalName"];

    // Usar señal capturada temporal o datos del request
    RFSignal* signalToSave = &tempCapturedSignal;

    if (doc.containsKey("data")) {
        // Usar datos del request
        String dataHex = doc["data"];
        signalToSave->length = min((int)(dataHex.length() / 2), RF_MAX_SIGNAL_LENGTH);

        for (uint16_t i = 0; i < signalToSave->length; i++) {
            String byteStr = dataHex.substring(i * 2, i * 2 + 2);
            signalToSave->data[i] = (uint8_t)strtol(byteStr.c_str(), NULL, 16);
        }

        signalToSave->frequency = doc["frequency"] | sysConfig->default_frequency;
        signalToSave->modulation = doc["modulation"] | sysConfig->default_modulation;
        signalToSave->valid = true;
    }

    if (!signalToSave->valid) {
        sendJsonError(request, 400, "No hay señal válida para guardar");
        return;
    }

    if (storage.saveSignalToDevice(deviceId.c_str(), signalIndex, signalToSave, signalName.c_str())) {
        sendJsonResponse(request, 200, "{\"success\":true}");
    } else {
        sendJsonError(request, 500, "Error al guardar señal");
    }
}

void WebServerManager::handleSetFrequency(AsyncWebServerRequest* request) {
    if (!request->hasParam("freq")) {
        sendJsonError(request, 400, "Falta parámetro 'freq'");
        return;
    }

    float freq = request->getParam("freq")->value().toFloat();
    rfModule.setFrequency(freq);

    DynamicJsonDocument doc(128);
    doc["success"] = true;
    doc["frequency"] = rfModule.getFrequency();

    String response;
    serializeJson(doc, response);
    sendJsonResponse(request, 200, response);
}

void WebServerManager::handleScanFrequency(AsyncWebServerRequest* request) {
    unsigned long timeout = 5000;
    if (request->hasParam("timeout")) {
        timeout = request->getParam("timeout")->value().toInt();
    }

    float detected = rfModule.scanForSignal((float*)RF_FREQUENCIES, RF_FREQUENCIES_COUNT, timeout);

    DynamicJsonDocument doc(256);
    doc["success"] = detected > 0;
    doc["detected_frequency"] = detected;
    doc["rssi"] = rfModule.getRSSI();

    String response;
    serializeJson(doc, response);
    sendJsonResponse(request, 200, response);
}

void WebServerManager::handleBackup(AsyncWebServerRequest* request) {
    String backup = storage.createBackup();

    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", backup);
    response->addHeader("Content-Disposition", "attachment; filename=\"rf_controller_backup.json\"");
    request->send(response);
}

void WebServerManager::handleRestore(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    String backupJson = String((char*)data).substring(0, len);

    if (storage.restoreBackup(backupJson)) {
        // Recargar configuración
        storage.loadConfig(sysConfig);
        sendJsonResponse(request, 200, "{\"success\":true,\"message\":\"Backup restaurado. Reiniciando...\"}");

        // Reiniciar después de un delay
        delay(1000);
        ESP.restart();
    } else {
        sendJsonError(request, 400, "Error al restaurar backup");
    }
}

void WebServerManager::handleWiFiScan(AsyncWebServerRequest* request) {
    int n = WiFi.scanNetworks();

    DynamicJsonDocument doc(2048);
    JsonArray networks = doc.createNestedArray("networks");

    for (int i = 0; i < n && i < 20; i++) {
        JsonObject net = networks.createNestedObject();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["encrypted"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        net["channel"] = WiFi.channel(i);
    }

    WiFi.scanDelete();

    String response;
    serializeJson(doc, response);
    sendJsonResponse(request, 200, response);
}

void WebServerManager::handleWiFiConnect(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, (char*)data, len);

    if (error || !doc.containsKey("ssid")) {
        sendJsonError(request, 400, "JSON inválido o falta SSID");
        return;
    }

    String ssid = doc["ssid"];
    String password = doc["password"] | "";

    // Guardar credenciales
    strncpy(sysConfig->wifi_ssid, ssid.c_str(), 63);
    strncpy(sysConfig->wifi_password, password.c_str(), 63);
    sysConfig->wifi_configured = true;
    storage.saveConfig(sysConfig);

    // Intentar conectar
    sendJsonResponse(request, 200, "{\"success\":true,\"message\":\"Conectando...\"}");

    delay(500);
    connectWiFi(ssid.c_str(), password.c_str());
}

void WebServerManager::handleReboot(AsyncWebServerRequest* request) {
    sendJsonResponse(request, 200, "{\"success\":true,\"message\":\"Reiniciando...\"}");
    delay(1000);
    ESP.restart();
}

void WebServerManager::handleFactoryReset(AsyncWebServerRequest* request) {
    sendJsonResponse(request, 200, "{\"success\":true,\"message\":\"Restaurando valores de fábrica...\"}");

    delay(500);
    storage.format();
    delay(500);
    ESP.restart();
}

void WebServerManager::onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                                  AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("[WS] Cliente conectado: %u\n", client->id());
            broadcastStatus();
            break;

        case WS_EVT_DISCONNECT:
            Serial.printf("[WS] Cliente desconectado: %u\n", client->id());
            break;

        case WS_EVT_DATA: {
            // Manejar comandos por WebSocket
            DynamicJsonDocument doc(512);
            DeserializationError error = deserializeJson(doc, (char*)data, len);

            if (!error && doc.containsKey("cmd")) {
                String cmd = doc["cmd"];

                if (cmd == "status") {
                    broadcastStatus();
                } else if (cmd == "transmit") {
                    // Transmitir señal
                    String deviceId = doc["deviceId"];
                    int signalIndex = doc["signalIndex"];

                    SavedDevice device;
                    if (storage.getDevice(deviceId.c_str(), &device) &&
                        signalIndex >= 0 && signalIndex < device.signalCount &&
                        device.signals[signalIndex].valid) {

                        rfModule.setFrequency(device.signals[signalIndex].frequency);
                        rfModule.transmitSignal(&device.signals[signalIndex]);
                        broadcastStatus();
                    }
                }
            }
            break;
        }

        default:
            break;
    }
}

void WebServerManager::broadcastStatus() {
    if (ws.count() == 0) return;

    DynamicJsonDocument doc(512);
    doc["type"] = "status";
    doc["wifi_connected"] = isConnected();
    doc["rf_connected"] = rfModule.isConnected();
    doc["capturing"] = rfModule.isCapturing();
    doc["frequency"] = rfModule.getFrequency();
    doc["rssi"] = rfModule.getRSSI();

    String message;
    serializeJson(doc, message);
    ws.textAll(message);
}

void WebServerManager::broadcastCapturedSignal(const RFSignal* signal) {
    if (ws.count() == 0) return;

    DynamicJsonDocument doc(2048);
    doc["type"] = "signal_captured";
    doc["valid"] = signal->valid;
    doc["length"] = signal->length;
    doc["frequency"] = signal->frequency;

    String message;
    serializeJson(doc, message);
    ws.textAll(message);
}

void WebServerManager::sendJsonResponse(AsyncWebServerRequest* request, int code, const String& json) {
    AsyncWebServerResponse* response = request->beginResponse(code, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

void WebServerManager::sendJsonError(AsyncWebServerRequest* request, int code, const String& message) {
    DynamicJsonDocument doc(256);
    doc["success"] = false;
    doc["error"] = message;

    String response;
    serializeJson(doc, response);
    sendJsonResponse(request, code, response);
}

String WebServerManager::getContentType(const String& filename) {
    if (filename.endsWith(".html")) return "text/html";
    if (filename.endsWith(".css")) return "text/css";
    if (filename.endsWith(".js")) return "application/javascript";
    if (filename.endsWith(".json")) return "application/json";
    if (filename.endsWith(".png")) return "image/png";
    if (filename.endsWith(".ico")) return "image/x-icon";
    return "text/plain";
}
