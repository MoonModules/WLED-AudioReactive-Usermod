#ifndef AUDIO_SYNC_H
#define AUDIO_SYNC_H

/*
   @title     Audio Sync Library
   @file      audio_sync.h
   @repo      https://github.com/MoonModules/WLED-MM
   @Authors   Extracted from audio_reactive.h
   @Copyright © 2024,2025 Github MoonModules Commit Authors
   @license   Licensed under the EUPL-1.2 or later

   UDP-based audio synchronization for sharing audio data across network
*/

#include <Arduino.h>

#ifdef ARDUINO_ARCH_ESP32
#include <WiFi.h>
#include <WiFiUdp.h>
#endif

#ifndef NUM_GEQ_CHANNELS
#define NUM_GEQ_CHANNELS 16
#endif

/**
 * @brief UDP-based audio synchronization
 *
 * Handles transmitting and receiving audio analysis data over UDP multicast.
 * Allows multiple devices to sync to one audio source on the network.
 */
class AudioSync {
public:
    /**
     * @brief Audio data packet V2 format (44 bytes)
     */
    struct __attribute__ ((packed)) AudioPacket {
        char    header[6];              // "00002" for V2 protocol
        uint8_t pressure[2];            // Sound pressure (fixed point)
        float   sampleRaw;              // Raw sample value
        float   sampleSmth;             // Smoothed sample value
        uint8_t samplePeak;             // Peak detected flag
        uint8_t frameCounter;           // Rolling counter for sequence
        uint8_t fftResult[16];          // 16 GEQ channels
        uint16_t zeroCrossingCount;     // Zero crossing count
        float   fftMagnitude;           // FFT magnitude
        float   fftMajorPeak;           // Major peak frequency
    };

    /**
     * @brief Audio data packet V1 format (88 bytes) - legacy
     */
    struct AudioPacket_v1 {
        char header[6];
        uint8_t myVals[32];
        int32_t sampleAgc;
        int32_t sampleRaw;
        float sampleAvg;
        bool samplePeak;
        uint8_t fftResult[16];
        double fftMagnitude;
        double fftMajorPeak;
    };

    /**
     * @brief Configuration for audio sync
     */
    struct Config {
        uint16_t port = 11988;              // UDP port
        bool enableTransmit = false;        // Enable transmission
        bool enableReceive = false;         // Enable reception
        bool sequenceCheck = true;          // Enable sequence checking
        uint8_t purgeCount = 1;             // Packets to read per cycle
        IPAddress multicastIP = IPAddress(239, 0, 0, 1);  // Multicast address
    };

    /**
     * @brief Received audio data
     */
    struct ReceivedData {
        float volumeSmth = 0.0f;
        float volumeRaw = 0.0f;
        bool samplePeak = false;
        uint8_t fftResult[NUM_GEQ_CHANNELS] = {0};
        float fftMagnitude = 0.0f;
        float fftMajorPeak = 1.0f;
        uint16_t zeroCrossingCount = 0;
        float soundPressure = 0.0f;
        float agcSensitivity = 128.0f;
        unsigned long lastReceiveTime = 0;
        int receivedFormat = 0;  // 0=none, 1=v1, 2=v2, 3=v2+
    };

    /**
     * @brief Constructor
     */
    AudioSync();

    /**
     * @brief Destructor
     */
    ~AudioSync();

    /**
     * @brief Configure audio sync
     * @param config Configuration
     */
    void configure(const Config& config);

    /**
     * @brief Start UDP sync (connect to network)
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Stop UDP sync (disconnect)
     */
    void end();

    /**
     * @brief Check if connected
     * @return true if UDP is active
     */
    bool isConnected() const { return m_connected; }

    /**
     * @brief Transmit audio data over UDP
     * @param volumeRaw Raw volume value
     * @param volumeSmth Smoothed volume value
     * @param samplePeak Peak detected flag
     * @param fftResult FFT result array (16 channels)
     * @param zeroCrossingCount Zero crossing count
     * @param fftMagnitude FFT magnitude
     * @param fftMajorPeak Major peak frequency
     * @param soundPressure Sound pressure estimate
     * @return true if transmitted successfully
     */
    bool transmit(
        float volumeRaw,
        float volumeSmth,
        bool samplePeak,
        const uint8_t* fftResult,
        uint16_t zeroCrossingCount,
        float fftMagnitude,
        float fftMajorPeak,
        float soundPressure
    );

    /**
     * @brief Receive audio data from UDP
     * @param maxPackets Maximum packets to read (255 = flush all)
     * @return true if new data was received
     */
    bool receive(unsigned maxPackets = 1);

    /**
     * @brief Get received audio data
     * @return Reference to received data structure
     */
    const ReceivedData& getReceivedData() const { return m_receivedData; }

    /**
     * @brief Check if reception has timed out
     * @param timeoutMs Timeout in milliseconds
     * @return true if timed out
     */
    bool hasTimedOut(unsigned long timeoutMs) const;

    /**
     * @brief Reset UDP connection (reconnect)
     */
    void reset();

private:
    // Configuration
    Config m_config;
    bool m_connected = false;

#ifdef ARDUINO_ARCH_ESP32
    WiFiUDP m_udp;
#endif

    // Transmission state
    uint8_t m_frameCounter = 0;
    bool m_udpSamplePeak = false;

    // Reception state
    ReceivedData m_receivedData;
    uint8_t m_lastFrameCounter = 0;

    // Buffer for receiving
    uint8_t m_receiveBuffer[96];

    // Internal methods
    bool decodePacketV2(int packetSize, const uint8_t* buffer);
    bool decodePacketV1(int packetSize, const uint8_t* buffer);
    bool isValidHeaderV2(const char* header) const;
    bool isValidHeaderV1(const char* header) const;
    bool checkSequence(uint8_t frameCounter);
};

#endif // AUDIO_SYNC_H

