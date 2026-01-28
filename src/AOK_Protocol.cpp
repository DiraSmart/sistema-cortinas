#include "AOK_Protocol.h"

// Global instance
AOK_Protocol aokProtocol;

AOK_Protocol::AOK_Protocol() {
    remoteId = 0x123456;    // Default ID - should be set to match original remote
    currentChannel = 1;
    initialized = false;
}

bool AOK_Protocol::begin() {
    // Check if CC1101 is available
    if (!ELECHOUSE_cc1101.getCC1101()) {
        Serial.println("[A-OK] ERROR: CC1101 no disponible");
        return false;
    }

    initialized = true;
    Serial.println("[A-OK] Protocolo A-OK inicializado");
    Serial.printf("[A-OK] Remote ID: 0x%06X, Canal: %d\n", remoteId, currentChannel);
    return true;
}

void AOK_Protocol::setRemoteId(uint32_t id) {
    // Only use lower 24 bits
    remoteId = id & 0xFFFFFF;
    Serial.printf("[A-OK] Remote ID establecido: 0x%06X\n", remoteId);
}

uint32_t AOK_Protocol::getRemoteId() {
    return remoteId;
}

void AOK_Protocol::setChannel(uint8_t channel) {
    // Channel 0 = group (all), Channel 1-16 = individual
    if (channel > 16) channel = 16;
    currentChannel = channel;
    if (channel == 0) {
        Serial.println("[A-OK] Canal establecido: 0 (GRUPO - todas las cortinas)");
    } else {
        Serial.printf("[A-OK] Canal establecido: %d\n", currentChannel);
    }
}

uint8_t AOK_Protocol::getChannel() {
    return currentChannel;
}

uint16_t AOK_Protocol::getChannelAddress(uint8_t channel) {
    // Channel address encoding for A-OK protocol
    // Channel 1-15 = single bit (1 << (channel-1))
    // Channel 0 (group/all) = 0x3F00 (bits 8-13 = channels 9-14)
    if (channel == 0) return 0x3F00;  // Group - captured from original remote
    return (uint16_t)(1 << (channel - 1));
}

uint8_t AOK_Protocol::calculateChecksum(uint32_t id, uint16_t address, uint8_t command) {
    // Checksum = sum of ID bytes + address bytes + command, truncated to 8 bits
    uint8_t sum = 0;
    sum += (id >> 16) & 0xFF;       // ID high byte
    sum += (id >> 8) & 0xFF;        // ID mid byte
    sum += id & 0xFF;               // ID low byte
    sum += (address >> 8) & 0xFF;   // Address high byte
    sum += address & 0xFF;          // Address low byte
    sum += command;                  // Command byte
    return sum;
}

void AOK_Protocol::buildFrame(uint8_t command, uint8_t* frame) {
    uint16_t address = getChannelAddress(currentChannel);
    uint8_t checksum = calculateChecksum(remoteId, address, command);

    // Frame structure (8 bytes + 1 trailing bit):
    // Byte 0: Start (0xA3)
    // Byte 1-3: Remote ID (24 bits)
    // Byte 4-5: Address (16 bits)
    // Byte 6: Command
    // Byte 7: Checksum
    // + 1 trailing bit (always 1)

    frame[0] = AOK_START_BYTE;
    frame[1] = (remoteId >> 16) & 0xFF;
    frame[2] = (remoteId >> 8) & 0xFF;
    frame[3] = remoteId & 0xFF;
    frame[4] = (address >> 8) & 0xFF;
    frame[5] = address & 0xFF;
    frame[6] = command;
    frame[7] = checksum;

    Serial.printf("[A-OK] Frame: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  frame[0], frame[1], frame[2], frame[3],
                  frame[4], frame[5], frame[6], frame[7]);
}

void AOK_Protocol::configureTransmitter() {
    // Configure CC1101 for A-OK transmission
    ELECHOUSE_cc1101.setSidle();
    delay(1);

    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setMHZ(AOK_FREQUENCY);
    ELECHOUSE_cc1101.setModulation(2);      // ASK/OOK
    ELECHOUSE_cc1101.setPA(12);             // Max power
    ELECHOUSE_cc1101.setCCMode(0);          // Transparent mode
    ELECHOUSE_cc1101.setSyncMode(0);        // No sync
    ELECHOUSE_cc1101.setCrc(0);             // No CRC
    ELECHOUSE_cc1101.setDcFilterOff(1);
    ELECHOUSE_cc1101.setPktFormat(3);       // Async serial mode

    // Configure GDO2 for TX
    pinMode(CC1101_GDO2, OUTPUT);
    digitalWrite(CC1101_GDO2, LOW);

    Serial.println("[A-OK] TX configurado: 433.92 MHz, ASK/OOK");
}

void AOK_Protocol::restoreConfig() {
    // Restore default configuration
    digitalWrite(CC1101_GDO2, LOW);
    delay(1);
    ELECHOUSE_cc1101.setSidle();
    pinMode(CC1101_GDO2, INPUT);

    // Restore to default receive mode
    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setMHZ(433.92);
    ELECHOUSE_cc1101.setModulation(2);
    ELECHOUSE_cc1101.setCCMode(1);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setCrc(0);
    ELECHOUSE_cc1101.setPA(10);

    Serial.println("[A-OK] Configuración restaurada");
}

void AOK_Protocol::sendAGC() {
    // Send AGC preamble: 5300µs HIGH + 530µs LOW
    digitalWrite(CC1101_GDO2, HIGH);
    delayMicroseconds(AOK_AGC1_PULSE);
    digitalWrite(CC1101_GDO2, LOW);
    delayMicroseconds(AOK_AGC2_PULSE);
}

void AOK_Protocol::sendBit(bool bit) {
    // A-OK bit encoding:
    // Bit 0: 270µs HIGH + 565µs LOW
    // Bit 1: 565µs HIGH + 270µs LOW
    if (bit) {
        // Bit 1: long HIGH, short LOW
        digitalWrite(CC1101_GDO2, HIGH);
        delayMicroseconds(AOK_LONG_PULSE);
        digitalWrite(CC1101_GDO2, LOW);
        delayMicroseconds(AOK_SHORT_PULSE);
    } else {
        // Bit 0: short HIGH, long LOW
        digitalWrite(CC1101_GDO2, HIGH);
        delayMicroseconds(AOK_SHORT_PULSE);
        digitalWrite(CC1101_GDO2, LOW);
        delayMicroseconds(AOK_LONG_PULSE);
    }
}

bool AOK_Protocol::transmitFrame(uint8_t* frame, int repeats) {
    if (!initialized) {
        Serial.println("[A-OK] ERROR: No inicializado");
        return false;
    }

    configureTransmitter();

    // Enter TX mode
    ELECHOUSE_cc1101.SetTx();
    delay(5);

    Serial.printf("[A-OK] Transmitiendo %d veces...\n", repeats);

    for (int rep = 0; rep < repeats; rep++) {
        // Disable interrupts for precise timing
        portDISABLE_INTERRUPTS();

        // Send AGC preamble
        sendAGC();

        // Send 64 bits from 8 bytes (MSB first)
        for (int byte = 0; byte < 8; byte++) {
            for (int bit = 7; bit >= 0; bit--) {
                bool b = (frame[byte] >> bit) & 0x01;
                sendBit(b);
            }
        }

        // Send trailing bit (always 1)
        sendBit(true);

        // Ensure LOW at end
        digitalWrite(CC1101_GDO2, LOW);

        portENABLE_INTERRUPTS();

        // Radio silence between repetitions
        if (rep < repeats - 1) {
            delayMicroseconds(AOK_RADIO_SILENCE);
        }
    }

    Serial.printf("[A-OK] TX completado: %d repeticiones\n", repeats);

    restoreConfig();
    return true;
}

bool AOK_Protocol::sendUp(int repeats) {
    Serial.println("[A-OK] Enviando UP");
    return sendCommand(AOK_CMD_UP, repeats);
}

bool AOK_Protocol::sendDown(int repeats) {
    Serial.println("[A-OK] Enviando DOWN");
    return sendCommand(AOK_CMD_DOWN, repeats);
}

bool AOK_Protocol::sendStop(int repeats) {
    Serial.println("[A-OK] Enviando STOP");
    return sendCommand(AOK_CMD_STOP, repeats);
}

bool AOK_Protocol::sendProgram(int repeats) {
    Serial.println("[A-OK] Enviando PROGRAM");
    return sendCommand(AOK_CMD_PROGRAM, repeats);
}

bool AOK_Protocol::sendCommand(uint8_t command, int repeats) {
    uint8_t frame[8];
    buildFrame(command, frame);
    return transmitFrame(frame, repeats);
}

bool AOK_Protocol::learnFromCapture(const uint8_t* capturedData, uint16_t length) {
    // Decode A-OK signal from captured raw pulse data
    // Format: AGC (5300µs + 530µs) + 65 bits of data
    // Bit encoding: 0 = short-long (270µs-565µs), 1 = long-short (565µs-270µs)

    Serial.println(">>> [A-OK] ENTRANDO A learnFromCapture <<<");
    Serial.flush();
    Serial.printf(">>> [A-OK] Datos: ptr=%p, len=%d <<<\n", capturedData, length);
    Serial.flush();

    if (length < 20) {
        Serial.println("[A-OK] Señal muy corta");
        return false;
    }

    // Print first 40 pulses for debugging
    Serial.println("[A-OK] Primeros pulsos capturados (µs):");
    for (int i = 0; i < min((int)length, 80); i += 2) {
        uint16_t pulse = (capturedData[i] << 8) | capturedData[i + 1];
        Serial.printf("%d ", pulse);
        if ((i/2 + 1) % 10 == 0) Serial.println();
    }
    Serial.println();

    // Tolerance for pulse timing - NO OVERLAP between short and long
    // Short pulse: 270µs nominal, range 135-400µs
    // Long pulse: 565µs nominal, range 420-850µs
    // Gap at 400-420µs to avoid ambiguity
    const uint16_t PULSE_SHORT_MIN = 135;   // 270 - 50%
    const uint16_t PULSE_SHORT_MAX = 400;   // Below midpoint (417µs)
    const uint16_t PULSE_LONG_MIN = 420;    // Above midpoint (417µs)
    const uint16_t PULSE_LONG_MAX = 850;    // 565 + 50%
    const uint16_t PULSE_AGC_MIN = 3500;    // 5300 - 35%
    const uint16_t PULSE_AGC_MAX = 8000;    // 5300 + 50%
    const uint16_t PULSE_MIDPOINT = 417;    // (270 + 565) / 2 - for ambiguous pulses

    uint16_t idx = 0;
    uint8_t decodedBytes[8] = {0};
    int bitCount = 0;
    int byteIdx = 0;
    bool foundAGC = false;

    // Step 1: Find AGC preamble (long pulse ~5300µs)
    while (idx + 1 < length) {
        uint16_t pulse = (capturedData[idx] << 8) | capturedData[idx + 1];
        idx += 2;

        if (pulse >= PULSE_AGC_MIN && pulse <= PULSE_AGC_MAX) {
            foundAGC = true;
            Serial.printf("[A-OK] AGC encontrado: %d µs en posición %d\n", pulse, idx - 2);
            // Skip AGC2 pulse (~530µs)
            if (idx + 1 < length) {
                uint16_t agc2 = (capturedData[idx] << 8) | capturedData[idx + 1];
                idx += 2;
                Serial.printf("[A-OK] AGC2: %d µs\n", agc2);
            }
            break;
        }
    }

    if (!foundAGC) {
        Serial.println("[A-OK] No se encontró preámbulo AGC - intentando detectar de otra forma...");
        // Try to find any long pulse > 2000µs as potential AGC
        idx = 0;
        while (idx + 1 < length) {
            uint16_t pulse = (capturedData[idx] << 8) | capturedData[idx + 1];
            if (pulse > 2000 && pulse < 10000) {
                foundAGC = true;
                Serial.printf("[A-OK] Pulso largo encontrado: %d µs - usando como AGC\n", pulse);
                idx += 2;
                // Skip next pulse (AGC2)
                if (idx + 1 < length) {
                    idx += 2;
                }
                break;
            }
            idx += 2;
        }
        if (!foundAGC) {
            Serial.println("[A-OK] Intentando decodificar desde el inicio...");
            idx = 0;
        }
    }

    // Step 2: Decode bits (each bit = 2 pulses)
    Serial.println("[A-OK] Decodificando bits...");
    while (idx + 3 < length && byteIdx < 8) {
        uint16_t pulse1 = (capturedData[idx] << 8) | capturedData[idx + 1];
        uint16_t pulse2 = (capturedData[idx + 2] << 8) | capturedData[idx + 3];
        idx += 4;

        // Skip if pulse is too long (gap between repetitions)
        if (pulse1 > 2000 || pulse2 > 2000) {
            Serial.printf("[A-OK] Gap detectado: %d, %d - fin de frame\n", pulse1, pulse2);
            break;
        }

        // Skip very short pulses (noise)
        if (pulse1 < 100 || pulse2 < 100) {
            Serial.printf("[A-OK] Ruido ignorado: %d, %d\n", pulse1, pulse2);
            continue;
        }

        bool bit;
        // Classify each pulse as short or long using midpoint for ambiguous cases
        bool p1Short = (pulse1 < PULSE_MIDPOINT);
        bool p2Short = (pulse2 < PULSE_MIDPOINT);

        if (pulse1 >= PULSE_SHORT_MIN && pulse1 <= PULSE_SHORT_MAX &&
            pulse2 >= PULSE_LONG_MIN && pulse2 <= PULSE_LONG_MAX) {
            // Clear Short-Long = bit 0
            bit = false;
        } else if (pulse1 >= PULSE_LONG_MIN && pulse1 <= PULSE_LONG_MAX &&
                   pulse2 >= PULSE_SHORT_MIN && pulse2 <= PULSE_SHORT_MAX) {
            // Clear Long-Short = bit 1
            bit = true;
        } else {
            // Ambiguous - use midpoint classification
            bit = !p1Short;  // If pulse1 >= midpoint, it's "long" = bit 1
            if (bitCount < 8) {  // Only print first 8 ambiguous for debug
                Serial.printf("[A-OK] Bit %d: %d/%d -> %d (midpoint)\n", bitCount, pulse1, pulse2, bit);
            }
        }

        // Store bit in current byte (MSB first)
        decodedBytes[byteIdx] = (decodedBytes[byteIdx] << 1) | (bit ? 1 : 0);
        bitCount++;

        if (bitCount % 8 == 0) {
            Serial.printf("[A-OK] Byte %d: 0x%02X\n", byteIdx, decodedBytes[byteIdx]);
            byteIdx++;
        }
    }

    Serial.printf("[A-OK] Decodificados %d bits (%d bytes)\n", bitCount, byteIdx);

    // Handle partial last byte
    if (bitCount % 8 != 0 && byteIdx < 8) {
        int remainingBits = bitCount % 8;
        decodedBytes[byteIdx] <<= (8 - remainingBits);  // Shift to align
        Serial.printf("[A-OK] Byte %d parcial: 0x%02X (%d bits)\n", byteIdx, decodedBytes[byteIdx], remainingBits);
        byteIdx++;
    }

    // Step 3: Verify and extract data
    if (byteIdx < 3) {
        Serial.println("[A-OK] Muy pocos bytes decodificados - puede no ser señal A-OK");
        Serial.println("[A-OK] Intente acercar más el control al receptor");
        return false;
    }

    // Print all decoded bytes for analysis
    Serial.print("[A-OK] Bytes decodificados: ");
    for (int i = 0; i < byteIdx; i++) {
        Serial.printf("%02X ", decodedBytes[i]);
    }
    Serial.println();

    // Check start byte (should be 0xA3)
    bool validStartByte = (decodedBytes[0] == AOK_START_BYTE);
    if (validStartByte) {
        Serial.println("[A-OK] Start byte 0xA3 verificado - Señal A-OK válida!");
    } else {
        Serial.printf("[A-OK] Start byte: 0x%02X (esperado 0xA3)\n", decodedBytes[0]);
    }

    // Extract Remote ID (bytes 1-3)
    uint32_t extractedId = ((uint32_t)decodedBytes[1] << 16) |
                           ((uint32_t)decodedBytes[2] << 8) |
                           decodedBytes[3];

    // Verify checksum if we have enough bytes
    bool validChecksum = false;
    if (byteIdx >= 8) {
        uint16_t address = ((uint16_t)decodedBytes[4] << 8) | decodedBytes[5];
        uint8_t cmd = decodedBytes[6];
        uint8_t receivedChecksum = decodedBytes[7];
        uint8_t calculatedChecksum = calculateChecksum(extractedId, address, cmd);

        validChecksum = (receivedChecksum == calculatedChecksum);
        if (validChecksum) {
            Serial.printf("[A-OK] Checksum VÁLIDO: 0x%02X\n", receivedChecksum);
        } else {
            Serial.printf("[A-OK] Checksum: recibido 0x%02X, calculado 0x%02X\n",
                         receivedChecksum, calculatedChecksum);
        }
    }

    // Extract channel from address (bytes 4-5)
    uint16_t address = ((uint16_t)decodedBytes[4] << 8) | decodedBytes[5];
    uint8_t extractedChannel = 0;

    // Count bits set - if multiple bits, it's a group (channel 0)
    int bitsSet = 0;
    int firstBit = -1;
    for (int i = 0; i < 16; i++) {
        if (address & (1 << i)) {
            bitsSet++;
            if (firstBit < 0) firstBit = i;
        }
    }

    // Determine channel: if multiple bits set = group (channel 0), else single channel
    if (bitsSet > 1) {
        extractedChannel = 0;  // Group/All
        Serial.printf("[A-OK] Grupo detectado: %d canales (address=0x%04X)\n", bitsSet, address);
    } else if (bitsSet == 1) {
        extractedChannel = firstBit + 1;
    } else {
        extractedChannel = 1; // Default to channel 1
    }

    // Extract command (byte 6)
    uint8_t cmd = byteIdx > 6 ? decodedBytes[6] : 0;
    const char* cmdName = "Desconocido";
    if (cmd == AOK_CMD_UP) cmdName = "UP";
    else if (cmd == AOK_CMD_DOWN) cmdName = "DOWN";
    else if (cmd == AOK_CMD_STOP) cmdName = "STOP";
    else if (cmd == AOK_CMD_PROGRAM) cmdName = "PROGRAM";

    Serial.println("[A-OK] ====== RESULTADO ======");
    Serial.printf("[A-OK] Remote ID: 0x%06X\n", extractedId);
    Serial.printf("[A-OK] Canal: %d\n", extractedChannel);
    Serial.printf("[A-OK] Comando: %s (0x%02X)\n", cmdName, cmd);
    Serial.printf("[A-OK] Start byte válido: %s\n", validStartByte ? "Sí" : "No");
    Serial.printf("[A-OK] Checksum válido: %s\n", validChecksum ? "Sí" : "No");

    // Calculate confidence level
    int confidence = 0;
    if (validStartByte) confidence += 40;
    if (validChecksum) confidence += 40;
    if (extractedId != 0 && extractedId != 0xFFFFFF) confidence += 20;
    Serial.printf("[A-OK] Confianza: %d%%\n", confidence);
    Serial.println("[A-OK] =======================");

    // Accept if checksum is valid (highest confidence) or start byte + reasonable ID
    if (validChecksum || (validStartByte && extractedId != 0 && extractedId != 0xFFFFFF)) {
        remoteId = extractedId;
        currentChannel = extractedChannel;
        Serial.println("[A-OK] Remote ID guardado automáticamente!");
        return true;
    }

    // Fallback: Accept the signal if we got a reasonable ID (even without exact start byte)
    // This helps with real-world signal variations
    if (extractedId != 0 && extractedId != 0xFFFFFF) {
        remoteId = extractedId;
        currentChannel = extractedChannel;
        Serial.println("[A-OK] Remote ID guardado (sin verificación completa)");
        return true;
    }

    // If ID is 0 or all 1s, try interpreting bytes differently
    // Maybe the signal uses different byte ordering
    if (byteIdx >= 4) {
        uint32_t altId = ((uint32_t)decodedBytes[0] << 16) |
                         ((uint32_t)decodedBytes[1] << 8) |
                         decodedBytes[2];
        if (altId != 0 && altId != 0xFFFFFF) {
            Serial.printf("[A-OK] Usando ID alternativo: 0x%06X\n", altId);
            remoteId = altId;
            currentChannel = extractedChannel;
            return true;
        }
    }

    Serial.println("[A-OK] No se pudo extraer un ID válido");
    return false;
}

bool AOK_Protocol::generateSignal(uint8_t command, uint8_t* buffer, uint16_t* length) {
    // Generate raw pulse data that can be stored and played back
    // This allows A-OK signals to be stored in the existing device/signal system

    uint8_t frame[8];
    buildFrame(command, frame);

    uint16_t idx = 0;

    // AGC pulse (5300µs HIGH)
    buffer[idx++] = (AOK_AGC1_PULSE >> 8) & 0xFF;
    buffer[idx++] = AOK_AGC1_PULSE & 0xFF;

    // AGC gap (530µs LOW) - represented as next pulse timing
    buffer[idx++] = (AOK_AGC2_PULSE >> 8) & 0xFF;
    buffer[idx++] = AOK_AGC2_PULSE & 0xFF;

    // 64 bits from frame + 1 trailing bit = 65 bits
    // Each bit is 2 pulses (HIGH + LOW timing)
    for (int byte = 0; byte < 8; byte++) {
        for (int bit = 7; bit >= 0; bit--) {
            bool b = (frame[byte] >> bit) & 0x01;
            if (b) {
                // Bit 1: long HIGH, short LOW
                buffer[idx++] = (AOK_LONG_PULSE >> 8) & 0xFF;
                buffer[idx++] = AOK_LONG_PULSE & 0xFF;
                buffer[idx++] = (AOK_SHORT_PULSE >> 8) & 0xFF;
                buffer[idx++] = AOK_SHORT_PULSE & 0xFF;
            } else {
                // Bit 0: short HIGH, long LOW
                buffer[idx++] = (AOK_SHORT_PULSE >> 8) & 0xFF;
                buffer[idx++] = AOK_SHORT_PULSE & 0xFF;
                buffer[idx++] = (AOK_LONG_PULSE >> 8) & 0xFF;
                buffer[idx++] = AOK_LONG_PULSE & 0xFF;
            }

            // Safety check
            if (idx >= RF_MAX_SIGNAL_LENGTH - 4) {
                *length = idx;
                return true;
            }
        }
    }

    // Trailing bit (1)
    buffer[idx++] = (AOK_LONG_PULSE >> 8) & 0xFF;
    buffer[idx++] = AOK_LONG_PULSE & 0xFF;
    buffer[idx++] = (AOK_SHORT_PULSE >> 8) & 0xFF;
    buffer[idx++] = AOK_SHORT_PULSE & 0xFF;

    *length = idx;
    Serial.printf("[A-OK] Señal generada: %d bytes\n", idx);
    return true;
}

String AOK_Protocol::getStatusString() {
    String status = "A-OK Protocol: ";
    if (!initialized) {
        status += "No inicializado";
    } else {
        status += "OK\n";
        status += "Remote ID: 0x" + String(remoteId, HEX) + "\n";
        status += "Canal: " + String(currentChannel);
    }
    return status;
}
