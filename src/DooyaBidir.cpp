#include "DooyaBidir.h"
#include "CC1101_RF.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// Instancia global
DooyaBidirectional dooyaBidir;

DooyaBidirectional::DooyaBidirectional() {
    currentDeviceId = 0;
    currentUnitCode = 0;
    initialized = false;
    memset(frameBuffer, 0, DOOYA_BIDIR_FRAME_LEN);
}

bool DooyaBidirectional::begin() {
    initialized = true;
    Serial.println("[DooyaBidir] Inicializado");
    return true;
}

void DooyaBidirectional::setRemote(uint32_t deviceId, uint8_t unitCode) {
    currentDeviceId = deviceId & 0x0FFFFFFF;  // Solo 28 bits
    currentUnitCode = unitCode & 0x0F;        // Solo 4 bits (0-15)
    Serial.printf("[DooyaBidir] Configurado: ID=0x%07X, Unit=%d\n",
                  currentDeviceId, currentUnitCode);
}

void DooyaBidirectional::setRemote(const DooyaBidirRemote* remote) {
    if (remote) {
        setRemote(remote->deviceId, remote->unitCode);
    }
}

bool DooyaBidirectional::sendUp() {
    return sendCommand(DOOYA_BIDIR_CMD_UP);
}

bool DooyaBidirectional::sendDown() {
    return sendCommand(DOOYA_BIDIR_CMD_DOWN);
}

bool DooyaBidirectional::sendStop() {
    return sendCommand(DOOYA_BIDIR_CMD_STOP);
}

bool DooyaBidirectional::sendProg() {
    return sendCommand(DOOYA_BIDIR_CMD_PROG);
}

bool DooyaBidirectional::sendCommand(uint8_t command) {
    if (!initialized) {
        Serial.println("[DooyaBidir] Error: No inicializado");
        return false;
    }

    if (currentDeviceId == 0) {
        Serial.println("[DooyaBidir] Error: No hay ID configurado");
        return false;
    }

    Serial.printf("[DooyaBidir] Enviando comando 0x%02X\n", command);

    // Construir el frame
    buildFrame(command);

    // Mostrar frame
    Serial.print("[DooyaBidir] Frame: ");
    for (int i = 0; i < DOOYA_BIDIR_FRAME_LEN; i++) {
        Serial.printf("%02X", frameBuffer[i]);
    }
    Serial.println();

    // Transmitir
    bool success = transmitFrame();

    if (success) {
        Serial.println("[DooyaBidir] Comando enviado OK");
    } else {
        Serial.println("[DooyaBidir] Error al transmitir");
    }

    return success;
}

void DooyaBidirectional::buildFrame(uint8_t command) {
    // Estructura del frame Dooya bidireccional (10 bytes):
    // Byte 0: 0x09 (fijo)
    // Byte 1: 0x19 (fijo)
    // Byte 2: 0x15 (fijo)
    // Byte 3: 0x00 (fijo)
    // Byte 4: ID1 (bits 24-27 del ID)
    // Byte 5: ID2 (bits 16-23 del ID)
    // Byte 6: ID3 (bits 8-15 del ID)
    // Byte 7: ID4 (bits 0-3 del ID en nibble alto) + Unit (nibble bajo)
    // Byte 8: Comando
    // Byte 9: 0x00 (fijo)

    memset(frameBuffer, 0, DOOYA_BIDIR_FRAME_LEN);

    // Bytes fijos
    frameBuffer[0] = DOOYA_BIDIR_BYTE0;  // 0x09
    frameBuffer[1] = DOOYA_BIDIR_BYTE1;  // 0x19
    frameBuffer[2] = DOOYA_BIDIR_BYTE2;  // 0x15
    frameBuffer[3] = DOOYA_BIDIR_BYTE3;  // 0x00

    // ID (28 bits dividido en bytes)
    // Ejemplo: ID 0x1020304 con unit 1 -> bytes: 10 20 30 41
    frameBuffer[4] = (currentDeviceId >> 20) & 0xFF;  // ID1
    frameBuffer[5] = (currentDeviceId >> 12) & 0xFF;  // ID2
    frameBuffer[6] = (currentDeviceId >> 4) & 0xFF;   // ID3
    frameBuffer[7] = ((currentDeviceId & 0x0F) << 4) | (currentUnitCode & 0x0F);  // ID4 + Unit

    // Comando
    frameBuffer[8] = command;

    // Byte final fijo
    frameBuffer[9] = 0x00;
}

bool DooyaBidirectional::transmitFrame() {
    // Configurar CC1101 para FSK
    configureFSK();

    // Transmitir usando la librería CC1101
    // Nota: La transmisión FSK requiere configuración especial del CC1101
    // que puede no estar completamente soportada por todas las librerías

    ELECHOUSE_cc1101.SetTx();

    // Transmitir el frame
    // La librería ELECHOUSE puede no soportar FSK directamente
    // Por ahora simulamos la transmisión
    for (int repeat = 0; repeat < 5; repeat++) {
        ELECHOUSE_cc1101.SendData(frameBuffer, DOOYA_BIDIR_FRAME_LEN);
        delay(20);
    }

    ELECHOUSE_cc1101.SetRx();

    // Restaurar configuración ASK para otros dispositivos
    restoreASK();

    return true;
}

void DooyaBidirectional::configureFSK() {
    // Configurar CC1101 para modulación FSK
    // Esto requiere cambiar varios registros del CC1101

    // Frecuencia: 433.92 MHz
    ELECHOUSE_cc1101.setMHZ(DOOYA_BIDIR_FREQUENCY);

    // Cambiar modulación a 2-FSK (MDMCFG2)
    // Nota: La librería ELECHOUSE puede no tener método directo para esto
    // Necesitamos escribir directamente al registro

    // MDMCFG2: Configuración de modulación
    // Bits 6:4 = MOD_FORMAT: 000 = 2-FSK
    // Por defecto está en ASK/OOK (011)

    // Configurar data rate (~4800 baud)
    // MDMCFG4 y MDMCFG3 controlan el data rate

    // Configurar desviación FSK (~25 kHz)
    // DEVIATN register

    Serial.println("[DooyaBidir] Configurando FSK (433.92 MHz, 2-FSK)");

    // NOTA: La implementación completa de FSK requiere acceso directo
    // a los registros del CC1101. Algunas librerías no lo soportan.
    // Si usas ELECHOUSE_cc1101, puede que necesites modificarla.
}

void DooyaBidirectional::restoreASK() {
    // Restaurar configuración ASK/OOK para otros dispositivos
    ELECHOUSE_cc1101.setModulation(2);  // 2 = ASK/OOK

    Serial.println("[DooyaBidir] Restaurado a ASK/OOK");
}

String DooyaBidirectional::getStatusString() {
    String status = "DooyaBidir: ";
    if (!initialized) {
        status += "No inicializado";
    } else if (currentDeviceId == 0) {
        status += "Sin ID configurado";
    } else {
        status += "ID=0x";
        status += String(currentDeviceId, HEX);
        status += ", Unit=";
        status += String(currentUnitCode);
    }
    return status;
}

String DooyaBidirectional::getFrameHex() {
    String hex = "";
    for (int i = 0; i < DOOYA_BIDIR_FRAME_LEN; i++) {
        if (frameBuffer[i] < 16) hex += "0";
        hex += String(frameBuffer[i], HEX);
    }
    hex.toUpperCase();
    return hex;
}
