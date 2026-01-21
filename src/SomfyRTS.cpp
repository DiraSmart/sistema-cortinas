#include "SomfyRTS.h"

// Instancia global
SomfyRTS somfyRTS;

SomfyRTS::SomfyRTS() {
    txPin = CC1101_GDO0;
    remoteAddress = 0;
    currentRollingCode = 0;
    encryptionKey = 0xA7;
    initialized = false;
    memset(frameBuffer, 0, SOMFY_FRAME_LENGTH);
}

bool SomfyRTS::begin(uint8_t pin) {
    txPin = pin;
    pinMode(txPin, OUTPUT);
    digitalWrite(txPin, LOW);
    initialized = true;
    Serial.println("[SomfyRTS] Inicializado en pin " + String(txPin));
    return true;
}

void SomfyRTS::setRemote(uint32_t address, uint16_t rollingCode, uint8_t key) {
    remoteAddress = address & 0xFFFFFF;  // Solo 24 bits
    currentRollingCode = rollingCode;
    encryptionKey = key & 0x0F;  // Solo 4 bits para el key
    Serial.printf("[SomfyRTS] Configurado: Address=0x%06X, RC=%d, Key=0x%X\n",
                  remoteAddress, currentRollingCode, encryptionKey);
}

void SomfyRTS::setRemote(const SomfyRemote* remote) {
    if (remote) {
        setRemote(remote->address, remote->rollingCode, remote->encryptionKey);
    }
}

bool SomfyRTS::sendUp() {
    return sendCommand(SOMFY_CMD_UP);
}

bool SomfyRTS::sendDown() {
    return sendCommand(SOMFY_CMD_DOWN);
}

bool SomfyRTS::sendStop() {
    return sendCommand(SOMFY_CMD_MY);
}

bool SomfyRTS::sendProg() {
    return sendCommand(SOMFY_CMD_PROG);
}

bool SomfyRTS::sendCommand(uint8_t command) {
    if (!initialized) {
        Serial.println("[SomfyRTS] Error: No inicializado");
        return false;
    }

    if (remoteAddress == 0) {
        Serial.println("[SomfyRTS] Error: No hay dirección configurada");
        return false;
    }

    Serial.printf("[SomfyRTS] Enviando comando 0x%X (RC=%d)\n", command, currentRollingCode);

    // Construir el frame
    buildFrame(command);

    // Ofuscar el frame
    obfuscateFrame();

    // Deshabilitar interrupciones para timing preciso
    noInterrupts();

    // Transmitir primer frame (con 2 hardware syncs = wakeup)
    transmitFrame(true);

    // Esperar gap inter-frame
    delayMicrosecondsPrecise(SOMFY_INTER_FRAME_GAP);

    // Transmitir frames de repetición
    for (int i = 0; i < SOMFY_TOTAL_FRAMES - 1; i++) {
        transmitFrame(false);
        if (i < SOMFY_TOTAL_FRAMES - 2) {
            delayMicrosecondsPrecise(SOMFY_INTER_FRAME_GAP);
        }
    }

    // Restaurar interrupciones
    interrupts();

    digitalWrite(txPin, LOW);

    // Incrementar rolling code para próximo uso
    incrementRollingCode();

    Serial.println("[SomfyRTS] Comando enviado OK");
    return true;
}

void SomfyRTS::buildFrame(uint8_t command) {
    // Estructura del frame Somfy RTS (7 bytes = 56 bits):
    // Byte 0: Key (4 bits) + Ctrl/Checksum placeholder (4 bits)
    // Byte 1: Ctrl (4 bits) + Checksum (4 bits)
    // Byte 2: Rolling Code MSB
    // Byte 3: Rolling Code LSB
    // Byte 4: Address byte 0 (LSB)
    // Byte 5: Address byte 1
    // Byte 6: Address byte 2 (MSB)

    memset(frameBuffer, 0, SOMFY_FRAME_LENGTH);

    // Byte 0: Encryption key (4 bits altos)
    frameBuffer[0] = encryptionKey << 4;

    // Byte 1: Comando (4 bits altos) - checksum se calcula después
    frameBuffer[1] = (command & 0x0F) << 4;

    // Bytes 2-3: Rolling code (big endian)
    frameBuffer[2] = (currentRollingCode >> 8) & 0xFF;
    frameBuffer[3] = currentRollingCode & 0xFF;

    // Bytes 4-6: Address (little endian, 24 bits)
    frameBuffer[4] = remoteAddress & 0xFF;
    frameBuffer[5] = (remoteAddress >> 8) & 0xFF;
    frameBuffer[6] = (remoteAddress >> 16) & 0xFF;

    // Calcular checksum (XOR de todos los nibbles)
    uint8_t checksum = 0;
    for (int i = 0; i < SOMFY_FRAME_LENGTH; i++) {
        checksum ^= frameBuffer[i] ^ (frameBuffer[i] >> 4);
    }
    checksum &= 0x0F;

    // Insertar checksum en los 4 bits bajos del byte 1
    frameBuffer[1] |= checksum;

    // Debug: mostrar frame antes de ofuscar
    Serial.print("[SomfyRTS] Frame (claro): ");
    for (int i = 0; i < SOMFY_FRAME_LENGTH; i++) {
        Serial.printf("%02X ", frameBuffer[i]);
    }
    Serial.println();
}

void SomfyRTS::obfuscateFrame() {
    // Ofuscación Somfy: cada byte XOR con el byte anterior (ofuscado)
    // El primer byte no se modifica
    for (int i = 1; i < SOMFY_FRAME_LENGTH; i++) {
        frameBuffer[i] ^= frameBuffer[i - 1];
    }

    // Debug: mostrar frame ofuscado
    Serial.print("[SomfyRTS] Frame (ofuscado): ");
    for (int i = 0; i < SOMFY_FRAME_LENGTH; i++) {
        Serial.printf("%02X ", frameBuffer[i]);
    }
    Serial.println();
}

void SomfyRTS::transmitFrame(bool isFirstFrame) {
    // Enviar hardware sync
    int hwSyncCount = isFirstFrame ? SOMFY_FIRST_FRAME_REPS * 2 : SOMFY_REPEAT_REPS;
    sendHardwareSync(hwSyncCount);

    // Enviar software sync
    sendSoftwareSync();

    // Enviar datos (Manchester encoding)
    // En Manchester: 0 = LOW->HIGH, 1 = HIGH->LOW (o viceversa según convención)
    // Somfy usa: 0 = rising edge, 1 = falling edge
    for (int i = 0; i < SOMFY_FRAME_LENGTH; i++) {
        uint8_t byte = frameBuffer[i];
        for (int bit = 7; bit >= 0; bit--) {
            sendBit((byte >> bit) & 1);
        }
    }

    digitalWrite(txPin, LOW);
}

void SomfyRTS::sendBit(bool bit) {
    // Manchester encoding para Somfy RTS
    // Bit 0: LOW durante half-symbol, HIGH durante half-symbol (rising edge en medio)
    // Bit 1: HIGH durante half-symbol, LOW durante half-symbol (falling edge en medio)

    if (bit) {
        // Bit 1: HIGH -> LOW
        digitalWrite(txPin, HIGH);
        delayMicrosecondsPrecise(SOMFY_SYMBOL_WIDTH);
        digitalWrite(txPin, LOW);
        delayMicrosecondsPrecise(SOMFY_SYMBOL_WIDTH);
    } else {
        // Bit 0: LOW -> HIGH
        digitalWrite(txPin, LOW);
        delayMicrosecondsPrecise(SOMFY_SYMBOL_WIDTH);
        digitalWrite(txPin, HIGH);
        delayMicrosecondsPrecise(SOMFY_SYMBOL_WIDTH);
    }
}

void SomfyRTS::sendHardwareSync(int count) {
    // Hardware sync: pulsos high/low de 2416us cada uno
    for (int i = 0; i < count; i++) {
        digitalWrite(txPin, HIGH);
        delayMicrosecondsPrecise(SOMFY_HWSYNC_HIGH);
        digitalWrite(txPin, LOW);
        delayMicrosecondsPrecise(SOMFY_HWSYNC_LOW);
    }
}

void SomfyRTS::sendSoftwareSync() {
    // Software sync: 4550us HIGH + 604us LOW
    digitalWrite(txPin, HIGH);
    delayMicrosecondsPrecise(SOMFY_SWSYNC_HIGH);
    digitalWrite(txPin, LOW);
    delayMicrosecondsPrecise(SOMFY_SWSYNC_LOW);
}

void SomfyRTS::incrementRollingCode() {
    currentRollingCode++;
    Serial.printf("[SomfyRTS] Rolling code incrementado a %d\n", currentRollingCode);
}

void SomfyRTS::delayMicrosecondsPrecise(unsigned long us) {
    // Para timings muy precisos en ESP32
    unsigned long start = micros();
    while (micros() - start < us) {
        // Spin wait
    }
}

String SomfyRTS::getStatusString() {
    String status = "SomfyRTS: ";
    if (!initialized) {
        status += "No inicializado";
    } else if (remoteAddress == 0) {
        status += "Sin dirección configurada";
    } else {
        status += "Addr=0x";
        status += String(remoteAddress, HEX);
        status += ", RC=";
        status += String(currentRollingCode);
    }
    return status;
}
