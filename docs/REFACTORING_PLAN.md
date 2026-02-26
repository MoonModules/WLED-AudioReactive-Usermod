# Audio Reactive Usermod Refactoring Plan

## Overview
This document outlines the refactoring of `audio_reactive.h` (3283 lines) into a modular, object-oriented architecture that separates audio processing logic from WLED usermod integration.

## File Structure

### New Audio Processing Library Files

1. **audio_filters.h / audio_filters.cpp** ✅ CREATED
   - DC blocker filter
   - PDM bandpass filter
   - Sample smoothing utilities
   - Class: `AudioFilters`

2. **agc_controller.h / agc_controller.cpp** ✅ CREATED
   - AGC PI controller with 3 presets (normal, vivid, lazy)
   - Automatic gain control logic
   - Sample level management
   - Class: `AGCController`

3. **audio_processor.h / audio_processor.cpp** ✅ CREATED
   - FFT processing (wraps ArduinoFFT)
   - Frequency band calculations (GEQ channels)
   - Peak detection
   - Sample processing pipeline
   - Class: `AudioProcessor`
   - Manages FreeRTOS task for ESP32

4. **audio_source_manager.h / audio_source_manager.cpp**
   - Factory for creating AudioSource instances
   - Hardware abstraction for different mic types
   - Builds on existing audio_source.h
   - Class: `AudioSourceManager`

### Modified Files

5. **audio_reactive.h** (refactored)
   - WLED Usermod class only
   - UDP audio sync
   - Configuration management
   - UI integration
   - Lifecycle hooks (setup, loop, connected, etc.)
   - Usermod data exchange

6. **audio_reactive.cpp**
   - Usermod registration
   - Static initialization

## Code Organization

### What Goes Into Audio Processing Library

**AudioFilters** ✅
- DC blocker (line ~477-494)
- Bandpass filter for PDM (line ~963-973)
- Filter state management

**AGCController**
- AGC presets and constants (line 234-253)
- agcAvg() function (line 1353-1447)
- getSample() function (line 1449-1550)
- getSensitivity() function (line 1553-1568)
- estimatePressure() function (line 1574-1601)
- State variables: multAgc, sampleAgc, rawSampleAgc, sampleMax, micLev, control_integrated

**AudioProcessor**
- FFT buffer allocation (line 453-476)
- FFTcode() main task (line 497-962)
- fftAddAvg() helpers (line 417-442)
- detectSamplePeak() (line 1084-1097)
- postProcessFFTResults() (line 974-1081)
- FFT result arrays: vReal, vImag, fftResult, fftCalc, fftAvg
- Peak tracking: FFT_MajorPeak, FFT_Magnitude, samplePeak

**AudioSourceManager**
- Audio source creation logic (line 1986-2122)
- I2S configuration
- Hardware initialization

### What Stays In WLED Usermod

- Usermod class inheritance (line 1115)
- setup() override (line 1921-2145)
- loop() override (line 2255-2350)
- connected() / onStateChange() (line 2351-2388)
- readFromConfig() / addToConfig() (line 2659-2968)
- addToJsonInfo() / addToJsonState() (line 2390-2534)
- UDP sync: transmitAudioData(), receiveAudioData(), decodeAudioData() (line 1703-1918)
- Configuration variables: enabled, dmType, audioSyncPort, etc.
- WLED-specific integration: um_data exchange, LED strip interaction

## Interface Design

### AudioProcessor Public API
```cpp
class AudioProcessor {
public:
    struct Config {
        uint16_t sampleRate;
        uint16_t fftSize;
        uint8_t numGEQChannels;
        uint8_t scalingMode;
        uint8_t pinkIndex;
        bool useSlidingWindow;
        // ... other config
    };
    
    void configure(const Config& config);
    void processSamples(float* samples, size_t count);
    
    // Getters for results
    const uint8_t* getFFTResult() const;
    float getMajorPeak() const;
    float getMagnitude() const;
    bool getSamplePeak() const;
    float getVolumeSmooth() const;
    float getVolumeRaw() const;
    
    // Task management
    void startTask();
    void stopTask();
};
```

### AGCController Public API
```cpp
class AGCController {
public:
    enum Preset { NORMAL = 0, VIVID = 1, LAZY = 2 };
    
    struct Config {
        Preset preset;
        float squelch;
        float sampleGain;
        uint8_t inputLevel;
        uint8_t micQuality;
        uint8_t micLevelMethod;
    };
    
    void configure(const Config& config);
    void processAGC(float rawSample, unsigned long timestamp);
    void processSample(float micDataReal);
    
    float getSampleAGC() const;
    float getRawSampleAGC() const;
    int16_t getSampleRaw() const;
    float getMultiplier() const;
    float getSensitivity() const;
    float estimatePressure(float micDataReal, uint8_t dmType) const;
};
```

### AudioFilters Public API ✅
```cpp
class AudioFilters {
public:
    struct Config {
        uint8_t filterMode;  // 0=none, 1=PDM, 2=DC blocker
        uint8_t micQuality;
    };
    
    void configure(const Config& config);
    void applyFilter(uint16_t numSamples, float* buffer);
    void reset();
};
```

## Migration Strategy

1. ✅ Create AudioFilters - DONE
2. ✅ Create AGCController - DONE
3. ✅ Create AudioProcessor skeleton - DONE
4. 🔄 Extract FFT code into AudioProcessor - IN PROGRESS
5. 🔄 Create AudioSourceManager - NEXT
6. 🔄 Refactor AudioReactive usermod to use new components
7. Test compilation
8. Verify functionality
9. Update documentation

## Benefits

- **Modularity**: Each component has single responsibility
- **Testability**: Audio processing can be tested independently
- **Reusability**: Library can be used in other projects
- **Maintainability**: Clear separation of concerns
- **Performance**: No runtime overhead, same memory footprint

## Notes

- Maintain ESP32-specific optimizations (#ifdef ARDUINO_ARCH_ESP32)
- Keep backward compatibility with effects via um_data
- Preserve all tuning parameters and presets
- Ensure thread-safety for FreeRTOS task
- No changes to AudioSource hierarchy (audio_source.h)

