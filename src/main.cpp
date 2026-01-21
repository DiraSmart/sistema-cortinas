/*
 * ==============================================
 * RF Controller - ESP32 + CC1101
 * ==============================================
 *
 * Sistema de control RF para dispositivos genéricos
 * - Copy/Replay de señales RF
 * - Interfaz web de configuración
 * - Integración MQTT con Home Assistant
 * - Soporte para cortinas, interruptores, portones, etc.
 *
 * Conexiones ESP32 -> CC1101:
 * - GDO0 -> GPIO 13
 * - GDO2 -> GPIO 12
 * - CSN  -> GPIO 5
 * - SCK  -> GPIO 18
 * - MISO -> GPIO 19
 * - MOSI -> GPIO 23
 * - VCC  -> 3.3V
 * - GND  -> GND
 *
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
#include "WebServer.h"
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

void setup() {
    // Inicializar Serial
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("==============================================");
    Serial.println("   RF Controller - ESP32 + CC1101");
    Serial.println("   Version 1.0.0");
    Serial.println("==============================================");
    Serial.println();

    initSystem();
}

void loop() {
    if (!systemReady) return;

    // Loop del servidor web
    webServer.loop();

    // Loop del cliente MQTT
    mqttClient.loop();

    // Imprimir estado cada 30 segundos
    if (millis() - lastStatusPrint > 30000) {
        lastStatusPrint = millis();
        printStatus();
    }
}

void initSystem() {
    Serial.println("[System] Iniciando sistema...");

    // 1. Inicializar almacenamiento
    Serial.println("\n[1/7] Inicializando almacenamiento...");
    if (!storage.begin()) {
        Serial.println("[ERROR] Fallo al inicializar almacenamiento!");
        return;
    }

    // 2. Cargar configuración
    Serial.println("\n[2/7] Cargando configuración...");
    if (!storage.loadConfig(&systemConfig)) {
        Serial.println("[WARNING] Usando configuración por defecto");
        storage.setDefaultConfig(&systemConfig);
    }

    Serial.printf("  - Nombre: %s\n", systemConfig.device_name);
    Serial.printf("  - WiFi configurado: %s\n", systemConfig.wifi_configured ? "Sí" : "No");
    Serial.printf("  - MQTT habilitado: %s\n", systemConfig.mqtt_enabled ? "Sí" : "No");
    Serial.printf("  - Frecuencia RF: %.2f MHz\n", systemConfig.default_frequency);

    // 3. Inicializar servidor web y WiFi PRIMERO (antes del RF)
    // IMPORTANTE: WiFi debe inicializarse antes que CC1101 para evitar
    // interferencias durante la negociación WiFi 2.4GHz
    Serial.println("\n[3/7] Iniciando WiFi y servidor web...");
    webServer.begin(&systemConfig);

    // 4. Esperar estabilización de WiFi antes de iniciar RF
    Serial.println("\n[4/7] Esperando estabilización de WiFi...");
    if (WiFi.status() == WL_CONNECTED) {
        delay(500);  // Pequeña pausa para estabilizar
        Serial.println("[OK] WiFi estable");
    } else {
        Serial.println("[INFO] Modo AP activo");
    }

    // 5. Inicializar módulo RF DESPUÉS de WiFi estable
    // Esto evita interferencias del ESP32 2.4GHz con el CC1101
    Serial.println("\n[5/7] Inicializando módulo CC1101...");
    if (!rfModule.begin()) {
        Serial.println("[WARNING] CC1101 no detectado, continuando sin RF");
    } else {
        rfModule.setFrequency(systemConfig.default_frequency);
        rfModule.setModulation(systemConfig.default_modulation);
        Serial.println("[OK] CC1101 inicializado correctamente");

        // Inicializar módulo Somfy RTS (usa el mismo pin GDO0 para TX)
        somfyRTS.begin(CC1101_GDO0);
        Serial.println("[OK] Somfy RTS inicializado");

        // Inicializar módulo Dooya Bidireccional
        dooyaBidir.begin();
        Serial.println("[OK] Dooya Bidireccional inicializado");
    }

    // 6. Sincronizar tiempo (si hay WiFi)
    Serial.println("\n[6/7] Configurando hora...");
    if (WiFi.status() == WL_CONNECTED) {
        timeManager.begin(&systemConfig);
    } else {
        Serial.println("[WARNING] Sin WiFi, hora no sincronizada");
    }

    // 7. Inicializar MQTT y publicar Auto-Discovery
    Serial.println("\n[7/7] Configurando MQTT...");
    if (systemConfig.mqtt_enabled && WiFi.status() == WL_CONNECTED) {
        mqttClient.begin(&systemConfig);
        mqttClient.setCommandCallback(handleRFCommand);

        // Los dispositivos se publican automáticamente con MQTT Discovery
        // Home Assistant los detectará como:
        // - cover.* para cortinas
        // - switch.* para interruptores
        // - button.* para otros dispositivos
        Serial.println("[OK] MQTT Discovery publicado para Home Assistant");
    } else if (systemConfig.mqtt_enabled) {
        Serial.println("[WARNING] MQTT habilitado pero sin WiFi");
    } else {
        Serial.println("[INFO] MQTT deshabilitado");
    }

    // Cargar dispositivos
    SavedDevice devices[MAX_DEVICES];
    uint8_t deviceCount = 0;
    storage.loadDevices(devices, &deviceCount);
    Serial.printf("\n[System] %d dispositivos cargados\n", deviceCount);

    // Sistema listo
    systemReady = true;

    Serial.println();
    Serial.println("==============================================");
    Serial.println("   SISTEMA LISTO - DIRASMART RF CONTROLLER");
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
    if (mqttClient.isConnected()) {
        Serial.println("   MQTT: Conectado (Auto-Discovery activo)");
    }
    Serial.println("==============================================");
    Serial.println();
}

void printStatus() {
    Serial.println("\n--- Estado del Sistema ---");
    Serial.printf("Uptime: %lu segundos\n", millis() / 1000);
    Serial.printf("Heap libre: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("WiFi: %s\n", webServer.isConnected() ? "Conectado" : "Desconectado");
    Serial.printf("MQTT: %s\n", mqttClient.isConnected() ? "Conectado" : "Desconectado");
    Serial.printf("RF CC1101: %s\n", rfModule.isConnected() ? "OK" : "Error");

    if (rfModule.isConnected()) {
        Serial.printf("RF Frecuencia: %.2f MHz\n", rfModule.getFrequency());
        Serial.printf("RF RSSI: %d dBm\n", rfModule.getRSSI());
    }

    if (timeManager.isSynced()) {
        Serial.printf("Hora: %s\n", timeManager.getDateTimeString().c_str());
    }

    Serial.println("--------------------------\n");
}

void handleRFCommand(const char* deviceId, const char* command) {
    Serial.printf("[Main] Comando RF recibido: %s -> %s\n", deviceId, command);

    SavedDevice device;
    if (!storage.getDevice(deviceId, &device)) {
        Serial.println("[Main] Dispositivo no encontrado");
        return;
    }

    String cmd = String(command);
    cmd.toLowerCase();

    // Manejar dispositivos Somfy RTS de forma especial (rolling code)
    if (device.type == DEVICE_CURTAIN_SOMFY) {
        Serial.printf("[Main] Comando Somfy RTS para %s\n", device.name);

        // Configurar el módulo RF para Somfy (433.42 MHz)
        rfModule.setFrequency(SOMFY_FREQUENCY);

        // Configurar el control virtual
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
            device.lastUsed = millis();
            storage.updateDevice(deviceId, &device);
        }
        return;
    }

    // Manejar dispositivos Dooya Bidireccional (DDxxxx con FSK)
    if (device.type == DEVICE_CURTAIN_DOOYA_BIDIR) {
        Serial.printf("[Main] Comando Dooya Bidir para %s\n", device.name);

        // Configurar el control virtual
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
            device.lastUsed = millis();
            storage.updateDevice(deviceId, &device);
        }
        return;
    }

    // Interpretar comando según tipo de dispositivo funcional (señales capturadas)
    int signalIndex = -1;

    switch (device.type) {
        case DEVICE_CURTAIN:
            // Cortinas: señal 0=abrir, 1=cerrar, 2=parar
            if (cmd == "open" || cmd == "up") signalIndex = 0;
            else if (cmd == "close" || cmd == "down") signalIndex = 1;
            else if (cmd == "stop") signalIndex = 2;
            break;

        case DEVICE_SWITCH:
        case DEVICE_LIGHT:
            // Interruptores/Luces: 0=encender, 1=apagar
            if (cmd == "on" || cmd == "1") signalIndex = 0;
            else if (cmd == "off" || cmd == "0") signalIndex = 1;
            else if (cmd == "toggle") signalIndex = 0;
            break;

        case DEVICE_BUTTON:
            // Botones: cualquier comando activa señal 0
            signalIndex = 0;
            break;

        case DEVICE_GATE:
            // Portones: 0=toggle/abrir, 1=cerrar
            if (cmd == "open" || cmd == "toggle") signalIndex = 0;
            else if (cmd == "close") signalIndex = 1;
            break;

        case DEVICE_FAN:
            // Ventiladores: 0=encender, 1=apagar, 2=velocidad
            if (cmd == "on") signalIndex = 0;
            else if (cmd == "off") signalIndex = 1;
            else if (cmd == "speed") signalIndex = 2;
            break;

        case DEVICE_DIMMER:
            // Dimmers: 0=encender, 1=apagar, 2=subir, 3=bajar
            if (cmd == "on") signalIndex = 0;
            else if (cmd == "off") signalIndex = 1;
            else if (cmd == "up" || cmd == "brightness_up") signalIndex = 2;
            else if (cmd == "down" || cmd == "brightness_down") signalIndex = 3;
            break;

        default:
            // Para DEVICE_OTHER y otros, intentar como índice numérico
            signalIndex = cmd.toInt();
            break;
    }

    if (signalIndex >= 0 && signalIndex < device.signalCount) {
        if (device.signals[signalIndex].valid) {
            Serial.printf("[Main] Transmitiendo señal %d de %s\n", signalIndex, device.name);

            rfModule.setFrequency(device.signals[signalIndex].frequency);
            rfModule.setModulation(device.signals[signalIndex].modulation);
            rfModule.transmitSignal(&device.signals[signalIndex]);

            // Actualizar último uso
            device.lastUsed = millis();
            storage.updateDevice(deviceId, &device);
        } else {
            Serial.printf("[Main] Señal %d no válida\n", signalIndex);
        }
    } else {
        Serial.printf("[Main] Índice de señal inválido: %d\n", signalIndex);
    }
}
