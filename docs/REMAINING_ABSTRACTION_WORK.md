# Remaining Abstraction Work

## Goal
Extract **WLED-independent audio processing code** from `audio_reactive.orig` into library components, while keeping the **WLED Usermod wrapper** for integration.

---

## ✅ COMPLETED (All Audio Processing - 100%)

### AudioProcessor
- ✅ fftTask() - Full FFT processing (237 lines)
- ✅ computeFrequencyBands() - Bin-to-GEQ mapping (67 lines)
- ✅ postProcessFFT() - Post-processing pipeline (104 lines)
- ✅ fftAddAvg() - FFT bin averaging (19 lines)
- ✅ detectPeak() - Sample peak detection (16 lines)
- ✅ autoResetPeak() - Automatic peak reset (5 lines)
- ✅ limitSampleDynamics() - Volume dynamics limiting (27 lines)
- ✅ limitGEQDynamics() - GEQ dynamics limiting (28 lines)

### AGCController
- ✅ agcAvg() - PI controller for AGC (120 lines)
- ✅ processSample() (getSample) - Sample processing and volume tracking (100 lines)
- ✅ getSensitivity() - Sensitivity calculation (17 lines)
- ✅ estimatePressure() - Sound pressure estimation (38 lines)
- ✅ AGC presets and state management
- ✅ Configuration and initialization

### AudioFilters
- ✅ runMicFilter() - PDM bandpass filter
- ✅ runDCBlocker() - DC blocker
- ✅ Filter configuration

---

## ✅ ALL AUDIO PROCESSING CODE MIGRATED

**All WLED-independent audio processing functions have been successfully abstracted into library components!**

### 1. **getSample()** → Move to AGCController
**Location**: Lines 1472-1571 in audio_reactive.orig (~100 lines)
**Purpose**: Post-FFT sample processing and volume tracking

**Functionality**:
- Mic level (DC offset) tracking with "freeze" modes
- Exponential filtering for smooth volume
- Noise gate implementation
- Dynamic gain adjustment
- Peak detection integration
- Sample averaging with quality modes
- Sound pressure estimation hook

**WLED Dependencies**: None - Pure audio processing
**Variables Used**:
- `micLev`, `expAdjF`, `sampleReal`, `sampleRaw`, `sampleAdj`
- `sampleMax`, `sampleAvg`, `samplePeak`
- `isFrozen`, `haveSilence`, `lastSoundTime`, `startuptime`

**Recommendation**: Add to AGCController as `processSample()`
```cpp
class AGCController {
    void processSample(float micDataReal, unsigned long timestamp);
    float getSampleRaw() const;
    float getSampleAvg() const;
    float getSampleReal() const;
    // ... getters for other outputs
};
```

---

### 2. **getSensitivity()** → Move to AGCController
**Location**: Lines 1574-1590 in audio_reactive.orig (~17 lines)
**Purpose**: Calculate current sensitivity based on AGC gain

**Functionality**:
- Converts AGC multiplier to 0-255 sensitivity value
- Scales logarithmically for better UI representation
- Handles AGC on/off modes

**WLED Dependencies**: None
**Recommendation**: Add to AGCController as `getSensitivity()`
```cpp
class AGCController {
    float getSensitivity() const;
};
```

---

### 3. **estimatePressure()** → Move to AGCController  
**Location**: Lines 1595-1632 in audio_reactive.orig (~38 lines)
**Purpose**: Estimate sound pressure level (dB) from sample values

**Functionality**:
- Maps sample values to 0-255 representing ~5dB to ~105dB
- Uses logarithmic scaling
- Corrects for different mic types (ADC, PDM, Line-In)

**WLED Dependencies**: Uses `dmType` - **Needs parameter**
**Recommendation**: Add to AGCController with dmType parameter
```cpp
class AGCController {
    float estimatePressure(float micSample, uint8_t dmType) const;
};
```

---

### 4. **limitSampleDynamics()** → Move to AudioProcessor
**Location**: Lines 1637-1663 in audio_reactive.orig (~27 lines)
**Purpose**: Limit dynamics of volumeSmth (attack/decay envelope)

**Functionality**:
- Configurable attack time
- Configurable decay time
- Time-based rate limiting
- Prevents extreme jumps in volume

**WLED Dependencies**: None - Pure DSP
**Used By**: Multiple effects (Gravimeter, Plasmoid, Freqpixels, etc.)

**Recommendation**: Add to AudioProcessor
```cpp
class AudioProcessor {
    struct DynamicsConfig {
        bool enabled = true;
        uint16_t attackTime = 50;   // ms
        uint16_t decayTime = 2000;  // ms
    };
    
    void limitDynamics(float& volumeSmth);
    void configureDynamics(const DynamicsConfig& config);
};
```

---

### 5. **limitGEQDynamics()** → Move to AudioProcessor
**Location**: Lines 1666-1693 in audio_reactive.orig (~28 lines)
**Purpose**: Smooth GEQ channel transitions (UDP receiver mode)

**Functionality**:
- Limits rate of change for all 16 GEQ channels
- Attack/decay envelope per channel
- Reduces flickering in UDP sync mode

**WLED Dependencies**: None - Pure DSP
**Recommendation**: Add to AudioProcessor
```cpp
class AudioProcessor {
    void limitGEQDynamics(bool gotNewSample);
};
```

---

## 🚫 STAYS IN WLED USERMOD (WLED-Specific)

### UDP Audio Sync Functions
- `connectUDPSoundSync()` - WiFi/UDP management
- `transmitAudioData()` - Send over network
- `receiveAudioData()` - Receive from network
- `decodeAudioData()` - Parse packets
- `decodeAudioData_v1()` - Legacy support

### WLED Integration
- `setup()` - Usermod lifecycle
- `loop()` - Main processing loop
- `connected()` / `onStateChange()` - WiFi events
- `addToConfig()` / `readFromConfig()` - Settings
- `addToJsonInfo()` / `addToJsonState()` - UI/API

### WLED-Specific Variables
- `enabled`, `dmType`, `audioSyncPort`
- `i2ssdPin`, `i2swsPin`, `i2sckPin` (hardware pins)
- `udpSyncConnected`, `fftUdp`
- `um_data` (usermod data exchange)

---

## Summary Table

| Function | Lines | Target Component | Complexity | Status |
|----------|-------|------------------|------------|--------|
| fftTask() | 237 | AudioProcessor | ⚫⚫⚫⚫⚫ | ✅ Done |
| computeFrequencyBands() | 67 | AudioProcessor | ⚫⚫⚫⚪⚪ | ✅ Done |
| postProcessFFT() | 104 | AudioProcessor | ⚫⚫⚫⚫⚪ | ✅ Done |
| agcAvg() | 120 | AGCController | ⚫⚫⚫⚫⚪ | ✅ Done |
| runMicFilter() | 15 | AudioFilters | ⚫⚫⚫⚪⚪ | ✅ Done |
| runDCBlocker() | 20 | AudioFilters | ⚫⚫⚫⚪⚪ | ✅ Done |
| **getSample()** | **100** | **AGCController** | **⚫⚫⚫⚫⚪** | **✅ Done** |
| **getSensitivity()** | **17** | **AGCController** | **⚫⚫⚪⚪⚪** | **✅ Done** |
| **estimatePressure()** | **38** | **AGCController** | **⚫⚫⚫⚪⚪** | **✅ Done** |
| **limitSampleDynamics()** | **27** | **AudioProcessor** | **⚫⚫⚫⚪⚪** | **✅ Done** |
| **limitGEQDynamics()** | **28** | **AudioProcessor** | **⚫⚫⚫⚪⚪** | **✅ Done** |

---

## Progress

### Abstracted: ~920 lines (100% of audio processing) ✅
- FFT core
- AGC controller (including sample processing)
- Audio filters
- Frequency band mapping
- Post-processing
- Dynamics limiting
- Volume tracking
- All helper functions

### WLED Integration: ~1500 lines (stays in usermod)
- UDP sync
- Settings/config
- Usermod lifecycle
- Hardware initialization

---

## Implementation Complete ✅

### ALL FUNCTIONS MIGRATED
1. ✅ **getSample()** → AGCController
2. ✅ **limitSampleDynamics()** → AudioProcessor
3. ✅ **getSensitivity()** → AGCController
4. ✅ **estimatePressure()** → AGCController
5. ✅ **limitGEQDynamics()** → AudioProcessor

---

## Next Steps

1. ✅ Core DSP abstraction - **COMPLETE**
2. ✅ **Migrate getSample() to AGCController** - **COMPLETE**
3. ✅ Migrate dynamics limiting to AudioProcessor - **COMPLETE**
4. ✅ Migrate helper functions to AGCController - **COMPLETE**
5. ⏳ Update WLED Usermod to use library components - **NEXT**
6. ⏳ Test integration
7. ⏳ Update documentation

---

## Bottom Line

**ALL audio processing code is abstracted** ✅ (100% complete)

**What's Done:**
- All FFT processing and DSP algorithms
- AGC with sample processing and volume tracking
- Audio filtering (bandpass, DC blocker)
- Dynamics/envelope processing
- All helper/utility functions
- **~920 lines of pure audio processing code**

**What Remains:**
The **WLED Usermod wrapper** needs to be updated to:
- Use AudioProcessor for FFT results
- Use AGCController for volume tracking
- Use AudioFilters for preprocessing
- Keep UDP sync (WLED-specific)
- Keep settings/config (WLED-specific)

The refactoring is essentially **COMPLETE** - all WLED-independent audio processing is now in standalone library components!

---
Generated: February 26, 2026

