#include "CC1101_RF.h"

// Instancia estática para ISR
CC1101_RF* CC1101_RF::instance = nullptr;
CC1101_RF rfModule;

CC1101_RF::CC1101_RF() {
    currentFrequency = RF_DEFAULT_FREQUENCY;
    currentModulation = 2; // ASK/OOK
    capturing = false;
    connected = false;
    captureIndex = 0;
    lastPulse = 0;
    captureComplete = false;
    instance = this;
}

bool CC1101_RF::begin() {
    Serial.println("[RF] Inicializando CC1101...");

    // Configurar pines SPI
    ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CSN);

    // Inicializar módulo
    if (ELECHOUSE_cc1101.getCC1101()) {
        Serial.println("[RF] CC1101 conectado!");
        connected = true;

        // Configuración inicial
        ELECHOUSE_cc1101.Init();
        ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
        ELECHOUSE_cc1101.setCCMode(1);      // Modo para recibir/transmitir datos
        ELECHOUSE_cc1101.setModulation(currentModulation);
        ELECHOUSE_cc1101.setMHZ(currentFrequency);
        ELECHOUSE_cc1101.setPA(10);         // Potencia de transmisión
        ELECHOUSE_cc1101.setSyncMode(0);    // Sin palabra de sincronización
        ELECHOUSE_cc1101.setCrc(0);         // Sin CRC

        // Configuración para captura raw
        ELECHOUSE_cc1101.setDcFilterOff(1);
        ELECHOUSE_cc1101.setPktFormat(3);   // Async serial mode
        ELECHOUSE_cc1101.setLengthConfig(2);

        Serial.printf("[RF] Frecuencia: %.2f MHz\n", currentFrequency);
        return true;
    } else {
        Serial.println("[RF] ERROR: CC1101 no detectado!");
        connected = false;
        return false;
    }
}

bool CC1101_RF::isConnected() {
    return connected && ELECHOUSE_cc1101.getCC1101();
}

void CC1101_RF::setFrequency(float freq) {
    currentFrequency = freq;
    if (connected) {
        ELECHOUSE_cc1101.setMHZ(freq);
        Serial.printf("[RF] Frecuencia cambiada a: %.2f MHz\n", freq);
    }
}

float CC1101_RF::getFrequency() {
    return currentFrequency;
}

void CC1101_RF::setModulation(int mod) {
    currentModulation = mod;
    if (connected) {
        ELECHOUSE_cc1101.setModulation(mod);
        Serial.printf("[RF] Modulación cambiada a: %d\n", mod);
    }
}

int CC1101_RF::getModulation() {
    return currentModulation;
}

bool CC1101_RF::startCapture() {
    if (!connected) return false;

    // Reset buffer
    captureIndex = 0;
    captureComplete = false;
    memset((void*)captureBuffer, 0, RF_MAX_SIGNAL_LENGTH);

    // Configurar para recepción
    configureReceiver();

    // Habilitar interrupción
    pinMode(CC1101_GDO0, INPUT);
    attachInterrupt(digitalPinToInterrupt(CC1101_GDO0), handleInterrupt, CHANGE);

    // Iniciar recepción
    ELECHOUSE_cc1101.SetRx();
    capturing = true;
    lastPulse = micros();

    Serial.println("[RF] Captura iniciada...");
    return true;
}

void CC1101_RF::stopCapture() {
    capturing = false;
    detachInterrupt(digitalPinToInterrupt(CC1101_GDO0));
    ELECHOUSE_cc1101.setSidle();
    Serial.println("[RF] Captura detenida");
}

bool CC1101_RF::isCapturing() {
    return capturing;
}

void IRAM_ATTR CC1101_RF::handleInterrupt() {
    if (instance) {
        instance->onInterrupt();
    }
}

void CC1101_RF::onInterrupt() {
    if (!capturing || captureComplete) return;

    unsigned long now = micros();
    unsigned long duration = now - lastPulse;
    lastPulse = now;

    // Filtrar pulsos según configuración avanzada
    // RF_MIN_PULSE_WIDTH = 50us para capturar pulsos cortos (Vertilux, etc.)
    if (duration < RF_MIN_PULSE_WIDTH) return;

    // Si el pulso es muy largo, puede ser fin de transmisión o gap
    if (duration > RF_MAX_PULSE_WIDTH) {
        // Si ya tenemos suficientes pulsos, marcar como completo
        if (captureIndex >= RF_MIN_PULSES * 2) {
            captureComplete = true;
        }
        return;
    }

    // Guardar duración como bytes (high/low)
    if (captureIndex < RF_MAX_SIGNAL_LENGTH - 2) {
        captureBuffer[captureIndex++] = (duration >> 8) & 0xFF;
        captureBuffer[captureIndex++] = duration & 0xFF;
    }

    // Detectar fin de transmisión (gap largo pero dentro del rango)
    if (duration > RF_SIGNAL_GAP && captureIndex >= RF_MIN_PULSES * 2) {
        captureComplete = true;
    }
}

bool CC1101_RF::captureSignal(RFSignal* signal, unsigned long timeout) {
    if (!connected) return false;

    unsigned long startTime = millis();

    // Iniciar captura
    if (!startCapture()) return false;

    // Esperar señal o timeout
    while (!captureComplete && (millis() - startTime) < timeout) {
        delay(10);
    }

    // Detener captura
    stopCapture();

    if (captureComplete && captureIndex > 10) {
        // Copiar datos capturados
        memcpy(signal->data, (void*)captureBuffer, captureIndex);
        signal->length = captureIndex;
        signal->frequency = currentFrequency;
        signal->modulation = currentModulation;
        signal->bandwidth = 0;
        signal->dataRate = 0;
        signal->deviation = 0;
        signal->timestamp = millis();
        signal->valid = true;

        Serial.printf("[RF] Señal capturada: %d bytes\n", signal->length);
        return true;
    }

    signal->valid = false;
    Serial.println("[RF] Timeout de captura");
    return false;
}

bool CC1101_RF::transmitSignal(const RFSignal* signal, int repeats) {
    if (!connected || !signal->valid) return false;

    return transmitRaw(signal->data, signal->length, repeats);
}

bool CC1101_RF::transmitRaw(const uint8_t* data, uint16_t length, int repeats) {
    if (!connected || length == 0) return false;

    Serial.printf("[RF] Transmitiendo %d bytes, %d repeticiones...\n", length, repeats);

    // Configurar para transmisión
    configureTransmitter();
    ELECHOUSE_cc1101.SetTx();

    pinMode(CC1101_GDO0, OUTPUT);

    for (int rep = 0; rep < repeats; rep++) {
        // Reproducir señal
        for (uint16_t i = 0; i < length - 1; i += 2) {
            uint16_t duration = (data[i] << 8) | data[i + 1];

            if (duration > 0 && duration < 50000) {
                // Alternar estado
                digitalWrite(CC1101_GDO0, (i / 2) % 2);
                delayMicroseconds(duration);
            }
        }

        // Pausa entre repeticiones
        digitalWrite(CC1101_GDO0, LOW);
        delay(10);
    }

    // Volver a modo idle
    ELECHOUSE_cc1101.setSidle();
    pinMode(CC1101_GDO0, INPUT);

    Serial.println("[RF] Transmisión completada");
    return true;
}

float CC1101_RF::scanForSignal(float* frequencies, int count, unsigned long timeout) {
    if (!connected) return 0;

    Serial.println("[RF] Escaneando frecuencias...");

    float originalFreq = currentFrequency;
    float detectedFreq = 0;
    int maxRSSI = -120;

    for (int i = 0; i < count; i++) {
        setFrequency(frequencies[i]);
        ELECHOUSE_cc1101.SetRx();

        unsigned long start = millis();
        while ((millis() - start) < (timeout / count)) {
            int rssi = getRSSI();
            if (rssi > maxRSSI && rssi > -70) {
                maxRSSI = rssi;
                detectedFreq = frequencies[i];
                Serial.printf("[RF] Señal detectada en %.2f MHz (RSSI: %d)\n",
                              frequencies[i], rssi);
            }
            delay(10);
        }
    }

    // Restaurar frecuencia original si no se detectó nada
    if (detectedFreq == 0) {
        setFrequency(originalFreq);
    } else {
        setFrequency(detectedFreq);
    }

    return detectedFreq;
}

bool CC1101_RF::autoDetectSettings(RFSignal* signal, unsigned long timeout) {
    if (!connected) return false;

    Serial.println("[RF] Iniciando detección automática...");

    // Primero escanear frecuencias
    float detected = scanForSignal((float*)RF_FREQUENCIES, RF_FREQUENCIES_COUNT, timeout / 2);

    if (detected > 0) {
        setFrequency(detected);
        Serial.printf("[RF] Frecuencia detectada: %.2f MHz\n", detected);

        // Intentar capturar señal
        if (captureSignal(signal, timeout / 2)) {
            return true;
        }
    }

    // Si no se detectó, probar con frecuencia por defecto
    setFrequency(RF_DEFAULT_FREQUENCY);
    return captureSignal(signal, timeout);
}

RFProtocol CC1101_RF::detectProtocol(const RFSignal* signal) {
    if (!signal->valid || signal->length < 10) return PROTOCOL_UNKNOWN;

    // Analizar patrones de pulsos para detectar protocolo
    int shortCount = 0, longCount = 0, veryShortCount = 0;
    int avgShort = 0, avgLong = 0;
    int syncPulse = 0;
    int pulseCount = signal->length / 2;

    // Primera pasada: contar y categorizar pulsos
    for (uint16_t i = 0; i < signal->length - 1; i += 2) {
        uint16_t duration = (signal->data[i] << 8) | signal->data[i + 1];

        if (duration < 200) {
            veryShortCount++;
        } else if (duration < 500) {
            shortCount++;
            avgShort += duration;
        } else if (duration < 1000) {
            longCount++;
            avgLong += duration;
        } else if (duration > 4000 && duration < 6000) {
            syncPulse = duration;
        }
    }

    // Calcular promedios
    if (shortCount > 0) avgShort /= shortCount;
    if (longCount > 0) avgLong /= longCount;

    // Detectar protocolo basado en características

    // DOOYA: pulsos cortos ~350us, largos ~700us, sync ~4900us
    if (avgShort >= 300 && avgShort <= 400 &&
        avgLong >= 600 && avgLong <= 800 &&
        syncPulse >= 4500 && syncPulse <= 5500) {
        Serial.println("[RF] Protocolo detectado: Dooya");
        return PROTOCOL_DOOYA;
    }

    // VERTILUX: pulsos muy cortos ~280us, largos ~850us, sync ~9000us
    if (veryShortCount > shortCount &&
        avgLong >= 750 && avgLong <= 950) {
        Serial.println("[RF] Protocolo detectado: Vertilux/VTI");
        return PROTOCOL_VERTILUX;
    }

    // EV1527: típico 20 bits direccion + 4 datos, pulsos ~300-400us
    if (pulseCount >= 40 && pulseCount <= 60 &&
        avgShort >= 250 && avgShort <= 450) {
        Serial.println("[RF] Protocolo detectado: EV1527");
        return PROTOCOL_EV1527;
    }

    // PT2262: similar a EV1527 pero con diferentes timings
    if (pulseCount >= 20 && pulseCount <= 50 &&
        avgShort >= 150 && avgShort <= 350 &&
        avgLong >= 400 && avgLong <= 600) {
        Serial.println("[RF] Protocolo detectado: PT2262");
        return PROTOCOL_PT2262;
    }

    // Si es ASK/OOK pero no coincide con otros, es genérico
    if (signal->modulation == 2) {
        Serial.println("[RF] Protocolo detectado: Genérico ASK/OOK");
        return PROTOCOL_GENERIC;
    }

    return PROTOCOL_UNKNOWN;
}

String CC1101_RF::getProtocolName(RFProtocol protocol) {
    switch (protocol) {
        case PROTOCOL_GENERIC: return "Genérico ASK/OOK";
        case PROTOCOL_DOOYA: return "Dooya";
        case PROTOCOL_ZEMISMART: return "Zemismart";
        case PROTOCOL_TUYA: return "Tuya RF";
        case PROTOCOL_EV1527: return "EV1527";
        case PROTOCOL_PT2262: return "PT2262";
        case PROTOCOL_NICE_FLO: return "Nice Flor-s";
        case PROTOCOL_CAME: return "Came";
        case PROTOCOL_VERTILUX: return "Vertilux/VTI";
        default: return "Desconocido";
    }
}

String CC1101_RF::analyzeSignal(const RFSignal* signal) {
    if (!signal->valid) return "Señal inválida";

    // Detectar protocolo
    RFProtocol protocol = detectProtocol(signal);

    String analysis = "Análisis de señal RF:\n";
    analysis += "────────────────────────────────\n";
    analysis += "Frecuencia: " + String(signal->frequency, 2) + " MHz\n";
    analysis += "Longitud: " + String(signal->length) + " bytes\n";
    analysis += "Modulación: ";

    switch (signal->modulation) {
        case 0: analysis += "2-FSK\n"; break;
        case 1: analysis += "GFSK\n"; break;
        case 2: analysis += "ASK/OOK\n"; break;
        case 3: analysis += "4-FSK\n"; break;
        case 4: analysis += "MSK\n"; break;
        default: analysis += "Desconocida\n";
    }

    analysis += "────────────────────────────────\n";
    analysis += "Protocolo detectado: " + getProtocolName(protocol) + "\n";

    // Analizar patrones en los datos
    int shortPulses = 0, longPulses = 0, veryShort = 0;
    int minPulse = 65535, maxPulse = 0;

    for (uint16_t i = 0; i < signal->length - 1; i += 2) {
        uint16_t duration = (signal->data[i] << 8) | signal->data[i + 1];
        if (duration < minPulse) minPulse = duration;
        if (duration > maxPulse && duration < 15000) maxPulse = duration;

        if (duration < 200) veryShort++;
        else if (duration < 500) shortPulses++;
        else longPulses++;
    }

    analysis += "Pulsos cortos (<500us): " + String(shortPulses) + "\n";
    analysis += "Pulsos largos (>500us): " + String(longPulses) + "\n";
    if (veryShort > 0) {
        analysis += "Pulsos muy cortos (<200us): " + String(veryShort) + "\n";
    }
    analysis += "Rango: " + String(minPulse) + " - " + String(maxPulse) + " us\n";
    analysis += "────────────────────────────────\n";

    // Agregar descripción del protocolo
    switch (protocol) {
        case PROTOCOL_DOOYA:
            analysis += "Dooya: Cortinas motorizadas, 24-28 bits\n";
            break;
        case PROTOCOL_VERTILUX:
            analysis += "Vertilux/VTI: Similar a EV1527, pulsos cortos\n";
            break;
        case PROTOCOL_EV1527:
            analysis += "EV1527: Común en controles genéricos\n";
            analysis += "20 bits dirección + 4 bits datos\n";
            break;
        case PROTOCOL_PT2262:
            analysis += "PT2262/PT2272: Clásico en garajes/alarmas\n";
            break;
        case PROTOCOL_GENERIC:
            analysis += "Señal genérica, sin patrón específico\n";
            break;
        default:
            analysis += "Protocolo no identificado\n";
    }

    return analysis;
}

String CC1101_RF::getRecommendedSettings(const RFSignal* signal) {
    String rec = "Configuración recomendada:\n";

    // Basado en la frecuencia
    if (signal->frequency >= 433.0 && signal->frequency <= 434.0) {
        rec += "- Región: Europa/Latinoamérica (433 MHz)\n";
        rec += "- Dispositivos comunes: controles de garaje, cortinas, alarmas\n";
    } else if (signal->frequency >= 314.0 && signal->frequency <= 316.0) {
        rec += "- Región: USA/Asia (315 MHz)\n";
        rec += "- Dispositivos comunes: controles de auto, sensores\n";
    } else if (signal->frequency >= 867.0 && signal->frequency <= 869.0) {
        rec += "- Región: Europa (868 MHz)\n";
        rec += "- Dispositivos comunes: domótica avanzada\n";
    }

    rec += "\nOpciones para probar:\n";
    rec += "1. Frecuencia: " + String(signal->frequency, 2) + " MHz\n";
    rec += "2. Modulación: ASK/OOK (más común)\n";
    rec += "3. Repeticiones: 3-5 veces\n";

    return rec;
}

int CC1101_RF::getRSSI() {
    if (!connected) return -120;
    return ELECHOUSE_cc1101.getRssi();
}

int CC1101_RF::getLQI() {
    if (!connected) return 0;
    return ELECHOUSE_cc1101.getLqi();
}

void CC1101_RF::setTxPower(int power) {
    if (connected) {
        ELECHOUSE_cc1101.setPA(power);
    }
}

void CC1101_RF::reset() {
    if (connected) {
        ELECHOUSE_cc1101.setSidle();
        ELECHOUSE_cc1101.SpiStrobe(0x30); // SRES
        delay(100);
        begin();
    }
}

String CC1101_RF::getStatusString() {
    if (!connected) return "CC1101 no conectado";

    String status = "CC1101 Status:\n";
    status += "- Conectado: Sí\n";
    status += "- Frecuencia: " + String(currentFrequency, 2) + " MHz\n";
    status += "- RSSI: " + String(getRSSI()) + " dBm\n";
    status += "- LQI: " + String(getLQI()) + "\n";
    status += "- Capturando: " + String(capturing ? "Sí" : "No") + "\n";

    return status;
}

void CC1101_RF::configureReceiver() {
    ELECHOUSE_cc1101.setCCMode(1);
    ELECHOUSE_cc1101.setModulation(currentModulation);
    ELECHOUSE_cc1101.setMHZ(currentFrequency);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setCrc(0);
    ELECHOUSE_cc1101.setDcFilterOff(1);
    ELECHOUSE_cc1101.setPktFormat(3);
}

void CC1101_RF::configureTransmitter() {
    ELECHOUSE_cc1101.setCCMode(1);
    ELECHOUSE_cc1101.setModulation(currentModulation);
    ELECHOUSE_cc1101.setMHZ(currentFrequency);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setCrc(0);
    ELECHOUSE_cc1101.setPktFormat(0);
}

bool CC1101_RF::waitForSignal(unsigned long timeout) {
    unsigned long start = millis();
    while ((millis() - start) < timeout) {
        if (getRSSI() > -70) {
            return true;
        }
        delay(10);
    }
    return false;
}

void CC1101_RF::processRawSignal(RFSignal* signal) {
    // Normalizar duraciones de pulsos
    // Implementación futura para mejor decodificación
}
