#ifndef AOK_PROTOCOL_H
#define AOK_PROTOCOL_H

#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include "config.h"

// ============================================
// A-OK AC114-01B PROTOCOL SPECIFICATIONS
// Based on: https://github.com/akirjavainen/A-OK
// ============================================

// Timing constants (microseconds)
#define AOK_AGC1_PULSE      5300    // AGC HIGH pulse
#define AOK_AGC2_PULSE      530     // AGC LOW pulse
#define AOK_RADIO_SILENCE   5030    // Gap between repetitions
#define AOK_SHORT_PULSE     270     // Short pulse (bit component)
#define AOK_LONG_PULSE      565     // Long pulse (bit component)

// Protocol structure
#define AOK_TOTAL_BITS      65      // Total bits per command
#define AOK_START_BYTE      0xA3    // Fixed start byte
#define AOK_REPEAT_COUNT    8       // Number of transmissions

// Commands
#define AOK_CMD_UP          0x0B    // Capturado del control original
#define AOK_CMD_DOWN        0x43    // 67 decimal
#define AOK_CMD_STOP        0x23    // 35 decimal
#define AOK_CMD_PROGRAM     0x53    // 83 decimal
#define AOK_CMD_CONFIRM     0x24    // 36 decimal (after UP/DOWN)

// Frequency
#define AOK_FREQUENCY       433.92  // MHz

class AOK_Protocol {
public:
    AOK_Protocol();

    // Initialize with CC1101 module
    bool begin();

    // Set remote ID (24 bits) - get this from original remote or use random
    void setRemoteId(uint32_t id);
    uint32_t getRemoteId();

    // Set channel (1-16)
    void setChannel(uint8_t channel);
    uint8_t getChannel();

    // Commands
    bool sendUp(int repeats = AOK_REPEAT_COUNT);
    bool sendDown(int repeats = AOK_REPEAT_COUNT);
    bool sendStop(int repeats = AOK_REPEAT_COUNT);
    bool sendProgram(int repeats = AOK_REPEAT_COUNT);
    bool sendCommand(uint8_t command, int repeats = AOK_REPEAT_COUNT);

    // Learn remote ID from captured signal (optional)
    bool learnFromCapture(const uint8_t* capturedData, uint16_t length);

    // Generate raw signal for storage (for compatibility with existing system)
    bool generateSignal(uint8_t command, uint8_t* buffer, uint16_t* length);

    // Get status
    String getStatusString();

private:
    uint32_t remoteId;      // 24-bit remote ID
    uint8_t currentChannel; // 1-16
    bool initialized;

    // Build the 65-bit command frame
    void buildFrame(uint8_t command, uint8_t* frame);

    // Calculate checksum
    uint8_t calculateChecksum(uint32_t id, uint16_t address, uint8_t command);

    // Get address word for channel
    uint16_t getChannelAddress(uint8_t channel);

    // Transmit frame
    bool transmitFrame(uint8_t* frame, int repeats);

    // Send single bit (0 or 1)
    void sendBit(bool bit);

    // Send AGC preamble
    void sendAGC();

    // Configure CC1101 for A-OK transmission
    void configureTransmitter();
    void restoreConfig();
};

// Global instance
extern AOK_Protocol aokProtocol;

#endif // AOK_PROTOCOL_H
