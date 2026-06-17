# WLED AudioReactive Usermod - Refactored Architecture

## Overview

This repository contains a refactored version of the WLED AudioReactive usermod, split into reusable object-oriented components. The original monolithic `audio_reactive.h` file (3283 lines) has been separated into:

1. **Audio Processing Library** - Reusable, platform-independent audio processing components
2. **WLED Usermod Integration** - WLED-specific integration layer

## Architecture

### Audio Processing Library Components

#### 1. AudioFilters (`audio_filters.h/cpp`)

Provides audio filtering utilities:
- **DC Blocker**: High-pass filter to remove DC offset (~40Hz cutoff)
- **PDM Bandpass Filter**: IIR Butterworth 4th order bandpass (200Hz-8kHz) for PDM microphones
- **Configurable Modes**: None, DC blocker, or full PDM filtering

**Usage:**
```cpp
AudioFilters filters;
AudioFilters::Config config;
config.filterMode = 2;  // DC blocker
config.micQuality = 1;  // Good quality
filters.configure(config);

// Apply to sample buffer
filters.applyFilter(numSamples, sampleBuffer);
```

#### 2. AGCController (`agc_controller.h/cpp`)

Automatic Gain Control using PI (Proportional-Integral) controller:
- **Three Presets**: Normal, Vivid (fast), Lazy (slow)
- **Dual Setpoints**: ~60% and ~85% of maximum signal
- **Emergency Zones**: Fast response for very low (<10%) or high (>90%) signals
- **DC Level Tracking**: With freeze modes to prevent drift
- **Noise Gate**: Configurable squelch threshold
- **Sound Pressure Estimation**: Logarithmic scaling (5-105dB)

**Features:**
- PI controller with anti-windup
- Multiple smoothing modes based on mic quality
- Sensitivity calculation for UI display
- Separate raw and smoothed AGC outputs

**Usage:**
```cpp
AGCController agc;
AGCController::Config config;
config.preset = AGCController::NORMAL;
config.squelch = 10.0f;
config.sampleGain = 60.0f;
config.inputLevel = 128;
config.micQuality = 1;
agc.configure(config);
agc.setEnabled(true);

// Process sample
agc.processSample(micDataReal);

// Get results
float agcSample = agc.getSampleAGC();
float sensitivity = agc.getSensitivity();
float pressure = agc.estimatePressure(micDataReal, dmType);
```

#### 3. AudioProcessor (`audio_processor.h/cpp`) - *To be implemented*

Will handle:
- FFT processing (ArduinoFFT wrapper)
- Frequency band calculations (GEQ channels)
- Peak detection
- FreeRTOS task management

#### 4. AudioSourceManager (`audio_source_manager.h/cpp`) - *To be implemented*

Will provide:
- Factory for creating AudioSource instances
- Hardware initialization for various mic types:
  - Generic I2S
  - ES7243, ES8388, ES8311
  - SPH0645
  - WM8978, AC101
  - PDM microphones

### WLED Usermod Integration

#### AudioReactive Usermod (`audio_reactive.h/cpp`)

Handles WLED-specific integration:
- Inherits from `Usermod` base class
- UDP audio sync (transmit/receive)
- Configuration management (JSON)
- UI integration (`addToJsonInfo`, `addToConfig`)
- Lifecycle hooks (`setup`, `loop`, `connected`)
- Usermod data exchange for effects

## Benefits of Refactoring

### 1. Modularity
- Each component has a single, well-defined responsibility
- Clear interfaces between components
- Easier to understand and maintain

### 2. Testability
- Audio processing logic can be tested independently
- No dependency on WLED framework for core algorithms
- Mock audio sources for unit testing

### 3. Reusability
- Audio processing library can be used in other projects
- Not tied to WLED architecture
- Platform-independent where possible

### 4. Maintainability
- Clear separation between audio algorithms and WLED integration
- Easier to update AGC or FFT logic without touching WLED code
- Better documentation and code organization

### 5. Performance
- No runtime overhead - same memory footprint
- Preserves all ESP32-specific optimizations
- Maintains FreeRTOS task structure

## Current Status

### ✅ Completed
- [x] `AudioFilters` - DC blocker and bandpass filtering
- [x] `AGCController` - Complete AGC implementation
- [x] Documentation and refactoring plan

### 🔄 In Progress
- [ ] `AudioProcessor` - FFT and frequency band processing
- [ ] `AudioSourceManager` - Audio source factory and initialization
- [ ] Refactor `AudioReactive` usermod to use new components
- [ ] Testing and validation

### 📋 To Do
- [ ] Unit tests for each component
- [ ] Integration testing
- [ ] Performance benchmarking
- [ ] Update example configurations
- [ ] Migration guide for users

## Technical Details

### AGC Algorithm

The AGC uses a PI (Proportional-Integral) controller with several innovations:

1. **Dual Setpoints**: 
   - First setpoint at ~60% (AGC_TARGET0)
   - Second setpoint at ~85% (AGC_TARGET1)
   - Switches between them using bang-bang control

2. **Emergency Zones**:
   - Fast response when signal is <10% or >90%
   - Slow, stable response in normal zone (10-90%)

3. **Integrator Anti-Windup**:
   - Clamps integrator when gain is outside 0.085-6.5 range
   - Ceiling at >140% of maximum signal

4. **DC Level Tracking**:
   - Follows input DC level with exponential smoothing
   - Three freeze modes to prevent drift:
     - Mode 0: Floating (always tracks)
     - Mode 1: Freeze on sound, unfreeze after 4s silence
     - Mode 2: Fast freeze, unfreeze after 6s silence, no freeze first 12s

### Filter Design

**DC Blocker:**
- High-pass filter with R=0.990 (~40Hz cutoff at 18kHz sample rate)
- Minimal phase distortion
- Very low computational cost

**PDM Bandpass:**
- IIR Butterworth 4th order: 200Hz - 8kHz
- Optimized for PDM microphones (SPM1423)
- Additional FIR lowpass and IIR highpass for noise reduction
- Reduces noise floor from 5% to 0.05% on PDM mics

## Compatibility

- **ESP32**: Full support with all optimizations
- **ESP32-S2/C3**: Supported with appropriate defines
- **ESP32-S3**: Full support with fast path optimizations
- **ESP8266**: Limited support (no FFT task, receive-only mode)

## Configuration

### AGC Presets

| Preset | Decay | Response | Use Case |
|--------|-------|----------|----------|
| Normal | 0.9994 | Balanced | General use, music |
| Vivid  | 0.9985 | Fast     | Beat detection, reactive effects |
| Lazy   | 0.9997 | Slow     | Ambient, smooth transitions |

### Filter Modes

| Mode | Description | Use Case |
|------|-------------|----------|
| 0    | None        | High-quality line-in, pre-filtered input |
| 1    | PDM Bandpass | PDM microphones (SPM1423, etc.) |
| 2    | DC Blocker  | I2S digital mics, most analog mics |

## Example Integration

```cpp
// In WLED usermod setup()
#include "audio_filters.h"
#include "agc_controller.h"

AudioFilters filters;
AGCController agc;

void setup() {
    // Configure filters
    AudioFilters::Config filterConfig;
    filterConfig.filterMode = 2;  // DC blocker
    filters.configure(filterConfig);
    
    // Configure AGC
    AGCController::Config agcConfig;
    agcConfig.preset = AGCController::NORMAL;
    agcConfig.squelch = 10.0f;
    agcConfig.sampleGain = 60.0f;
    agc.configure(agcConfig);
    agc.setEnabled(true);
}

void loop() {
    // Get samples from audio source
    float samples[512];
    audioSource->getSamples(samples, 512);
    
    // Apply filtering
    filters.applyFilter(512, samples);
    
    // Process each sample through AGC
    for (int i = 0; i < 512; i++) {
        agc.processSample(samples[i]);
    }
    
    // Get results
    float volume = agc.getSampleAGC();
    float sensitivity = agc.getSensitivity();
}
```

## Contributing

When contributing to the audio processing library:
1. Keep components platform-independent where possible
2. Use `#ifdef ARDUINO_ARCH_ESP32` for ESP32-specific code
3. Document all public APIs with Doxygen comments
4. Add unit tests for new functionality
5. Preserve performance - this runs in real-time audio processing

## License

Licensed under the EUPL-1.2 or later

## Authors

Extracted and refactored from MoonModules WLED-MM audio_reactive.h
- Original authors: See [MoonModules/WLED-MM commits](https://github.com/MoonModules/WLED-MM/commits/mdev/)
- Refactoring: 2025

## References

- [WLED Project](https://github.com/Aircoookie/WLED)
- [MoonModules WLED-MM](https://github.com/MoonModules/WLED-MM)
- [ArduinoFFT Library](https://github.com/kosme/arduinoFFT)
- [DC Blocker Filter](https://www.dsprelated.com/freebooks/filters/DC_Blocker.html)
- [IIR Filter Design](https://www-users.cs.york.ac.uk/~fisher/cgi-bin/mkfscript)

