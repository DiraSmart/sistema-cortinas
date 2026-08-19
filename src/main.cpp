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
unsigned long lastWiFiScan = 0;
int8_t currentRSSI = -100;

// WiFi Fallback AP - si no hay WiFi por 2 minutos, activar AP para configuración
#define WIFI_TIMEOUT_MS     120000  // 2 minutos sin WiFi → activar AP
#define WIFI_RETRY_MS       30000   // Reintentar conexión cada 30 segundos
unsigned long wifiDisconnectedSince = 0;
unsigned long lastWiFiRetry = 0;
bool apActive = false;
bool wasConnected = false;

// Caché del último escaneo WiFi (para diagnóstico sin cortar la conexión)
ScannedAP scannedAPs[MAX_SCANNED_APS];
uint8_t scannedAPCount = 0;
unsigned long lastScanTime = 0;
uint8_t currentBSSID[6] = {0};

// Watchdog del módulo RF
unsigned long lastRFCheck = 0;
unsigned long rfDownSince = 0;      // 0 = el módulo responde
bool rfWasOk = true;

// Contador de reinicios por watchdog. RTC_DATA_ATTR sobrevive a ESP.restart()
// pero NO a un corte de energía, que es justo lo que queremos: si el CC1101
// está muerto por hardware el equipo no se queda reiniciándose cada 15 min para
// siempre, y un corte de luz (o una reparación) le da otra oportunidad.
RTC_DATA_ATTR uint32_t rfWatchdogReboots = 0;
#define RF_WATCHDOG_MAX_REBOOTS 3

// Prototipos
void initSystem();
void printStatus();
void checkRFModule();
bool initRFModule();
int wifiScanAndCache();
void handleRFCommand(const char* deviceId, const char* command);
void WiFiEvent(WiFiEvent_t event);
void checkWiFiRoaming();
bool connectToBestAP(const char* ssid, const char* password);
void manageWiFiAndAP();
void wifiConnectNext();

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

// Escanea TODAS las redes y arranca la conexión al AP con mayor RSSI del SSID dado,
// sin importar el canal (clave en oficinas con varias antenas del mismo SSID).
// No espera el resultado: solo bloquea durante el escaneo; manageWiFiAndAP() detecta
// la conexión después. Devuelve true si encontró un AP e inició la conexión.
// Copia a la caché los resultados de un escaneo YA realizado (no escanea).
// La caché permite consultar desde la web qué antenas ve el ESP32 sin tener
// que desconectar la WiFi (que es lo que hace /api/wifi/scan y por eso corta
// la propia petición que la pidió).
// IMPORTANTE: no se llama desde la ruta de arranque del WiFi. Esa ruta se deja
// exactamente igual a la que lleva semanas estable en los equipos en campo.
void cacheScanResults(int n) {
    scannedAPCount = 0;
    for (int i = 0; i < n && scannedAPCount < MAX_SCANNED_APS; i++) {
        const uint8_t* bssid = WiFi.BSSID(i);
        if (!bssid) continue;   // entrada inválida: WiFi.BSSID() devuelve nullptr

        ScannedAP* ap = &scannedAPs[scannedAPCount];
        strncpy(ap->ssid, WiFi.SSID(i).c_str(), 32);
        ap->ssid[32] = '\0';
        memcpy(ap->bssid, bssid, 6);
        ap->rssi = WiFi.RSSI(i);
        ap->channel = WiFi.channel(i);
        scannedAPCount++;
    }
    lastScanTime = millis();
}

// Escaneo bajo demanda para el endpoint de diagnóstico. No desconecta la WiFi.
int wifiScanAndCache() {
    int n = WiFi.scanNetworks(false, false, false, 300);
    if (n > 0) cacheScanResults(n);
    WiFi.scanDelete();
    return n;
}

bool beginBestAP(const char* ssid, const char* password) {
    int n = WiFi.scanNetworks(false, false, false, 300);
    if (n <= 0) {
        WiFi.scanDelete();
        return false;
    }

    int8_t bestRSSI = -127;
    uint8_t bestBSSID[6] = {0};
    int bestChannel = 0;
    bool found = false;

    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == ssid) {
            int8_t rssi = WiFi.RSSI(i);
            if (rssi > bestRSSI) {
                bestRSSI = rssi;
                memcpy(bestBSSID, WiFi.BSSID(i), 6);
                bestChannel = WiFi.channel(i);
                found = true;
            }
        }
    }
    WiFi.scanDelete();

    if (!found) return false;

    Serial.printf("[WiFi] Mejor AP '%s': RSSI %d dB, canal %d, BSSID %02X:%02X:%02X:%02X:%02X:%02X\n",
        ssid, bestRSSI, bestChannel,
        bestBSSID[0], bestBSSID[1], bestBSSID[2], bestBSSID[3], bestBSSID[4], bestBSSID[5]);

    WiFi.setHostname(systemConfig.device_name);
    // Conexión dirigida al BSSID concreto en su canal → siempre la mejor antena
    WiFi.begin(ssid, password, bestChannel, bestBSSID, true);
    return true;
}

// Intenta conectar: primero a la mejor antena de la red principal; si esa red no
// aparece en el escaneo, prueba la red de respaldo. No bloquea (salvo el escaneo).
void wifiConnectNext() {
    if (beginBestAP(systemConfig.wifi_ssid, systemConfig.wifi_password)) return;

    if (strlen(systemConfig.wifi_ssid2) > 0) {
        Serial.println("[WiFi] Red principal no visible, probando red de respaldo...");
        if (beginBestAP(systemConfig.wifi_ssid2, systemConfig.wifi_password2)) return;
    }

    // El escaneo no vio ninguna de las dos redes: intento directo a la principal
    // (por si el AP está oculto o el escaneo falló puntualmente).
    Serial.println("[WiFi] Ninguna red visible en el escaneo, intento directo a la principal...");
    WiFi.setHostname(systemConfig.device_name);
    WiFi.begin(systemConfig.wifi_ssid, systemConfig.wifi_password);
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

    // Gestionar WiFi y AP de respaldo
    manageWiFiAndAP();

    if (systemConfig.mqtt_enabled && WiFi.status() == WL_CONNECTED) {
        mqttClient.loop();
    }

    // Vigilar el CC1101: reintentar init y, si sigue caído, reiniciar el ESP32
    checkRFModule();

    if (millis() - lastStatusPrint > 60000) {
        printStatus();
        lastStatusPrint = millis();
    }

    // WiFi Roaming - buscar mejor AP periódicamente
    #if WIFI_ROAMING_ENABLED
    if (WiFi.status() == WL_CONNECTED && millis() - lastWiFiScan > WIFI_SCAN_INTERVAL) {
        checkWiFiRoaming();
        lastWiFiScan = millis();
    }
    #endif
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

    // Inicializar estado de AP
    apActive = true;
    wifiDisconnectedSince = millis();
    Serial.printf("[INFO] AP '%s' activo para configuración\n", AP_SSID);

    // Intentar WiFi cliente si está configurado (NO bloqueante, en segundo plano)
    if (systemConfig.wifi_configured && strlen(systemConfig.wifi_ssid) > 0) {
        Serial.println("[INFO] Conectando a WiFi en segundo plano...");
        wifiConnectNext();
        // No esperar - manageWiFiAndAP() en loop() detectará cuando conecte
    } else {
        Serial.println("[INFO] WiFi no configurado, AP activo para configuración");
    }

    lastWiFiRetry = millis();

    // 3. CC1101 - Inicializar siempre (independiente de WiFi)
    Serial.println("[3/6] CC1101...");
    Serial.flush();
    if (!initRFModule()) {
        Serial.println("[WARNING] CC1101 no detectado");
        rfWasOk = false;
        rfDownSince = millis();
    } else {
        Serial.println("[OK] CC1101 inicializado");
        rfWasOk = true;
        rfDownSince = 0;
    }
    lastRFCheck = millis();
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
    if (WiFi.status() == WL_CONNECTED) {
        currentRSSI = WiFi.RSSI();
        Serial.printf("Uptime: %lu s | Heap: %d bytes | WiFi RSSI: %d dB\n",
            millis() / 1000, ESP.getFreeHeap(), currentRSSI);
    } else {
        Serial.printf("Uptime: %lu s | Heap: %d bytes | WiFi: Desconectado\n",
            millis() / 1000, ESP.getFreeHeap());
    }
}

// ============================================
// MÓDULO RF: INICIALIZACIÓN Y WATCHDOG
// ============================================

// Inicializa (o reinicializa) el CC1101 y los protocolos que dependen de él
bool initRFModule() {
    if (!rfModule.begin()) return false;

    rfModule.setFrequency(systemConfig.default_frequency);
    rfModule.setModulation(systemConfig.default_modulation);
    somfyRTS.begin();
    dooyaBidir.begin();
    aokProtocol.begin();
    return true;
}

// Verifica periódicamente que el CC1101 responda por SPI.
// Si no responde: intenta reinicializarlo, avisa a Home Assistant (las cortinas
// pasan a "no disponible") y, si el watchdog está activo, reinicia el ESP32
// tras rf_watchdog_minutes minutos sin recuperarse.
void checkRFModule() {
    if (millis() - lastRFCheck < RF_CHECK_INTERVAL) return;
    lastRFCheck = millis();

    // No interrogar el módulo en medio de una captura
    if (rfModule.isCapturing()) return;

    bool rfOk = rfModule.isConnected();

    if (!rfOk) {
        // Intento de recuperación en caliente antes de pensar en reiniciar
        if (initRFModule()) {
            rfOk = true;
            Serial.println("[RF] Modulo recuperado tras reinicializar");
        }
    }

    if (rfOk) {
        if (rfDownSince != 0) {
            Serial.println("[RF] Modulo operativo de nuevo");
        }
        rfDownSince = 0;
        rfWatchdogReboots = 0;   // el módulo funciona: contador de reinicios a cero
    } else if (rfDownSince == 0) {
        rfDownSince = millis();
        Serial.println("[RF] ATENCION: el CC1101 no responde");
    }

    // Notificar el cambio de estado a Home Assistant en cuanto ocurre
    if (rfOk != rfWasOk) {
        rfWasOk = rfOk;
        if (systemConfig.mqtt_enabled && mqttClient.isConnected()) {
            mqttClient.publishRFStatus(rfOk);
        }
    }

    // Reinicio automático si el módulo lleva demasiado tiempo caído
    if (!rfOk && systemConfig.rf_watchdog_enabled && rfDownSince != 0) {
        unsigned long downMs = millis() - rfDownSince;
        unsigned long limitMs = (unsigned long)systemConfig.rf_watchdog_minutes * 60000UL;

        if (downMs >= limitMs) {
            // Si ya reiniciamos varias veces sin recuperar el módulo, el fallo es
            // de hardware: seguir reiniciando solo deja el equipo inutilizable.
            if (rfWatchdogReboots >= RF_WATCHDOG_MAX_REBOOTS) {
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    Serial.printf("[RF] %d reinicios sin recuperar el modulo: fallo de hardware, "
                                  "watchdog detenido (revisar CC1101)\n", RF_WATCHDOG_MAX_REBOOTS);
                }
                return;   // se sigue publicando el RF como offline en Home Assistant
            }

            rfWatchdogReboots++;
            Serial.printf("[RF] Sin modulo RF por %lu min, reiniciando ESP32 (intento %u/%d)...\n",
                downMs / 60000UL, rfWatchdogReboots, RF_WATCHDOG_MAX_REBOOTS);
            Serial.flush();
            if (systemConfig.mqtt_enabled && mqttClient.isConnected()) {
                mqttClient.publishRFStatus(false);
                mqttClient.stop();   // publica 'offline' (LWT)
            }
            delay(500);
            ESP.restart();
        }
    }
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

// ============================================
// WIFI ROAMING - Conexión al AP más fuerte
// ============================================

// Conectar al AP más fuerte con el SSID configurado
bool connectToBestAP(const char* ssid, const char* password) {
    Serial.printf("[WiFi Roaming] Escaneando redes para SSID: %s\n", ssid);

    int n = WiFi.scanNetworks(false, false, false, 300);
    if (n <= 0) {
        Serial.println("[WiFi Roaming] No se encontraron redes");
        WiFi.scanDelete();
        return false;
    }

    int8_t bestRSSI = -127;
    uint8_t bestBSSID[6] = {0};
    int bestChannel = 0;
    bool found = false;

    Serial.printf("[WiFi Roaming] %d redes encontradas:\n", n);

    for (int i = 0; i < n; i++) {
        String foundSSID = WiFi.SSID(i);
        int8_t rssi = WiFi.RSSI(i);

        // Solo mostrar redes con el mismo SSID
        if (foundSSID == ssid) {
            uint8_t* bssid = WiFi.BSSID(i);
            Serial.printf("  -> %s (RSSI: %d dB, Canal: %d, BSSID: %02X:%02X:%02X:%02X:%02X:%02X)\n",
                foundSSID.c_str(), rssi, WiFi.channel(i),
                bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);

            if (rssi > bestRSSI) {
                bestRSSI = rssi;
                memcpy(bestBSSID, bssid, 6);
                bestChannel = WiFi.channel(i);
                found = true;
            }
        }
    }

    WiFi.scanDelete();

    if (!found) {
        Serial.printf("[WiFi Roaming] No se encontró ningún AP con SSID: %s\n", ssid);
        return false;
    }

    Serial.printf("[WiFi Roaming] Mejor AP: RSSI %d dB, Canal %d, BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
        bestRSSI, bestChannel,
        bestBSSID[0], bestBSSID[1], bestBSSID[2], bestBSSID[3], bestBSSID[4], bestBSSID[5]);

    // Conectar al mejor AP especificando el BSSID
    WiFi.disconnect();
    delay(100);
    WiFi.begin(ssid, password, bestChannel, bestBSSID, true);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        currentRSSI = WiFi.RSSI();
        Serial.printf("\n[WiFi Roaming] Conectado! IP: %s, RSSI: %d dB\n",
            WiFi.localIP().toString().c_str(), currentRSSI);
        return true;
    }

    Serial.println("\n[WiFi Roaming] Error al conectar");
    return false;
}

// Verificar si hay un AP mejor y cambiar si es necesario
void checkWiFiRoaming() {
    if (!systemConfig.wifi_configured || strlen(systemConfig.wifi_ssid) == 0) {
        return;
    }

    // Hacer roaming dentro de la red REALMENTE conectada (principal o respaldo),
    // no asumir la principal: así sigue saltando a la mejor antena en cualquier caso.
    String connectedSSID = WiFi.SSID();
    if (connectedSSID.length() == 0) return;
    const char* roamSSID = connectedSSID.c_str();
    const char* roamPass = (connectedSSID == systemConfig.wifi_ssid2)
                               ? systemConfig.wifi_password2
                               : systemConfig.wifi_password;

    currentRSSI = WiFi.RSSI();

    // Copiar el BSSID actual: WiFi.BSSID() devuelve un puntero a un buffer
    // interno que el escaneo puede pisar.
    uint8_t* bssidPtr = WiFi.BSSID();
    if (bssidPtr) memcpy(currentBSSID, bssidPtr, 6);

    int8_t threshold = WIFI_RSSI_THRESHOLD;

    Serial.printf("[WiFi Roaming] RSSI actual: %d dB (umbral +%d dB), escaneando mejores APs...\n",
        currentRSSI, threshold);

    int n = WiFi.scanNetworks(false, false, false, 300);
    if (n <= 0) {
        WiFi.scanDelete();
        return;
    }

    // Guardar lo visto para poder consultarlo después desde /api/wifi/aps
    cacheScanResults(n);

    int8_t bestRSSI = currentRSSI;
    uint8_t bestBSSID[6] = {0};
    int bestChannel = 0;
    bool betterFound = false;

    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == connectedSSID) {
            int8_t rssi = WiFi.RSSI(i);
            uint8_t* bssid = WiFi.BSSID(i);

            // Verificar si es un AP diferente y significativamente mejor
            bool isDifferentAP = memcmp(bssid, currentBSSID, 6) != 0;
            bool isSignificantlyBetter = (rssi - currentRSSI) >= threshold;

            if (isDifferentAP && isSignificantlyBetter && rssi > bestRSSI) {
                bestRSSI = rssi;
                memcpy(bestBSSID, bssid, 6);
                bestChannel = WiFi.channel(i);
                betterFound = true;
            }
        }
    }

    WiFi.scanDelete();

    if (betterFound) {
        Serial.printf("[WiFi Roaming] Encontrado AP mejor! RSSI: %d dB (+%d dB)\n",
            bestRSSI, bestRSSI - currentRSSI);
        Serial.printf("[WiFi Roaming] Cambiando a BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
            bestBSSID[0], bestBSSID[1], bestBSSID[2], bestBSSID[3], bestBSSID[4], bestBSSID[5]);

        WiFi.disconnect();
        delay(100);
        WiFi.begin(roamSSID, roamPass, bestChannel, bestBSSID, true);

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            currentRSSI = WiFi.RSSI();
            memcpy(currentBSSID, bestBSSID, 6);
            Serial.printf("[WiFi Roaming] Cambio exitoso! Nuevo RSSI: %d dB\n", currentRSSI);
        } else {
            Serial.println("[WiFi Roaming] Error al cambiar, reconectando al anterior...");
            WiFi.begin(roamSSID, roamPass);
        }
    } else {
        Serial.printf("[WiFi Roaming] No hay AP mejor (umbral: +%d dB)\n", threshold);
    }
}

// ============================================
// GESTIÓN DE WIFI Y AP DE RESPALDO
// Si no hay WiFi por 2 minutos, activar AP
// ============================================

void manageWiFiAndAP() {
    bool currentlyConnected = (WiFi.status() == WL_CONNECTED);

    // Si acabamos de conectarnos
    if (currentlyConnected && !wasConnected) {
        wasConnected = true;
        currentRSSI = WiFi.RSSI();
        Serial.printf("[WiFi] Conectado! IP: %s, RSSI: %d dB\n",
            WiFi.localIP().toString().c_str(), currentRSSI);

        // Desactivar AP si estaba activo (sin cambiar modo, mantener AP_STA)
        if (apActive) {
            WiFi.softAPdisconnect(true);
            apActive = false;
            Serial.println("[WiFi] AP desactivado (WiFi conectado)");
        }

        // Inicializar módulos que requieren WiFi si no estaban inicializados
        if (!rfModule.isConnected()) {
            if (initRFModule()) {
                rfDownSince = 0;
                rfWasOk = true;
                Serial.println("[WiFi] CC1101 inicializado");
            }
        }

        // Sincronizar hora
        timeManager.begin(&systemConfig);

        // Reconectar MQTT
        if (systemConfig.mqtt_enabled) {
            mqttClient.begin(&systemConfig);
        }
    }

    // Si acabamos de desconectarnos
    if (!currentlyConnected && wasConnected) {
        wasConnected = false;
        wifiDisconnectedSince = millis();
        Serial.println("[WiFi] Conexión perdida, buscando red...");
    }

    // Si no hay WiFi
    if (!currentlyConnected) {
        unsigned long disconnectedTime = millis() - wifiDisconnectedSince;

        // Si llevamos más de 2 minutos sin WiFi y el AP no está activo
        if (disconnectedTime > WIFI_TIMEOUT_MS && !apActive) {
            Serial.printf("[WiFi] Sin conexión por %lu segundos, activando AP\n", disconnectedTime / 1000);
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP(AP_SSID, AP_PASSWORD);
            apActive = true;
            Serial.printf("[WiFi] AP '%s' activo para configuración\n", AP_SSID);
        }

        // Reintentar conexión periódicamente (sin escaneo para no interferir con AP)
        if (systemConfig.wifi_configured && strlen(systemConfig.wifi_ssid) > 0) {
            if (millis() - lastWiFiRetry > WIFI_RETRY_MS) {
                lastWiFiRetry = millis();
                Serial.println("[WiFi] Reintentando conexión (mejor antena disponible)...");
                wifiConnectNext();
            }
        }
    }
}
