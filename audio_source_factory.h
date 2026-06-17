#pragma once

/*
 * audio_source_factory.h — AudioSource factory for the WLEDMM audio pipeline.
 *
 * Eliminates the need for every consumer (WLED AudioReactive, MoonLight
 * MoonModulesAudioDriver, etc.) to maintain its own copy of the big
 * switch(dmType) block.
 *
 * Usage
 * -----
 *   AudioSourceConfig cfg;
 *   cfg.dmType    = 1;           // Generic I2S
 *   cfg.sampleRate = 22050;
 *   cfg.blockSize  = 128;
 *   cfg.i2swsPin   = 15;
 *   cfg.i2ssdPin   = 32;
 *   cfg.i2sckPin   = 14;
 *   cfg.mclkPin    = I2S_PIN_NO_CHANGE;
 *   cfg.i2c_sda    = -1;
 *   cfg.i2c_scl    = -1;
 *   cfg.audioPin   = -1;
 *
 *   AudioSource* src = createAudioSource(cfg);          // allocator = nullptr
 *   // or, inside WLED:
 *   AudioSource* src = createAudioSource(cfg, &wledPinAllocator);
 *
 * dmType values
 * -------------
 *   0   Generic Analog (ADC over I2S — classic ESP32 only)
 *   1   Generic I2S
 *   2   ES7243  (I2S + I2C init)
 *   3   SPH0645 (I2S with timing fixup)
 *   4   Generic I2S with MCLK
 *   5   I2S PDM
 *   6   ES8388  (I2S + I2C init)
 *   7   WM8978  (I2S + I2C init)
 *   8   AC101   (I2S + I2C init)
 *   9   ES8311  (I2S + I2C init)
 *   10  Legacy PDM — MoonLight sequential alias for 51 (treated identically)
 *   51  Legacy PDM (WLED canonical value)
 *   254 / 255  Network-receive-only — returns nullptr (caller must handle)
 *
 * PDM auto-promotion
 * ------------------
 * If dmType is 1 or 4 AND i2sckPin is I2S_PIN_NO_CHANGE on a platform that
 * supports PDM, the factory promotes dmType to 51 (Legacy PDM) automatically.
 * Call normalizeDmType() first if you need to know the effective type.
 */

#include "audio_source.h"  // all AudioSource subclass definitions

/* ---------------------------------------------------------------------------
 * AudioSourceConfig — all parameters needed to create and initialise an
 * AudioSource.  Callers fill in the fields relevant to their dmType;
 * unused fields should be left at their default (I2S_PIN_NO_CHANGE / -1).
 * --------------------------------------------------------------------------*/
struct AudioSourceConfig {
    uint8_t  dmType     = 1;                   // driver type — see table above
    uint32_t sampleRate = 22050;
    uint16_t blockSize  = 128;
    int8_t   audioPin   = -1;                  // ADC analog pin (dmType 0 only)
    int8_t   i2ssdPin   = I2S_PIN_NO_CHANGE;   // I2S data
    int8_t   i2swsPin   = I2S_PIN_NO_CHANGE;   // I2S word select
    int8_t   i2sckPin   = I2S_PIN_NO_CHANGE;   // I2S bit clock (NO_CHANGE → PDM)
    int8_t   mclkPin    = I2S_PIN_NO_CHANGE;   // I2S master clock
    int8_t   i2c_sda    = -1;                  // I2C SDA for codec init
    int8_t   i2c_scl    = -1;                  // I2C SCL for codec init
};

/* ---------------------------------------------------------------------------
 * normalizeDmType() — apply the PDM auto-promotion rule in isolation.
 *
 * On platforms that support PDM: if dmType is 1 (Generic I2S) or 4
 * (I2S+MCLK) and i2sckPin is I2S_PIN_NO_CHANGE, the effective type is 51
 * (Legacy PDM).  createAudioSource() calls this internally, but exposing it
 * lets callers persist or log the resolved type.
 * --------------------------------------------------------------------------*/
inline uint8_t normalizeDmType(uint8_t dmType, int8_t i2sckPin) {
#if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
    if ((i2sckPin == I2S_PIN_NO_CHANGE) && ((dmType == 1) || (dmType == 4)))
        return 51;
#else
    (void)i2sckPin;
#endif
    return dmType;
}

/* ---------------------------------------------------------------------------
 * createAudioSource() — the canonical factory.
 *
 * Creates, initialises, and returns an AudioSource* for the given config.
 * Returns nullptr for dmType 254/255 (network-receive-only) — the caller is
 * responsible for any associated state changes (e.g. enabling UDP sync).
 *
 * allocator may be nullptr; all AudioSource pin-allocation helpers already
 * guard against a null allocator.
 * --------------------------------------------------------------------------*/
inline AudioSource* createAudioSource(const AudioSourceConfig& cfg,
                                      IPinAllocator* allocator = nullptr)
{
#ifndef ARDUINO_ARCH_ESP32
    (void)cfg; (void)allocator;
    return nullptr;
#else
    // Resolve effective type: MoonLight uses 10 for legacy PDM; WLED uses 51.
    // Also apply PDM auto-promotion for types 1 and 4 with no SCK pin.
    uint8_t eff = cfg.dmType;
    if (eff == 10) eff = 51;  // MoonLight compat alias
    eff = normalizeDmType(eff, cfg.i2sckPin);

    AudioSource* source = nullptr;

    switch (eff) {

        // ------------------------------------------------------------------ //
        // S2 / C3 / S3 — ADC analog and (on S2/C3) PDM are not supported.   //
        // Fall through to Generic I2S so the build succeeds on those MCUs.   //
        // ------------------------------------------------------------------ //
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32S3)
        case 0:   // ADC Analog — not available on S2/C3/S3
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32C3)
        case 5:   // PDM — not available on S2/C3
        case 51:  // Legacy PDM — not available on S2/C3
#endif
        // Generic I2S (INMP441, ICS43434, …)
        case 1:
            DEBUGSR_PRINT(F("AR: Generic I2S Microphone - ")); DEBUGSR_PRINTLN(F(I2S_MIC_CHANNEL_TEXT));
            source = new I2SSource(cfg.sampleRate, cfg.blockSize, 1.0f, true, allocator);
            delay(100);
            if (source) source->initialize(cfg.i2swsPin, cfg.i2ssdPin, cfg.i2sckPin);
            break;

        // ES7243 — I2C codec
        case 2:
            DEBUGSR_PRINTLN(F("AR: ES7243 Microphone (right channel only)."));
            source = new ES7243(cfg.sampleRate, cfg.blockSize, 1.0f, true, cfg.i2c_sda, cfg.i2c_scl, allocator);
            delay(100);
            if (source) source->initialize(cfg.i2swsPin, cfg.i2ssdPin, cfg.i2sckPin, cfg.mclkPin);
            break;

        // SPH0645
        case 3:
            DEBUGSR_PRINT(F("AR: SPH0645 Microphone - ")); DEBUGSR_PRINTLN(F(I2S_MIC_CHANNEL_TEXT));
            source = new SPH0654(cfg.sampleRate, cfg.blockSize, 1.0f, true, allocator);
            delay(100);
            if (source) source->initialize(cfg.i2swsPin, cfg.i2ssdPin, cfg.i2sckPin);
            break;

        // Generic I2S with MCLK
        case 4:
            DEBUGSR_PRINT(F("AR: Generic I2S Microphone with Master Clock - ")); DEBUGSR_PRINTLN(F(I2S_MIC_CHANNEL_TEXT));
            source = new I2SSource(cfg.sampleRate, cfg.blockSize, 1.0f / 24.0f, true, allocator);
            delay(100);
            if (source) source->initialize(cfg.i2swsPin, cfg.i2ssdPin, cfg.i2sckPin, cfg.mclkPin);
            break;

#if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)
        // I2S PDM
        case 5:
            DEBUGSR_PRINT(F("AR: I2S PDM Microphone - ")); DEBUGSR_PRINTLN(F(I2S_PDM_MIC_CHANNEL_TEXT));
            source = new I2SSource(cfg.sampleRate, cfg.blockSize, 1.0f / 4.0f, true, allocator);
            delay(100);
            if (source) source->initialize(cfg.i2swsPin, cfg.i2ssdPin);
            break;

        // Legacy PDM (also handles MoonLight alias 10, mapped to 51 above)
        case 51:
            DEBUGSR_PRINT(F("AR: Legacy PDM Microphone - ")); DEBUGSR_PRINTLN(F(I2S_PDM_MIC_CHANNEL_TEXT));
            source = new I2SSource(cfg.sampleRate, cfg.blockSize, 1.0f, true, allocator);
            delay(100);
            if (source) source->initialize(cfg.i2swsPin, cfg.i2ssdPin);
            break;
#endif

        // ES8388 — I2C codec
        case 6:
#ifdef use_es8388_mic
            DEBUGSR_PRINTLN(F("AR: ES8388 Source (Mic)"));
#else
            DEBUGSR_PRINTLN(F("AR: ES8388 Source (Line-In)"));
#endif
            source = new ES8388Source(cfg.sampleRate, cfg.blockSize, 1.0f, true, cfg.i2c_sda, cfg.i2c_scl, allocator);
            delay(100);
            if (source) source->initialize(cfg.i2swsPin, cfg.i2ssdPin, cfg.i2sckPin, cfg.mclkPin);
            break;

        // WM8978 — I2C codec
        case 7:
#ifdef use_wm8978_mic
            DEBUGSR_PRINTLN(F("AR: WM8978 Source (Mic)"));
#else
            DEBUGSR_PRINTLN(F("AR: WM8978 Source (Line-In)"));
#endif
            source = new WM8978Source(cfg.sampleRate, cfg.blockSize, 1.0f, true, cfg.i2c_sda, cfg.i2c_scl, allocator);
            delay(100);
            if (source) source->initialize(cfg.i2swsPin, cfg.i2ssdPin, cfg.i2sckPin, cfg.mclkPin);
            break;

        // AC101 — I2C codec
        case 8:
            DEBUGSR_PRINTLN(F("AR: AC101 Source (Line-In)"));
            source = new AC101Source(cfg.sampleRate, cfg.blockSize, 1.0f, true, cfg.i2c_sda, cfg.i2c_scl, allocator);
            delay(100);
            if (source) source->initialize(cfg.i2swsPin, cfg.i2ssdPin, cfg.i2sckPin, cfg.mclkPin);
            break;

        // ES8311 — I2C codec
        case 9:
            DEBUGSR_PRINTLN(F("AR: ES8311 Source (Mic)"));
            source = new ES8311Source(cfg.sampleRate, cfg.blockSize, 1.0f, true, cfg.i2c_sda, cfg.i2c_scl, allocator);
            delay(100);
            if (source) source->initialize(cfg.i2swsPin, cfg.i2ssdPin, cfg.i2sckPin, cfg.mclkPin);
            break;

        // Network-receive-only — no local audio source created.
        // Caller is responsible for setting disableSoundProcessing / audioSyncEnabled.
        case 254:
        case 255:
            return nullptr;

#if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
        // ADC over I2S (classic ESP32 only)
        case 0:
        default:
            DEBUGSR_PRINTLN(F("AR: Analog Microphone (left channel only)."));
            source = new I2SAdcSource(cfg.sampleRate, cfg.blockSize, 1.0f, allocator);
            delay(100);
            if (source) source->initialize(cfg.audioPin);
            break;
#endif
    }

    delay(250); // allow microphone hardware to settle
    return source;
#endif // ARDUINO_ARCH_ESP32
}
