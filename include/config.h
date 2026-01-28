#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================
// VERSION DEL FIRMWARE
// ============================================
#define FIRMWARE_VERSION    "1.2.0"

// ============================================
// CONFIGURACIÓN DE PINES ESP32 -> CC1101
// ============================================
#define CC1101_GDO0    13  // Pin de datos/interrupción
#define CC1101_GDO2    12  // Pin de estado
#define CC1101_CSN     5   // Chip Select (SS)
#define CC1101_SCK     18  // SPI Clock
#define CC1101_MISO    19  // SPI MISO
#define CC1101_MOSI    23  // SPI MOSI

// ============================================
// CONFIGURACIÓN DE RED
// ============================================
// WiFi por defecto (si no se conecta, pasa a modo AP)
#define DEFAULT_WIFI_SSID       "dirasmart"
#define DEFAULT_WIFI_PASSWORD   "dirasmart1"

// Access Point (modo fallback)
#define AP_SSID         "RF_Controller"
#define AP_PASSWORD     "12345678"

// Autenticación Web
#define WEB_AUTH_USER       "admin"
#define WEB_AUTH_PASSWORD   "dirasmart1"
#define AP_IP           IPAddress(192, 168, 4, 1)
#define AP_GATEWAY      IPAddress(192, 168, 4, 1)
#define AP_SUBNET       IPAddress(255, 255, 255, 0)

// ============================================
// CONFIGURACIÓN MQTT
// ============================================
#define MQTT_PORT               1883
#define MQTT_RECONNECT_DELAY    5000
#define MQTT_BASE_TOPIC         "rf_controller"
#define MQTT_DISCOVERY_PREFIX   "homeassistant"

// ============================================
// CONFIGURACIÓN RF
// ============================================
#define RF_DEFAULT_FREQUENCY    433.92  // MHz
#define RF_CAPTURE_TIMEOUT      10000   // ms
#define RF_MAX_SIGNAL_LENGTH    512     // bytes
#define RF_REPEAT_TRANSMIT      6      // repeticiones (aumentado para mejor confiabilidad)

// Frecuencias predefinidas comunes
const float RF_FREQUENCIES[] = {
    300.00,   // 300 MHz
    303.87,   // 303.87 MHz (común en USA)
    310.00,   // 310 MHz
    315.00,   // 315 MHz (común en USA/Asia)
    390.00,   // 390 MHz
    418.00,   // 418 MHz
    433.42,   // 433.42 MHz (Somfy RTS)
    433.92,   // 433.92 MHz (común en Europa/Latam)
    868.00,   // 868 MHz (Europa)
    868.35,   // 868.35 MHz
    915.00    // 915 MHz (USA)
};
#define RF_FREQUENCIES_COUNT (sizeof(RF_FREQUENCIES) / sizeof(RF_FREQUENCIES[0]))

// ============================================
// TIPOS DE DISPOSITIVOS (Funcionales, no por marca)
// ============================================
enum DeviceType {
    DEVICE_UNKNOWN = 0,
    DEVICE_CURTAIN = 1,         // Cortinas genéricas (3 señales: abrir, cerrar, parar)
    DEVICE_SWITCH = 2,          // Interruptores (2 señales: on, off)
    DEVICE_BUTTON = 3,          // Botones/pulsadores (1 señal: press)
    DEVICE_GATE = 4,            // Portones (1-2 señales: toggle o abrir/cerrar)
    DEVICE_LIGHT = 5,           // Luces (2 señales: on, off)
    DEVICE_FAN = 6,             // Ventiladores (2-3 señales: on, off, velocidad)
    DEVICE_DIMMER = 7,          // Dimmers (4 señales: on, off, +, -)
    DEVICE_CURTAIN_SOMFY = 10,  // Cortinas Somfy RTS (rolling code)
    DEVICE_CURTAIN_DOOYA_BIDIR = 11, // Cortinas Dooya DDxxxx bidireccional (FSK)
    DEVICE_CURTAIN_AOK = 12,    // Cortinas A-OK AC114 (protocolo específico)
    DEVICE_OTHER = 99           // Otros (señales personalizadas)
};

// ============================================
// PROTOCOLOS RF CONOCIDOS
// ============================================
enum RFProtocol {
    PROTOCOL_UNKNOWN = 0,
    PROTOCOL_GENERIC = 1,       // Genérico ASK/OOK
    PROTOCOL_DOOYA = 2,         // Dooya/Dongguan unidireccional (ASK)
    PROTOCOL_ZEMISMART = 3,     // Zemismart
    PROTOCOL_TUYA = 4,          // Tuya 433
    PROTOCOL_EV1527 = 5,        // EV1527 (común en controles baratos)
    PROTOCOL_PT2262 = 6,        // PT2262/PT2272
    PROTOCOL_NICE_FLO = 7,      // Nice Flor-s
    PROTOCOL_CAME = 8,          // Came
    PROTOCOL_VERTILUX = 9,      // Vertilux/VTI
    PROTOCOL_SOMFY_RTS = 10,    // Somfy RTS (rolling code)
    PROTOCOL_DOOYA_BIDIR = 11,  // Dooya DDxxxx bidireccional (FSK encriptado)
    PROTOCOL_AOK = 12           // A-OK AC114 (cortinas motorizadas)
};

// ============================================
// CONFIGURACIÓN DOOYA
// Protocolo: 433.92 MHz, ASK/OOK
// Timing: ~350us por bit, código de 24-28 bits
// ============================================
#define DOOYA_FREQUENCY         433.92
#define DOOYA_PULSE_SHORT       350     // us - pulso corto
#define DOOYA_PULSE_LONG        700     // us - pulso largo
#define DOOYA_SYNC_HIGH         4900    // us - sincronización alta
#define DOOYA_SYNC_LOW          1500    // us - sincronización baja
#define DOOYA_REPEAT_COUNT      8       // repeticiones para mejor recepción
#define DOOYA_GAP_TIME          10000   // us - espacio entre repeticiones

// ============================================
// CONFIGURACIÓN VERTILUX / VTI
// Protocolo similar a EV1527, 433.92 MHz
// Pulsos más cortos, necesita captura precisa
// ============================================
#define VERTILUX_FREQUENCY      433.92
#define VERTILUX_PULSE_SHORT    280     // us - pulso corto
#define VERTILUX_PULSE_LONG     850     // us - pulso largo
#define VERTILUX_SYNC_LOW       9000    // us - sincronización
#define VERTILUX_REPEAT_COUNT   6

// ============================================
// CONFIGURACIÓN SOMFY RTS
// Protocolo: 433.42 MHz, ASK/OOK con rolling code
// Frame: 7 bytes encriptados con XOR
// Comandos: My=0x1, Up=0x2, Down=0x4, Prog=0x8
// ============================================
#define SOMFY_FREQUENCY         433.42  // MHz - frecuencia específica Somfy
#define SOMFY_SYMBOL_WIDTH      640     // us - ancho de símbolo (half bit)
#define SOMFY_HWSYNC_HIGH       2416    // us - hardware sync pulso alto
#define SOMFY_HWSYNC_LOW        2416    // us - hardware sync pulso bajo
#define SOMFY_SWSYNC_HIGH       4550    // us - software sync pulso alto
#define SOMFY_SWSYNC_LOW        604     // us - software sync pulso bajo (half symbol)
#define SOMFY_INTER_FRAME_GAP   30415   // us - espacio entre frames
#define SOMFY_FRAME_LENGTH      7       // bytes por frame (56 bits)
#define SOMFY_FIRST_FRAME_REPS  2       // repeticiones primer frame (2 = 4 hw syncs)
#define SOMFY_REPEAT_REPS       7       // repeticiones frames siguientes (7 hw syncs)
#define SOMFY_TOTAL_FRAMES      3       // frames totales a transmitir

// Comandos Somfy RTS
#define SOMFY_CMD_MY            0x1     // My/Stop
#define SOMFY_CMD_UP            0x2     // Subir
#define SOMFY_CMD_DOWN          0x4     // Bajar
#define SOMFY_CMD_PROG          0x8     // Programar (mantener 3 seg)
#define SOMFY_CMD_UP_DOWN       0x3     // Up + Down (límites)
#define SOMFY_CMD_MY_UP         0x5     // My + Up
#define SOMFY_CMD_MY_DOWN       0x6     // My + Down

// ============================================
// CONFIGURACIÓN DOOYA BIDIRECCIONAL (DDxxxx)
// Protocolo: 433.92 MHz, FSK con encriptación
// Frame: 10 bytes (estructura RFXCOM compatible)
// Comandos: Up=0x00, Down=0x01, Stop=0x02, Prog=0x03
// ============================================
#define DOOYA_BIDIR_FREQUENCY   433.92  // MHz
#define DOOYA_BIDIR_MODULATION  0       // 2-FSK (no ASK/OOK)
#define DOOYA_BIDIR_DATARATE    4800    // baudios aproximado
#define DOOYA_BIDIR_DEVIATION   25      // kHz desviación FSK
#define DOOYA_BIDIR_FRAME_LEN   10      // bytes por frame

// Bytes fijos del protocolo Dooya bidireccional
#define DOOYA_BIDIR_BYTE0       0x09    // Siempre 09
#define DOOYA_BIDIR_BYTE1       0x19    // Siempre 19
#define DOOYA_BIDIR_BYTE2       0x15    // Siempre 15
#define DOOYA_BIDIR_BYTE3       0x00    // Siempre 00

// Comandos Dooya bidireccional
#define DOOYA_BIDIR_CMD_UP      0x00    // Subir
#define DOOYA_BIDIR_CMD_DOWN    0x01    // Bajar
#define DOOYA_BIDIR_CMD_STOP    0x02    // Parar
#define DOOYA_BIDIR_CMD_PROG    0x03    // P2 (emparejar)

// ============================================
// CONFIGURACIÓN DE CAPTURA AVANZADA
// Ajustar estos valores si hay problemas de captura
// ============================================
#define RF_MIN_PULSE_WIDTH      50      // us - mínimo para filtrar ruido (más bajo = más sensible)
#define RF_MAX_PULSE_WIDTH      20000   // us - máximo antes de considerar gap
#define RF_SIGNAL_GAP           8000    // us - gap que indica fin de transmisión
#define RF_MIN_PULSES           16      // mínimo de pulsos para señal válida

// ============================================
// ESTRUCTURA DE SEÑAL RF CAPTURADA
// ============================================
struct RFSignal {
    uint8_t data[RF_MAX_SIGNAL_LENGTH];
    uint16_t length;
    float frequency;
    int modulation;         // 0=ASK/OOK, 2=2-FSK, etc
    int bandwidth;
    int dataRate;
    int deviation;
    unsigned long timestamp;
    bool valid;
    uint8_t repeatCount;    // Number of times to repeat transmission (1-20, default 5)
    bool inverted;          // If true, start transmission with LOW instead of HIGH
};

// ============================================
// ESTRUCTURA SOMFY RTS
// Rolling code y dirección para control Somfy
// ============================================
struct SomfyRemote {
    uint32_t address;       // Dirección de 24 bits (único por control)
    uint16_t rollingCode;   // Código rotativo de 16 bits
    uint8_t encryptionKey;  // Clave de encriptación (0-15)
};

// ============================================
// ESTRUCTURA DOOYA BIDIRECCIONAL
// ID y unit code para controles DDxxxx
// ============================================
struct DooyaBidirRemote {
    uint32_t deviceId;      // ID de 28 bits (7 nibbles: ID1-ID4 + parte de unit)
    uint8_t unitCode;       // Código de unidad 0-15 (0 = grupo)
};

// ============================================
// ESTRUCTURA A-OK AC114
// Remote ID y canal para controles A-OK
// ============================================
struct AOKRemote {
    uint32_t remoteId;      // ID de 24 bits (único por control)
    uint8_t channel;        // Canal 1-16 (0 = todos)
};

// ============================================
// ESTRUCTURA DE DISPOSITIVO GUARDADO
// ============================================
struct SavedDevice {
    char id[37];            // UUID
    char name[64];
    DeviceType type;
    RFSignal signals[4];    // Hasta 4 señales (ej: abrir, cerrar, parar, toggle)
    char signalNames[4][32];
    uint8_t signalCount;
    bool enabled;
    char room[32];
    unsigned long createdAt;
    unsigned long lastUsed;

    // Somfy RTS (solo usado si type == DEVICE_CURTAIN_SOMFY)
    SomfyRemote somfy;      // Dirección y rolling code

    // Dooya Bidireccional (solo usado si type == DEVICE_CURTAIN_DOOYA_BIDIR)
    DooyaBidirRemote dooyaBidir; // ID y unit code

    // A-OK AC114 (solo usado si type == DEVICE_CURTAIN_AOK)
    AOKRemote aok;          // Remote ID y canal
};

// ============================================
// CONFIGURACIÓN DEL SISTEMA
// ============================================
struct SystemConfig {
    // WiFi
    char wifi_ssid[64];
    char wifi_password[64];
    bool wifi_configured;

    // MQTT
    char mqtt_server[64];
    uint16_t mqtt_port;
    char mqtt_user[32];
    char mqtt_password[64];
    char mqtt_client_id[32];
    bool mqtt_enabled;
    bool mqtt_discovery;

    // Zona horaria
    char timezone[64];
    char ntp_server[64];
    int utc_offset;
    bool dst_enabled;

    // RF por defecto
    float default_frequency;
    int default_modulation;

    // Sistema
    char device_name[32];
    bool auto_detect_enabled;
};

// ============================================
// CONFIGURACIÓN POR DEFECTO
// ============================================
#define DEFAULT_TIMEZONE        "America/Bogota"
#define DEFAULT_NTP_SERVER      "pool.ntp.org"
#define DEFAULT_DEVICE_NAME     "RF_Controller"

// ============================================
// ALMACENAMIENTO
// ============================================
#define CONFIG_FILE             "/config.json"
#define DEVICES_FILE            "/devices.json"
#define BACKUP_FILE             "/backup.json"
#define MAX_DEVICES             50

// ============================================
// TAMAÑOS DE BUFFER
// ============================================
#define JSON_BUFFER_SIZE        16384  // Increased for multiple signals with large data
#define WEB_BUFFER_SIZE         4096

#endif // CONFIG_H
