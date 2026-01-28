/*
 * ==============================================
 * RF Controller - ESP32 + CC1101 - FINAL
 * ==============================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>

#include "config.h"
#include "Storage.h"
#include "CC1101_RF.h"
#include "SomfyRTS.h"
#include "DooyaBidir.h"
#include "AOK_Protocol.h"
#include "WebServerManager.h"
#include "MQTTClient.h"
#include "TimeManager.h"

// Configuración del sistema
SystemConfig systemConfig;

// Variables de estado
bool systemReady = false;
unsigned long lastStatusPrint = 0;

// Prototipos
void initSystem();
void printStatus();
void handleRFCommand(const char* deviceId, const char* command);
void WiFiEvent(WiFiEvent_t event);

// Callback para eventos WiFi
void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("[WiFi] Conectado al AP");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[WiFi] IP obtenida: %s\n", WiFi.localIP().toString().c_str());
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("[WiFi] Desconectado - reconectando...");
            if (systemConfig.wifi_configured && strlen(systemConfig.wifi_ssid) > 0) {
                WiFi.reconnect();
            }
            break;
        default:
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("==============================================");
    Serial.println("   RF Controller - ESP32 + CC1101");
    Serial.printf("   Version %s\n", FIRMWARE_VERSION);
    Serial.println("==============================================");
    Serial.println();

    initSystem();
}

void loop() {
    if (!systemReady) {
        delay(100);
        return;
    }

    webServer.loop();

    if (systemConfig.mqtt_enabled && WiFi.status() == WL_CONNECTED) {
        mqttClient.loop();
    }

    if (millis() - lastStatusPrint > 60000) {
        printStatus();
        lastStatusPrint = millis();
    }
}

void initSystem() {
    // 1. WiFi AP+STA (modo mixto para permitir escaneo de redes)
    Serial.println("[1/6] Configurando WiFi...");
    Serial.flush();

    // Registrar callback de eventos WiFi para reconexión automática
    WiFi.onEvent(WiFiEvent);

    // Habilitar auto-reconexión nativa del ESP32
    WiFi.setAutoReconnect(true);

    // Guardar credenciales en flash del ESP32 (doble respaldo)
    WiFi.persistent(true);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.println("[OK] WiFi AP iniciado (modo mixto)");
    Serial.flush();

    // 2. Storage
    Serial.println("[2/6] Inicializando Storage...");
    Serial.flush();
    if (!storage.begin()) {
        Serial.println("[ERROR] Storage falló!");
        return;
    }
    Serial.println("[OK] Storage inicializado");
    Serial.flush();

    // Cargar configuración
    storage.setDefaultConfig(&systemConfig);
    storage.loadConfig(&systemConfig);

    // Intentar WiFi cliente si está configurado (manteniendo AP activo)
    if (systemConfig.wifi_configured && strlen(systemConfig.wifi_ssid) > 0) {
        Serial.printf("[INFO] Conectando a %s...\n", systemConfig.wifi_ssid);
        // Configurar hostname antes de conectar
        WiFi.setHostname(systemConfig.device_name);
        // Ya estamos en WIFI_AP_STA, solo iniciamos conexión
        WiFi.begin(systemConfig.wifi_ssid, systemConfig.wifi_password);

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("\n[OK] Conectado! IP: %s\n", WiFi.localIP().toString().c_str());
            // Apagar AP cuando hay WiFi conectado
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            Serial.println("[INFO] AP apagado (WiFi conectado)");
        } else {
            Serial.println("\n[WARNING] No se pudo conectar, AP sigue activo");
        }
    }

    // 3. CC1101 - Solo inicializar si hay WiFi conectado (evita interferencia)
    Serial.println("[3/6] CC1101...");
    Serial.flush();
    if (WiFi.status() == WL_CONNECTED) {
        if (!rfModule.begin()) {
            Serial.println("[WARNING] CC1101 no detectado");
        } else {
            rfModule.setFrequency(systemConfig.default_frequency);
            rfModule.setModulation(systemConfig.default_modulation);
            Serial.println("[OK] CC1101 inicializado");
            somfyRTS.begin(CC1101_GDO0);
            dooyaBidir.begin();
            aokProtocol.begin();
        }
    } else {
        Serial.println("[INFO] CC1101 desactivado hasta conectar WiFi (evita interferencia)");
    }
    Serial.flush();

    // 4. WebServer
    Serial.println("[4/6] Iniciando WebServer...");
    Serial.flush();
    if (!webServer.begin(&systemConfig)) {
        Serial.println("[WARNING] WebServer falló");
    } else {
        Serial.println("[OK] WebServer iniciado");
    }
    Serial.flush();

    // 5. Time
    Serial.println("[5/6] Configurando hora...");
    if (WiFi.status() == WL_CONNECTED) {
        timeManager.begin(&systemConfig);
    } else {
        Serial.println("[INFO] Sin WiFi, hora no sincronizada");
    }

    // 6. MQTT
    Serial.println("[6/6] Configurando MQTT...");
    if (systemConfig.mqtt_enabled && WiFi.status() == WL_CONNECTED) {
        mqttClient.begin(&systemConfig);
        mqttClient.setCommandCallback(handleRFCommand);
        Serial.println("[OK] MQTT configurado");
    } else {
        Serial.println("[INFO] MQTT deshabilitado o sin WiFi");
    }

    // Contar dispositivos sin cargarlos todos en RAM
    // NOTA: No cargamos el array completo porque consume demasiada RAM
    uint8_t deviceCount = 0;
    if (LittleFS.exists(DEVICES_FILE)) {
        File f = LittleFS.open(DEVICES_FILE, "r");
        if (f) {
            // Contar llaves abiertas para estimar dispositivos
            String content = f.readString();
            f.close();
            for (size_t i = 0; i < content.length(); i++) {
                if (content[i] == '{') deviceCount++;
            }
            if (deviceCount > 0) deviceCount--; // El array tiene una llave extra
        }
    }
    Serial.printf("[INFO] ~%d dispositivos guardados\n", deviceCount);

    systemReady = true;

    Serial.println();
    Serial.println("==============================================");
    Serial.println("   SISTEMA LISTO - RF CONTROLLER");
    Serial.println("==============================================");
    Serial.printf("   IP: %s\n", webServer.getIPAddress().c_str());
    Serial.printf("   Modo: %s\n", webServer.isAPMode() ? "Access Point" : "WiFi Cliente");
    if (webServer.isAPMode()) {
        Serial.printf("   SSID: %s\n", AP_SSID);
        Serial.printf("   Pass: %s\n", AP_PASSWORD);
    }
    if (rfModule.isConnected()) {
        Serial.printf("   RF: %.2f MHz\n", rfModule.getFrequency());
    }
    Serial.println("==============================================");
    Serial.printf("   Heap libre: %d bytes\n", ESP.getFreeHeap());
    Serial.println("==============================================");
    Serial.println();
}

void printStatus() {
    Serial.printf("Uptime: %lu s | Heap: %d bytes\n", millis() / 1000, ESP.getFreeHeap());
}

void handleRFCommand(const char* deviceId, const char* command) {
    Serial.printf("[Main] Comando: %s -> %s\n", deviceId, command);

    // Cargar solo UN dispositivo (no el array completo)
    SavedDevice device;
    if (!storage.getDevice(deviceId, &device)) {
        Serial.println("[Main] Dispositivo no encontrado");
        return;
    }

    String cmd = String(command);
    cmd.toLowerCase();

    // Somfy RTS
    if (device.type == DEVICE_CURTAIN_SOMFY) {
        rfModule.setFrequency(SOMFY_FREQUENCY);
        somfyRTS.setRemote(&device.somfy);

        bool success = false;
        if (cmd == "open" || cmd == "up") success = somfyRTS.sendUp();
        else if (cmd == "close" || cmd == "down") success = somfyRTS.sendDown();
        else if (cmd == "stop" || cmd == "my") success = somfyRTS.sendStop();
        else if (cmd == "prog") success = somfyRTS.sendProg();

        if (success) {
            storage.updateSomfyRollingCode(deviceId, somfyRTS.getRollingCode());
        }
        return;
    }

    // Dooya Bidireccional
    if (device.type == DEVICE_CURTAIN_DOOYA_BIDIR) {
        dooyaBidir.setRemote(&device.dooyaBidir);

        if (cmd == "open" || cmd == "up") dooyaBidir.sendUp();
        else if (cmd == "close" || cmd == "down") dooyaBidir.sendDown();
        else if (cmd == "stop") dooyaBidir.sendStop();
        else if (cmd == "prog") dooyaBidir.sendProg();
        return;
    }

    // A-OK AC114
    if (device.type == DEVICE_CURTAIN_AOK) {
        aokProtocol.setRemoteId(device.aok.remoteId);
        aokProtocol.setChannel(device.aok.channel);

        if (cmd == "open" || cmd == "up") aokProtocol.sendUp();
        else if (cmd == "close" || cmd == "down") aokProtocol.sendDown();
        else if (cmd == "stop") aokProtocol.sendStop();
        else if (cmd == "prog") aokProtocol.sendProgram();
        return;
    }

    // Dispositivos con señales capturadas
    int signalIndex = -1;
    switch (device.type) {
        case DEVICE_CURTAIN:
            if (cmd == "open" || cmd == "up") signalIndex = 0;
            else if (cmd == "close" || cmd == "down") signalIndex = 1;
            else if (cmd == "stop") signalIndex = 2;
            break;
        case DEVICE_SWITCH:
        case DEVICE_LIGHT:
            if (cmd == "on") signalIndex = 0;
            else if (cmd == "off") signalIndex = 1;
            break;
        case DEVICE_BUTTON:
            signalIndex = 0;
            break;
        case DEVICE_GATE:
            if (cmd == "open" || cmd == "toggle") signalIndex = 0;
            else if (cmd == "close") signalIndex = 1;
            break;
        default:
            signalIndex = cmd.toInt();
            break;
    }

    if (signalIndex >= 0 && signalIndex < device.signalCount && device.signals[signalIndex].valid) {
        rfModule.setFrequency(device.signals[signalIndex].frequency);
        rfModule.setModulation(device.signals[signalIndex].modulation);
        rfModule.transmitSignal(&device.signals[signalIndex]);
    }
}
