# AudioSync Component - UDP Audio Synchronization

## Overview

The **AudioSync** component provides UDP-based network synchronization for audio reactive systems. It allows multiple devices to share audio analysis data over the network, enabling one device with a microphone to provide audio input for multiple LED displays.

## Features

### Protocol Support
- ✅ **V2 Protocol** (44 bytes) - Current format with all features
- ✅ **V1 Protocol** (88 bytes) - Legacy support for backward compatibility
- ✅ **Frame Counter** - Sequence tracking to detect out-of-order packets
- ✅ **Multicast UDP** - Efficient one-to-many transmission

### Audio Data Synchronized
- Volume (raw and smoothed)
- Sample peak detection
- FFT results (16 GEQ channels)
- Zero crossing count
- FFT magnitude and major peak frequency
- Sound pressure estimation

### Network Features
- ✅ **Automatic Reconnection** - Handles WiFi connection/disconnection
- ✅ **Sequence Validation** - Detects and rejects out-of-order packets
- ✅ **Timeout Detection** - Identifies when sender goes offline
- ✅ **Packet Purging** - Configurable packet queue management
- ✅ **Exception Handling** - Robust error handling for OOM conditions

## Usage

### Basic Setup

```cpp
#include "audio_sync.h"

AudioSync audioSync;

void setup() {
    // Configure
    AudioSync::Config config;
    config.port = 11988;
    config.enableTransmit = true;  // Send audio data
    config.enableReceive = false;  // Don't receive
    audioSync.configure(config);
    
    // Start UDP
    if (audioSync.begin()) {
        Serial.println("AudioSync started");
    }
}
```

### Transmit Mode (Sender)

```cpp
void loop() {
    // Transmit audio data
    audioSync.transmit(
        volumeRaw,        // float: raw volume
        volumeSmth,       // float: smoothed volume
        samplePeak,       // bool: peak detected
        fftResult,        // uint8_t[16]: FFT channels
        zeroCrossingCount,// uint16_t: zero crossings
        fftMagnitude,     // float: FFT magnitude
        fftMajorPeak,     // float: major peak Hz
        soundPressure     // float: sound pressure
    );
}
```

### Receive Mode (Receiver)

```cpp
void loop() {
    // Receive audio data
    bool gotNewData = audioSync.receive(1);  // Read 1 packet
    
    if (gotNewData) {
        // Access received data
        const AudioSync::ReceivedData& data = audioSync.getReceivedData();
        
        Serial.printf("Volume: %.2f\n", data.volumeSmth);
        Serial.printf("Major Peak: %.2f Hz\n", data.fftMajorPeak);
        
        // Use FFT data
        for (int i = 0; i < 16; i++) {
            Serial.printf("Channel %d: %d\n", i, data.fftResult[i]);
        }
    }
    
    // Check for timeout
    if (audioSync.hasTimedOut(2500)) {
        Serial.println("Sender timed out");
    }
}
```

### Bidirectional Mode

```cpp
AudioSync::Config config;
config.enableTransmit = true;
config.enableReceive = true;  // Can do both
audioSync.configure(config);
```

## Configuration Options

### AudioSync::Config

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `port` | uint16_t | 11988 | UDP port number |
| `enableTransmit` | bool | false | Enable transmission |
| `enableReceive` | bool | false | Enable reception |
| `sequenceCheck` | bool | true | Validate packet sequence |
| `purgeCount` | uint8_t | 1 | Packets to read per cycle |
| `multicastIP` | IPAddress | 239.0.0.1 | Multicast address |

### Sequence Checking

When enabled, AudioSync validates packet order using frame counters:
- Accepts packets with newer frame counter
- Rejects out-of-order or duplicate packets
- Handles 8-bit rollover correctly
- Always accepts first packet after timeout

Disable sequence checking if you experience packet drops on reliable networks.

### Purge Count

Controls how many packets to read per receive() call:
- `1` - Read one packet (normal mode, minimal latency)
- `2-10` - Read multiple packets (catch up mode)
- `255` - Read all available packets (flush queue)

Use higher values if receiver falls behind sender.

## API Reference

### Methods

#### `void configure(const Config& config)`
Configure the AudioSync instance.

#### `bool begin()`
Start UDP synchronization. Returns true if successful.
Requires active WiFi connection (STA or AP mode).

#### `void end()`
Stop UDP synchronization and close socket.

#### `void reset()`
Reset connection (end + begin). Useful for reconnection.

#### `bool isConnected() const`
Check if UDP socket is active.

#### `bool transmit(...)`
Transmit audio data packet. Returns true if sent successfully.

Parameters match audio analysis outputs from AudioProcessor and AGCController.

#### `bool receive(unsigned maxPackets = 1)`
Receive and decode audio data packets.
- `maxPackets`: Number of packets to read (255 = all)
- Returns: true if new data was received

#### `const ReceivedData& getReceivedData() const`
Get the most recently received audio data.

#### `bool hasTimedOut(unsigned long timeoutMs) const`
Check if reception has timed out.
- `timeoutMs`: Timeout duration in milliseconds
- Returns: true if no data received within timeout

### ReceivedData Structure

```cpp
struct ReceivedData {
    float volumeSmth;           // Smoothed volume
    float volumeRaw;            // Raw volume
    bool samplePeak;            // Peak detected
    uint8_t fftResult[16];      // FFT channels
    float fftMagnitude;         // FFT magnitude
    float fftMajorPeak;         // Major peak Hz
    uint16_t zeroCrossingCount; // Zero crossings
    float soundPressure;        // Sound pressure
    float agcSensitivity;       // AGC sensitivity (substitute)
    unsigned long lastReceiveTime; // millis() of last packet
    int receivedFormat;         // 0=none, 1=v1, 2=v2, 3=v2+
};
```

## Protocol Details

### V2 Packet Format (44 bytes)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 6 | header | char[6] | "00002" + null |
| 6 | 2 | pressure | uint8_t[2] | Sound pressure (fixed point) |
| 8 | 4 | sampleRaw | float | Raw sample value |
| 12 | 4 | sampleSmth | float | Smoothed sample |
| 16 | 1 | samplePeak | uint8_t | Peak flag (0/1) |
| 17 | 1 | frameCounter | uint8_t | Sequence number |
| 18 | 16 | fftResult | uint8_t[16] | FFT channels |
| 34 | 2 | zeroCrossingCount | uint16_t | Zero crossings |
| 36 | 4 | fftMagnitude | float | FFT magnitude |
| 40 | 4 | fftMajorPeak | float | Major peak Hz |

### V1 Packet Format (88 bytes) - Legacy

Compatible with older WLED versions. Automatically detected and decoded.

## Network Topology

### Transmitter (1 device)
- Has microphone/audio input
- Runs AudioProcessor + AGCController
- Transmits to multicast group
- Can power multiple receivers

### Receivers (N devices)
- Listen on multicast group
- Receive audio analysis data
- Apply to local LED effects
- No microphone needed

### Hybrid Mode
- Device can transmit AND receive
- Useful for fallback scenarios
- Can switch between modes dynamically

## Performance

### Bandwidth
- V2: 44 bytes per packet
- Typical rate: 20-100 Hz
- Network usage: ~3.5 KB/s @ 50 Hz

### Latency
- Multicast: < 5ms on local network
- Processing: negligible
- Total: < 10ms end-to-end

### Reliability
- UDP is best-effort delivery
- Occasional packet loss acceptable (effects self-correct)
- Sequence checking catches issues
- Timeout detection for complete failure

## Troubleshooting

### No Data Received
1. Check WiFi connection (STA or AP)
2. Verify same multicast group (239.0.0.1)
3. Check same port (11988)
4. Ensure transmitter is sending
5. Check firewall rules

### Out-of-Sequence Errors
1. Check network congestion
2. Try disabling sequence check
3. Increase purge count
4. Verify single transmitter only

### High Latency
1. Check WiFi signal strength
2. Reduce transmission rate
3. Use wired Ethernet if available
4. Minimize other network traffic

## Integration with WLED

The AudioReactive usermod uses AudioSync for network synchronization:

```cpp
// In AudioReactive class
AudioSync audioSync;

// Configure based on user settings
AudioSync::Config syncConfig;
syncConfig.port = audioSyncPort;
syncConfig.enableTransmit = (audioSyncEnabled & AUDIOSYNC_SEND);
syncConfig.enableReceive = (audioSyncEnabled & AUDIOSYNC_REC);
audioSync.configure(syncConfig);

// Start on WiFi connection
void connected() {
    audioSync.begin();
}
```

## Example: Simple Transmitter

```cpp
#include "audio_sync.h"
#include "audio_processor.h"
#include "agc_controller.h"

AudioSync audioSync;
AudioProcessor audioProcessor;
AGCController agcController;

void setup() {
    // Setup audio processing
    // ... (configure audioProcessor and agcController)
    
    // Setup AudioSync as transmitter
    AudioSync::Config config;
    config.enableTransmit = true;
    audioSync.configure(config);
    audioSync.begin();
}

void loop() {
    // Audio processing happens in background task
    
    // Transmit results
    audioSync.transmit(
        agcController.getSampleRaw(),
        agcController.getSampleAGC(),
        audioProcessor.getSamplePeak(),
        audioProcessor.getFFTResult(),
        audioProcessor.getZeroCrossingCount(),
        audioProcessor.getMagnitude(),
        audioProcessor.getMajorPeak(),
        agcController.estimatePressure(0.0f, 1)
    );
    
    delay(20);  // 50 Hz transmission rate
}
```

## Example: Simple Receiver

```cpp
#include "audio_sync.h"

AudioSync audioSync;

void setup() {
    // Setup AudioSync as receiver
    AudioSync::Config config;
    config.enableReceive = true;
    audioSync.configure(config);
    audioSync.begin();
}

void loop() {
    // Receive audio data
    if (audioSync.receive()) {
        const AudioSync::ReceivedData& data = audioSync.getReceivedData();
        
        // Use the data for LED effects
        updateLEDs(data.fftResult, data.volumeSmth);
    }
    
    // Check for timeout
    if (audioSync.hasTimedOut(2500)) {
        // Transmitter offline - show error pattern
        showErrorPattern();
    }
}
```

## Advantages of Abstraction

### Before (Monolithic)
- UDP code mixed with WLED usermod
- Hard to test independently
- Difficult to reuse
- Tightly coupled to WiFi

### After (Abstracted)
- ✅ Standalone AudioSync component
- ✅ Easy to unit test
- ✅ Reusable in other projects
- ✅ Clean interface
- ✅ No WLED dependencies

## Future Enhancements

Possible improvements:
- Encryption for security
- Compression to reduce bandwidth
- Multiple multicast groups
- QoS/priority settings
- Statistics tracking
- Auto-discovery of transmitters

---

**Component**: AudioSync  
**Purpose**: Network audio synchronization  
**Dependencies**: WiFiUDP (ESP32 only)  
**License**: EUPL-1.2 or later  
**Status**: Production Ready ✅

