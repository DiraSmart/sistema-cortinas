#include "MQTTClient.h"
#include "config.h"
#include "CC1101_RF.h"
#include "SomfyRTS.h"
#include "DooyaBidir.h"

MQTTClientManager* MQTTClientManager::instance = nullptr;
MQTTClientManager mqttClient;

MQTTClientManager::MQTTClientManager() : mqtt(wifiClient) {
    enabled = false;
    lastReconnectAttempt = 0;
    lastStatusPublish = 0;
    onCommand = nullptr;
    sysConfig = nullptr;
    instance = this;
}

bool MQTTClientManager::begin(SystemConfig* config) {
    sysConfig = config;

    Serial.println("[MQTT] ========== CONFIGURACIÓN MQTT ==========");
    Serial.printf("[MQTT] Habilitado: %s\n", config->mqtt_enabled ? "SI" : "NO");
    Serial.printf("[MQTT] Servidor: '%s'\n", config->mqtt_server);
    Serial.printf("[MQTT] Puerto: %d\n", config->mqtt_port);
    Serial.printf("[MQTT] Usuario: '%s'\n", strlen(config->mqtt_user) > 0 ? config->mqtt_user : "(vacío)");
    Serial.printf("[MQTT] Password: %s\n", strlen(config->mqtt_password) > 0 ? "(configurado)" : "(vacío)");
    Serial.printf("[MQTT] Client ID: '%s'\n", config->mqtt_client_id);
    Serial.printf("[MQTT] Discovery: %s\n", config->mqtt_discovery ? "SI" : "NO");
    Serial.println("[MQTT] ==========================================");

    if (!config->mqtt_enabled || strlen(config->mqtt_server) == 0) {
        Serial.println("[MQTT] MQTT deshabilitado o servidor vacío");
        enabled = false;
        return false;
    }

    Serial.printf("[MQTT] Configurando conexión a %s:%d\n",
                  config->mqtt_server, config->mqtt_port);

    mqtt.setServer(config->mqtt_server, config->mqtt_port);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(1024);

    setupTopics();
    enabled = true;

    return connect();
}

void MQTTClientManager::stop() {
    if (mqtt.connected()) {
        // Publicar offline antes de desconectar
        mqtt.publish(availabilityTopic.c_str(), "offline", true);
        mqtt.disconnect();
    }
    enabled = false;
}

void MQTTClientManager::setupTopics() {
    baseTopic = String(MQTT_BASE_TOPIC) + "/" + String(sysConfig->mqtt_client_id);
    commandTopic = baseTopic + "/+/set";
    stateTopic = baseTopic + "/state";
    availabilityTopic = baseTopic + "/status";
}

bool MQTTClientManager::connect() {
    if (!enabled || !sysConfig) return false;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MQTT] WiFi no conectado");
        return false;
    }

    Serial.println("[MQTT] Conectando...");

    bool connected = false;
    String willTopic = availabilityTopic;

    if (strlen(sysConfig->mqtt_user) > 0) {
        connected = mqtt.connect(
            sysConfig->mqtt_client_id,
            sysConfig->mqtt_user,
            sysConfig->mqtt_password,
            willTopic.c_str(),
            0, true, "offline"
        );
    } else {
        connected = mqtt.connect(
            sysConfig->mqtt_client_id,
            willTopic.c_str(),
            0, true, "offline"
        );
    }

    if (connected) {
        Serial.println("[MQTT] Conectado!");

        // Publicar disponibilidad
        mqtt.publish(availabilityTopic.c_str(), "online", true);

        // Suscribirse a comandos
        subscribe();

        // Publicar discovery si está habilitado
        if (sysConfig->mqtt_discovery) {
            publishDiscovery();
            delay(100);  // Pausa después de discovery
            yield();
        }

        // Publicar estado inicial
        publishAllStates();
        delay(50);
        publishSystemStatus();

        return true;
    } else {
        int state = mqtt.state();
        Serial.printf("[MQTT] Error de conexión, código: %d\n", state);
        switch(state) {
            case -4: Serial.println("[MQTT] TIMEOUT - broker no responde"); break;
            case -3: Serial.println("[MQTT] Conexión perdida"); break;
            case -2: Serial.println("[MQTT] FALLÓ - no se pudo conectar al broker"); break;
            case -1: Serial.println("[MQTT] Desconectado"); break;
            case 1: Serial.println("[MQTT] Protocolo incorrecto"); break;
            case 2: Serial.println("[MQTT] Client ID rechazado"); break;
            case 3: Serial.println("[MQTT] Servidor no disponible"); break;
            case 4: Serial.println("[MQTT] Credenciales incorrectas (user/password)"); break;
            case 5: Serial.println("[MQTT] No autorizado - necesita autenticación"); break;
        }
        return false;
    }
}

void MQTTClientManager::disconnect() {
    if (mqtt.connected()) {
        mqtt.publish(availabilityTopic.c_str(), "offline", true);
        mqtt.disconnect();
    }
}

bool MQTTClientManager::isConnected() {
    return mqtt.connected();
}

void MQTTClientManager::loop() {
    if (!enabled) return;

    if (!mqtt.connected()) {
        unsigned long now = millis();
        if (now - lastReconnectAttempt > MQTT_RECONNECT_DELAY) {
            lastReconnectAttempt = now;
            connect();
        }
    } else {
        mqtt.loop();

        // Publicar estado periódicamente
        if (millis() - lastStatusPublish > 60000) {
            lastStatusPublish = millis();
            publishSystemStatus();
        }
    }
}

void MQTTClientManager::subscribe() {
    // Suscribirse a comandos de todos los dispositivos
    String topic = baseTopic + "/+/set";
    mqtt.subscribe(topic.c_str());
    Serial.printf("[MQTT] Suscrito a: %s\n", topic.c_str());

    // Suscribirse a comandos específicos de señales
    topic = baseTopic + "/+/+/set";
    mqtt.subscribe(topic.c_str());
    Serial.printf("[MQTT] Suscrito a: %s\n", topic.c_str());

    // Suscribirse a comandos del sistema (rediscover, reboot)
    topic = baseTopic + "/system/+";
    mqtt.subscribe(topic.c_str());
    Serial.printf("[MQTT] Suscrito a: %s\n", topic.c_str());
}

void MQTTClientManager::mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
    if (instance) {
        instance->handleMessage(topic, payload, length);
    }
}

void MQTTClientManager::handleMessage(char* topic, uint8_t* payload, unsigned int length) {
    // Convertir payload a string
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';

    Serial.printf("[MQTT] Mensaje recibido: %s -> %s\n", topic, message);

    // Parsear topic: baseTopic/deviceId/set o baseTopic/deviceId/signalIndex/set
    String topicStr = String(topic);
    String baseStr = baseTopic + "/";

    if (!topicStr.startsWith(baseStr)) return;

    String remainder = topicStr.substring(baseStr.length());

    // Check for system commands first
    if (remainder.startsWith("system/")) {
        String sysCmd = remainder.substring(7);  // Remove "system/"
        processSystemCommand(sysCmd.c_str(), message);
        return;
    }

    int slashPos = remainder.indexOf('/');

    if (slashPos < 0) return;

    String deviceId = remainder.substring(0, slashPos);
    String rest = remainder.substring(slashPos + 1);

    // Verificar si es comando directo o de señal específica
    if (rest == "set") {
        // Comando directo al dispositivo
        processDeviceCommand(deviceId.c_str(), message);
    } else if (rest.endsWith("/set")) {
        // Comando a señal específica
        String signalStr = rest.substring(0, rest.length() - 4);
        int signalIndex = signalStr.toInt();
        processSignalCommand(deviceId.c_str(), signalIndex, message);
    }
}

void MQTTClientManager::processDeviceCommand(const char* deviceId, const char* command) {
    Serial.printf("[MQTT] Comando para dispositivo %s: %s\n", deviceId, command);

    SavedDevice device;
    if (!storage.getDevice(deviceId, &device)) {
        Serial.println("[MQTT] Dispositivo no encontrado");
        return;
    }

    String cmd = String(command);
    cmd.toLowerCase();

    // Manejar dispositivos Somfy RTS de forma especial (rolling code)
    if (device.type == DEVICE_CURTAIN_SOMFY) {
        Serial.printf("[MQTT] Comando Somfy RTS para %s\n", device.name);

        // Configurar el módulo RF para Somfy (433.42 MHz)
        rfModule.setFrequency(SOMFY_FREQUENCY);
        somfyRTS.setRemote(&device.somfy);

        bool success = false;
        if (cmd == "open" || cmd == "up") {
            success = somfyRTS.sendUp();
        } else if (cmd == "close" || cmd == "down") {
            success = somfyRTS.sendDown();
        } else if (cmd == "stop" || cmd == "my") {
            success = somfyRTS.sendStop();
        } else if (cmd == "prog") {
            success = somfyRTS.sendProg();
        }

        if (success) {
            // Actualizar rolling code en storage
            storage.updateSomfyRollingCode(deviceId, somfyRTS.getRollingCode());
            publishDeviceState(deviceId, command);
        }
        return;
    }

    // Manejar dispositivos Dooya Bidireccional (DDxxxx con FSK)
    if (device.type == DEVICE_CURTAIN_DOOYA_BIDIR) {
        Serial.printf("[MQTT] Comando Dooya Bidir para %s\n", device.name);

        dooyaBidir.setRemote(&device.dooyaBidir);

        bool success = false;
        if (cmd == "open" || cmd == "up") {
            success = dooyaBidir.sendUp();
        } else if (cmd == "close" || cmd == "down") {
            success = dooyaBidir.sendDown();
        } else if (cmd == "stop") {
            success = dooyaBidir.sendStop();
        } else if (cmd == "prog") {
            success = dooyaBidir.sendProg();
        }

        if (success) {
            publishDeviceState(deviceId, command);
        }
        return;
    }

    // Interpretar comando según tipo de dispositivo funcional
    int signalIndex = -1;

    switch (device.type) {
        case DEVICE_CURTAIN:
            // Cortinas: OPEN, CLOSE, STOP (índices 0, 1, 2)
            if (cmd == "open") signalIndex = 0;
            else if (cmd == "close") signalIndex = 1;
            else if (cmd == "stop") signalIndex = 2;
            break;

        case DEVICE_SWITCH:
        case DEVICE_LIGHT:
            // Interruptores/Luces: ON, OFF (índices 0, 1)
            if (cmd == "on") signalIndex = 0;
            else if (cmd == "off") signalIndex = 1;
            break;

        case DEVICE_BUTTON:
            // Botones: cualquier comando activa señal 0
            signalIndex = 0;
            break;

        case DEVICE_GATE:
            // Portones: TOGGLE o OPEN/CLOSE
            if (cmd == "toggle" || cmd == "open") signalIndex = 0;
            else if (cmd == "close") signalIndex = 1;
            break;

        case DEVICE_FAN:
            // Ventiladores: ON, OFF, SPEED (índices 0, 1, 2)
            if (cmd == "on") signalIndex = 0;
            else if (cmd == "off") signalIndex = 1;
            else if (cmd == "speed") signalIndex = 2;
            break;

        case DEVICE_DIMMER:
            // Dimmers: ON, OFF, UP, DOWN (índices 0, 1, 2, 3)
            if (cmd == "on") signalIndex = 0;
            else if (cmd == "off") signalIndex = 1;
            else if (cmd == "up" || cmd == "brightness_up") signalIndex = 2;
            else if (cmd == "down" || cmd == "brightness_down") signalIndex = 3;
            break;

        default:
            // Para otros tipos, interpretar como índice numérico
            signalIndex = cmd.toInt();
            break;
    }

    if (signalIndex >= 0 && signalIndex < device.signalCount && device.signals[signalIndex].valid) {
        // Transmitir señal
        rfModule.setFrequency(device.signals[signalIndex].frequency);
        rfModule.setModulation(device.signals[signalIndex].modulation);
        rfModule.transmitSignal(&device.signals[signalIndex]);

        // Publicar estado actualizado
        publishDeviceState(deviceId, command);

        Serial.printf("[MQTT] Señal %d transmitida\n", signalIndex);
    } else {
        Serial.printf("[MQTT] Señal no válida: %d\n", signalIndex);
    }
}

void MQTTClientManager::processSignalCommand(const char* deviceId, int signalIndex, const char* command) {
    Serial.printf("[MQTT] Comando para señal %s/%d: %s\n", deviceId, signalIndex, command);

    SavedDevice device;
    if (!storage.getDevice(deviceId, &device)) {
        Serial.println("[MQTT] Dispositivo no encontrado");
        return;
    }

    if (signalIndex < 0 || signalIndex >= device.signalCount || !device.signals[signalIndex].valid) {
        Serial.println("[MQTT] Señal no válida");
        return;
    }

    // Transmitir señal
    rfModule.setFrequency(device.signals[signalIndex].frequency);
    rfModule.setModulation(device.signals[signalIndex].modulation);
    rfModule.transmitSignal(&device.signals[signalIndex]);

    Serial.printf("[MQTT] Señal %d transmitida\n", signalIndex);
}

void MQTTClientManager::processSystemCommand(const char* command, const char* payload) {
    Serial.printf("[MQTT] Comando sistema: %s -> %s\n", command, payload);

    String cmd = String(command);

    if (cmd == "rediscover") {
        Serial.println("[MQTT] Ejecutando rediscovery...");
        publishDiscovery();
    } else if (cmd == "reboot") {
        Serial.println("[MQTT] Reiniciando...");
        mqtt.publish(availabilityTopic.c_str(), "offline", true);
        delay(500);
        ESP.restart();
    }
}

void MQTTClientManager::setCommandCallback(void (*callback)(const char* deviceId, const char* command)) {
    onCommand = callback;
}

void MQTTClientManager::publishDeviceState(const char* deviceId, const char* state) {
    if (!mqtt.connected()) return;

    String topic = baseTopic + "/" + String(deviceId) + "/state";
    mqtt.publish(topic.c_str(), state, true);
}

void MQTTClientManager::publishAllStates() {
    if (!mqtt.connected()) return;

    uint8_t count = storage.getDeviceCount();
    SavedDevice device;

    for (uint8_t i = 0; i < count; i++) {
        if (storage.getDeviceByIndex(i, &device)) {
            publishDeviceState(device.id, "unknown");
        }
    }
}

void MQTTClientManager::publishSystemStatus() {
    if (!mqtt.connected()) return;

    // Publicar al topic de diagnosticos para los sensores HA
    StaticJsonDocument<384> doc;
    doc["uptime"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    doc["rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress();
    doc["ssid"] = WiFi.SSID();
    doc["rf_ok"] = rfModule.isConnected();
    doc["freq"] = rfModule.getFrequency();

    String payload;
    serializeJson(doc, payload);

    // Publicar a diagnostics (para sensores HA)
    String diagTopic = baseTopic + "/diagnostics";
    mqtt.publish(diagTopic.c_str(), payload.c_str(), true);

    // También publicar a system (legacy)
    String sysTopic = baseTopic + "/system";
    mqtt.publish(sysTopic.c_str(), payload.c_str(), true);
}

// ============================================
// Home Assistant Discovery
// ============================================

void MQTTClientManager::publishDiscovery() {
    if (!mqtt.connected() || !sysConfig->mqtt_discovery) return;

    Serial.println("[MQTT] Publicando Home Assistant Discovery...");

    // Publish system buttons (Rediscover, Reboot)
    publishSystemButtons();

    // Publish diagnostic sensors
    publishDiagnosticSensors();

    // Cargar dispositivos uno a uno para evitar stack overflow
    uint8_t count = storage.getDeviceCount();
    SavedDevice device;  // Solo UN dispositivo en stack

    for (uint8_t i = 0; i < count; i++) {
        if (!storage.getDeviceByIndex(i, &device)) continue;

        switch (device.type) {
            case DEVICE_CURTAIN:
            case DEVICE_CURTAIN_SOMFY:
            case DEVICE_CURTAIN_DOOYA_BIDIR:
                publishCoverDiscovery(&device);
                break;

            case DEVICE_SWITCH:
            case DEVICE_LIGHT:
                publishSwitchDiscovery(&device);
                break;

            case DEVICE_GATE:
                publishGateDiscovery(&device);
                break;

            case DEVICE_FAN:
                publishSwitchDiscovery(&device);
                break;

            default:
                for (uint8_t j = 0; j < device.signalCount; j++) {
                    if (device.signals[j].valid) {
                        publishButtonDiscovery(&device, j);
                    }
                }
                break;
        }
        delay(20);
        yield();
    }

    Serial.printf("[MQTT] Discovery publicado para %d dispositivos\n", count);
}

void MQTTClientManager::publishSystemButtons() {
    // Rediscover button - usar StaticJsonDocument para menos overhead
    {
        StaticJsonDocument<384> doc;
        String uniqueId = String(sysConfig->mqtt_client_id) + "_rediscover";
        String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/button/" + uniqueId + "/config";

        doc["name"] = "Redescubrir";
        doc["unique_id"] = uniqueId;
        doc["cmd_t"] = baseTopic + "/system/rediscover";
        doc["avty_t"] = availabilityTopic;
        doc["pl_prs"] = "PRESS";
        doc["ic"] = "mdi:refresh";

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(sysConfig->mqtt_client_id);
        dev["name"] = sysConfig->device_name;
        dev["mf"] = "Dirasmart";
        dev["sw"] = FIRMWARE_VERSION;

        String payload;
        serializeJson(doc, payload);
        mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
    }

    delay(50);  // Pequeña pausa entre publicaciones

    // Reboot button
    {
        StaticJsonDocument<384> doc;
        String uniqueId = String(sysConfig->mqtt_client_id) + "_reboot";
        String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/button/" + uniqueId + "/config";

        doc["name"] = "Reiniciar";
        doc["unique_id"] = uniqueId;
        doc["cmd_t"] = baseTopic + "/system/reboot";
        doc["avty_t"] = availabilityTopic;
        doc["pl_prs"] = "PRESS";
        doc["ic"] = "mdi:restart";
        doc["dev_cla"] = "restart";

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(sysConfig->mqtt_client_id);
        dev["name"] = sysConfig->device_name;
        dev["mf"] = "Dirasmart";
        dev["sw"] = FIRMWARE_VERSION;

        String payload;
        serializeJson(doc, payload);
        mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
    }

    Serial.println("[MQTT] System buttons published");
}

void MQTTClientManager::publishDiagnosticSensors() {
    String sysStateTopic = baseTopic + "/diagnostics";

    // WiFi Signal Strength (RSSI)
    {
        StaticJsonDocument<384> doc;
        String uniqueId = String(sysConfig->mqtt_client_id) + "_wifi_signal";
        String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/" + uniqueId + "/config";

        doc["name"] = "WiFi Signal";
        doc["uniq_id"] = uniqueId;
        doc["stat_t"] = sysStateTopic;
        doc["val_tpl"] = "{{ value_json.rssi }}";
        doc["unit_of_meas"] = "dBm";
        doc["dev_cla"] = "signal_strength";
        doc["ent_cat"] = "diagnostic";
        doc["ic"] = "mdi:wifi";
        doc["avty_t"] = availabilityTopic;

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(sysConfig->mqtt_client_id);
        dev["name"] = sysConfig->device_name;
        dev["mf"] = "Dirasmart";
        dev["sw"] = FIRMWARE_VERSION;

        String payload;
        serializeJson(doc, payload);
        mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
    }
    delay(30);

    // IP Address
    {
        StaticJsonDocument<384> doc;
        String uniqueId = String(sysConfig->mqtt_client_id) + "_ip_address";
        String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/" + uniqueId + "/config";

        doc["name"] = "IP Address";
        doc["uniq_id"] = uniqueId;
        doc["stat_t"] = sysStateTopic;
        doc["val_tpl"] = "{{ value_json.ip }}";
        doc["ent_cat"] = "diagnostic";
        doc["ic"] = "mdi:ip-network";
        doc["avty_t"] = availabilityTopic;

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(sysConfig->mqtt_client_id);
        dev["name"] = sysConfig->device_name;
        dev["mf"] = "Dirasmart";
        dev["sw"] = FIRMWARE_VERSION;

        String payload;
        serializeJson(doc, payload);
        mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
    }
    delay(30);

    // MAC Address
    {
        StaticJsonDocument<384> doc;
        String uniqueId = String(sysConfig->mqtt_client_id) + "_mac_address";
        String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/" + uniqueId + "/config";

        doc["name"] = "MAC Address";
        doc["uniq_id"] = uniqueId;
        doc["stat_t"] = sysStateTopic;
        doc["val_tpl"] = "{{ value_json.mac }}";
        doc["ent_cat"] = "diagnostic";
        doc["ic"] = "mdi:network-outline";
        doc["avty_t"] = availabilityTopic;

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(sysConfig->mqtt_client_id);
        dev["name"] = sysConfig->device_name;
        dev["mf"] = "Dirasmart";
        dev["sw"] = FIRMWARE_VERSION;

        String payload;
        serializeJson(doc, payload);
        mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
    }
    delay(30);

    // SSID
    {
        StaticJsonDocument<384> doc;
        String uniqueId = String(sysConfig->mqtt_client_id) + "_ssid";
        String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/" + uniqueId + "/config";

        doc["name"] = "WiFi SSID";
        doc["uniq_id"] = uniqueId;
        doc["stat_t"] = sysStateTopic;
        doc["val_tpl"] = "{{ value_json.ssid }}";
        doc["ent_cat"] = "diagnostic";
        doc["ic"] = "mdi:wifi-settings";
        doc["avty_t"] = availabilityTopic;

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(sysConfig->mqtt_client_id);
        dev["name"] = sysConfig->device_name;
        dev["mf"] = "Dirasmart";
        dev["sw"] = FIRMWARE_VERSION;

        String payload;
        serializeJson(doc, payload);
        mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
    }
    delay(30);

    // Uptime
    {
        StaticJsonDocument<384> doc;
        String uniqueId = String(sysConfig->mqtt_client_id) + "_uptime";
        String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/" + uniqueId + "/config";

        doc["name"] = "Uptime";
        doc["uniq_id"] = uniqueId;
        doc["stat_t"] = sysStateTopic;
        doc["val_tpl"] = "{{ value_json.uptime }}";
        doc["unit_of_meas"] = "s";
        doc["dev_cla"] = "duration";
        doc["ent_cat"] = "diagnostic";
        doc["ic"] = "mdi:timer-outline";
        doc["avty_t"] = availabilityTopic;

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(sysConfig->mqtt_client_id);
        dev["name"] = sysConfig->device_name;
        dev["mf"] = "Dirasmart";
        dev["sw"] = FIRMWARE_VERSION;

        String payload;
        serializeJson(doc, payload);
        mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
    }
    delay(30);

    // Free Heap
    {
        StaticJsonDocument<384> doc;
        String uniqueId = String(sysConfig->mqtt_client_id) + "_free_heap";
        String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/" + uniqueId + "/config";

        doc["name"] = "Free Memory";
        doc["uniq_id"] = uniqueId;
        doc["stat_t"] = sysStateTopic;
        doc["val_tpl"] = "{{ value_json.heap }}";
        doc["unit_of_meas"] = "B";
        doc["dev_cla"] = "data_size";
        doc["ent_cat"] = "diagnostic";
        doc["ic"] = "mdi:memory";
        doc["avty_t"] = availabilityTopic;

        JsonObject dev = doc.createNestedObject("dev");
        JsonArray ids = dev.createNestedArray("ids");
        ids.add(sysConfig->mqtt_client_id);
        dev["name"] = sysConfig->device_name;
        dev["mf"] = "Dirasmart";
        dev["sw"] = FIRMWARE_VERSION;

        String payload;
        serializeJson(doc, payload);
        mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
    }

    Serial.println("[MQTT] Diagnostic sensors published");
}

void MQTTClientManager::publishCoverDiscovery(const SavedDevice* device) {
    StaticJsonDocument<512> doc;

    String uniqueId = String(sysConfig->mqtt_client_id) + "_" + String(device->id);
    String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/cover/" + uniqueId + "/config";

    doc["name"] = device->name;
    doc["uniq_id"] = uniqueId;
    doc["dev_cla"] = "curtain";
    doc["cmd_t"] = baseTopic + "/" + String(device->id) + "/set";
    doc["stat_t"] = baseTopic + "/" + String(device->id) + "/state";
    doc["avty_t"] = availabilityTopic;
    doc["pl_open"] = "OPEN";
    doc["pl_cls"] = "CLOSE";
    doc["pl_stop"] = "STOP";

    JsonObject dev = doc.createNestedObject("dev");
    JsonArray ids = dev.createNestedArray("ids");
    ids.add(sysConfig->mqtt_client_id);
    dev["name"] = sysConfig->device_name;
    dev["mf"] = "Dirasmart";
    dev["sw"] = FIRMWARE_VERSION;

    String payload;
    serializeJson(doc, payload);
    mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
    delay(30);
}

void MQTTClientManager::publishGateDiscovery(const SavedDevice* device) {
    StaticJsonDocument<512> doc;

    String uniqueId = String(sysConfig->mqtt_client_id) + "_" + String(device->id);
    String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/cover/" + uniqueId + "/config";

    doc["name"] = device->name;
    doc["uniq_id"] = uniqueId;
    doc["dev_cla"] = "garage";
    doc["cmd_t"] = baseTopic + "/" + String(device->id) + "/set";
    doc["stat_t"] = baseTopic + "/" + String(device->id) + "/state";
    doc["avty_t"] = availabilityTopic;
    doc["pl_open"] = "TOGGLE";
    doc["pl_cls"] = "CLOSE";
    doc["pl_stop"] = "TOGGLE";

    JsonObject dev = doc.createNestedObject("dev");
    JsonArray ids = dev.createNestedArray("ids");
    ids.add(sysConfig->mqtt_client_id);
    dev["name"] = sysConfig->device_name;
    dev["mf"] = "Dirasmart";
    dev["sw"] = FIRMWARE_VERSION;

    String payload;
    serializeJson(doc, payload);
    mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
    delay(30);
}

void MQTTClientManager::publishSwitchDiscovery(const SavedDevice* device) {
    StaticJsonDocument<512> doc;

    String uniqueId = String(sysConfig->mqtt_client_id) + "_" + String(device->id);
    String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/switch/" + uniqueId + "/config";

    doc["name"] = device->name;
    doc["uniq_id"] = uniqueId;
    doc["cmd_t"] = baseTopic + "/" + String(device->id) + "/set";
    doc["stat_t"] = baseTopic + "/" + String(device->id) + "/state";
    doc["avty_t"] = availabilityTopic;
    doc["pl_on"] = "ON";
    doc["pl_off"] = "OFF";

    JsonObject dev = doc.createNestedObject("dev");
    JsonArray ids = dev.createNestedArray("ids");
    ids.add(sysConfig->mqtt_client_id);
    dev["name"] = sysConfig->device_name;
    dev["mf"] = "Dirasmart";
    dev["sw"] = FIRMWARE_VERSION;

    String payload;
    serializeJson(doc, payload);
    mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
    delay(30);
}

void MQTTClientManager::publishButtonDiscovery(const SavedDevice* device, uint8_t signalIndex) {
    StaticJsonDocument<448> doc;

    String uniqueId = String(sysConfig->mqtt_client_id) + "_" + String(device->id) + "_" + String(signalIndex);
    String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/button/" + uniqueId + "/config";

    String signalName = String(device->signalNames[signalIndex]);
    if (signalName.length() == 0) {
        signalName = "Senal " + String(signalIndex + 1);
    }

    doc["name"] = String(device->name) + " - " + signalName;
    doc["uniq_id"] = uniqueId;
    doc["cmd_t"] = baseTopic + "/" + String(device->id) + "/" + String(signalIndex) + "/set";
    doc["avty_t"] = availabilityTopic;
    doc["pl_prs"] = "PRESS";

    JsonObject dev = doc.createNestedObject("dev");
    JsonArray ids = dev.createNestedArray("ids");
    ids.add(sysConfig->mqtt_client_id);
    dev["name"] = sysConfig->device_name;
    dev["mf"] = "Dirasmart";
    dev["sw"] = FIRMWARE_VERSION;

    String payload;
    serializeJson(doc, payload);
    mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
    delay(30);
}

void MQTTClientManager::removeDiscovery() {
    if (!mqtt.connected()) return;

    uint8_t count = storage.getDeviceCount();
    SavedDevice device;

    for (uint8_t i = 0; i < count; i++) {
        if (!storage.getDeviceByIndex(i, &device)) continue;
        String uniqueId = String(sysConfig->mqtt_client_id) + "_" + String(device.id);

        // Eliminar discovery según tipo
        String topics[] = {
            String(MQTT_DISCOVERY_PREFIX) + "/cover/" + uniqueId + "/config",
            String(MQTT_DISCOVERY_PREFIX) + "/switch/" + uniqueId + "/config",
            String(MQTT_DISCOVERY_PREFIX) + "/light/" + uniqueId + "/config"
        };

        for (const String& topic : topics) {
            mqtt.publish(topic.c_str(), "", true);
        }

        // Eliminar botones de señales
        for (uint8_t j = 0; j < 4; j++) {
            String btnId = uniqueId + "_" + String(j);
            String btnTopic = String(MQTT_DISCOVERY_PREFIX) + "/button/" + btnId + "/config";
            mqtt.publish(btnTopic.c_str(), "", true);
        }
    }
}
