#ifndef SOMFY_RTS_H
#define SOMFY_RTS_H

#include <Arduino.h>
#include "config.h"

class SomfyRTS {
public:
    SomfyRTS();

    // Inicialización
    bool begin();

    // Configurar control remoto virtual
    void setRemote(uint32_t address, uint16_t rollingCode = 0, uint8_t encryptionKey = 0xA7);
    void setRemote(const SomfyRemote* remote);

    // Obtener estado actual
    uint32_t getAddress() const { return remoteAddress; }
    uint16_t getRollingCode() const { return currentRollingCode; }

    // Comandos principales
    bool sendUp();
    bool sendDown();
    bool sendStop();      // My button
    bool sendProg();      // Para emparejar (mantener 3 seg)

    // Comando genérico
    bool sendCommand(uint8_t command);

    // Utilidades
    void incrementRollingCode();
    String getStatusString();

private:
    uint32_t remoteAddress;
    uint16_t currentRollingCode;
    uint8_t encryptionKey;
    bool initialized;

    // Frame buffer
    uint8_t frameBuffer[SOMFY_FRAME_LENGTH];

    // Configuración CC1101
    void configureTransmitter();
    void restoreConfig();

    // Métodos internos
    void buildFrame(uint8_t command);
    void obfuscateFrame();
    void transmitFrame(bool isFirstFrame);
    void sendBit(bool bit);
    void sendHardwareSync(int count);
    void sendSoftwareSync();

    // Timing preciso
    void delayMicrosecondsPrecise(unsigned long us);
};

// Instancia global
extern SomfyRTS somfyRTS;

#endif // SOMFY_RTS_H
