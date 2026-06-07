/*
   @title     Audio Sync Library
   @file      audio_sync.cpp
   @repo      https://github.com/MoonModules/WLED-MM
   @Authors   Extracted from audio_reactive.h
   @Copyright © 2024,2025 Github MoonModules Commit Authors
   @license   Licensed under the EUPL-1.2 or later
*/

#include "audio_sync.h"
#include <string.h>

// Protocol headers
static const char UDP_SYNC_HEADER_V2[] = "00002";
static const char UDP_SYNC_HEADER_V1[] = "00001";

AudioSync::AudioSync() {
}

AudioSync::~AudioSync() {
    end();
}

void AudioSync::configure(const Config& config) {
    m_config = config;
}

bool AudioSync::begin() {
#ifdef ARDUINO_ARCH_ESP32
    if (m_connected) {
        return true;  // Already connected
    }

    if (m_config.port == 0) {
        return false;  // Invalid port
    }

    // Check if WiFi is available
    if (WiFi.status() != WL_CONNECTED && !WiFi.softAPgetStationNum()) {
        return false;  // No WiFi connection
    }

    // Start UDP
    if (m_config.enableReceive) {
        // Join multicast group for receiving
        if (!m_udp.beginMulticast(m_config.multicastIP, m_config.port)) {
            return false;
        }
    } else if (m_config.enableTransmit) {
        // Begin UDP for transmitting
        if (!m_udp.begin(m_config.port)) {
            return false;
        }
    }

    m_connected = true;
    m_frameCounter = 0;
    m_lastFrameCounter = 0;

    return true;
#else
    return false;  // Not supported on non-ESP32
#endif
}

void AudioSync::end() {
#ifdef ARDUINO_ARCH_ESP32
    if (m_connected) {
        m_udp.stop();
        m_connected = false;
    }
#endif
}

void AudioSync::reset() {
    end();
    delay(100);
    begin();
}

bool AudioSync::transmit(
    float volumeRaw,
    float volumeSmth,
    bool samplePeak,
    const uint8_t* fftResult,
    uint16_t zeroCrossingCount,
    float fftMagnitude,
    float fftMajorPeak,
    float soundPressure
) {
#ifdef ARDUINO_ARCH_ESP32
    if (!m_connected || !m_config.enableTransmit) {
        return false;
    }

    // Prepare packet
    AudioPacket packet;
    memset(&packet, 0, sizeof(packet));

    // Header
    strncpy(packet.header, UDP_SYNC_HEADER_V2, 6);

    // Sound pressure as 16-bit fixed point
    uint32_t pressure16bit = max(0.0f, soundPressure) * 256.0f;
    uint16_t pressInt = pressure16bit / 256;
    uint16_t pressFract = pressure16bit % 256;
    if (pressInt > 255) pressInt = 255;
    packet.pressure[0] = (uint8_t)pressInt;
    packet.pressure[1] = (uint8_t)pressFract;

    // Sample data
    packet.sampleRaw = volumeRaw;
    packet.sampleSmth = volumeSmth;
    packet.samplePeak = samplePeak ? 1 : 0;
    packet.frameCounter = m_frameCounter++;

    // FFT data
    if (fftResult) {
        memcpy(packet.fftResult, fftResult, NUM_GEQ_CHANNELS);
    }
    packet.zeroCrossingCount = zeroCrossingCount;
    packet.fftMagnitude = fftMagnitude;
    packet.fftMajorPeak = fftMajorPeak;

    // Send multicast packet
    if (m_udp.beginMulticastPacket()) {
        m_udp.write((uint8_t*)&packet, sizeof(packet));
        return m_udp.endPacket();
    }

    return false;
#else
    return false;
#endif
}

bool AudioSync::receive(unsigned maxPackets) {
#ifdef ARDUINO_ARCH_ESP32
    if (!m_connected || !m_config.enableReceive) {
        return false;
    }

    bool gotNewData = false;
    unsigned packetsReceived = 0;

    // Read available packets
    while (packetsReceived < maxPackets || maxPackets == 255) {
        int packetSize = 0;

        #if __cpp_exceptions
        try {
            packetSize = m_udp.parsePacket();
        } catch (...) {
            m_udp.flush();
            break;
        }
        #else
        packetSize = m_udp.parsePacket();
        #endif

        // No more packets
        if (packetSize == 0) {
            break;
        }

        // Skip invalid packets
        if (packetSize < 5 || packetSize > 96) {
            m_udp.flush();
            continue;
        }

        // Read packet
        int bytesRead = m_udp.read(m_receiveBuffer, min(packetSize, 96));
        if (bytesRead != packetSize) {
            continue;
        }

        // Decode packet
        bool decoded = false;
        if (packetSize == sizeof(AudioPacket) && isValidHeaderV2((const char*)m_receiveBuffer)) {
            decoded = decodePacketV2(packetSize, m_receiveBuffer);
        } else if (packetSize == sizeof(AudioPacket_v1) && isValidHeaderV1((const char*)m_receiveBuffer)) {
            decoded = decodePacketV1(packetSize, m_receiveBuffer);
        }

        if (decoded) {
            gotNewData = true;
            m_receivedData.lastReceiveTime = millis();
        }

        packetsReceived++;
    }

    return gotNewData;
#else
    return false;
#endif
}

bool AudioSync::decodePacketV2(int packetSize, const uint8_t* buffer) {
    AudioPacket packet;
    memset(&packet, 0, sizeof(packet));
    memcpy(&packet, buffer, min((size_t)packetSize, sizeof(packet)));

    // Validate sequence if enabled
    if (m_config.sequenceCheck) {
        if (!checkSequence(packet.frameCounter)) {
            return false;  // Out of sequence
        }
    }

    // Update received data
    m_receivedData.volumeSmth = fmaxf(packet.sampleSmth, 0.0f);
    m_receivedData.volumeRaw = fmaxf(packet.sampleRaw, 0.0f);
    m_receivedData.samplePeak = (packet.samplePeak > 0);

    // FFT data
    memcpy(m_receivedData.fftResult, packet.fftResult, NUM_GEQ_CHANNELS);
    m_receivedData.fftMagnitude = fmaxf(packet.fftMagnitude, 0.0f);
    m_receivedData.fftMajorPeak = constrain(packet.fftMajorPeak, 1.0f, 11025.0f);
    m_receivedData.zeroCrossingCount = packet.zeroCrossingCount;

    // Sound pressure from fixed point
    if (packet.pressure[0] != 0 || packet.pressure[1] != 0) {
        m_receivedData.soundPressure = float(packet.pressure[1]) / 256.0f;
        m_receivedData.soundPressure += float(packet.pressure[0]);
    } else {
        m_receivedData.soundPressure = m_receivedData.volumeSmth;
    }

    m_receivedData.agcSensitivity = 128.0f;  // Substitute - V2 doesn't include this

    // Update format indicator
    if (packet.frameCounter > 0 && m_lastFrameCounter > 0) {
        m_receivedData.receivedFormat = 3;  // V2+
    } else {
        m_receivedData.receivedFormat = 2;  // V2
    }

    m_lastFrameCounter = packet.frameCounter;
    return true;
}

bool AudioSync::decodePacketV1(int packetSize, const uint8_t* buffer) {
    AudioPacket_v1 packet;
    memcpy(&packet, buffer, min((size_t)packetSize, sizeof(packet)));

    // Update received data
    m_receivedData.volumeSmth = fmaxf(packet.sampleAgc, 0.0f);
    m_receivedData.volumeRaw = m_receivedData.volumeSmth;  // V1 doesn't have raw
    m_receivedData.samplePeak = packet.samplePeak;

    // FFT data
    memcpy(m_receivedData.fftResult, packet.fftResult, NUM_GEQ_CHANNELS);
    m_receivedData.fftMagnitude = fmaxf(packet.fftMagnitude, 0.0f);
    m_receivedData.fftMajorPeak = constrain(packet.fftMajorPeak, 1.0, 11025.0);

    // V1 doesn't have these
    m_receivedData.zeroCrossingCount = 0;
    m_receivedData.soundPressure = m_receivedData.volumeSmth;
    m_receivedData.agcSensitivity = 128.0f;

    m_receivedData.receivedFormat = 1;  // V1

    return true;
}

bool AudioSync::checkSequence(uint8_t frameCounter) {
    // Always accept first packet or zero (legacy)
    if (m_lastFrameCounter == 0 || frameCounter == 0) {
        return true;
    }

    // Check for timeout - accept anything after timeout
    if (hasTimedOut(2500)) {
        return true;
    }

    // Check if this is newer than last (with 8-bit rollover)
    int8_t delta = (int8_t)(frameCounter - m_lastFrameCounter);
    return (delta > 0);
}

bool AudioSync::isValidHeaderV2(const char* header) const {
    return strncmp(header, UDP_SYNC_HEADER_V2, 5) == 0;
}

bool AudioSync::isValidHeaderV1(const char* header) const {
    return strncmp(header, UDP_SYNC_HEADER_V1, 5) == 0;
}

bool AudioSync::hasTimedOut(unsigned long timeoutMs) const {
    if (m_receivedData.lastReceiveTime == 0) {
        return true;
    }
    return (millis() - m_receivedData.lastReceiveTime) > timeoutMs;
}

