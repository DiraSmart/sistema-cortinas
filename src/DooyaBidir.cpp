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
    // Configurar CC1101 para modulación 2-FSK (Dooya Bidireccional)
    // Referencia: CC1101 Datasheet (SWRS061I)

    Serial.println("[DooyaBidir] Configurando 2-FSK para Dooya...");

    // 1. Ir a estado IDLE antes de configurar
    ELECHOUSE_cc1101.setSidle();
    delay(1);

    // 2. Frecuencia: 433.92 MHz
    ELECHOUSE_cc1101.setMHZ(DOOYA_BIDIR_FREQUENCY);

    // 3. DEVIATN (0x15) - Desviación de frecuencia ~25 kHz
    // Fórmula: Deviation = (f_xosc / 2^17) * (8 + DEVIATION_M) * 2^DEVIATION_E
    // Con crystal 26 MHz, DEVIATION_E=3, DEVIATION_M=7: ~23.8 kHz
    // Registro: 0x37 = 0011 0111 (E=3, M=7)
    ELECHOUSE_cc1101.SpiWriteReg(0x15, 0x37);

    // 4. MDMCFG4 (0x10) - Channel BW y Data Rate Exponent
    // Bits 7:4 = CHANBW (channel bandwidth) - usar valor medio
    // Bits 3:0 = DRATE_E (data rate exponent) = 7 para ~4800 baud
    // Valor: 0xC7 = 1100 0111 (BW~100kHz, DRATE_E=7)
    ELECHOUSE_cc1101.SpiWriteReg(0x10, 0xC7);

    // 5. MDMCFG3 (0x11) - Data Rate Mantissa
    // DRATE_M = 131 (0x83) para ~4797 baud
    // Fórmula: DRATE = (f_xosc/2^28) * (256+DRATE_M) * 2^DRATE_E
    ELECHOUSE_cc1101.SpiWriteReg(0x11, 0x83);

    // 6. MDMCFG2 (0x12) - Formato de modulación
    // Bits 6:4 = MOD_FORMAT: 000 = 2-FSK (no ASK/OOK que es 011)
    // Bit 3 = MANCHESTER_EN: 0 = deshabilitado
    // Bits 2:0 = SYNC_MODE: 010 = 16-bit sync word
    // Valor: 0x02 = 0000 0010
    ELECHOUSE_cc1101.SpiWriteReg(0x12, 0x02);

    // 7. MDMCFG1 (0x13) - FEC, preámbulo, channel spacing
    // Bits 6:4 = NUM_PREAMBLE: 010 = 4 bytes preamble
    // Valor: 0x22
    ELECHOUSE_cc1101.SpiWriteReg(0x13, 0x22);

    // 8. SYNC1/SYNC0 (0x04, 0x05) - Sync word para Dooya
    // Sync word típico: 0xD391
    ELECHOUSE_cc1101.SpiWriteReg(0x04, 0xD3);  // SYNC1
    ELECHOUSE_cc1101.SpiWriteReg(0x05, 0x91);  // SYNC0

    // 9. PKTCTRL1 (0x07) - Control de paquete 1
    // Bit 2 = APPEND_STATUS: 0 = no agregar RSSI/LQI
    ELECHOUSE_cc1101.SpiWriteReg(0x07, 0x00);

    // 10. PKTCTRL0 (0x08) - Control de paquete 0
    // Bits 1:0 = LENGTH_CONFIG: 00 = fixed length
    // Bit 2 = CRC_EN: 0 = sin CRC
    ELECHOUSE_cc1101.SpiWriteReg(0x08, 0x00);

    // 11. PKTLEN (0x06) - Longitud fija del paquete
    ELECHOUSE_cc1101.SpiWriteReg(0x06, DOOYA_BIDIR_FRAME_LEN);

    // 12. Configurar potencia de transmisión
    ELECHOUSE_cc1101.setPA(12);  // Potencia máxima

    Serial.printf("[DooyaBidir] FSK configurado: 433.92 MHz, 2-FSK, ~4800 baud, dev ~25kHz\n");
}

void DooyaBidirectional::restoreASK() {
    // Restaurar configuración ASK/OOK para otros dispositivos

    // Ir a IDLE
    ELECHOUSE_cc1101.setSidle();
    delay(1);

    // Reinicializar el módulo con configuración por defecto
    ELECHOUSE_cc1101.Init();

    // Restaurar frecuencia por defecto (433.92 MHz)
    ELECHOUSE_cc1101.setMHZ(433.92);

    // Restaurar modulación ASK/OOK
    ELECHOUSE_cc1101.setModulation(2);  // 2 = ASK/OOK

    // Configuración para modo async/raw (captura de señales)
    ELECHOUSE_cc1101.setCCMode(1);
    ELECHOUSE_cc1101.setSyncMode(0);    // Sin sync word
    ELECHOUSE_cc1101.setCrc(0);         // Sin CRC
    ELECHOUSE_cc1101.setPA(10);         // Potencia normal

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
