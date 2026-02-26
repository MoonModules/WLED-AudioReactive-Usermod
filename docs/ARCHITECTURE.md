# WLED AudioReactive Architecture Diagram

## Component Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    WLED AudioReactive Usermod                    │
│                      (audio_reactive.h/cpp)                      │
│                                                                   │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                   WLED Integration Layer                  │  │
│  │  • Usermod lifecycle (setup, loop, connected)            │  │
│  │  • Configuration (JSON, UI)                              │  │
│  │  • UDP Audio Sync (transmit/receive)                     │  │
│  │  • Effect data exchange (um_data)                        │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              ↓ ↑                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              Audio Processing Orchestration               │  │
│  │  • Coordinate components                                  │  │
│  │  • Manage audio pipeline                                  │  │
│  │  • Handle FreeRTOS task                                   │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              ↓ ↑
         ┌────────────────────┴────────────────────┐
         ↓                    ↓                     ↓
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│  AudioFilters   │  │  AGCController  │  │ AudioProcessor  │
│    ✅ DONE      │  │    ✅ DONE      │  │   🔄 TODO       │
├─────────────────┤  ├─────────────────┤  ├─────────────────┤
│ • DC Blocker    │  │ • PI Controller │  │ • FFT Engine    │
│ • Bandpass      │  │ • 3 Presets     │  │ • Peak Detect   │
│ • PDM Filter    │  │ • DC Tracking   │  │ • GEQ Bands     │
│ • Noise Reduce  │  │ • Noise Gate    │  │ • Scaling       │
└─────────────────┘  │ • Pressure Est  │  │ • Post-Process  │
                     └─────────────────┘  └─────────────────┘
                                                    ↓ ↑
                                          ┌─────────────────┐
                                          │AudioSourceMgr   │
                                          │   🔄 TODO       │
                                          ├─────────────────┤
                                          │ • Source Factory│
                                          │ • Hardware Init │
                                          │ • I2S Setup     │
                                          └─────────────────┘
                                                    ↓ ↑
                     ┌─────────────────────────────┴──────────────┐
                     ↓                ↓                ↓            ↓
            ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐
            │ I2S Source │  │ ES7243/ES8 │  │  PDM Mic   │  │ UDP Source │
            │            │  │  (Line-In) │  │  SPH0645   │  │  (Network) │
            └────────────┘  └────────────┘  └────────────┘  └────────────┘
```

## Data Flow

```
┌───────────────┐
│ Audio Source  │ (Microphone, Line-In, or UDP)
└───────┬───────┘
        │ Raw samples (int16_t or float)
        ↓
┌───────────────┐
│ AudioFilters  │ ✅ IMPLEMENTED
└───────┬───────┘
        │ Filtered samples
        │ • DC offset removed
        │ • Bandpass filtered (PDM)
        ↓
┌───────────────┐
│ AGCController │ ✅ IMPLEMENTED
└───────┬───────┘
        │ Gain-adjusted samples
        │ • Auto gain control
        │ • Noise gate applied
        │ • DC level tracked
        ↓
┌───────────────┐
│AudioProcessor │ 🔄 TO BE IMPLEMENTED
└───────┬───────┘
        │ FFT Results
        │ • Frequency bands (fftResult[16])
        │ • Major peak frequency
        │ • Magnitude
        │ • Sample peak flag
        ↓
┌───────────────────────┐
│  WLED Effects / UDP   │
└───────────────────────┘
```

## Thread Architecture (ESP32)

```
┌─────────────────────────────────────────────────────────┐
│                        Core 0                           │
│                                                          │
│  ┌────────────────────────────────────────────────┐   │
│  │           FFT Task (FreeRTOS)                   │   │
│  │  Priority: 1-4 (configurable)                   │   │
│  │  Stack: 3592 words                              │   │
│  │                                                  │   │
│  │  Loop:                                          │   │
│  │    1. Read samples from AudioSource             │   │
│  │    2. Apply AudioFilters                        │   │
│  │    3. Process with AGCController                │   │
│  │    4. Run FFT (AudioProcessor)                  │   │
│  │    5. Calculate GEQ bands                       │   │
│  │    6. Detect peaks                              │   │
│  │    7. Update shared results (thread-safe)       │   │
│  │    8. Sleep 15-25ms                             │   │
│  └────────────────────────────────────────────────┘   │
│                                                          │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│                        Core 1                           │
│                                                          │
│  ┌────────────────────────────────────────────────┐   │
│  │           WLED Loop Task                        │   │
│  │                                                  │   │
│  │  • LED effects (read FFT results)               │   │
│  │  • Web server                                    │   │
│  │  • UDP sync transmit/receive                     │   │
│  │  • Configuration updates                         │   │
│  └────────────────────────────────────────────────┘   │
│                                                          │
└─────────────────────────────────────────────────────────┘

        Shared Memory (Thread-Safe Access)
        ┌──────────────────────────────┐
        │ • fftResult[16]              │
        │ • FFT_MajorPeak              │
        │ • FFT_Magnitude              │
        │ • volumeSmth / volumeRaw     │
        │ • samplePeak flag            │
        │ • soundPressure              │
        └──────────────────────────────┘
```

## Configuration Flow

```
┌─────────────┐
│  User Input │ (Web UI or JSON API)
└──────┬──────┘
       │
       ↓
┌─────────────────────────┐
│ AudioReactive::         │
│   readFromConfig()      │
└──────┬──────────────────┘
       │
       ├─────────────────────────────┐
       │                             │
       ↓                             ↓
┌──────────────────┐        ┌──────────────────┐
│ AudioFilters::   │        │ AGCController::  │
│   configure()    │        │   configure()    │
└──────────────────┘        └──────────────────┘
       │                             │
       │   ┌─────────────────────────┘
       │   │
       ↓   ↓
┌──────────────────┐
│ AudioProcessor:: │ (TODO)
│   configure()    │
└──────────────────┘
```

## Memory Layout

```
┌─────────────────────────────────────────────────────┐
│                    Heap Memory                       │
├─────────────────────────────────────────────────────┤
│                                                      │
│  FFT Buffers (allocated on first run)               │
│  ├── vReal[512]          float  2048 bytes           │
│  ├── vImag[512]          float  2048 bytes           │
│  └── pinkFactors[512]    float  2048 bytes (opt)     │
│                                                      │
│  Sliding Window (if enabled)                         │
│  └── oldSamples[256]     float  1024 bytes           │
│                                                      │
│  Window Weights (cached)                             │
│  └── windowWeights[512]  float  2048 bytes           │
│                                                      │
├─────────────────────────────────────────────────────┤
│                 ~8-10 KB total                       │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│                    Stack Memory                      │
├─────────────────────────────────────────────────────┤
│  FFT Task Stack: 3592 words (14368 bytes)           │
│  Main Loop Stack: Default                            │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│                   Global/Static                      │
├─────────────────────────────────────────────────────┤
│  Configuration variables                             │
│  Preset tables (const, in flash)                     │
│  AudioFilters state       <100 bytes                 │
│  AGCController state      <200 bytes                 │
│  AudioProcessor state     <300 bytes (estimated)     │
└─────────────────────────────────────────────────────┘
```

## Class Hierarchy

```
Usermod (WLED base class)
  ↑
  │ inherits
  │
AudioReactive
  │
  ├── has-a ──→ AudioFilters ✅
  │
  ├── has-a ──→ AGCController ✅
  │
  ├── has-a ──→ AudioProcessor 🔄
  │              │
  │              └── uses ──→ ArduinoFFT (external lib)
  │
  ├── has-a ──→ AudioSourceManager 🔄
  │              │
  │              └── creates ──→ AudioSource* (interface)
  │                               ↑
  │                               │ implements
  │              ┌────────────────┴──────────────────┐
  │              │         │         │                │
  │         I2SSource  ES7243  SPH0654  ... (many types)
  │
  └── uses ──→ WiFiUDP (for audio sync)
```

## Compilation Dependencies

```
audio_filters.cpp
  └── Arduino.h

agc_controller.cpp
  └── Arduino.h

audio_processor.cpp (TODO)
  ├── Arduino.h
  ├── arduinoFFT.h (external)
  └── audio_filters.h
  └── agc_controller.h

audio_reactive.cpp
  ├── wled.h
  ├── audio_reactive.h
  ├── audio_filters.h
  ├── agc_controller.h
  ├── audio_processor.h (TODO)
  └── audio_source.h

Platform-specific:
  #ifdef ARDUINO_ARCH_ESP32
    ├── driver/i2s.h
    ├── driver/adc.h
    └── esp_timer.h
```

## Status Legend

- ✅ DONE: Fully implemented and tested
- 🔄 TODO: Planned but not yet implemented
- 📝 REFACTOR: Needs refactoring to use new components
- ⚠️ CAUTION: Complex integration required

## Next Implementation Priority

1. **AudioProcessor** (High) 🔄
   - Most complex component
   - Core FFT functionality
   - Required for full pipeline

2. **AudioSourceManager** (Medium) 🔄
   - Hardware abstraction
   - Simplifies source creation
   - Cleaner initialization

3. **AudioReactive Refactor** (High) 📝
   - Integrate all components
   - Update to use new APIs
   - Maintain compatibility

4. **Testing** (Critical) ⚠️
   - Unit tests
   - Integration tests
   - Performance validation

