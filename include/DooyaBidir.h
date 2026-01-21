#ifndef DOOYA_BIDIR_H
#define DOOYA_BIDIR_H

#include <Arduino.h>
#include "config.h"

class DooyaBidirectional {
public:
    DooyaBidirectional();

    // Inicialización
    bool begin();

    // Configurar control remoto virtual
    void setRemote(uint32_t deviceId, uint8_t unitCode);
    void setRemote(const DooyaBidirRemote* remote);

    // Obtener estado actual
    uint32_t getDeviceId() const { return currentDeviceId; }
    uint8_t getUnitCode() const { return currentUnitCode; }

    // Comandos principales
    bool sendUp();
    bool sendDown();
    bool sendStop();
    bool sendProg();      // P2 para emparejar

    // Comando genérico
    bool sendCommand(uint8_t command);

    // Utilidades
    String getStatusString();
    String getFrameHex();

private:
    uint32_t currentDeviceId;
    uint8_t currentUnitCode;
    bool initialized;

    // Frame buffer
    uint8_t frameBuffer[DOOYA_BIDIR_FRAME_LEN];

    // Métodos internos
    void buildFrame(uint8_t command);
    bool transmitFrame();

    // Configuración CC1101 para FSK
    void configureFSK();
    void restoreASK();
};

// Instancia global
extern DooyaBidirectional dooyaBidir;

#endif // DOOYA_BIDIR_H
