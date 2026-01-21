#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"

class StorageManager {
public:
    StorageManager();

    // Inicialización
    bool begin();
    bool format();

    // Configuración del sistema
    bool loadConfig(SystemConfig* config);
    bool saveConfig(const SystemConfig* config);
    void setDefaultConfig(SystemConfig* config);

    // Dispositivos guardados
    bool loadDevices(SavedDevice* devices, uint8_t* count);
    bool saveDevices(const SavedDevice* devices, uint8_t count);
    bool addDevice(const SavedDevice* device);
    bool updateDevice(const char* id, const SavedDevice* device);
    bool deleteDevice(const char* id);
    bool getDevice(const char* id, SavedDevice* device);

    // Señales RF
    bool saveSignalToDevice(const char* deviceId, uint8_t signalIndex,
                            const RFSignal* signal, const char* signalName);
    bool deleteSignalFromDevice(const char* deviceId, uint8_t signalIndex);

    // Somfy RTS
    bool updateSomfyRollingCode(const char* deviceId, uint16_t newRollingCode);

    // Backup y Restore
    String createBackup();
    bool restoreBackup(const String& backupJson);
    bool exportToFile(const char* filename);
    bool importFromFile(const char* filename);

    // Utilidades
    String generateUUID();
    bool fileExists(const char* path);
    size_t getFreeSpace();
    size_t getTotalSpace();
    String listFiles();

private:
    bool initialized;

    // Helpers JSON
    void signalToJson(JsonObject& obj, const RFSignal* signal);
    void jsonToSignal(JsonObject& obj, RFSignal* signal);
    void deviceToJson(JsonObject& obj, const SavedDevice* device);
    void jsonToDevice(JsonObject& obj, SavedDevice* device);
    void configToJson(JsonObject& obj, const SystemConfig* config);
    void jsonToConfig(JsonObject& obj, SystemConfig* config);
};

// Instancia global
extern StorageManager storage;

#endif // STORAGE_H
