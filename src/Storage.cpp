#include "Storage.h"

StorageManager storage;

StorageManager::StorageManager() {
    initialized = false;
}

bool StorageManager::begin() {
    Serial.println("[Storage] Inicializando LittleFS...");

    if (!LittleFS.begin(true)) {
        Serial.println("[Storage] Error al montar LittleFS, formateando...");
        if (!LittleFS.format()) {
            Serial.println("[Storage] Error al formatear!");
            return false;
        }
        if (!LittleFS.begin()) {
            Serial.println("[Storage] Error fatal al montar LittleFS");
            return false;
        }
    }

    initialized = true;
    Serial.printf("[Storage] LittleFS montado. Espacio: %d/%d bytes\n",
                  getTotalSpace() - getFreeSpace(), getTotalSpace());

    return true;
}

bool StorageManager::format() {
    Serial.println("[Storage] Formateando sistema de archivos...");
    return LittleFS.format();
}

void StorageManager::setDefaultConfig(SystemConfig* config) {
    memset(config, 0, sizeof(SystemConfig));

    // WiFi por defecto: dirasmart / dirasmart1
    strcpy(config->wifi_ssid, DEFAULT_WIFI_SSID);
    strcpy(config->wifi_password, DEFAULT_WIFI_PASSWORD);
    config->wifi_configured = true;  // Intentar conectar por defecto

    strcpy(config->mqtt_server, "");
    config->mqtt_port = MQTT_PORT;
    strcpy(config->mqtt_user, "");
    strcpy(config->mqtt_password, "");
    strcpy(config->mqtt_client_id, DEFAULT_DEVICE_NAME);
    config->mqtt_enabled = false;
    config->mqtt_discovery = true;

    strcpy(config->timezone, DEFAULT_TIMEZONE);
    strcpy(config->ntp_server, DEFAULT_NTP_SERVER);
    config->utc_offset = -5; // Colombia/Peru (GMT-5)
    config->dst_enabled = false;

    config->default_frequency = RF_DEFAULT_FREQUENCY;
    config->default_modulation = 2; // ASK/OOK

    strcpy(config->device_name, DEFAULT_DEVICE_NAME);
    config->auto_detect_enabled = true;
}

bool StorageManager::loadConfig(SystemConfig* config) {
    if (!initialized) return false;

    if (!fileExists(CONFIG_FILE)) {
        Serial.println("[Storage] Archivo de config no existe, creando default...");
        setDefaultConfig(config);
        return saveConfig(config);
    }

    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) {
        Serial.println("[Storage] Error al abrir archivo de config");
        setDefaultConfig(config);
        return false;
    }

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("[Storage] Error JSON: %s\n", error.c_str());
        setDefaultConfig(config);
        return false;
    }

    JsonObject obj = doc.as<JsonObject>();
    jsonToConfig(obj, config);

    Serial.println("[Storage] Configuración cargada");
    return true;
}

bool StorageManager::saveConfig(const SystemConfig* config) {
    if (!initialized) return false;

    DynamicJsonDocument doc(2048);
    JsonObject obj = doc.to<JsonObject>();
    configToJson(obj, config);

    File file = LittleFS.open(CONFIG_FILE, "w");
    if (!file) {
        Serial.println("[Storage] Error al crear archivo de config");
        return false;
    }

    serializeJson(doc, file);
    file.close();

    Serial.println("[Storage] Configuración guardada");
    return true;
}

bool StorageManager::loadDevices(SavedDevice* devices, uint8_t* count) {
    if (!initialized) return false;

    *count = 0;

    if (!fileExists(DEVICES_FILE)) {
        Serial.println("[Storage] No hay dispositivos guardados");
        return true;
    }

    File file = LittleFS.open(DEVICES_FILE, "r");
    if (!file) {
        Serial.println("[Storage] Error al abrir archivo de dispositivos");
        return false;
    }

    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("[Storage] Error JSON dispositivos: %s\n", error.c_str());
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    uint8_t idx = 0;

    for (JsonObject obj : arr) {
        if (idx >= MAX_DEVICES) break;
        jsonToDevice(obj, &devices[idx]);
        idx++;
    }

    *count = idx;
    Serial.printf("[Storage] %d dispositivos cargados\n", *count);
    return true;
}

bool StorageManager::saveDevices(const SavedDevice* devices, uint8_t count) {
    if (!initialized) return false;

    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    JsonArray arr = doc.to<JsonArray>();

    for (uint8_t i = 0; i < count; i++) {
        JsonObject obj = arr.createNestedObject();
        deviceToJson(obj, &devices[i]);
    }

    File file = LittleFS.open(DEVICES_FILE, "w");
    if (!file) {
        Serial.println("[Storage] Error al crear archivo de dispositivos");
        return false;
    }

    serializeJson(doc, file);
    file.close();

    Serial.printf("[Storage] %d dispositivos guardados\n", count);
    return true;
}

bool StorageManager::addDevice(const SavedDevice* device) {
    if (!initialized) return false;

    // Leer archivo JSON existente
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);

    if (fileExists(DEVICES_FILE)) {
        File file = LittleFS.open(DEVICES_FILE, "r");
        if (file) {
            deserializeJson(doc, file);
            file.close();
        }
    }

    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

    if (arr.size() >= MAX_DEVICES) {
        Serial.println("[Storage] Máximo de dispositivos alcanzado");
        return false;
    }

    // Agregar nuevo dispositivo al JSON
    JsonObject obj = arr.createNestedObject();
    deviceToJson(obj, device);

    // Guardar
    File file = LittleFS.open(DEVICES_FILE, "w");
    if (!file) {
        Serial.println("[Storage] Error al guardar dispositivo");
        return false;
    }

    serializeJson(doc, file);
    file.close();

    Serial.printf("[Storage] Dispositivo agregado: %s\n", device->name);
    return true;
}

bool StorageManager::updateDevice(const char* id, const SavedDevice* device) {
    if (!initialized || !fileExists(DEVICES_FILE)) return false;

    // Leer archivo JSON
    File file = LittleFS.open(DEVICES_FILE, "r");
    if (!file) return false;

    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) return false;

    JsonArray arr = doc.as<JsonArray>();
    bool found = false;

    for (size_t i = 0; i < arr.size(); i++) {
        JsonObject obj = arr[i];
        if (strcmp(obj["id"] | "", id) == 0) {
            // Actualizar este dispositivo
            deviceToJson(obj, device);
            found = true;
            break;
        }
    }

    if (!found) return false;

    // Guardar
    file = LittleFS.open(DEVICES_FILE, "w");
    if (!file) return false;

    serializeJson(doc, file);
    file.close();

    Serial.printf("[Storage] Dispositivo actualizado: %s\n", id);
    return true;
}

bool StorageManager::deleteDevice(const char* id) {
    if (!initialized || !fileExists(DEVICES_FILE)) return false;

    // Leer archivo JSON
    File file = LittleFS.open(DEVICES_FILE, "r");
    if (!file) return false;

    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) return false;

    JsonArray arr = doc.as<JsonArray>();
    bool found = false;

    for (size_t i = 0; i < arr.size(); i++) {
        JsonObject obj = arr[i];
        if (strcmp(obj["id"] | "", id) == 0) {
            arr.remove(i);
            found = true;
            break;
        }
    }

    if (!found) return false;

    // Guardar
    file = LittleFS.open(DEVICES_FILE, "w");
    if (!file) return false;

    serializeJson(doc, file);
    file.close();

    Serial.printf("[Storage] Dispositivo eliminado: %s\n", id);
    return true;
}

bool StorageManager::getDevice(const char* id, SavedDevice* device) {
    if (!initialized || !fileExists(DEVICES_FILE)) return false;

    File file = LittleFS.open(DEVICES_FILE, "r");
    if (!file) return false;

    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) return false;

    JsonArray arr = doc.as<JsonArray>();

    for (JsonObject obj : arr) {
        if (strcmp(obj["id"] | "", id) == 0) {
            jsonToDevice(obj, device);
            return true;
        }
    }

    return false;
}

uint8_t StorageManager::getDeviceCount() {
    if (!initialized || !fileExists(DEVICES_FILE)) return 0;

    File file = LittleFS.open(DEVICES_FILE, "r");
    if (!file) return 0;

    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) return 0;

    JsonArray arr = doc.as<JsonArray>();
    return arr.size();
}

bool StorageManager::getDeviceByIndex(uint8_t index, SavedDevice* device) {
    if (!initialized || !fileExists(DEVICES_FILE)) return false;

    File file = LittleFS.open(DEVICES_FILE, "r");
    if (!file) return false;

    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) return false;

    JsonArray arr = doc.as<JsonArray>();
    if (index >= arr.size()) return false;

    JsonObject obj = arr[index];
    jsonToDevice(obj, device);
    return true;
}

bool StorageManager::saveSignalToDevice(const char* deviceId, uint8_t signalIndex,
                                        const RFSignal* signal, const char* signalName) {
    Serial.printf("[Storage] saveSignalToDevice: id=%s, index=%d, name=%s\n", deviceId, signalIndex, signalName);

    if (signalIndex >= 4) {
        Serial.println("[Storage] Error: signal index >= 4");
        return false;
    }

    SavedDevice device;
    if (!getDevice(deviceId, &device)) {
        Serial.printf("[Storage] Error: device not found: %s\n", deviceId);
        return false;
    }

    Serial.printf("[Storage] Device found: %s, current signalCount=%d\n", device.name, device.signalCount);

    memcpy(&device.signals[signalIndex], signal, sizeof(RFSignal));
    strncpy(device.signalNames[signalIndex], signalName, 31);
    device.signalNames[signalIndex][31] = '\0';

    if (signalIndex >= device.signalCount) {
        device.signalCount = signalIndex + 1;
    }

    Serial.printf("[Storage] Saving signal: valid=%d, len=%d, freq=%.2f\n",
                  signal->valid, signal->length, signal->frequency);

    bool result = updateDevice(deviceId, &device);
    Serial.printf("[Storage] updateDevice result: %s\n", result ? "OK" : "FAILED");

    return result;
}

bool StorageManager::deleteSignalFromDevice(const char* deviceId, uint8_t signalIndex) {
    if (signalIndex >= 4) return false;

    SavedDevice device;
    if (!getDevice(deviceId, &device)) return false;

    memset(&device.signals[signalIndex], 0, sizeof(RFSignal));
    memset(device.signalNames[signalIndex], 0, 32);

    return updateDevice(deviceId, &device);
}

bool StorageManager::updateSignalRepeatCount(const char* deviceId, uint8_t signalIndex, uint8_t repeatCount) {
    if (signalIndex >= 4) return false;

    SavedDevice device;
    if (!getDevice(deviceId, &device)) return false;

    if (!device.signals[signalIndex].valid) {
        Serial.println("[Storage] Signal not valid, cannot update repeat count");
        return false;
    }

    // Clamp repeat count between 1-20
    if (repeatCount < 1) repeatCount = 1;
    if (repeatCount > 20) repeatCount = 20;

    device.signals[signalIndex].repeatCount = repeatCount;
    Serial.printf("[Storage] Updated signal %d repeat count to %d\n", signalIndex, repeatCount);

    return updateDevice(deviceId, &device);
}

bool StorageManager::updateSomfyRollingCode(const char* deviceId, uint16_t newRollingCode) {
    SavedDevice device;
    if (!getDevice(deviceId, &device)) return false;

    if (device.type != DEVICE_CURTAIN_SOMFY) {
        Serial.println("[Storage] Error: dispositivo no es Somfy");
        return false;
    }

    device.somfy.rollingCode = newRollingCode;
    return updateDevice(deviceId, &device);
}

String StorageManager::createBackup() {
    DynamicJsonDocument doc(JSON_BUFFER_SIZE * 2);

    // Configuración
    SystemConfig config;
    if (loadConfig(&config)) {
        JsonObject configObj = doc.createNestedObject("config");
        configToJson(configObj, &config);
    }

    // Dispositivos - leer directamente del archivo
    if (fileExists(DEVICES_FILE)) {
        File file = LittleFS.open(DEVICES_FILE, "r");
        if (file) {
            DynamicJsonDocument devDoc(JSON_BUFFER_SIZE);
            if (!deserializeJson(devDoc, file)) {
                doc["devices"] = devDoc.as<JsonArray>();
            }
            file.close();
        }
    }

    // Metadata
    doc["backup_version"] = 1;
    doc["timestamp"] = millis();
    doc["device_name"] = config.device_name;

    String output;
    serializeJson(doc, output);
    return output;
}

bool StorageManager::restoreBackup(const String& backupJson) {
    DynamicJsonDocument doc(JSON_BUFFER_SIZE * 2);
    DeserializationError error = deserializeJson(doc, backupJson);

    if (error) {
        Serial.printf("[Storage] Error al parsear backup: %s\n", error.c_str());
        return false;
    }

    // Restaurar configuración
    if (doc.containsKey("config")) {
        SystemConfig config;
        JsonObject configObj = doc["config"];
        jsonToConfig(configObj, &config);
        saveConfig(&config);
    }

    // Restaurar dispositivos - guardar directamente el JSON
    if (doc.containsKey("devices")) {
        File file = LittleFS.open(DEVICES_FILE, "w");
        if (file) {
            serializeJson(doc["devices"], file);
            file.close();
        }
    }

    Serial.println("[Storage] Backup restaurado");
    return true;
}

bool StorageManager::exportToFile(const char* filename) {
    String backup = createBackup();

    File file = LittleFS.open(filename, "w");
    if (!file) return false;

    file.print(backup);
    file.close();

    return true;
}

bool StorageManager::importFromFile(const char* filename) {
    if (!fileExists(filename)) return false;

    File file = LittleFS.open(filename, "r");
    if (!file) return false;

    String content = file.readString();
    file.close();

    return restoreBackup(content);
}

String StorageManager::generateUUID() {
    char uuid[37];
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    uint32_t r3 = esp_random();
    uint32_t r4 = esp_random();

    snprintf(uuid, sizeof(uuid), "%08x-%04x-%04x-%04x-%04x%08x",
             r1,
             (r2 >> 16) & 0xFFFF,
             ((r2 & 0x0FFF) | 0x4000),
             ((r3 >> 16) & 0x3FFF) | 0x8000,
             r3 & 0xFFFF, r4);

    return String(uuid);
}

bool StorageManager::fileExists(const char* path) {
    return LittleFS.exists(path);
}

size_t StorageManager::getFreeSpace() {
    return LittleFS.totalBytes() - LittleFS.usedBytes();
}

size_t StorageManager::getTotalSpace() {
    return LittleFS.totalBytes();
}

String StorageManager::listFiles() {
    String list = "Archivos en LittleFS:\n";

    File root = LittleFS.open("/");
    File file = root.openNextFile();

    while (file) {
        list += "  " + String(file.name()) + " (" + String(file.size()) + " bytes)\n";
        file = root.openNextFile();
    }

    return list;
}

// ============================================
// Helpers JSON
// ============================================

void StorageManager::signalToJson(JsonObject& obj, const RFSignal* signal) {
    // Convertir datos binarios a Base64 o array hex
    String dataHex = "";
    for (uint16_t i = 0; i < signal->length && i < RF_MAX_SIGNAL_LENGTH; i++) {
        char hex[3];
        sprintf(hex, "%02X", signal->data[i]);
        dataHex += hex;
    }

    obj["data"] = dataHex;
    obj["length"] = signal->length;
    obj["frequency"] = signal->frequency;
    obj["modulation"] = signal->modulation;
    obj["bandwidth"] = signal->bandwidth;
    obj["dataRate"] = signal->dataRate;
    obj["deviation"] = signal->deviation;
    obj["timestamp"] = signal->timestamp;
    obj["valid"] = signal->valid;
    obj["repeatCount"] = signal->repeatCount > 0 ? signal->repeatCount : RF_REPEAT_TRANSMIT;
}

void StorageManager::jsonToSignal(JsonObject& obj, RFSignal* signal) {
    memset(signal, 0, sizeof(RFSignal));

    String dataHex = obj["data"] | "";
    signal->length = min((int)(dataHex.length() / 2), RF_MAX_SIGNAL_LENGTH);

    for (uint16_t i = 0; i < signal->length; i++) {
        String byteStr = dataHex.substring(i * 2, i * 2 + 2);
        signal->data[i] = (uint8_t)strtol(byteStr.c_str(), NULL, 16);
    }

    signal->frequency = obj["frequency"] | RF_DEFAULT_FREQUENCY;
    signal->modulation = obj["modulation"] | 2;
    signal->bandwidth = obj["bandwidth"] | 0;
    signal->dataRate = obj["dataRate"] | 0;
    signal->deviation = obj["deviation"] | 0;
    signal->timestamp = obj["timestamp"] | 0;
    signal->valid = obj["valid"] | false;
    signal->repeatCount = obj["repeatCount"] | RF_REPEAT_TRANSMIT;
}

void StorageManager::deviceToJson(JsonObject& obj, const SavedDevice* device) {
    obj["id"] = device->id;
    obj["name"] = device->name;
    obj["type"] = (int)device->type;
    obj["signalCount"] = device->signalCount;
    obj["enabled"] = device->enabled;
    obj["room"] = device->room;
    obj["createdAt"] = device->createdAt;
    obj["lastUsed"] = device->lastUsed;

    JsonArray signalsArr = obj.createNestedArray("signals");
    JsonArray namesArr = obj.createNestedArray("signalNames");

    for (uint8_t i = 0; i < 4; i++) {
        JsonObject sigObj = signalsArr.createNestedObject();
        signalToJson(sigObj, &device->signals[i]);
        // Add index and name for frontend compatibility
        sigObj["index"] = i;
        sigObj["name"] = device->signalNames[i];
        namesArr.add(device->signalNames[i]);
    }

    // Datos Somfy RTS (solo si es tipo Somfy)
    if (device->type == DEVICE_CURTAIN_SOMFY) {
        JsonObject somfyObj = obj.createNestedObject("somfy");
        somfyObj["address"] = device->somfy.address;
        somfyObj["rollingCode"] = device->somfy.rollingCode;
        somfyObj["encryptionKey"] = device->somfy.encryptionKey;
    }

    // Datos Dooya Bidireccional
    if (device->type == DEVICE_CURTAIN_DOOYA_BIDIR) {
        JsonObject dooyaObj = obj.createNestedObject("dooyaBidir");
        dooyaObj["deviceId"] = device->dooyaBidir.deviceId;
        dooyaObj["unitCode"] = device->dooyaBidir.unitCode;
    }
}

void StorageManager::jsonToDevice(JsonObject& obj, SavedDevice* device) {
    memset(device, 0, sizeof(SavedDevice));

    strncpy(device->id, obj["id"] | "", 36);
    device->id[36] = '\0';
    strncpy(device->name, obj["name"] | "Sin nombre", 63);
    device->name[63] = '\0';
    device->type = (DeviceType)(obj["type"] | DEVICE_UNKNOWN);
    device->signalCount = obj["signalCount"] | 0;
    device->enabled = obj["enabled"] | true;
    strncpy(device->room, obj["room"] | "", 31);
    device->room[31] = '\0';
    device->createdAt = obj["createdAt"] | 0;
    device->lastUsed = obj["lastUsed"] | 0;

    JsonArray signalsArr = obj["signals"];
    JsonArray namesArr = obj["signalNames"];

    for (uint8_t i = 0; i < 4 && i < signalsArr.size(); i++) {
        JsonObject sigObj = signalsArr[i];
        jsonToSignal(sigObj, &device->signals[i]);

        if (i < namesArr.size()) {
            strncpy(device->signalNames[i], namesArr[i] | "", 31);
            device->signalNames[i][31] = '\0';
        }
    }

    // Datos Somfy RTS
    if (obj.containsKey("somfy")) {
        JsonObject somfyObj = obj["somfy"];
        device->somfy.address = somfyObj["address"] | 0;
        device->somfy.rollingCode = somfyObj["rollingCode"] | 0;
        device->somfy.encryptionKey = somfyObj["encryptionKey"] | 0xA7;
    }

    // Datos Dooya Bidireccional
    if (obj.containsKey("dooyaBidir")) {
        JsonObject dooyaObj = obj["dooyaBidir"];
        device->dooyaBidir.deviceId = dooyaObj["deviceId"] | 0;
        device->dooyaBidir.unitCode = dooyaObj["unitCode"] | 0;
    }
}

void StorageManager::configToJson(JsonObject& obj, const SystemConfig* config) {
    // WiFi
    obj["wifi_ssid"] = config->wifi_ssid;
    obj["wifi_password"] = config->wifi_password;
    obj["wifi_configured"] = config->wifi_configured;

    // MQTT
    obj["mqtt_server"] = config->mqtt_server;
    obj["mqtt_port"] = config->mqtt_port;
    obj["mqtt_user"] = config->mqtt_user;
    obj["mqtt_password"] = config->mqtt_password;
    obj["mqtt_client_id"] = config->mqtt_client_id;
    obj["mqtt_enabled"] = config->mqtt_enabled;
    obj["mqtt_discovery"] = config->mqtt_discovery;

    // Zona horaria
    obj["timezone"] = config->timezone;
    obj["ntp_server"] = config->ntp_server;
    obj["utc_offset"] = config->utc_offset;
    obj["dst_enabled"] = config->dst_enabled;

    // RF
    obj["default_frequency"] = config->default_frequency;
    obj["default_modulation"] = config->default_modulation;

    // Sistema
    obj["device_name"] = config->device_name;
    obj["auto_detect_enabled"] = config->auto_detect_enabled;
}

void StorageManager::jsonToConfig(JsonObject& obj, SystemConfig* config) {
    // WiFi
    strncpy(config->wifi_ssid, obj["wifi_ssid"] | "", 63);
    config->wifi_ssid[63] = '\0';
    strncpy(config->wifi_password, obj["wifi_password"] | "", 63);
    config->wifi_password[63] = '\0';
    config->wifi_configured = obj["wifi_configured"] | false;

    // MQTT
    strncpy(config->mqtt_server, obj["mqtt_server"] | "", 63);
    config->mqtt_server[63] = '\0';
    config->mqtt_port = obj["mqtt_port"] | MQTT_PORT;
    strncpy(config->mqtt_user, obj["mqtt_user"] | "", 31);
    config->mqtt_user[31] = '\0';
    strncpy(config->mqtt_password, obj["mqtt_password"] | "", 63);
    config->mqtt_password[63] = '\0';
    strncpy(config->mqtt_client_id, obj["mqtt_client_id"] | DEFAULT_DEVICE_NAME, 31);
    config->mqtt_client_id[31] = '\0';
    config->mqtt_enabled = obj["mqtt_enabled"] | false;
    config->mqtt_discovery = obj["mqtt_discovery"] | true;

    // Zona horaria
    strncpy(config->timezone, obj["timezone"] | DEFAULT_TIMEZONE, 63);
    config->timezone[63] = '\0';
    strncpy(config->ntp_server, obj["ntp_server"] | DEFAULT_NTP_SERVER, 63);
    config->ntp_server[63] = '\0';
    config->utc_offset = obj["utc_offset"] | -3;
    config->dst_enabled = obj["dst_enabled"] | false;

    // RF
    config->default_frequency = obj["default_frequency"] | RF_DEFAULT_FREQUENCY;
    config->default_modulation = obj["default_modulation"] | 2;

    // Sistema
    strncpy(config->device_name, obj["device_name"] | DEFAULT_DEVICE_NAME, 31);
    config->device_name[31] = '\0';
    config->auto_detect_enabled = obj["auto_detect_enabled"] | true;
}
