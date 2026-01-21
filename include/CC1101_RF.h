#ifndef CC1101_RF_H
#define CC1101_RF_H

#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include "config.h"

class CC1101_RF {
public:
    CC1101_RF();

    // Inicialización
    bool begin();
    bool isConnected();

    // Configuración de frecuencia
    void setFrequency(float freq);
    float getFrequency();
    void setModulation(int mod);
    int getModulation();

    // Captura de señales
    bool startCapture();
    void stopCapture();
    bool isCapturing();
    bool captureSignal(RFSignal* signal, unsigned long timeout = RF_CAPTURE_TIMEOUT);

    // Transmisión de señales
    bool transmitSignal(const RFSignal* signal, int repeats = RF_REPEAT_TRANSMIT);
    bool transmitRaw(const uint8_t* data, uint16_t length, int repeats = RF_REPEAT_TRANSMIT);

    // Detección automática de frecuencia
    float scanForSignal(float* frequencies, int count, unsigned long timeout = 3000);
    bool autoDetectSettings(RFSignal* signal, unsigned long timeout = 5000);

    // Análisis de señal y detección de protocolo
    RFProtocol detectProtocol(const RFSignal* signal);
    String getProtocolName(RFProtocol protocol);
    String analyzeSignal(const RFSignal* signal);
    String getRecommendedSettings(const RFSignal* signal);

    // Utilidades
    int getRSSI();
    int getLQI();
    void setTxPower(int power);
    void reset();

    // Estado
    String getStatusString();

private:
    float currentFrequency;
    int currentModulation;
    bool capturing;
    bool connected;

    // Buffer para captura raw
    volatile uint8_t captureBuffer[RF_MAX_SIGNAL_LENGTH];
    volatile uint16_t captureIndex;
    volatile unsigned long lastPulse;
    volatile bool captureComplete;

    // Métodos internos
    void configureReceiver();
    void configureTransmitter();
    bool waitForSignal(unsigned long timeout);
    void processRawSignal(RFSignal* signal);

    // ISR helper
    static CC1101_RF* instance;
    static void IRAM_ATTR handleInterrupt();
    void onInterrupt();
};

// Instancia global
extern CC1101_RF rfModule;

#endif // CC1101_RF_H
