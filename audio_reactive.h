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

// Include our object-oriented audio processing libraries
#include "audio_filters.h"
#include "agc_controller.h"
#include "audio_processor.h"
#include "audio_source.h"
#include "audio_sync.h"

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

// Debug macros
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

static volatile bool disableSoundProcessing = false;
static uint8_t audioSyncEnabled = AUDIOSYNC_NONE;
static bool audioSyncSequence = true;
static uint8_t audioSyncPurge = 1;
static bool udpSyncConnected = false;

// Sample rate and block size
#if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)
  constexpr uint16_t SAMPLE_RATE = 22050;
  constexpr uint16_t BLOCK_SIZE = 128;
#else
  constexpr uint16_t SAMPLE_RATE = 18000;
  constexpr uint16_t BLOCK_SIZE = 128;
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
// These now proxy to the library instances
static uint8_t fftResult[NUM_GEQ_CHANNELS] = {0};  // Updated by AudioProcessor
static float FFT_MajorPeak = 1.0f;                 // Updated by AudioProcessor
static float FFT_Magnitude = 0.0f;                 // Updated by AudioProcessor
static bool samplePeak = false;                    // Updated by AudioProcessor
static float volumeSmth = 0.0f;                    // Updated by AGCController
static uint16_t volumeRaw = 0;                     // Updated by AGCController
static uint16_t zeroCrossingCount = 0;             // Updated by AudioProcessor
static float soundPressure = 0.0f;                 // Updated by AGCController
static float agcSensitivity = 128.0f;              // Updated by AGCController


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

    // Limiter settings
    bool limiterOn = true;
    uint16_t attackTime = 80;
    uint16_t decayTime = 1400;

    // Info page data
    unsigned long last_UDPTime = 0;
    int receivedFormat = 0;
    float maxSample5sec = 0.0f;
    unsigned long sampleMaxTimer = 0;
    #define CYCLE_SAMPLEMAX 3500

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
    void limitSampleDynamics();
    void limitGEQDynamics(bool gotNewSample);
    void createAudioSource();


  public:
    // Usermod interface
    void setup() override;
    void loop() override;
    void connected() override;
    void onStateChange(uint8_t mode) override;
    void onUpdateBegin(bool init) override;
    void addToJsonInfo(JsonObject& root) override;
    void addToJsonState(JsonObject& root) override;
    void readFromJsonState(JsonObject& root) override;
    void addToConfig(JsonObject& root) override;
    bool readFromConfig(JsonObject& root) override;
    void handleButton(uint8_t b) override;
    um_data_t* getUMData() override;
    uint16_t getId() override { return USERMOD_ID_AUDIOREACTIVE; }
};

////////////////////
// Implementation //
////////////////////

// Configure all audio processing libraries based on current settings
inline void AudioReactive::configureAudioLibraries() {
    // Configure AudioFilters
    AudioFilters::Config filterConfig;
    filterConfig.filterMode = 2;  // Default: DC blocker
    if (dmType == 5 || dmType == 51) {
        filterConfig.filterMode = 1;  // PDM bandpass
    }
    filterConfig.micQuality = micQuality;
    audioFilters.configure(filterConfig);

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
    procConfig.minCycle = 25;
    procConfig.inputLevel = inputLevel;
    procConfig.sampleGain = sampleGain;
    procConfig.limiterOn = limiterOn;
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
    // Copy from AudioProcessor
    const uint8_t* procFFT = audioProcessor.getFFTResult();
    memcpy(fftResult, procFFT, NUM_GEQ_CHANNELS);
    FFT_MajorPeak = audioProcessor.getMajorPeak();
    FFT_Magnitude = audioProcessor.getMagnitude();
    samplePeak = audioProcessor.getSamplePeak();
    zeroCrossingCount = audioProcessor.getZeroCrossingCount();

    // Copy from AGCController
    volumeSmth = agcController.getSampleAGC();
    volumeRaw = agcController.getSampleRaw();
    soundPressure = agcController.estimatePressure(agcController.getSampleReal(), dmType);
    agcSensitivity = agcController.getSensitivity();
}

// Create audio source based on dmType
inline void AudioReactive::createAudioSource() {
    #ifdef ARDUINO_ARCH_ESP32
    if (audioSource) {
        delete audioSource;
        audioSource = nullptr;
    }

    switch (dmType) {
        case 1: // Generic I2S
            DEBUGSR_PRINTLN(F("AR: Generic I2S Microphone"));
            audioSource = new I2SSource(SAMPLE_RATE, BLOCK_SIZE);
            if (audioSource) audioSource->initialize(i2swsPin, i2ssdPin, i2sckPin);
            break;

        case 2: // ES7243
            DEBUGSR_PRINTLN(F("AR: ES7243 Microphone"));
            audioSource = new ES7243(SAMPLE_RATE, BLOCK_SIZE);
            if (audioSource) audioSource->initialize(i2swsPin, i2ssdPin, i2sckPin, mclkPin);
            break;

        case 3: // SPH0645
            DEBUGSR_PRINTLN(F("AR: SPH0645 Microphone"));
            audioSource = new SPH0654(SAMPLE_RATE, BLOCK_SIZE);
            if (audioSource) audioSource->initialize(i2swsPin, i2ssdPin, i2sckPin);
            break;

        case 4: // I2S with MCLK
            DEBUGSR_PRINTLN(F("AR: I2S Microphone with MCLK"));
            audioSource = new I2SSource(SAMPLE_RATE, BLOCK_SIZE, 1.0f/24.0f);
            if (audioSource) audioSource->initialize(i2swsPin, i2ssdPin, i2sckPin, mclkPin);
            break;

        #if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)
        case 5: // PDM
            DEBUGSR_PRINTLN(F("AR: I2S PDM Microphone"));
            audioSource = new I2SSource(SAMPLE_RATE, BLOCK_SIZE, 1.0f/4.0f);
            if (audioSource) audioSource->initialize(i2swsPin, i2ssdPin);
            break;

        case 51: // Legacy PDM
            DEBUGSR_PRINTLN(F("AR: Legacy PDM Microphone"));
            audioSource = new I2SSource(SAMPLE_RATE, BLOCK_SIZE, 1.0f);
            if (audioSource) audioSource->initialize(i2swsPin, i2ssdPin);
            break;
        #endif

        case 6: // ES8388
            DEBUGSR_PRINTLN(F("AR: ES8388 Source"));
            audioSource = new ES8388Source(SAMPLE_RATE, BLOCK_SIZE, 1.0f);
            if (audioSource) audioSource->initialize(i2swsPin, i2ssdPin, i2sckPin, mclkPin);
            break;

        case 7: // WM8978
            DEBUGSR_PRINTLN(F("AR: WM8978 Source"));
            audioSource = new WM8978Source(SAMPLE_RATE, BLOCK_SIZE, 1.0f);
            if (audioSource) audioSource->initialize(i2swsPin, i2ssdPin, i2sckPin, mclkPin);
            break;

        case 8: // AC101
            DEBUGSR_PRINTLN(F("AR: AC101 Source"));
            audioSource = new AC101Source(SAMPLE_RATE, BLOCK_SIZE, 1.0f);
            if (audioSource) audioSource->initialize(i2swsPin, i2ssdPin, i2sckPin, mclkPin);
            break;

        case 9: // ES8311
            DEBUGSR_PRINTLN(F("AR: ES8311 Source"));
            audioSource = new ES8311Source(SAMPLE_RATE, BLOCK_SIZE, 1.0f);
            if (audioSource) audioSource->initialize(i2swsPin, i2ssdPin, i2sckPin, mclkPin);
            break;

        case 254: // Network receive only
        case 255:
            DEBUGSR_PRINTLN(F("AR: Network receive only"));
            break;

        default:
            DEBUGSR_PRINTLN(F("AR: Unknown audio source type"));
            break;
    }
    #endif
}

// Setup method - initialize everything
inline void AudioReactive::setup() {
    disableSoundProcessing = true;

    if (!initDone) {
        // Setup usermod data exchange
        um_data = new um_data_t;
        um_data->u_size = 12;
        um_data->u_type = new um_types_t[um_data->u_size];
        um_data->u_data = new void*[um_data->u_size];
        um_data->u_data[0] = &volumeSmth;
        um_data->u_type[0] = UMT_FLOAT;
        um_data->u_data[1] = &volumeRaw;
        um_data->u_type[1] = UMT_UINT16;
        um_data->u_data[2] = fftResult;
        um_data->u_type[2] = UMT_BYTE_ARR;
        um_data->u_data[3] = &samplePeak;
        um_data->u_type[3] = UMT_BYTE;
        um_data->u_data[4] = &FFT_MajorPeak;
        um_data->u_type[4] = UMT_FLOAT;
        um_data->u_data[5] = &FFT_Magnitude;
        um_data->u_type[5] = UMT_FLOAT;
        um_data->u_data[6] = nullptr; // maxVol - assigned by effects
        um_data->u_type[6] = UMT_BYTE;
        um_data->u_data[7] = nullptr; // binNum - assigned by effects
        um_data->u_type[7] = UMT_BYTE;
        um_data->u_data[8] = &FFT_MajorPeak;
        um_data->u_type[8] = UMT_FLOAT;
        um_data->u_data[9] = &soundPressure;
        um_data->u_type[9] = UMT_FLOAT;
        um_data->u_data[10] = &agcSensitivity;
        um_data->u_type[10] = UMT_FLOAT;
        um_data->u_data[11] = &zeroCrossingCount;
        um_data->u_type[11] = UMT_UINT16;
    }

    #ifdef ARDUINO_ARCH_ESP32
    delay(100); // Let microphone settle

    // Create audio source
    createAudioSource();

    // Configure audio libraries
    configureAudioLibraries();

    // Initialize audio processor
    if (!audioProcessor.initialize()) {
        ERRORSR_PRINTLN(F("AR: Failed to initialize AudioProcessor"));
        disableSoundProcessing = true;
        return;
    }

    // Start FFT task if we have an audio source
    if (audioSource && audioSyncEnabled != AUDIOSYNC_REC) {
        if (audioProcessor.startTask(FFTTASK_PRIORITY, 0)) {
            DEBUGSR_PRINTLN(F("AR: FFT task started"));
            disableSoundProcessing = false;
        } else {
            ERRORSR_PRINTLN(F("AR: Failed to start FFT task"));
            disableSoundProcessing = true;
        }
    }
    #endif

    initDone = true;
}

// Loop method - update global variables and handle UDP
inline void AudioReactive::loop() {
    if (!enabled || updateIsRunning) return;

    // Auto-reset peak detection
    autoResetPeak();

    // Handle UDP audio sync reception
    if (audioSyncEnabled & AUDIOSYNC_REC) {
        bool gotNewData = audioSync.receive(audioSyncPurge > 0 ? audioSyncPurge : 1);

        if (gotNewData) {
            last_UDPTime = millis();

            // Copy received data to global variables
            const AudioSync::ReceivedData& rxData = audioSync.getReceivedData();
            volumeSmth = rxData.volumeSmth;
            volumeRaw = rxData.volumeRaw;
            samplePeak = rxData.samplePeak;
            memcpy(fftResult, rxData.fftResult, NUM_GEQ_CHANNELS);
            FFT_Magnitude = rxData.fftMagnitude;
            FFT_MajorPeak = rxData.fftMajorPeak;
            zeroCrossingCount = rxData.zeroCrossingCount;
            soundPressure = rxData.soundPressure;
            agcSensitivity = rxData.agcSensitivity;
            receivedFormat = rxData.receivedFormat;

            // Apply GEQ dynamics limiting if enabled
            if (limiterOn) {
                limitGEQDynamics(true);
            }
        }

        // Check for timeout
        if (audioSync.hasTimedOut(AUDIOSYNC_IDLE_MS)) {
            // Timeout - enable local processing if configured
            if ((audioSyncEnabled & AUDIOSYNC_REC_PLUS) && audioSource) {
                disableSoundProcessing = false;
            }
        } else {
            disableSoundProcessing = true; // Disable local while receiving
        }
    }

    // Update global variables from library instances (local processing)
    if (!disableSoundProcessing) {
        updateGlobalVariables();
    }

    // Transmit UDP if enabled
    if ((audioSyncEnabled & AUDIOSYNC_SEND) && !disableSoundProcessing) {
        unsigned long now = millis();
        if (now - lastTime > delayMs) {
            audioSync.transmit(
                volumeRaw,
                volumeSmth,
                samplePeak,
                fftResult,
                zeroCrossingCount,
                FFT_Magnitude,
                FFT_MajorPeak,
                soundPressure
            );
            lastTime = now;
        }
    }

    // Apply sample dynamics limiting
    if (limiterOn && !disableSoundProcessing) {
        audioProcessor.limitSampleDynamics(volumeSmth);
    }
}

// The rest of the methods use library getters

inline void AudioReactive::connected() {
    if (audioSyncEnabled != AUDIOSYNC_NONE) {
        if (audioSync.begin()) {
            udpSyncConnected = true;
            DEBUGSR_PRINTLN(F("AR: UDP audio sync connected"));
        } else {
            udpSyncConnected = false;
            DEBUGSR_PRINTLN(F("AR: Failed to connect UDP audio sync"));
        }
    }
}

inline void AudioReactive::onUpdateBegin(bool init) {
    updateIsRunning = true;
    #ifdef ARDUINO_ARCH_ESP32
    audioProcessor.stopTask();
    #endif
}

inline um_data_t* AudioReactive::getUMData() {
    return um_data;
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

#endif // audio_reactive.h

