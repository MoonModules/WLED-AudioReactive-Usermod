#pragma once

/*
   @title     MoonModules WLED - audioreactive usermod (REFACTORED)
   @file      audio_reactive.h
   @repo      https://github.com/MoonModules/WLED-MM
   @Authors   https://github.com/MoonModules/WLED-MM/commits/mdev/
   @Copyright © 2024,2025 Github MoonModules Commit Authors
   @license   Licensed under the EUPL-1.2 or later

   REFACTORED VERSION using object-oriented audio processing libraries:
   - AudioFilters: Signal preprocessing
   - AGCController: Automatic gain control
   - AudioProcessor: FFT and frequency analysis
*/

#include "wled.h"

// Fallback error codes absent from standard WLED (present in MoonModules/WLED-MM).
// Using unused values above ERR_UNDERVOLT (32); the UI will show a non-zero error flag.
#ifndef ERR_REBOOT_NEEDED
  #define ERR_REBOOT_NEEDED   254
#endif
#ifndef ERR_POWEROFF_NEEDED
  #define ERR_POWEROFF_NEEDED 253
#endif

// Debug macros — must be defined before #include "audio_source.h" which uses them.
// #define MIC_LOGGER
// #define FFT_SAMPLING_LOG
// #define SR_DEBUG

#ifdef SR_DEBUG
  #define DEBUGSR_PRINT(x) DEBUGOUT(x)
  #define DEBUGSR_PRINTLN(x) DEBUGOUTLN(x)
  #define DEBUGSR_PRINTF(x...) DEBUGOUTF(x)
#else
  #define DEBUGSR_PRINT(x)
  #define DEBUGSR_PRINTLN(x)
  #define DEBUGSR_PRINTF(x...)
#endif

#if defined(SR_DEBUG)
#define ERRORSR_PRINT(x) DEBUGSR_PRINT(x)
#define ERRORSR_PRINTLN(x) DEBUGSR_PRINTLN(x)
#define ERRORSR_PRINTF(x...) DEBUGSR_PRINTF(x)
#else
#if defined(WLED_DEBUG)
#define ERRORSR_PRINT(x) DEBUG_PRINT(x)
#define ERRORSR_PRINTLN(x) DEBUG_PRINTLN(x)
#define ERRORSR_PRINTF(x...) DEBUG_PRINTF(x)
#else
  #define ERRORSR_PRINT(x)
  #define ERRORSR_PRINTLN(x)
  #define ERRORSR_PRINTF(x...)
#endif
#endif

#ifndef USER_PRINT
  #define USER_PRINT(x) DEBUGSR_PRINT(x)
  #define USER_PRINTLN(x) DEBUGSR_PRINTLN(x)
  #define USER_PRINTF(x...) DEBUGSR_PRINTF(x)
  #define USER_FLUSH()
 
  #define oappendi oappend
#endif

#if defined(MIC_LOGGER) || defined(FFT_SAMPLING_LOG)
  #define PLOT_PRINT(x) DEBUGOUT(x)
  #define PLOT_PRINTLN(x) DEBUGOUTLN(x)
  #define PLOT_PRINTF(x...) DEBUGOUTF(x)
  #define PLOT_FLUSH() DEBUGOUTFlush()
#else
  #define PLOT_PRINT(x)
  #define PLOT_PRINTLN(x)
  #define PLOT_PRINTF(x...)
  #define PLOT_FLUSH()
#endif

// Include our object-oriented audio processing libraries
#include "audio_filters.h"
#include "agc_controller.h"
#include "audio_source_factory.h"  // AudioSourceConfig, createAudioSource(), normalizeDmType()
                                   // also transitively includes audio_source.h and audio_processor.h
#include "audio_sync.h"

/* ---------------------------------------------------------------------------
 * WLEDPinAllocator — concrete IPinAllocator backed by WLED's PinManager.
 *
 * This is the only place in the OO library that touches PinManager /
 * PinOwner directly; audio_source.h is kept free of those symbols.
 * ---------------------------------------------------------------------------*/
class WLEDPinAllocator : public IPinAllocator {
public:
    bool allocatePin(int8_t pin, bool output) override {
        return PinManager::allocatePin(pin, output, PinOwner::UM_Audioreactive);
    }
    void deallocatePin(int8_t pin) override {
        PinManager::deallocatePin(pin, PinOwner::UM_Audioreactive);
    }
    bool joinWire(int8_t sda, int8_t scl) override {
#ifdef WLEDMM
        return PinManager::joinWire(sda, scl);
#else
        (void)sda; (void)scl;
        return true;
#endif
    }
};

#ifdef ARDUINO_ARCH_ESP32
#include <driver/i2s.h>
#include <driver/adc.h>
#include <math.h>
#endif

#if defined(ARDUINO_ARCH_ESP32) && (defined(WLED_DEBUG) || defined(SR_DEBUG))
#include <esp_timer.h>
#endif

/*
 * Usermods allow you to add own functionality to WLED more easily
 * See: https://github.com/Aircoookie/WLED/wiki/Add-own-functionality
 *
 * This is an audioreactive v2 usermod - REFACTORED to use object-oriented libraries.
 */

// FFT Configuration
#if defined(WLEDMM_FASTPATH) && defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32)
#define FFT_USE_SLIDING_WINDOW
#endif

#define FFT_PREFER_EXACT_PEAKS
//#define SR_STATS

#if !defined(FFTTASK_PRIORITY)
#if defined(WLEDMM_FASTPATH) && !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3) && defined(ARDUINO_ARCH_ESP32)
#define FFTTASK_PRIORITY 4
#else
#define FFTTASK_PRIORITY 1
#endif
#endif

// Sanity checks
#ifdef ARDUINO_ARCH_ESP32
  #if SETTINGS_STACK_BUF_SIZE < 3904
    #warning please increase SETTINGS_STACK_BUF_SIZE >= 3904
  #endif
  #if (CONFIG_ASYNC_TCP_STACK_SIZE - SETTINGS_STACK_BUF_SIZE) < 4352
    #error remaining async_tcp stack will be too low - please increase CONFIG_ASYNC_TCP_STACK_SIZE
  #endif
#endif

// Audio sync constants
#define AUDIOSYNC_NONE 0x00
#define AUDIOSYNC_SEND 0x01
#define AUDIOSYNC_REC  0x02
#define AUDIOSYNC_REC_PLUS 0x06
#define AUDIOSYNC_IDLE_MS  2500

// AGC presets (count must match AGCController::Preset enum members)
#ifndef AGC_NUM_PRESETS
#define AGC_NUM_PRESETS 3
#endif

static volatile bool disableSoundProcessing = false;
static uint8_t audioSyncEnabled = AUDIOSYNC_NONE;
static bool audioSyncSequence = true;
static uint8_t audioSyncPurge = 1;
static bool udpSyncConnected = false;

// Sample rate and block size
#if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)
  constexpr uint16_t SAMPLE_RATE = 22050;
  constexpr uint16_t BLOCK_SIZE = 128;
  // FFT_MIN_CYCLE matches main:368-374 — minimum ms between FFT iterations.
  #ifndef WLEDMM_FASTPATH
    #define FFT_MIN_CYCLE 21
  #else
    #ifdef FFT_USE_SLIDING_WINDOW
      #define FFT_MIN_CYCLE 8
    #else
      #define FFT_MIN_CYCLE 15
    #endif
  #endif
#else
  constexpr uint16_t SAMPLE_RATE = 18000;
  constexpr uint16_t BLOCK_SIZE = 128;
  #define FFT_MIN_CYCLE 25                       // matches main:384 for S2/C3
#endif

// Configuration defaults
#ifndef SR_AGC
  #ifdef SR_SQUELCH
   #define SR_AGC 1
  #else
   #define SR_AGC 0
  #endif
#endif

#ifndef SR_SQUELCH
  #define SR_SQUELCH 10
#endif

#ifndef SR_GAIN
  #define SR_GAIN 60
#endif

// Backward compatibility: Global variables for effects
// These now proxy to the library instances (refreshed by AudioReactive::updateGlobalVariables).
// They are kept at file scope (with internal linkage) so that um_data->u_data[] can take their
// addresses, matching the original API contract used by Puddlepeak/Ripplepeak/Waterfall and others.
static uint8_t fftResult[NUM_GEQ_CHANNELS] = {0};  // Updated by AudioProcessor (post-limiter, scaled 0..255)
static float   fftCalc  [NUM_GEQ_CHANNELS] = {0};  // Pre-limiter FFT band values (mirror of AudioProcessor::m_fftCalc)
static float   fftAvg   [NUM_GEQ_CHANNELS] = {0};  // Smoothed FFT band values (mirror of AudioProcessor::m_fftAvg)
static float FFT_MajorPeak    = 1.0f;              // Updated by AudioProcessor: dominant frequency (Hz)
static float FFT_MajPeakSmth  = 1.0f;              // Smoothed dominant frequency for "swooping peak" effects
static float FFT_Magnitude    = 0.0f;              // Updated by AudioProcessor: magnitude of dominant frequency
static float my_magnitude     = 0.0f;              // FFT_Magnitude scaled by multAgc (with noise gate); used by effects
static bool  samplePeak       = false;             // Updated by AudioProcessor: peak flag (auto-resets)
static bool  udpSamplePeak    = false;             // Mirror of samplePeak that survives until next UDP send (transmit-side)
static volatile bool haveNewFFTResult = false;     // Set by FFT task, consumed by UDP transmit loop (FASTPATH latency)
static float volumeSmth       = 0.0f;              // Effect-facing smoothed sample (= sampleAgc when AGC on, else sampleAvg)
static int16_t volumeRaw      = 0;                 // Effect-facing raw sample (= rawSampleAgc when AGC on, else sampleRaw). int16_t to match main for um_data slot 1.
static uint16_t zeroCrossingCount = 0;             // Updated by AudioProcessor
static float soundPressure    = 0.0f;              // Updated by AGCController (estimated dB SPL, 0..255)
static float agcSensitivity   = 128.0f;            // Updated by AGCController (UI-display sensitivity, 0..255)
static float multAgc          = 1.0f;              // Mirror of AGCController::getMultiplier(); used by effects
static float sampleAvg        = 0.0f;              // Mirror of AGCController::getSampleAvg()
static float sampleAgc        = 0.0f;              // Mirror of AGCController::getSampleAGC()
static int16_t sampleRaw      = 0;                 // Mirror of AGCController::getSampleRaw()
static int16_t rawSampleAgc   = 0;                 // Mirror of AGCController::getRawSampleAGC()
static float micDataReal      = 0.0f;              // Mirror of AGCController::getSampleReal()
static uint8_t maxVol         = 31;                // Effect-writable peak threshold (Puddlepeak/Ripplepeak/Waterfall)
static uint8_t binNum         = 8;                 // Effect-writable FFT bin selector (Puddlepeak/Ripplepeak/Waterfall)
static uint8_t useInputFilter = 0;                 // Mirror of AudioFilters mode; effects/info page may inspect


// Auto-reset peak based on timing
static unsigned long timeOfPeak = 0;
static void autoResetPeak(void) {
  uint16_t MinShowDelay = MAX(50, strip.getMinShowDelay());
  if (millis() - timeOfPeak > MinShowDelay) {
    samplePeak = false;
  }
}

////////////////////
// usermod class  //
////////////////////

class AudioReactive : public Usermod {
  private:
    // Audio processing library instances
    WLEDPinAllocator m_pinAllocator;   // concrete IPinAllocator backed by WLED PinManager
    AudioFilters audioFilters;
    AGCController agcController;
    AudioProcessor audioProcessor;
    AudioSync audioSync;
    AudioSource* audioSource = nullptr;

    // Configuration
    #ifdef ARDUINO_ARCH_ESP32
    #ifndef AUDIOPIN
    int8_t audioPin = -1;
    #else
    int8_t audioPin = AUDIOPIN;
    #endif

    #ifndef SR_DMTYPE
    uint8_t dmType = 1;
    #define SR_DMTYPE 1
    #else
    uint8_t dmType = SR_DMTYPE;
    #endif

    #ifndef I2S_SDPIN
    int8_t i2ssdPin = 32;
    #else
    int8_t i2ssdPin = I2S_SDPIN;
    #endif

    #ifndef I2S_WSPIN
    int8_t i2swsPin = 15;
    #else
    int8_t i2swsPin = I2S_WSPIN;
    #endif

    #ifndef I2S_CKPIN
    int8_t i2sckPin = 14;
    #else
    int8_t i2sckPin = I2S_CKPIN;
    #endif

    #ifndef ES7243_SDAPIN
    int8_t sdaPin = -1;
    #else
    int8_t sdaPin = ES7243_SDAPIN;
    #endif

    #ifndef ES7243_SCLPIN
    int8_t sclPin = -1;
    #else
    int8_t sclPin = ES7243_SCLPIN;
    #endif

    #ifndef MCLK_PIN
    int8_t mclkPin = I2S_PIN_NO_CHANGE;
    #else
    int8_t mclkPin = MCLK_PIN;
    #endif
    #endif

    // Usermod settings
    #if defined(SR_ENABLE_DEFAULT) || defined(UM_AUDIOREACTIVE_ENABLE)
    bool enabled = true;
    #else
    bool enabled = false;
    #endif
    bool initDone = false;

    // Timing
    unsigned long lastTime = 0;
    #if defined(WLEDMM_FASTPATH)
    const uint16_t delayMs = 5;
    #else
    const uint16_t delayMs = 10;
    #endif
    uint16_t audioSyncPort = 11988;
    bool updateIsRunning = false;

    // AGC/audio settings
    int soundAgc = SR_AGC;
    int sampleGain = SR_GAIN;
    int soundSquelch = SR_SQUELCH;
    int inputLevel = 128;
    int micQuality = 1;
    int micLevelMethod = 0;

    // FFT settings
    uint8_t FFTScalingMode = 3;
    uint8_t pinkIndex = 0;
    uint8_t freqDist = 0;
    uint8_t fftWindow = 0;
    #ifdef FFT_USE_SLIDING_WINDOW
    uint8_t doSlidingFFT = 1;
    #endif

    // Limiter settings — defaults match the originals in main:audio_reactive.h
    bool limiterOn = true;
    #if defined(WLEDMM_FASTPATH)
    uint16_t attackTime = 24;       // FASTPATH: attack time in ms (default 0.024 s)
    uint16_t decayTime  = 250;      // FASTPATH: decay time in ms
    #else
    uint16_t attackTime = 50;       // standard: attack time in ms (default 0.05 s)
    uint16_t decayTime  = 300;      // standard: decay time in ms
    #endif

    // Info page data
    unsigned long last_UDPTime = 0;
    int receivedFormat = 0;
    float maxSample5sec = 0.0f;
    unsigned long sampleMaxTimer = 0;
    #define CYCLE_SAMPLEMAX 3500

    // UDP GEQ dynamics limiter state (separate from AudioProcessor's internal limiter)
    float m_fftCalcUDP[NUM_GEQ_CHANNELS] = {0.0f};
    float m_fftAvgUDP[NUM_GEQ_CHANNELS]  = {0.0f};
    unsigned long m_lastGEQDynamicsTimeUDP = 0;

    // Usermod data exchange
    um_data_t *um_data = nullptr;

    // String constants
    static const char _name[];
    static const char _enabled[];
    static const char _inputLvl[];
    #if defined(ARDUINO_ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
    static const char _analogmic[];
    #endif
    static const char _digitalmic[];
    static const char UDP_SYNC_HEADER[];
    static const char UDP_SYNC_HEADER_v1[];

    // Private methods
    void configureAudioLibraries();
    void updateGlobalVariables();
    void logAudio();
    void limitGEQDynamics(bool gotNewSample);
    void createAudioSource();


  public:
    // Usermod interface
    void setup() override;
    void loop() override;
#if defined(_MoonModules_WLED_) && defined(WLEDMM_FASTPATH)
    void loop2() override;
#endif
    void connected() override;
    void onStateChange(uint8_t mode) override;
    void onUpdateBegin(bool init) override;
    void addToJsonInfo(JsonObject& root) override;
    void addToJsonState(JsonObject& root) override;
    void readFromJsonState(JsonObject& root) override;
    void addToConfig(JsonObject& root) override;
    bool readFromConfig(JsonObject& root) override;
    bool handleButton(uint8_t b) override;
     void appendConfigData() override;
     bool getUMData(um_data_t **data) override;
    uint16_t getId() override { return USERMOD_ID_AUDIOREACTIVE; }
};

////////////////////
// Implementation //
////////////////////

// Configure all audio processing libraries based on current settings
inline void AudioReactive::configureAudioLibraries() {
    // Configure AudioFilters
    AudioFilters::Config filterConfig;
    filterConfig.filterMode = 2;  // Default: DC blocker (matches main:1981 useInputFilter=2)
    if (dmType == 5 || dmType == 51) {
        filterConfig.filterMode = 1;  // PDM bandpass (matches main:2027, 2034)
    }
    #if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
    if (dmType == 0) {
        filterConfig.filterMode = 1;  // PDM bandpass works well for analog too (matches main:2111)
    }
    #endif
    filterConfig.micQuality = micQuality;
    audioFilters.configure(filterConfig);
    useInputFilter = filterConfig.filterMode;   // mirror filter mode for backward-compat code paths

    // Configure AGCController
    AGCController::Config agcConfig;
    agcConfig.preset = (AGCController::Preset)(soundAgc > 0 ? soundAgc - 1 : 0);
    agcConfig.squelch = soundSquelch;
    agcConfig.sampleGain = sampleGain;
    agcConfig.inputLevel = inputLevel;
    agcConfig.micQuality = micQuality;
    agcConfig.micLevelMethod = micLevelMethod;
    #if defined(WLEDMM_FASTPATH)
    agcConfig.fastPath = true;
    #else
    agcConfig.fastPath = false;
    #endif
    agcController.configure(agcConfig);
    agcController.setEnabled(soundAgc > 0);

    // Configure AudioProcessor
    AudioProcessor::Config procConfig;
    procConfig.sampleRate = SAMPLE_RATE;
    procConfig.fftSize = 512;
    procConfig.numGEQChannels = NUM_GEQ_CHANNELS;
    procConfig.scalingMode = FFTScalingMode;
    procConfig.pinkIndex = pinkIndex;
    procConfig.fftWindow = fftWindow;
    procConfig.freqDist = freqDist;
    #ifdef FFT_USE_SLIDING_WINDOW
    procConfig.useSlidingWindow = (doSlidingFFT > 0);
    #else
    procConfig.useSlidingWindow = false;
    #endif
    procConfig.averageByRMS = true;
    procConfig.minCycle = FFT_MIN_CYCLE;
    procConfig.inputLevel = inputLevel;
    procConfig.sampleGain = sampleGain;
    procConfig.limiterOn = limiterOn;
    procConfig.attackTime = attackTime;
    procConfig.decayTime = decayTime;
    procConfig.useInputFilter = filterConfig.filterMode;
    audioProcessor.configure(procConfig);

    // Configure AudioSync
    AudioSync::Config syncConfig;
    syncConfig.port = audioSyncPort;
    syncConfig.enableTransmit = (audioSyncEnabled & AUDIOSYNC_SEND) != 0;
    syncConfig.enableReceive = (audioSyncEnabled & AUDIOSYNC_REC) != 0;
    syncConfig.sequenceCheck = audioSyncSequence;
    syncConfig.purgeCount = audioSyncPurge;
    audioSync.configure(syncConfig);

    // Link components
    audioProcessor.setAudioFilters(&audioFilters);
    audioProcessor.setAGCController(&agcController);
    if (audioSource) {
        audioProcessor.setAudioSource(audioSource);
    }
}

// Update global variables from library instances (for backward compatibility)
inline void AudioReactive::updateGlobalVariables() {
    // --- AudioProcessor outputs ---
    const uint8_t* procFFT = audioProcessor.getFFTResult();
    memcpy(fftResult, procFFT, NUM_GEQ_CHANNELS);
    const float* procCalc = audioProcessor.getFFTCalc();
    const float* procAvg  = audioProcessor.getFFTAvg();
    memcpy(fftCalc, procCalc, sizeof(fftCalc));
    memcpy(fftAvg,  procAvg,  sizeof(fftAvg));

    FFT_MajorPeak     = audioProcessor.getMajorPeak();
    FFT_MajPeakSmth   = audioProcessor.getMajorPeakSmooth();
    FFT_Magnitude     = audioProcessor.getMagnitude();
    samplePeak        = audioProcessor.getSamplePeak();
    if (samplePeak) udpSamplePeak = true;     // sticky for UDP transmit (cleared by transmit path)
    haveNewFFTResult  = audioProcessor.hasNewFFTResult();
    zeroCrossingCount = audioProcessor.getZeroCrossingCount();

    // --- AGCController outputs ---
    multAgc        = agcController.getMultiplier();
    sampleAgc      = agcController.getSampleAGC();
    sampleAvg      = agcController.getSampleAvg();
    sampleRaw      = agcController.getSampleRaw();
    rawSampleAgc   = (int16_t)agcController.getRawSampleAGC();
    micDataReal    = agcController.getSampleReal();

    // Effect-facing volume mirrors original logic (main:2312-2313)
    volumeSmth = (soundAgc) ? sampleAgc    : sampleAvg;
    volumeRaw  = (soundAgc) ? rawSampleAgc : sampleRaw;

    // my_magnitude: FFT_Magnitude × multAgc with noise gate (matches main:2315-2317)
    my_magnitude = FFT_Magnitude;
    if (soundAgc) my_magnitude *= multAgc;
    if (volumeSmth < 1.0f) my_magnitude = 0.001f;
    // NOTE: soundPressure and agcSensitivity are NOT updated here.
    // They are updated periodically in loop() with smoothing to match main:2319-2332.
}

// Create audio source based on dmType — mirrors main:setup() switch statement exactly.
inline void AudioReactive::createAudioSource() {
    #ifdef ARDUINO_ARCH_ESP32
    if (audioSource) {
        delete audioSource;
        audioSource = nullptr;
    }

    // Apply PDM auto-promotion and persist the resolved type so that logging,
    // config saves, and the info page all reflect what was actually opened.
    dmType = normalizeDmType(dmType, i2sckPin);

    // Resolve I2C pins: codec types (ES7243, ES8388, WM8978, AC101, ES8311) can
    // use either the dedicated sdaPin/sclPin fields or the WLED global i2c_sda/
    // i2c_scl.  Merge them the same way the original code did (matches main:2003-2008).
    if ((sdaPin >= 0) && (i2c_sda < 0)) i2c_sda = sdaPin;
    if ((sclPin >= 0) && (i2c_scl < 0)) i2c_scl = sclPin;
    if (i2c_sda >= 0) sdaPin = -1;
    if (i2c_scl >= 0) sclPin = -1;

    AudioSourceConfig cfg;
    cfg.dmType    = dmType;
    cfg.sampleRate = SAMPLE_RATE;
    cfg.blockSize  = BLOCK_SIZE;
    cfg.audioPin   = audioPin;
    cfg.i2ssdPin   = i2ssdPin;
    cfg.i2swsPin   = i2swsPin;
    cfg.i2sckPin   = i2sckPin;
    cfg.mclkPin    = mclkPin;
    cfg.i2c_sda    = i2c_sda;
    cfg.i2c_scl    = i2c_scl;

    audioSource = ::createAudioSource(cfg, &m_pinAllocator);

    // Network-receive-only modes (254/255) return nullptr and require WLED-
    // specific state changes that cannot live inside the platform-neutral factory.
    if (!audioSource && (dmType == 254 || dmType == 255)) {
        disableSoundProcessing = true;
        audioSyncEnabled = AUDIOSYNC_REC; // force UDP receive mode
    }
    #endif
}

// Setup method - initialize everything
inline void AudioReactive::setup() {
    disableSoundProcessing = true;

    if (!initDone) {
        // Setup usermod data exchange — slots and types match the original main:audio_reactive.h
        // contract so existing effects (Puddlepeak/Ripplepeak/Waterfall/Blurz/DJLight/etc.) keep
        // working unchanged.
        um_data = new um_data_t;
        um_data->u_size = 12;
        um_data->u_type = new um_types_t[um_data->u_size];
        um_data->u_data = new void*[um_data->u_size];
        um_data->u_data[0] = &volumeSmth;        // *used (New)
        um_data->u_type[0] = UMT_FLOAT;
        um_data->u_data[1] = &volumeRaw;         //  used (New)
        um_data->u_type[1] = UMT_UINT16;
        um_data->u_data[2] = fftResult;          // *used (Blurz, DJ Light, Noisemove, GEQ_base, 2D Funky Plank, Akemi)
        um_data->u_type[2] = UMT_BYTE_ARR;
        um_data->u_data[3] = &samplePeak;        // *used (Puddlepeak, Ripplepeak, Waterfall)
        um_data->u_type[3] = UMT_BYTE;
        um_data->u_data[4] = &FFT_MajorPeak;     // *used (Ripplepeak, Freqmap, Freqmatrix, Freqpixels, Freqwave, Gravfreq, Rocktaves, Waterfall)
        um_data->u_type[4] = UMT_FLOAT;
        um_data->u_data[5] = &my_magnitude;      //  used (New) — FFT_Magnitude × multAgc with noise-gate
        um_data->u_type[5] = UMT_FLOAT;
        um_data->u_data[6] = &maxVol;            //  written by effect UI element (Puddlepeak, Ripplepeak, Waterfall)
        um_data->u_type[6] = UMT_BYTE;
        um_data->u_data[7] = &binNum;            //  written by effect UI element (Puddlepeak, Ripplepeak, Waterfall)
        um_data->u_type[7] = UMT_BYTE;
#ifdef ARDUINO_ARCH_ESP32
        um_data->u_data[8] = &FFT_MajPeakSmth;   //  smoothed major peak (ESP32)
        um_data->u_type[8] = UMT_FLOAT;
#else
        um_data->u_data[8] = &FFT_MajorPeak;     //  substitute for 8266 (no FFT)
        um_data->u_type[8] = UMT_FLOAT;
#endif
        um_data->u_data[9]  = &soundPressure;    //  used (New)
        um_data->u_type[9]  = UMT_FLOAT;
        um_data->u_data[10] = &agcSensitivity;   //  used (New) - dummy value on 8266
        um_data->u_type[10] = UMT_FLOAT;
        um_data->u_data[11] = &zeroCrossingCount;//  for auto-playlist usermod
        um_data->u_type[11] = UMT_UINT16;
    }

    #ifdef ARDUINO_ARCH_ESP32
    // createAudioSource() has its own per-case delay(100) and a final delay(250)
    createAudioSource();

    // Configure audio libraries — must come after createAudioSource() so dmType is finalised
    // (SCK==-1 auto-detection inside createAudioSource() may have updated dmType to 51).
    configureAudioLibraries();

    // Initialize audio processor
    if (!audioProcessor.initialize()) {
        ERRORSR_PRINTLN(F("AR: Failed to initialize AudioProcessor"));
        disableSoundProcessing = true;
        return;
    }

    if (!audioSource && (dmType < 254)) enabled = false;     // matches main:2120 — no source → disabled

    // Start FFT task if we have a valid audio source and are not in pure receive mode
    if (audioSource && (audioSyncEnabled != AUDIOSYNC_REC)) {
        if (audioProcessor.startTask(FFTTASK_PRIORITY, 0)) {
            DEBUGSR_PRINTLN(F("AR: FFT task started"));
        } else {
            ERRORSR_PRINTLN(F("AR: Failed to start FFT task"));
            enabled = false;                                  // matches main:2125 — no task → disabled
        }
    }

    // Verbose initialisation result (matches main:2126-2133)
    if ((!audioSource) || (!audioSource->isInitialized())) {
        if (dmType < 254) { USER_PRINTLN(F("AR: Failed to initialize sound input driver. Please check input PIN settings.")); }
        else              { USER_PRINTLN(F("AR: No sound input driver configured - network receive only.")); }
        disableSoundProcessing = true;
    } else {
        USER_PRINTLN(F("AR: sound input driver initialized successfully."));
    }

    if (enabled) disableSoundProcessing = false;              // matches main:2135 — all good
    #endif

    // Reset UDP state before first connect, then try to open UDP (matches main:2137-2140).
    last_UDPTime   = 0;
    receivedFormat = 0;
    if (enabled) connected();

    initDone = true;
    DEBUGSR_PRINT(F("AR: init done, enabled = "));
    DEBUGSR_PRINTLN(enabled ? F("true.") : F("false."));
}

// Loop method - mirrors the original main:loop() flow as closely as possible while
// delegating the heavy lifting to AudioProcessor / AGCController / AudioSync libraries.
inline void AudioReactive::loop() {
    static unsigned long lastUMRun = millis();   // tracks user-loop pacing for hick-up handling

    if (!enabled || updateIsRunning) {
        disableSoundProcessing = true;
        lastUMRun = millis();
        return;
    }

    // We cannot wait indefinitely before processing audio data.
    // Be polite while LED strip is busy, but never longer than ~2 ms.
    if (strip.isServicing() && (millis() - lastUMRun < 2)) return;

    // Sound sync "receive or local"
    bool useNetworkAudio = false;
    if (audioSyncEnabled > AUDIOSYNC_SEND) {                   // "receive" or "receive+local"
        useNetworkAudio = (udpSyncConnected
                           && ((millis() - last_UDPTime) <= AUDIOSYNC_IDLE_MS));
        if (audioSyncEnabled == AUDIOSYNC_REC) useNetworkAudio = true; // never fall back in pure REC
    }

    // Suspend local sound processing during real-time LED takeover modes
    if ((realtimeOverride == REALTIME_OVERRIDE_NONE) &&
        ((realtimeMode == REALTIME_MODE_GENERIC)
        || (realtimeMode == REALTIME_MODE_E131)
        || (realtimeMode == REALTIME_MODE_UDP)
        || (realtimeMode == REALTIME_MODE_ADALIGHT)
        || (realtimeMode == REALTIME_MODE_ARTNET))) {
        disableSoundProcessing = true;
        useNetworkAudio = false;
    } else {
        if ((disableSoundProcessing == true) && (audioSyncEnabled != AUDIOSYNC_REC))
            lastUMRun = millis();                              // just left realtime mode - reset timing
        disableSoundProcessing = false;
    }

    if (audioSyncEnabled == AUDIOSYNC_REC)  disableSoundProcessing = true;   // pure receive mode
    if (audioSyncEnabled == AUDIOSYNC_SEND) disableSoundProcessing = false;  // pure transmit mode

#ifdef ARDUINO_ARCH_ESP32
    if (!audioSource || !audioSource->isInitialized()) {       // no usable audio source
        disableSoundProcessing = true;
        if (audioSyncEnabled > AUDIOSYNC_SEND) useNetworkAudio = true;
    }
    if ((audioSyncEnabled == AUDIOSYNC_REC_PLUS) && useNetworkAudio)
        disableSoundProcessing = true;                          // UDP wins over local in mixed mode

    // Local processing path: copy library state into shadow globals + periodic estimates
    if ((audioSyncEnabled != AUDIOSYNC_REC) && !disableSoundProcessing && !useNetworkAudio) {
        if (soundAgc > AGC_NUM_PRESETS) soundAgc = 0;          // clamp invalid AGC preset

        unsigned long t_now = millis();
        lastUMRun = t_now;

        // The FFT task does the actual sampling + AGC; we just publish results
        updateGlobalVariables();

        // Periodic AGC sensitivity / sound-pressure estimate (matches main:2319-2332)
        static unsigned long lastEstimate = 0;
#ifdef WLEDMM_FASTPATH
        const unsigned long estInterval = 7;
#else
        const unsigned long estInterval = 12;
#endif
        if (t_now - lastEstimate > estInterval) {
            lastEstimate    = t_now;
            agcSensitivity  = agcController.getSensitivity();
            float pressure  = agcController.estimatePressure(micDataReal, dmType);
            // dynamics limiter ON -> some smoothing; OFF -> raw value
            soundPressure   = soundPressure
                              + (limiterOn ? 0.38f : 0.95f) * (pressure - soundPressure);
        }

        // Apply attack/decay envelope to volumeSmth (in-place)
        if (limiterOn) audioProcessor.limitSampleDynamics(volumeSmth);
    }
#endif

    autoResetPeak();                                            // auto-reset sample peak after strip min show delay
    if (!udpSyncConnected) udpSamplePeak = false;               // reset UDP-side peak while disconnected

    // Ensure UDP sync connection is up (handles AP mode and reconnections) — matches main:connectUDPSoundSync()
    if ((audioSyncEnabled != AUDIOSYNC_NONE) && (audioSyncPort > 0)) {
        static unsigned long lastConnectAttempt = 0;
        if (!(apActive || WLED_CONNECTED || interfacesInited)) {
            // No interfaces available -> tear down UDP if currently up
            if (udpSyncConnected) {
                udpSyncConnected = false;
                audioSync.end();
                receivedFormat = 0;
                DEBUGSR_PRINTLN(F("AR loop(): connection lost, UDP closed."));
            }
        } else if (!udpSyncConnected
                   && (millis() - lastConnectAttempt > 15000)) {
            lastConnectAttempt = millis();
            if (audioSync.begin()) {
                udpSyncConnected = true;
                DEBUGSR_PRINTLN(F("AR: UDP audio sync (re)connected"));
            }
        }
    }

    // -------- UDP receive --------
    if ((audioSyncEnabled & AUDIOSYNC_REC) && udpSyncConnected) {
        static float syncVolumeSmth = 0.0f;
        bool have_new_sample = false;

        if (millis() - lastTime > delayMs) {
            // Estimate a sensible drain count for the UDP queue
            unsigned timeElapsed   = (millis() - last_UDPTime);
            unsigned maxReadSamples = constrain(timeElapsed / 5U, 1U, 20U);
            switch (audioSyncPurge) {
                case 0:  maxReadSamples = 1;   break;           // never drop unless new connection / timed out
                case 2:  maxReadSamples = 255; break;           // always drop, latest only
                case 1:
                default:
                    if (fabsf(volumeSmth) < 0.25f) maxReadSamples = 255; // silence -> flush
                    break;
            }
            if (receivedFormat == 0)                  maxReadSamples = 255; // new conn -> flush
            if (timeElapsed >= AUDIOSYNC_IDLE_MS)     maxReadSamples = 255; // long-idle -> flush

            have_new_sample = audioSync.receive(maxReadSamples);
            if (have_new_sample) {
                last_UDPTime    = millis();
                useNetworkAudio = true;

                const AudioSync::ReceivedData& rxData = audioSync.getReceivedData();
                volumeSmth        = rxData.volumeSmth;
                volumeRaw         = (int16_t)rxData.volumeRaw;
                samplePeak        = rxData.samplePeak;
                memcpy(fftResult, rxData.fftResult, NUM_GEQ_CHANNELS);
                FFT_Magnitude     = rxData.fftMagnitude;
                FFT_MajorPeak     = rxData.fftMajorPeak;
                FFT_MajPeakSmth   = FFT_MajPeakSmth + 0.42f * (FFT_MajorPeak - FFT_MajPeakSmth);
                my_magnitude      = fmaxf(FFT_Magnitude, 0.0f);
                if (volumeSmth < 1.0f) my_magnitude = 0.001f;
                zeroCrossingCount = rxData.zeroCrossingCount;
                soundPressure     = rxData.soundPressure;
                agcSensitivity    = rxData.agcSensitivity;
                receivedFormat    = rxData.receivedFormat;
            }
            lastTime = millis();
        }

        if (useNetworkAudio) {
            if (have_new_sample) syncVolumeSmth = volumeSmth;   // remember received sample
            else                  volumeSmth     = syncVolumeSmth; // restore for next limiter run
            if (limiterOn) audioProcessor.limitSampleDynamics(volumeSmth);
            limitGEQDynamics(have_new_sample);                  // smooth FFT (GEQ) samples
        }
    } else if (!(audioSyncEnabled & AUDIOSYNC_REC)) {
        receivedFormat = 0;
    }

    // 25-second UDP idle auto-disconnect (matches main:2388-2404)
    if ((audioSyncEnabled & AUDIOSYNC_REC)
        && udpSyncConnected
        && (receivedFormat > 0)
        && ((millis() - last_UDPTime) > 25000)) {
        udpSyncConnected = false;
        receivedFormat   = 0;
        audioSync.end();
        volumeSmth       = 0.0f;
        volumeRaw        = 0;
        my_magnitude     = 0.1f;
        FFT_Magnitude    = 0.01f;
        FFT_MajorPeak    = 2.0f;
        soundPressure    = 1.0f;
        agcSensitivity   = 64.0f;
#ifdef ARDUINO_ARCH_ESP32
        multAgc          = 1.0f;
#endif
        DEBUGSR_PRINTLN(F("AR loop(): UDP closed due to inactivity."));
    }

    // Serial plotter output (active only when MIC_LOGGER / FFT_SAMPLING_LOG is defined)
#if defined(MIC_LOGGER) || defined(MIC_SAMPLING_LOG) || defined(FFT_SAMPLING_LOG)
    static unsigned long lastMicLoggerTime = 0;
    if (millis() - lastMicLoggerTime > 20) {
        lastMicLoggerTime = millis();
        logAudio();
    }
#endif

    // Info page: keep maximum sample value over the last ~5 s
#ifdef ARDUINO_ARCH_ESP32
    if ((millis() - sampleMaxTimer) > CYCLE_SAMPLEMAX) {
        sampleMaxTimer = millis();
        maxSample5sec  = (0.15f * maxSample5sec) + 0.85f * (soundAgc ? sampleAgc : sampleAvg);
        if (sampleAvg < 1.0f) maxSample5sec = 0.0f;
    } else {
        if (sampleAvg >= 1.0f)
            maxSample5sec = fmaxf(maxSample5sec, soundAgc ? (float)rawSampleAgc : (float)sampleRaw);
    }
#else  // 8266 receive-only path uses volumeSmth/volumeRaw
    if ((millis() - sampleMaxTimer) > CYCLE_SAMPLEMAX) {
        sampleMaxTimer = millis();
        maxSample5sec  = (0.15f * maxSample5sec) + 0.85f * volumeSmth;
        if (volumeSmth < 1.0f) maxSample5sec = 0.0f;
        if (maxSample5sec < 0.0f) maxSample5sec = 0.0f;
    } else {
        if (volumeSmth >= 1.0f) maxSample5sec = fmaxf(maxSample5sec, (float)volumeRaw);
    }
#endif

    // -------- UDP transmit --------
#ifdef ARDUINO_ARCH_ESP32
  #if defined(WLEDMM_FASTPATH)
    // FASTPATH: send as soon as a fresh FFT result is ready, else fall back to ~25 ms cadence
    // Read directly from the library so we don't depend on updateGlobalVariables() having run.
    bool fftReady = audioProcessor.hasNewFFTResult();
    if ((audioSyncEnabled & AUDIOSYNC_SEND) && (fftReady || (millis() - lastTime > 24))) {
  #else
    if ((audioSyncEnabled & AUDIOSYNC_SEND) && (millis() - lastTime > 20)) {
  #endif
        haveNewFFTResult = false;                               // reset shadow notification
        audioProcessor.clearNewFFTResult();
        audioSync.transmit(
            (float)volumeRaw,
            volumeSmth,
            udpSamplePeak ? true : samplePeak,
            fftResult,
            zeroCrossingCount,
            FFT_Magnitude,
            FFT_MajorPeak,
            soundPressure
        );
        udpSamplePeak = false;
        lastTime = millis();
    }
#endif
}

#if defined(_MoonModules_WLED_) && defined(WLEDMM_FASTPATH)
// FASTPATH dispatches loop2 (called from a separate scheduler) into the same body as loop().
inline void AudioReactive::loop2() {
    loop();
}
#endif

// The rest of the methods use library getters

inline void AudioReactive::connected() {
    // Close any pre-existing UDP socket — matches main:2178-2183.
    if (udpSyncConnected) {
        udpSyncConnected = false;
        audioSync.end();
        receivedFormat = 0;
        DEBUGSR_PRINTLN(F("AR connected(): old UDP connection closed."));
    }

    if ((audioSyncPort > 0) && (audioSyncEnabled > AUDIOSYNC_NONE)) {
        udpSyncConnected = audioSync.begin();
        receivedFormat   = 0;
        if (udpSyncConnected) last_UDPTime = millis();
        DEBUGSR_PRINTLN(udpSyncConnected ? F("AR connected(): UDP audio sync connected.")
                                          : F("AR connected(): UDP audio sync failed."));
    }
}

inline void AudioReactive::onUpdateBegin(bool init) {
    updateIsRunning = true;

#ifdef WLED_DEBUG
    // Drop accumulated stats so post-update info page starts fresh (matches main:2467).
    // We don't have a setter, but the stats refill quickly during normal operation.
#endif

    // Reset all sound state — matches the full reset in main:onUpdateBegin().
    // Effects continuing to render during/after OTA must see a clean baseline rather than
    // stale values from before the update.
    micDataReal   = 0.0f;
    volumeRaw     = 0;
    volumeSmth    = 0.0f;
    sampleAgc     = 0.0f;
    sampleAvg     = 0.0f;
    sampleRaw     = 0;
    rawSampleAgc  = 0;
    my_magnitude  = 0.0f;
    FFT_Magnitude = 0.0f;
    FFT_MajorPeak = 1.0f;
    FFT_MajPeakSmth = 1.0f;
    multAgc       = 1.0f;

    memset(fftCalc,   0, sizeof(fftCalc));
    memset(fftAvg,    0, sizeof(fftAvg));
    memset(fftResult, 0, sizeof(fftResult));
    // Tiny visible test pattern in fftResult so a spectator sees something during update
    for (int i = (init ? 0 : 1); i < NUM_GEQ_CHANNELS; i += 2) fftResult[i] = 16;

    inputLevel = 128;
    autoResetPeak();

    #ifdef ARDUINO_ARCH_ESP32
    // Gracefully suspend FFT task and close UDP if this is the start of an update
    disableSoundProcessing = true;
    if (init && audioProcessor.isTaskRunning()) {
        delay(25);                          // give I2S driver time to finish sampling
        audioProcessor.stopTask();          // matches vTaskSuspend semantics (we will recreate on resume)
        if (udpSyncConnected) {             // close UDP sync (will be reopened by connected())
            udpSyncConnected = false;
            audioSync.end();
            DEBUGSR_PRINTLN(F("AR onUpdateBegin(true): UDP connection closed."));
            receivedFormat = 0;
        }
    } else {
        // Update has failed/finished or task (re-)create requested - restart task and reconnect UDP
        if (audioProcessor.isTaskRunning()) {
            connected();                    // resume UDP
        } else if (audioSource) {           // only create FFT task if we have a valid audio source
            audioProcessor.startTask(FFTTASK_PRIORITY, 0);
            connected();                    // (re)connect UDP after task is up
        }
    }
    micDataReal = 0.0f;                     // just to be sure (matches main:2516)
    if (enabled && audioSource) disableSoundProcessing = false;
    #endif

    updateIsRunning = init;                 // match main:2518 — stays true while init is happening
}

inline bool AudioReactive::getUMData(um_data_t **data) {
    if (!data || !enabled) return false;   // no pointer provided or not enabled -> exit
    *data = um_data;
    return true;
}

// String constants
const char AudioReactive::_name[] PROGMEM = "AudioReactive";
const char AudioReactive::_enabled[] PROGMEM = "enabled";
const char AudioReactive::_inputLvl[] PROGMEM = "inputLevel";
#if defined(ARDUINO_ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
const char AudioReactive::_analogmic[] PROGMEM = "analogmic";
#endif
const char AudioReactive::_digitalmic[] PROGMEM = "digitalmic";
const char AudioReactive::UDP_SYNC_HEADER[] PROGMEM = "00002";
const char AudioReactive::UDP_SYNC_HEADER_v1[] PROGMEM = "00001";

////////////////////
// Missing methods //
////////////////////

// onStateChange - base class provides no-op default; override to satisfy declaration
inline void AudioReactive::onStateChange(uint8_t /*mode*/) {}

// limitGEQDynamics - smooth received UDP FFT data with attack/decay envelope
// Uses its own per-channel filter state (separate from AudioProcessor's internal limiter)
inline void AudioReactive::limitGEQDynamics(bool gotNewSample) {
    constexpr float bigChange = 202.0f;
    constexpr float smooth = 0.8f;

    if (!limiterOn) return;

    if (gotNewSample) {
        // Latch the newly arrived FFT values as target; revert fftResult to last filtered value
        for (unsigned i = 0; i < NUM_GEQ_CHANNELS; i++) {
            m_fftCalcUDP[i] = fftResult[i];
            fftResult[i]    = (uint8_t)m_fftAvgUDP[i];
        }
    }

    unsigned long now = millis();
    long delta_time = constrain((long)(now - m_lastGEQDynamicsTimeUDP), 1, 1000);
    float maxAttack = (attackTime <= 0) ?  255.0f : (bigChange * float(delta_time) / float(attackTime));
    float maxDecay  = (decayTime  <= 0) ? -255.0f : (-bigChange * float(delta_time) / float(decayTime));

    for (unsigned i = 0; i < NUM_GEQ_CHANNELS; i++) {
        float delta = m_fftCalcUDP[i] - m_fftAvgUDP[i];
        if (delta > maxAttack) delta = maxAttack;
        if (delta < maxDecay)  delta = maxDecay;
        delta *= smooth;                                    // matches main:1666 — smooth AFTER clamping
        m_fftAvgUDP[i] = fmaxf(0.0f, fminf(255.0f, m_fftAvgUDP[i] + delta));
        fftResult[i]   = (uint8_t)m_fftAvgUDP[i];
    }
    m_lastGEQDynamicsTimeUDP = now;
}

// logAudio - serial plotter output for MIC_LOGGER / FFT_SAMPLING_LOG debug modes
inline void AudioReactive::logAudio() {
    if (disableSoundProcessing && (!udpSyncConnected || ((audioSyncEnabled & AUDIOSYNC_REC) == 0))) return;

#ifdef MIC_LOGGER
    PLOT_PRINT("volumeSmth:"); PLOT_PRINT(volumeSmth + 256.0f); PLOT_PRINT("\t");
#ifdef ARDUINO_ARCH_ESP32
    PLOT_PRINT("micReal:");    PLOT_PRINT(agcController.getSampleReal() + 256.0f); PLOT_PRINT("\t");
#endif
    PLOT_PRINTLN();
    PLOT_FLUSH();
#endif

#ifdef FFT_SAMPLING_LOG
    const bool mapValuesToPlotterSpace     = false;
    const bool scaleValuesFromCurrentMaxVal = false;
    const bool printMaxVal = false;
    const bool printMinVal = false;
    const int  defaultScalingFromHighValue = 256;
    const int  scalingToHighValue          = 256;
    const int  minimumMaxVal               = 1;

    int maxVal = minimumMaxVal;
    int minVal = 0;
    for (int i = 0; i < NUM_GEQ_CHANNELS; i++) {
        if (fftResult[i] > maxVal) maxVal = fftResult[i];
        if (fftResult[i] < minVal) minVal = fftResult[i];
    }
    for (int i = 0; i < NUM_GEQ_CHANNELS; i++) {
        PLOT_PRINT(i); PLOT_PRINT(":");
        PLOT_PRINTF("%04ld ", map(fftResult[i], 0,
            (scaleValuesFromCurrentMaxVal ? maxVal : defaultScalingFromHighValue),
            (mapValuesToPlotterSpace * i * scalingToHighValue) + 0,
            (mapValuesToPlotterSpace * i * scalingToHighValue) + scalingToHighValue - 1));
    }
    if (printMaxVal) PLOT_PRINTF("maxVal:%04d ", maxVal + (mapValuesToPlotterSpace ? 16 * 256 : 0));
    if (printMinVal) PLOT_PRINTF("%04d:minVal ", minVal);
    if (mapValuesToPlotterSpace)
        PLOT_PRINTF("max:%04d ", (printMaxVal ? 17 : 16) * 256);
    else
        PLOT_PRINTF("max:%04d ", 256);
    PLOT_PRINTLN();
#endif
}

// handleButton - prevent analog mic pin being used as a button
#ifdef ARDUINO_ARCH_ESP32
inline bool AudioReactive::handleButton(uint8_t b) {
    yield();
    if (enabled && dmType == 0 && audioPin >= 0
        && (buttons[b].type == BTN_TYPE_ANALOG || buttons[b].type == BTN_TYPE_ANALOG_INVERTED)) {
        return true; // consume button event — pin is in use for audio
    }
    return false;
}
#else
inline bool AudioReactive::handleButton(uint8_t /*b*/) { return false; }
#endif

// addToJsonState - expose enabled flag to /json/state
inline void AudioReactive::addToJsonState(JsonObject& root) {
    if (!initDone || !enabled) return;  // matches main:2764
    JsonObject usermod = root[FPSTR(_name)];
    if (usermod.isNull()) usermod = root.createNestedObject(FPSTR(_name));
    usermod["on"] = enabled;
}

// readFromJsonState - accept enabled toggle and input-level slider from /json/state
inline void AudioReactive::readFromJsonState(JsonObject& root) {
    if (!initDone) return;
    bool prevEnabled = enabled;
    JsonObject usermod = root[FPSTR(_name)];
    if (!usermod.isNull()) {
        if (usermod[FPSTR(_enabled)].is<bool>()) {
            enabled = usermod[FPSTR(_enabled)].as<bool>();
            if (prevEnabled != enabled) onUpdateBegin(!enabled);
        }
#ifdef ARDUINO_ARCH_ESP32
        if (usermod[FPSTR(_inputLvl)].is<int>()) {
            inputLevel = min(255, max(0, usermod[FPSTR(_inputLvl)].as<int>()));
        }
#endif
    }
}

// addToConfig - persist all settings to cfg.json
inline void AudioReactive::addToConfig(JsonObject& root) {
    JsonObject top = root.createNestedObject(FPSTR(_name));
    top[FPSTR(_enabled)] = enabled;

#ifdef ARDUINO_ARCH_ESP32
  #if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
    JsonObject amic = top.createNestedObject(FPSTR(_analogmic));
    amic["pin"] = audioPin;
  #endif

    JsonObject dmic = top.createNestedObject(FPSTR(_digitalmic));
    dmic[F("type")] = dmType;
    // Use global I2C pins for codecs that share the bus
    if ((dmType == 2) || (dmType == 6)) {
        if (i2c_sda >= 0) sdaPin = -1;
        if (i2c_scl >= 0) sclPin = -1;
    }
    JsonArray pinArray = dmic.createNestedArray("pin");
    pinArray.add(i2ssdPin);
    pinArray.add(i2swsPin);
    pinArray.add(i2sckPin);
    pinArray.add(mclkPin);
    pinArray.add(sdaPin);
    pinArray.add(sclPin);

    JsonObject cfg = top.createNestedObject("config");
    cfg[F("squelch")] = soundSquelch;
    cfg[F("gain")]    = sampleGain;
    cfg[F("AGC")]     = soundAgc;

    JsonObject poweruser = top.createNestedObject("experiments");
    poweruser[F("micLev")]      = micLevelMethod;
    poweruser[F("Mic_Quality")] = micQuality;
    poweruser[F("freqDist")]    = freqDist;
    poweruser[F("FFT_Window")]  = fftWindow;
  #ifdef FFT_USE_SLIDING_WINDOW
    poweruser[F("I2S_FastPath")] = doSlidingFFT;
  #endif

    JsonObject freqScale = top.createNestedObject("frequency");
    freqScale[F("scale")]   = FFTScalingMode;
    freqScale[F("profile")] = pinkIndex;
#endif

    JsonObject dynLim = top.createNestedObject("dynamics");
    dynLim[F("limiter")] = limiterOn;
    dynLim[F("rise")]    = attackTime;
    dynLim[F("fall")]    = decayTime;

    JsonObject sync = top.createNestedObject("sync");
    sync[F("port")]           = audioSyncPort;
    sync[F("mode")]           = audioSyncEnabled;
    sync[F("skip_old_data")]  = audioSyncPurge;
    sync[F("check_sequence")] = audioSyncSequence;
}

// readFromConfig - load persisted settings, re-configure libraries if already running
inline bool AudioReactive::readFromConfig(JsonObject& root) {
    JsonObject top = root[FPSTR(_name)];
    bool configComplete = !top.isNull();

#ifdef ARDUINO_ARCH_ESP32
    auto oldEnabled   = enabled;
    auto oldDMType    = dmType;
    auto oldI2SsdPin  = i2ssdPin;
    auto oldI2SwsPin  = i2swsPin;
    auto oldI2SckPin  = i2sckPin;
    auto oldMclkPin   = mclkPin;
#endif

    configComplete &= getJsonValue(top[FPSTR(_enabled)], enabled);

#ifdef ARDUINO_ARCH_ESP32
  #if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
    configComplete &= getJsonValue(top[FPSTR(_analogmic)]["pin"], audioPin);
  #else
    audioPin = -1;
  #endif

    configComplete &= getJsonValue(top[FPSTR(_digitalmic)]["type"], dmType);
  #if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    if (dmType == 0) dmType = SR_DMTYPE;
    #if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32C3)
    if (dmType == 5)  dmType = SR_DMTYPE;
    if (dmType == 51) dmType = SR_DMTYPE;
    #endif
  #endif

    configComplete &= getJsonValue(top[FPSTR(_digitalmic)]["pin"][0], i2ssdPin);
    configComplete &= getJsonValue(top[FPSTR(_digitalmic)]["pin"][1], i2swsPin);
    configComplete &= getJsonValue(top[FPSTR(_digitalmic)]["pin"][2], i2sckPin);
    configComplete &= getJsonValue(top[FPSTR(_digitalmic)]["pin"][3], mclkPin);
    configComplete &= getJsonValue(top[FPSTR(_digitalmic)]["pin"][4], sdaPin);
    configComplete &= getJsonValue(top[FPSTR(_digitalmic)]["pin"][5], sclPin);

    configComplete &= getJsonValue(top["config"][F("squelch")], soundSquelch);
    configComplete &= getJsonValue(top["config"][F("gain")],    sampleGain);
    configComplete &= getJsonValue(top["config"][F("AGC")],     soundAgc);

    configComplete &= getJsonValue(top["experiments"][F("micLev")],      micLevelMethod);
    configComplete &= getJsonValue(top["experiments"][F("Mic_Quality")], micQuality);
    configComplete &= getJsonValue(top["experiments"][F("freqDist")],    freqDist);
    configComplete &= getJsonValue(top["experiments"][F("FFT_Window")],  fftWindow);
  #ifdef FFT_USE_SLIDING_WINDOW
    configComplete &= getJsonValue(top["experiments"][F("I2S_FastPath")], doSlidingFFT);
  #endif

    configComplete &= getJsonValue(top["frequency"][F("scale")],   FFTScalingMode);
    configComplete &= getJsonValue(top["frequency"][F("profile")], pinkIndex);
#endif

    configComplete &= getJsonValue(top["dynamics"][F("limiter")], limiterOn);
    configComplete &= getJsonValue(top["dynamics"][F("rise")],    attackTime);
    configComplete &= getJsonValue(top["dynamics"][F("fall")],    decayTime);

    configComplete &= getJsonValue(top["sync"][F("port")],           audioSyncPort);
    configComplete &= getJsonValue(top["sync"][F("mode")],           audioSyncEnabled);
    configComplete &= getJsonValue(top["sync"][F("skip_old_data")],  audioSyncPurge);
    configComplete &= getJsonValue(top["sync"][F("check_sequence")], audioSyncSequence);

#ifdef ARDUINO_ARCH_ESP32
    if (initDone) {
        // Hardware changes that require a full restart
        if ((audioSource != nullptr) && (oldDMType != dmType))                                  errorFlag = ERR_REBOOT_NEEDED;
        if ((audioSource != nullptr) && enabled
            && ((oldI2SsdPin != i2ssdPin) || (oldI2SwsPin != i2swsPin) || (oldI2SckPin != i2sckPin))) errorFlag = ERR_REBOOT_NEEDED;
        if ((audioSource != nullptr) && (oldMclkPin != mclkPin))                                errorFlag = ERR_REBOOT_NEEDED;
        if ((oldDMType != dmType) && (oldDMType == 0))                                          errorFlag = ERR_POWEROFF_NEEDED;
        if ((oldDMType != dmType) && (dmType == 0))                                             errorFlag = ERR_POWEROFF_NEEDED;

        // Push soft settings (gain, AGC mode, dynamics, sync) to the libraries when no reboot is pending.
        // If the enabled flag toggled, onUpdateBegin() handles tear-down/start-up; otherwise just refresh config.
        if (errorFlag == ERR_NONE) {
            if (enabled != oldEnabled) {
                onUpdateBegin(!enabled);
            } else {
                configureAudioLibraries();
            }
        }
    }
#endif

    return configComplete;
}

// appendConfigData - inject dropdown options and UI labels via JavaScript
inline void AudioReactive::appendConfigData() {
    oappend(SET_F("ux='AudioReactive';"));
    oappend(SET_F("uxp=ux+':digitalmic:pin[]';"));
    oappend(SET_F("addInfo(ux+':help',0,'<button onclick=\"location.href=&quot;https://mm.kno.wled.ge/soundreactive/Sound-Settings&quot;\" type=\"button\">?</button>');"));

#ifdef ARDUINO_ARCH_ESP32
  #if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
  #ifdef AUDIOPIN
    oappend(SET_F("xOpt(ux+':analogmic:pin',1,' ⎌',")); oappendi(AUDIOPIN); oappend(");");
  #endif
    oappend(SET_F("aOpt(ux+':analogmic:pin',1);"));
  #endif

    oappend(SET_F("dd=addDropdown(ux,'digitalmic:type');"));
  #if SR_DMTYPE==254
    oappend(SET_F("addOption(dd,'None - network receive only (⎌)',254);"));
  #else
    oappend(SET_F("addOption(dd,'None - network receive only',254);"));
  #endif
  #if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3)
    #if SR_DMTYPE==0
    oappend(SET_F("addOption(dd,'Generic Analog (⎌)',0);"));
    #else
    oappend(SET_F("addOption(dd,'Generic Analog',0);"));
    #endif
  #endif
  #if SR_DMTYPE==1
    oappend(SET_F("addOption(dd,'Generic I2S (⎌)',1);"));
  #else
    oappend(SET_F("addOption(dd,'Generic I2S',1);"));
  #endif
  #if SR_DMTYPE==2
    oappend(SET_F("addOption(dd,'ES7243 (⎌)',2);"));
  #else
    oappend(SET_F("addOption(dd,'ES7243',2);"));
  #endif
  #if SR_DMTYPE==3
    oappend(SET_F("addOption(dd,'SPH0654 (⎌)',3);"));
  #else
    oappend(SET_F("addOption(dd,'SPH0654',3);"));
  #endif
  #if SR_DMTYPE==4
    oappend(SET_F("addOption(dd,'Generic I2S with Mclk (⎌)',4);"));
  #else
    oappend(SET_F("addOption(dd,'Generic I2S with Mclk',4);"));
  #endif
  #if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)
    #if SR_DMTYPE==5
    oappend(SET_F("addOption(dd,'Generic I2S PDM (⎌)',5);"));
    #else
    oappend(SET_F("addOption(dd,'Generic I2S PDM',5);"));
    #endif
    #if SR_DMTYPE==51
    oappend(SET_F("addOption(dd,'.Legacy I2S PDM ☾ (⎌)',51);"));
    #else
    oappend(SET_F("addOption(dd,'.Legacy I2S PDM ☾',51);"));
    #endif
  #endif
  #if SR_DMTYPE==6
    oappend(SET_F("addOption(dd,'ES8388 ☾ (⎌)',6);"));
  #else
    oappend(SET_F("addOption(dd,'ES8388 ☾',6);"));
  #endif
  #if SR_DMTYPE==7
    oappend(SET_F("addOption(dd,'WM8978 ☾ (⎌)',7);"));
  #else
    oappend(SET_F("addOption(dd,'WM8978 ☾',7);"));
  #endif
  #if SR_DMTYPE==8
    oappend(SET_F("addOption(dd,'AC101 ☾ (⎌)',8);"));
  #else
    oappend(SET_F("addOption(dd,'AC101 ☾',8);"));
  #endif
  #if SR_DMTYPE==9
    oappend(SET_F("addOption(dd,'ES8311 ☾ (⎌)',9);"));
  #else
    oappend(SET_F("addOption(dd,'ES8311 ☾',9);"));
  #endif

    // SD/WS/SCK/MCLK pin labels
    oappend(SET_F("addInfo(uxp,0,'SD');"));
    oappend(SET_F("addInfo(uxp,1,'WS');"));
    oappend(SET_F("addInfo(uxp,2,'SCK');"));
    oappend(SET_F("addInfo(uxp,3,'MCLK');"));
    oappend(SET_F("addInfo(uxp,4,'SDA');"));
    oappend(SET_F("addInfo(uxp,5,'SCL');"));

    // AGC dropdown
    oappend(SET_F("dd=addDropdown(ux,'config:AGC');"));
    oappend(SET_F("addOption(dd,'Off',0);"));
    oappend(SET_F("addOption(dd,'Normal',1);"));
    oappend(SET_F("addOption(dd,'Vivid',2);"));
    oappend(SET_F("addOption(dd,'Lazy',3);"));

    // FFT window dropdown
    oappend(SET_F("dd=addDropdown(ux,'experiments:FFT_Window');"));
    oappend(SET_F("addOption(dd,'Blackman-Harris',0);"));
    oappend(SET_F("addOption(dd,'Hann',1);"));
    oappend(SET_F("addOption(dd,'Nuttall',2);"));
    oappend(SET_F("addOption(dd,'Hamming',3);"));
    oappend(SET_F("addOption(dd,'Flat-top',4);"));
    oappend(SET_F("addOption(dd,'Blackman',5);"));

    // Frequency scaling dropdown
    oappend(SET_F("dd=addDropdown(ux,'frequency:scale');"));
    oappend(SET_F("addOption(dd,'None',0);"));
    oappend(SET_F("addOption(dd,'Linear (Amplitude)',1);"));
    oappend(SET_F("addOption(dd,'Square Root',2);"));
    oappend(SET_F("addOption(dd,'Logarithmic',3);"));

    // UDP sync mode dropdown
    oappend(SET_F("dd=addDropdown(ux,'sync:mode');"));
    oappend(SET_F("addOption(dd,'Off',0);"));
    oappend(SET_F("addOption(dd,'Send',1);"));
    oappend(SET_F("addOption(dd,'Receive',2);"));
    oappend(SET_F("addOption(dd,'Receive + Local',6);"));
#endif
}

// addToJsonInfo - populate the Info page with audio status, levels and timing stats
inline void AudioReactive::addToJsonInfo(JsonObject& root) {
#ifdef ARDUINO_ARCH_ESP32
    char myStringBuffer[16];
#endif
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");

    JsonArray infoArr = user.createNestedArray(FPSTR(_name));

    // Enable / disable toggle button
    String uiDomString = F("<button class=\"btn btn-xs\" onclick=\"requestJson({");
    uiDomString += FPSTR(_name);
    uiDomString += F(":{");
    uiDomString += FPSTR(_enabled);
    uiDomString += enabled ? F(":false}});\">") : F(":true}});\">");
    uiDomString += F("<i class=\"icons");
    uiDomString += enabled ? F(" on") : F(" off");
    uiDomString += F("\">&#xe08f;</i></button>");
    infoArr.add(uiDomString);

    if (enabled) {
        bool audioSyncIDLE = false;

#ifdef ARDUINO_ARCH_ESP32
        // Sync idle: connected but nothing received for 2.5 s, and we have local audio
        if ((audioSyncEnabled & AUDIOSYNC_REC)
            && (!udpSyncConnected || (millis() - last_UDPTime > AUDIOSYNC_IDLE_MS)))
            audioSyncIDLE = true;
        if ((audioSource == nullptr) || (!audioSource->isInitialized()))
            audioSyncIDLE = false;

        // Input level slider (only when processing is active)
        if (!disableSoundProcessing) {
            if (soundAgc > 0) {
                infoArr = user.createNestedArray(F("GEQ Input Level"));
                float post_gain = (float)inputLevel / 128.0f;
                if (post_gain < 1.0f) post_gain = ((post_gain - 1.0f) * 0.8f) + 1.0f;
                post_gain = roundf(post_gain * 100.0f);
                snprintf_P(myStringBuffer, 15, PSTR("%3.0f %%"), post_gain);
                infoArr.add(myStringBuffer);
            } else {
                infoArr = user.createNestedArray(F("Audio Input Level"));
            }
            uiDomString = F("<div class=\"slider\"><div class=\"sliderwrap il\"><input class=\"noslide\" onchange=\"requestJson({");
            uiDomString += FPSTR(_name);
            uiDomString += F(":{");
            uiDomString += FPSTR(_inputLvl);
            uiDomString += F(":parseInt(this.value)}});\" oninput=\"updateTrail(this);\" max=255 min=0 type=\"range\" value=");
            uiDomString += inputLevel;
            uiDomString += F(" /><div class=\"sliderdisplay\"></div></div></div>");
            infoArr.add(uiDomString);
        }
#endif

        // Audio source
        infoArr = user.createNestedArray(F("Audio Source"));
        if ((audioSyncEnabled == AUDIOSYNC_REC) || (!audioSyncIDLE && (audioSyncEnabled == AUDIOSYNC_REC_PLUS))) {
            infoArr.add(F("UDP sound sync"));
            if (udpSyncConnected) {
                infoArr.add((millis() - last_UDPTime < AUDIOSYNC_IDLE_MS) ? F(" - receiving") : F(" - idle"));
            } else {
                infoArr.add(F(" - no connection"));
            }
#ifndef ARDUINO_ARCH_ESP32
        } else {
            infoArr.add(F("sound sync Off"));
        }
#else
        } else {
            if (audioSource && audioSource->isInitialized()) {
                if (audioSource->getType() == AudioSource::Type_I2SAdc) {
                    infoArr.add(F("ADC analog"));
                } else {
                    infoArr.add(dmType != 51 ? F("I2S digital") : F("legacy I2S PDM"));
                }
                if (maxSample5sec > 1.0f) {
                    float my_usage = 100.0f * (maxSample5sec / 255.0f);
                    snprintf_P(myStringBuffer, 15, PSTR(" - peak %3d%%"), int(my_usage));
                    infoArr.add(myStringBuffer);
                } else {
                    infoArr.add(F(" - quiet"));
                }
            } else {
                infoArr.add(F("not initialized"));
                if (dmType < 254) infoArr.add(F(" - check pin settings"));
            }
        }

        // Sound processing state
        infoArr = user.createNestedArray(F("Sound Processing"));
        infoArr.add((audioSource && !disableSoundProcessing) ? F("running") : F("suspended"));

        // Gain info
        if ((soundAgc == 0) && !disableSoundProcessing && !(audioSyncEnabled == AUDIOSYNC_REC)) {
            infoArr = user.createNestedArray(F("Manual Gain"));
            float myGain = ((float)sampleGain / 40.0f * (float)inputLevel / 128.0f) + 1.0f / 16.0f;
            infoArr.add(roundf(myGain * 100.0f) / 100.0f);
            infoArr.add("x");
        }
        if ((soundAgc > 0) && !disableSoundProcessing && !(audioSyncEnabled == AUDIOSYNC_REC)) {
            infoArr = user.createNestedArray(F("AGC Gain"));
            infoArr.add(roundf(agcController.getMultiplier() * 100.0f) / 100.0f);
            infoArr.add("x");
        }
#endif

        // UDP sync status
        infoArr = user.createNestedArray(F("UDP Sound Sync"));
        if (audioSyncEnabled) {
            if (audioSyncEnabled & AUDIOSYNC_SEND) {
                infoArr.add(F("send mode"));
                if (udpSyncConnected && (millis() - lastTime < AUDIOSYNC_IDLE_MS)) infoArr.add(F(" v2+"));
            } else if (audioSyncEnabled == AUDIOSYNC_REC) {
                infoArr.add(F("receive mode"));
            } else if (audioSyncEnabled == AUDIOSYNC_REC_PLUS) {
                infoArr.add(F("receive+local mode"));
            }
        } else {
            infoArr.add("off");
        }
        if (audioSyncEnabled && !udpSyncConnected) infoArr.add(F(" <i>(unconnected)</i>"));
        if (audioSyncEnabled && udpSyncConnected && (millis() - last_UDPTime < AUDIOSYNC_IDLE_MS)) {
            if (receivedFormat == 1) infoArr.add(F(" v1"));
            if (receivedFormat == 2) infoArr.add(F(" v2"));
            if (receivedFormat == 3) infoArr.add(audioSyncSequence ? F(" v2+") : F(" v2"));
        }

#if defined(WLED_DEBUG) || defined(SR_DEBUG) || defined(SR_STATS)
#ifdef ARDUINO_ARCH_ESP32
        const AudioProcessor::Stats& stats = audioProcessor.getStats();

        infoArr = user.createNestedArray(F("I2S cycle time"));
        infoArr.add(roundf(stats.fftTaskCycle) / 100.0f);
        infoArr.add(" ms");

        infoArr = user.createNestedArray(F("Sampling time"));
        infoArr.add(roundf(stats.sampleTime) / 100.0f);
        infoArr.add(" ms");

        infoArr = user.createNestedArray(F("Filtering time"));
        infoArr.add(roundf(stats.filterTime) / 100.0f);
        infoArr.add(" ms");

        infoArr = user.createNestedArray(F("FFT time"));
        infoArr.add(roundf(stats.fftTime) / 100.0f);

        // Annotate with red/orange when FFT eats too much of the cycle budget — matches main:2737-2747.
#ifdef FFT_USE_SLIDING_WINDOW
        unsigned timeBudget = (doSlidingFFT) ? (FFT_MIN_CYCLE) : stats.fftTaskCycle / 115;
#else
        unsigned timeBudget = (FFT_MIN_CYCLE);
#endif
        if ((stats.fftTime / 100) >= timeBudget)                  // FFT alone over budget -> I2S buffer will overflow
            infoArr.add(F("<b style=\"color:red;\">! ms</b>"));
        else if ((stats.fftTime / 85 + stats.filterTime / 85 + stats.sampleTime / 85) >= timeBudget) // FFT+filter+sample > 75% budget -> instability risk
            infoArr.add(F("<b style=\"color:orange;\"> ms!</b>"));
        else
            infoArr.add(" ms");

        DEBUGSR_PRINTF("AR I2S cycle time: %5.2f ms\n", roundf(stats.fftTaskCycle) / 100.0f);
        DEBUGSR_PRINTF("AR Sampling time : %5.2f ms\n", roundf(stats.sampleTime) / 100.0f);
        DEBUGSR_PRINTF("AR filter time   : %5.2f ms\n", roundf(stats.filterTime) / 100.0f);
        DEBUGSR_PRINTF("AR FFT time      : %5.2f ms\n", roundf(stats.fftTime) / 100.0f);
#endif
#endif
    }
}

