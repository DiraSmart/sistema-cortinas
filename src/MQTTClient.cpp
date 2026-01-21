#include "MQTTClient.h"
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

    if (!config->mqtt_enabled || strlen(config->mqtt_server) == 0) {
        Serial.println("[MQTT] MQTT deshabilitado");
        enabled = false;
        return false;
    }

    Serial.printf("[MQTT] Configurando servidor: %s:%d\n",
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
        }

        // Publicar estado inicial
        publishAllStates();
        publishSystemStatus();

        return true;
    } else {
        Serial.printf("[MQTT] Error de conexión: %d\n", mqtt.state());
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

    SavedDevice devices[MAX_DEVICES];
    uint8_t count = 0;
    storage.loadDevices(devices, &count);

    for (uint8_t i = 0; i < count; i++) {
        publishDeviceState(devices[i].id, "unknown");
    }
}

void MQTTClientManager::publishSystemStatus() {
    if (!mqtt.connected()) return;

    DynamicJsonDocument doc(512);
    doc["uptime"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["rf_connected"] = rfModule.isConnected();
    doc["rf_frequency"] = rfModule.getFrequency();
    doc["wifi_rssi"] = WiFi.RSSI();

    String payload;
    serializeJson(doc, payload);

    String topic = baseTopic + "/system";
    mqtt.publish(topic.c_str(), payload.c_str(), true);
}

// ============================================
// Home Assistant Discovery
// ============================================

void MQTTClientManager::publishDiscovery() {
    if (!mqtt.connected() || !sysConfig->mqtt_discovery) return;

    Serial.println("[MQTT] Publicando Home Assistant Discovery...");

    SavedDevice devices[MAX_DEVICES];
    uint8_t count = 0;
    storage.loadDevices(devices, &count);

    for (uint8_t i = 0; i < count; i++) {
        switch (devices[i].type) {
            case DEVICE_CURTAIN:
            case DEVICE_CURTAIN_SOMFY:
            case DEVICE_CURTAIN_DOOYA_BIDIR:
                // Cortinas (genéricas, Somfy y Dooya) se publican como cover
                publishCoverDiscovery(&devices[i]);
                break;

            case DEVICE_SWITCH:
            case DEVICE_LIGHT:
                // Interruptores y luces como switch
                publishSwitchDiscovery(&devices[i]);
                break;

            case DEVICE_GATE:
                // Portones como cover tipo garage
                publishGateDiscovery(&devices[i]);
                break;

            case DEVICE_FAN:
                // Ventiladores como fan (simplificado como switch por ahora)
                publishSwitchDiscovery(&devices[i]);
                break;

            default:
                // Botones, dimmers y otros: cada señal como botón
                for (uint8_t j = 0; j < devices[i].signalCount; j++) {
                    if (devices[i].signals[j].valid) {
                        publishButtonDiscovery(&devices[i], j);
                    }
                }
                break;
        }
    }

    Serial.printf("[MQTT] Discovery publicado para %d dispositivos\n", count);
}

void MQTTClientManager::publishCoverDiscovery(const SavedDevice* device) {
    DynamicJsonDocument doc(1024);

    String uniqueId = String(sysConfig->mqtt_client_id) + "_" + String(device->id);
    String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/cover/" + uniqueId + "/config";

    doc["name"] = device->name;
    doc["unique_id"] = uniqueId;
    doc["device_class"] = "curtain";
    doc["command_topic"] = baseTopic + "/" + String(device->id) + "/set";
    doc["state_topic"] = baseTopic + "/" + String(device->id) + "/state";
    doc["availability_topic"] = availabilityTopic;
    doc["payload_open"] = "OPEN";
    doc["payload_close"] = "CLOSE";
    doc["payload_stop"] = "STOP";

    // Información del dispositivo
    JsonObject deviceInfo = doc.createNestedObject("device");
    deviceInfo["identifiers"][0] = sysConfig->mqtt_client_id;
    deviceInfo["name"] = sysConfig->device_name;
    deviceInfo["model"] = "ESP32 RF Controller";
    deviceInfo["manufacturer"] = "DIY";

    String payload;
    serializeJson(doc, payload);
    mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
}

void MQTTClientManager::publishGateDiscovery(const SavedDevice* device) {
    DynamicJsonDocument doc(1024);

    String uniqueId = String(sysConfig->mqtt_client_id) + "_" + String(device->id);
    String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/cover/" + uniqueId + "/config";

    doc["name"] = device->name;
    doc["unique_id"] = uniqueId;
    doc["device_class"] = "garage";
    doc["command_topic"] = baseTopic + "/" + String(device->id) + "/set";
    doc["state_topic"] = baseTopic + "/" + String(device->id) + "/state";
    doc["availability_topic"] = availabilityTopic;
    doc["payload_open"] = "TOGGLE";
    doc["payload_close"] = "CLOSE";
    doc["payload_stop"] = "TOGGLE";

    // Información del dispositivo
    JsonObject deviceInfo = doc.createNestedObject("device");
    deviceInfo["identifiers"][0] = sysConfig->mqtt_client_id;
    deviceInfo["name"] = sysConfig->device_name;
    deviceInfo["model"] = "ESP32 RF Controller";
    deviceInfo["manufacturer"] = "DIY";

    String payload;
    serializeJson(doc, payload);
    mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
}

void MQTTClientManager::publishSwitchDiscovery(const SavedDevice* device) {
    DynamicJsonDocument doc(1024);

    String uniqueId = String(sysConfig->mqtt_client_id) + "_" + String(device->id);
    String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/switch/" + uniqueId + "/config";

    doc["name"] = device->name;
    doc["unique_id"] = uniqueId;
    doc["command_topic"] = baseTopic + "/" + String(device->id) + "/set";
    doc["state_topic"] = baseTopic + "/" + String(device->id) + "/state";
    doc["availability_topic"] = availabilityTopic;
    doc["payload_on"] = "ON";
    doc["payload_off"] = "OFF";

    if (device->type == DEVICE_LIGHT) {
        doc["device_class"] = "light";
    }

    // Información del dispositivo
    JsonObject deviceInfo = doc.createNestedObject("device");
    deviceInfo["identifiers"][0] = sysConfig->mqtt_client_id;
    deviceInfo["name"] = sysConfig->device_name;
    deviceInfo["model"] = "ESP32 RF Controller";
    deviceInfo["manufacturer"] = "DIY";

    String payload;
    serializeJson(doc, payload);
    mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
}

void MQTTClientManager::publishButtonDiscovery(const SavedDevice* device, uint8_t signalIndex) {
    DynamicJsonDocument doc(1024);

    String uniqueId = String(sysConfig->mqtt_client_id) + "_" + String(device->id) + "_" + String(signalIndex);
    String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/button/" + uniqueId + "/config";

    String signalName = String(device->signalNames[signalIndex]);
    if (signalName.length() == 0) {
        signalName = "Signal " + String(signalIndex + 1);
    }

    doc["name"] = String(device->name) + " - " + signalName;
    doc["unique_id"] = uniqueId;
    doc["command_topic"] = baseTopic + "/" + String(device->id) + "/" + String(signalIndex) + "/set";
    doc["availability_topic"] = availabilityTopic;
    doc["payload_press"] = "PRESS";

    // Información del dispositivo
    JsonObject deviceInfo = doc.createNestedObject("device");
    deviceInfo["identifiers"][0] = sysConfig->mqtt_client_id;
    deviceInfo["name"] = sysConfig->device_name;
    deviceInfo["model"] = "ESP32 RF Controller";
    deviceInfo["manufacturer"] = "DIY";

    String payload;
    serializeJson(doc, payload);
    mqtt.publish(discoveryTopic.c_str(), payload.c_str(), true);
}

void MQTTClientManager::removeDiscovery() {
    if (!mqtt.connected()) return;

    SavedDevice devices[MAX_DEVICES];
    uint8_t count = 0;
    storage.loadDevices(devices, &count);

    for (uint8_t i = 0; i < count; i++) {
        String uniqueId = String(sysConfig->mqtt_client_id) + "_" + String(devices[i].id);

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
