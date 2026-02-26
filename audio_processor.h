#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

/*
   @title     Audio Processor Library
   @file      audio_processor.h
   @repo      https://github.com/MoonModules/WLED-MM
   @Authors   Extracted from audio_reactive.h
   @Copyright © 2024,2025 Github MoonModules Commit Authors
   @license   Licensed under the EUPL-1.2 or later

   FFT audio processing, frequency band calculation, and peak detection
*/

#include <Arduino.h>

// Forward declarations (actual headers not needed for standalone mode)
class AudioSource;  // Defined in audio_source.h (WLED dependency)
class AudioFilters;
class AGCController;

#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#ifndef NUM_GEQ_CHANNELS
#define NUM_GEQ_CHANNELS 16
#endif

/**
 * @brief Audio processor with FFT analysis
 *
 * Performs FFT analysis on audio samples, calculates frequency bands (GEQ),
 * detects peaks, and manages audio processing task on ESP32.
 */
class AudioProcessor {
public:
    /**
     * @brief FFT configuration
     */
    struct Config {
        uint16_t sampleRate = 18000;           // Sample rate in Hz
        uint16_t fftSize = 512;                // FFT size (must be power of 2)
        uint8_t numGEQChannels = 16;           // Number of frequency bands
        uint8_t scalingMode = 3;               // 0=none, 1=log, 2=linear, 3=sqrt
        uint8_t pinkIndex = 0;                 // Pink noise profile: 0=default, 1=line-in, 2=IMNP441
        bool useSlidingWindow = true;          // Use 50% overlapping FFT windows
        uint8_t fftWindow = 0;                 // FFT windowing function
        uint8_t freqDist = 0;                  // Frequency distribution mode
        bool averageByRMS = true;              // Use RMS vs linear averaging
        uint8_t minCycle = 25;                 // Minimum ms between FFT cycles
        uint8_t inputLevel = 128;              // Input level (0-255)
        uint8_t sampleGain = 60;               // Sample gain (0-255)
        bool limiterOn = true;                 // Enable limiter/smoothing
        uint16_t decayTime = 2000;             // Decay time in ms
        uint8_t useInputFilter = 0;            // 0=none, 1=bandpass, 2=DC blocker
    };

    /**
     * @brief Constructor
     */
    AudioProcessor();

    /**
     * @brief Destructor
     */
    ~AudioProcessor();

    /**
     * @brief Configure the audio processor
     * @param config Processor configuration
     * @return true if successful
     */
    bool configure(const Config& config);

    /**
     * @brief Initialize FFT buffers and resources
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Set audio source for processing
     * @param source Pointer to audio source
     */
    void setAudioSource(AudioSource* source);

    /**
     * @brief Set audio filters for preprocessing
     * @param filters Pointer to audio filters
     */
    void setAudioFilters(AudioFilters* filters);

    /**
     * @brief Set AGC controller for gain management
     * @param agc Pointer to AGC controller
     */
    void setAGCController(AGCController* agc);

#ifdef ARDUINO_ARCH_ESP32
    /**
     * @brief Start FFT processing task (ESP32 only)
     * @param priority Task priority (1-4)
     * @param core CPU core to pin task to (-1 for any)
     * @return true if task started successfully
     */
    bool startTask(uint8_t priority = 1, int8_t core = 0);

    /**
     * @brief Stop FFT processing task
     */
    void stopTask();

    /**
     * @brief Check if processing task is running
     * @return true if task is active
     */
    bool isTaskRunning() const { return m_taskHandle != nullptr; }
#endif

    /**
     * @brief Process a batch of samples (non-ESP32 or manual mode)
     * @param samples Sample buffer
     * @param count Number of samples
     */
    void processSamples(float* samples, size_t count);

    /**
     * @brief Get FFT result for frequency bands
     * @return Pointer to GEQ channel array [NUM_GEQ_CHANNELS]
     */
    const uint8_t* getFFTResult() const { return m_fftResult; }

    /**
     * @brief Get major peak frequency
     * @return Frequency in Hz
     */
    float getMajorPeak() const { return m_fftMajorPeak; }

    /**
     * @brief Get FFT magnitude
     * @return Magnitude value
     */
    float getMagnitude() const { return m_fftMagnitude; }

    /**
     * @brief Check if sample peak detected
     * @return true if peak detected
     */
    bool getSamplePeak() const { return m_samplePeak; }

    /**
     * @brief Get smooth volume
     * @return Volume (0-255)
     */
    float getVolumeSmooth() const { return m_volumeSmth; }

    /**
     * @brief Get raw volume
     * @return Volume (0-65535)
     */
    uint16_t getVolumeRaw() const { return m_volumeRaw; }

    /**
     * @brief Get zero crossing count
     * @return Count of zero crossings in last batch
     */
    uint16_t getZeroCrossingCount() const { return m_zeroCrossingCount; }

    /**
     * @brief Reset sample peak flag
     */
    void resetSamplePeak() { m_samplePeak = false; }

    /**
     * @brief Auto-reset peak based on timing
     * @param minShowDelay Minimum delay in ms
     */
    void autoResetPeak(uint16_t minShowDelay);

    /**
     * @brief Limit sample dynamics (attack/decay envelope for volume)
     * @param volumeSmth Reference to volume value to be limited
     */
    void limitSampleDynamics(float& volumeSmth);

    /**
     * @brief Limit GEQ dynamics (attack/decay envelope for frequency channels)
     * @param gotNewSample True if new FFT samples are available
     */
    void limitGEQDynamics(bool gotNewSample);

    /**
     * @brief Get statistics (for debugging)
     */
    struct Stats {
        float fftTaskCycle;   // Average FFT task cycle time
        float fftTime;        // Average FFT computation time
        float sampleTime;     // Average sample read time
    };
    const Stats& getStats() const { return m_stats; }

    /**
     * @brief Cleanup and free resources
     */
    void cleanup();

private:
    // Configuration
    Config m_config;
    bool m_initialized = false;

    // Component references
    AudioSource* m_audioSource = nullptr;
    AudioFilters* m_audioFilters = nullptr;
    AGCController* m_agcController = nullptr;

    // FFT buffers
    float* m_vReal = nullptr;
    float* m_vImag = nullptr;
    float* m_pinkFactors = nullptr;
    float* m_oldSamples = nullptr;  // For sliding window

    // FFT results
    uint8_t m_fftResult[NUM_GEQ_CHANNELS] = {0};
    float m_fftCalc[NUM_GEQ_CHANNELS] = {0};
    float m_fftAvg[NUM_GEQ_CHANNELS] = {0};

    // Peak detection
    float m_fftMajorPeak = 1.0f;
    float m_fftMagnitude = 0.0f;
    bool m_samplePeak = false;
    unsigned long m_timeOfPeak = 0;

    // Volume tracking
    float m_volumeSmth = 0.0f;
    uint16_t m_volumeRaw = 0;
    uint16_t m_zeroCrossingCount = 0;

    // Statistics
    Stats m_stats = {0};

    // Dynamics limiter state
    unsigned long m_lastDynamicsTime = 0;
    float m_lastVolumeSmth = 0.0f;
    unsigned long m_lastGEQDynamicsTime = 0;

#ifdef ARDUINO_ARCH_ESP32
    // FreeRTOS task
    TaskHandle_t m_taskHandle = nullptr;
    static void fftTaskWrapper(void* parameter);
    void fftTask();
#endif

    // Internal methods
    bool allocateBuffers();
    void freeBuffers();
    float fftAddAvg(int from, int to);
    void computeFrequencyBands(bool noiseGateOpen, bool fastpath, float wc);
    void detectPeak();
    void postProcessFFT(bool noiseGateOpen, bool fastpath);
};

#endif // audio_processor.h

