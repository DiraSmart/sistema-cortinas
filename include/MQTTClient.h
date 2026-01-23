#ifndef MQTTCLIENT_H
#define MQTTCLIENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "Storage.h"

class MQTTClientManager {
public:
    MQTTClientManager();

    // Inicialización
    bool begin(SystemConfig* config);
    void stop();

    // Conexión
    bool connect();
    void disconnect();
    bool isConnected();
    void loop();

    // Home Assistant Discovery
    void publishDiscovery();
    void removeDiscovery();

    // Publicación de estado
    void publishDeviceState(const char* deviceId, const char* state);
    void publishAllStates();
    void publishSystemStatus();

    // Callbacks
    void setCommandCallback(void (*callback)(const char* deviceId, const char* command));

private:
    WiFiClient wifiClient;
    PubSubClient mqtt;
    SystemConfig* sysConfig;

    bool enabled;
    unsigned long lastReconnectAttempt;
    unsigned long lastStatusPublish;

    void (*onCommand)(const char* deviceId, const char* command);

    // Topics
    String baseTopic;
    String commandTopic;
    String stateTopic;
    String availabilityTopic;

    // Métodos internos
    void setupTopics();
    void subscribe();
    void handleMessage(char* topic, uint8_t* payload, unsigned int length);
    static void mqttCallback(char* topic, uint8_t* payload, unsigned int length);
    void processDeviceCommand(const char* deviceId, const char* command);
    void processSignalCommand(const char* deviceId, int signalIndex, const char* command);
    void processSystemCommand(const char* command, const char* payload);
    void publishSystemButtons();
    void publishDiagnosticSensors();

    // Home Assistant Discovery
    void publishCoverDiscovery(const SavedDevice* device);
    void publishGateDiscovery(const SavedDevice* device);
    void publishSwitchDiscovery(const SavedDevice* device);
    void publishButtonDiscovery(const SavedDevice* device, uint8_t signalIndex);
    void removeCoverDiscovery(const char* deviceId);
    void removeSwitchDiscovery(const char* deviceId);
    void removeButtonDiscovery(const char* deviceId, uint8_t signalIndex);

    // Singleton para callback estático
    static MQTTClientManager* instance;
};

// Instancia global
extern MQTTClientManager mqttClient;

#endif // MQTTCLIENT_H
