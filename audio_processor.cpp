/*
   @title     Audio Processor Library
   @file      audio_processor.cpp
   @repo      https://github.com/MoonModules/WLED-MM
   @Authors   Extracted from audio_reactive.h
   @Copyright © 2024,2025 Github MoonModules Commit Authors
   @license   Licensed under the EUPL-1.2 or later
*/

#include "audio_processor.h"
// audio_source.h is only needed when using AudioSource integration with WLED
// For standalone usage, it's not required
// #include "audio_source.h"
#include "audio_filters.h"
#include "agc_controller.h"

#ifdef ARDUINO_ARCH_ESP32
#define sqrt(x) sqrtf(x)        // Optimization for ESP32
#define sqrt_internal sqrtf
#include <arduinoFFT.h>
#endif

// Pink noise frequency response correction tables
#define MAX_PINK 10
static const float fftResultPink[MAX_PINK+1][NUM_GEQ_CHANNELS] = {
    { 1.70f, 1.71f, 1.73f, 1.78f, 1.68f, 1.56f, 1.55f, 1.63f, 1.79f, 1.62f, 1.80f, 2.06f, 2.47f, 3.35f, 6.83f, 9.55f },  //  0 default
    { 2.35f, 1.32f, 1.32f, 1.40f, 1.48f, 1.57f, 1.68f, 1.80f, 1.89f, 1.95f, 2.14f, 2.26f, 2.50f, 2.90f, 4.20f, 6.50f },  //  1 Line-In
    { 1.82f, 1.72f, 1.70f, 1.50f, 1.52f, 1.57f, 1.68f, 1.80f, 1.89f, 2.00f, 2.11f, 2.21f, 2.30f, 2.90f, 3.86f, 6.29f},   //  2 IMNP441
    { 2.80f, 2.20f, 1.30f, 1.15f, 1.55f, 2.45f, 4.20f, 2.80f, 3.20f, 3.60f, 4.20f, 4.90f, 5.70f, 6.05f,10.50f,14.85f},   //  3 IMNP441 bass
    { 12.0f, 6.60f, 2.60f, 1.15f, 1.35f, 2.05f, 2.85f, 2.50f, 2.85f, 3.30f, 2.25f, 4.35f, 3.80f, 3.75f, 6.50f, 9.00f},   //  4 IMNP441 voice
    { 2.75f, 1.60f, 1.40f, 1.46f, 1.52f, 1.57f, 1.68f, 1.80f, 1.89f, 2.00f, 2.11f, 2.21f, 2.30f, 1.75f, 2.55f, 3.60f },  //  5 ICS-43434
    { 2.90f, 1.25f, 0.75f, 1.08f, 2.35f, 3.55f, 3.60f, 3.40f, 2.75f, 3.45f, 4.40f, 6.35f, 6.80f, 6.80f, 8.50f,10.64f },  //  6 ICS-43434 bass
    { 1.65f, 1.00f, 1.05f, 1.30f, 1.48f, 1.30f, 1.80f, 3.00f, 1.50f, 1.65f, 2.56f, 3.00f, 2.60f, 2.30f, 5.00f, 3.00f },  //  7 SPM1423
    { 2.25f, 1.60f, 1.30f, 1.60f, 2.20f, 3.20f, 3.06f, 2.60f, 2.85f, 3.50f, 4.10f, 4.80f, 5.70f, 6.05f,10.50f,14.85f },  //  8 userdef 1
    { 4.75f, 3.60f, 2.40f, 2.46f, 3.52f, 1.60f, 1.68f, 3.20f, 2.20f, 2.00f, 2.30f, 2.41f, 2.30f, 1.25f, 4.55f, 6.50f },  //  9 userdef 2
    { 2.38f, 2.18f, 2.07f, 1.70f, 1.70f, 1.70f, 1.70f, 1.70f, 1.70f, 1.70f, 1.70f, 1.70f, 1.95f, 1.70f, 2.13f, 2.47f }   // 10 flat
};

#define FFT_DOWNSCALE 0.40f
#define LOG_256  5.54517744f

AudioProcessor::AudioProcessor() {
}

AudioProcessor::~AudioProcessor() {
    cleanup();
}

bool AudioProcessor::configure(const Config& config) {
    m_config = config;
    return true;
}

bool AudioProcessor::allocateBuffers() {
    // Allocate FFT buffers
    if (m_vReal) free(m_vReal);
    if (m_vImag) free(m_vImag);

    m_vReal = (float*) calloc(m_config.fftSize, sizeof(float));
    m_vImag = (float*) calloc(m_config.fftSize, sizeof(float));

    if (!m_vReal || !m_vImag) {
        freeBuffers();
        return false;
    }

    // Pink noise factors for human ear perception
    #if defined(ARDUINO_ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)
    if (m_pinkFactors) free(m_pinkFactors);
    m_pinkFactors = (float*) calloc(m_config.fftSize, sizeof(float));
    if (!m_pinkFactors) {
        freeBuffers();
        return false;
    }
    #endif

    // Sliding window buffer (50% overlap)
    if (m_config.useSlidingWindow) {
        if (m_oldSamples) free(m_oldSamples);
        m_oldSamples = (float*) calloc(m_config.fftSize / 2, sizeof(float));
        if (!m_oldSamples) {
            freeBuffers();
            return false;
        }
    }

    return true;
}

void AudioProcessor::freeBuffers() {
    if (m_vReal) { free(m_vReal); m_vReal = nullptr; }
    if (m_vImag) { free(m_vImag); m_vImag = nullptr; }
    if (m_pinkFactors) { free(m_pinkFactors); m_pinkFactors = nullptr; }
    if (m_oldSamples) { free(m_oldSamples); m_oldSamples = nullptr; }
}

bool AudioProcessor::initialize() {
    if (m_initialized) return true;

    if (!allocateBuffers()) {
        return false;
    }

    // Initialize FFT result arrays
    memset(m_fftResult, 0, sizeof(m_fftResult));
    memset(m_fftCalc, 0, sizeof(m_fftCalc));
    memset(m_fftAvg, 0, sizeof(m_fftAvg));

    m_initialized = true;
    return true;
}

void AudioProcessor::setAudioSource(AudioSource* source) {
    m_audioSource = source;
}

void AudioProcessor::setAudioFilters(AudioFilters* filters) {
    m_audioFilters = filters;
}

void AudioProcessor::setAGCController(AGCController* agc) {
    m_agcController = agc;
}

float AudioProcessor::fftAddAvg(int from, int to) {
    if (from == to) return m_vReal[from];

    if (m_config.averageByRMS) {
        // RMS average
        double result = 0.0;
        for (int i = from; i <= to; i++) {
            result += m_vReal[i] * m_vReal[i];
        }
        return sqrtf(result / float(to - from + 1));
    } else {
        // Linear average
        float result = 0.0f;
        for (int i = from; i <= to; i++) {
            result += m_vReal[i];
        }
        return result / float(to - from + 1);
    }
}

void AudioProcessor::computeFrequencyBands(bool noiseGateOpen, bool fastpath) {
    // This is a simplified version - the full implementation would have
    // all the frequency band calculations from FFTcode()
    // For now, we'll provide the structure

    float wc = 1.0f; // Windowing correction

    // Map FFT bins to 16 GEQ channels
    // These mappings are frequency-dependent and tuned for audio visualization
    m_fftCalc[ 0] = wc * fftAddAvg(1,1);     // Sub-bass
    m_fftCalc[ 1] = wc * fftAddAvg(2,2);     // Bass
    m_fftCalc[ 2] = wc * fftAddAvg(3,3);     // Bass
    m_fftCalc[ 3] = wc * fftAddAvg(4,5);     // Bass + midrange
    m_fftCalc[ 4] = wc * fftAddAvg(6,7);     // Midrange
    m_fftCalc[ 5] = wc * fftAddAvg(8,10);    // Midrange
    m_fftCalc[ 6] = wc * fftAddAvg(11,14);   // Midrange
    m_fftCalc[ 7] = wc * fftAddAvg(15,19);   // Upper midrange
    m_fftCalc[ 8] = wc * fftAddAvg(20,25);   // Upper midrange
    m_fftCalc[ 9] = wc * fftAddAvg(26,33);   // Presence
    m_fftCalc[10] = wc * fftAddAvg(34,43);   // Presence
    m_fftCalc[11] = wc * fftAddAvg(44,56);   // High
    m_fftCalc[12] = wc * fftAddAvg(57,73);   // High
    m_fftCalc[13] = wc * fftAddAvg(74,94);   // High
    m_fftCalc[14] = wc * fftAddAvg(95,122);  // Very high
    m_fftCalc[15] = wc * fftAddAvg(123,164) * 0.70f; // Very high (damped)
}

void AudioProcessor::postProcessFFT(bool noiseGateOpen, bool fastpath) {
    // Apply pink noise correction and scaling
    for (int i = 0; i < m_config.numGEQChannels; i++) {
        if (noiseGateOpen) {
            // Apply frequency response correction
            m_fftCalc[i] *= fftResultPink[m_config.pinkIndex][i];

            if (m_config.scalingMode > 0) {
                m_fftCalc[i] *= FFT_DOWNSCALE;
            }

            // Apply AGC or manual gain
            if (m_agcController && m_agcController->isEnabled()) {
                m_fftCalc[i] *= m_agcController->getMultiplier();
            }

            if (m_fftCalc[i] < 0) m_fftCalc[i] = 0;
        }

        // Smoothing/limiting
        float speed = fastpath ? 0.76f : 1.0f;

        if (m_fftCalc[i] > m_fftAvg[i]) {
            // Rise fast
            m_fftAvg[i] += speed * 0.78f * (m_fftCalc[i] - m_fftAvg[i]);
        } else {
            // Fall with configurable decay
            m_fftAvg[i] += speed * 0.22f * (m_fftCalc[i] - m_fftAvg[i]);
        }

        // Constrain values
        m_fftCalc[i] = constrain(m_fftCalc[i], 0.0f, 1023.0f);
        m_fftAvg[i] = constrain(m_fftAvg[i], 0.0f, 1023.0f);

        // Apply scaling mode
        float currentResult = m_fftAvg[i];

        switch (m_config.scalingMode) {
            case 1: // Logarithmic
                currentResult *= 0.42f;
                currentResult -= 8.0f;
                if (currentResult > 1.0f) currentResult = logf(currentResult);
                else currentResult = 0.0f;
                currentResult *= 0.85f + (float(i)/18.0f);
                currentResult = map(currentResult, 0.0f, LOG_256, 0.0f, 255.0f);
                break;

            case 2: // Linear
                currentResult *= 0.30f;
                currentResult -= 2.0f;
                if (currentResult < 1.0f) currentResult = 0.0f;
                currentResult *= 0.85f + (float(i)/1.8f);
                break;

            case 3: // Square root
                currentResult *= 0.38f;
                currentResult -= 6.0f;
                if (currentResult > 1.0f) currentResult = sqrtf(currentResult);
                else currentResult = 0.0f;
                currentResult *= 0.85f + (float(i)/4.5f);
                currentResult = map(currentResult, 0.0f, 16.0f, 0.0f, 255.0f);
                break;
        }

        // Store final result
        m_fftResult[i] = constrain((int)currentResult, 0, 255);
    }
}

void AudioProcessor::detectPeak() {
    // Simple peak detection based on volume increase
    // Full implementation would use FFT magnitude
    if (m_agcController) {
        float currentVolume = m_agcController->getSampleAGC();
        static float lastVolume = 0;

        if ((currentVolume > lastVolume * 1.3f) && (currentVolume > 50) &&
            (millis() - m_timeOfPeak > 80)) {
            m_samplePeak = true;
            m_timeOfPeak = millis();
        }

        lastVolume = currentVolume;
    }
}

void AudioProcessor::autoResetPeak(uint16_t minShowDelay) {
    if (millis() - m_timeOfPeak > minShowDelay) {
        m_samplePeak = false;
    }
}

void AudioProcessor::processSamples(float* samples, size_t count) {
    if (!m_initialized || !samples || count == 0) return;

    // This is a simplified version
    // Full implementation would:
    // 1. Copy samples to FFT buffer
    // 2. Apply windowing function
    // 3. Run FFT
    // 4. Calculate magnitude
    // 5. Find major peak frequency
    // 6. Compute frequency bands
    // 7. Post-process results

    // For now, just compute frequency bands from existing FFT results
    bool noiseGateOpen = m_agcController ? (m_agcController->getSampleAGC() > 10) : true;
    computeFrequencyBands(noiseGateOpen, false);
    postProcessFFT(noiseGateOpen, false);
    detectPeak();

    // Update volume tracking
    if (m_agcController) {
        m_volumeSmth = m_agcController->getSampleAGC();
        m_volumeRaw = m_agcController->getSampleRaw();
    }
}

#ifdef ARDUINO_ARCH_ESP32
bool AudioProcessor::startTask(uint8_t priority, int8_t core) {
    if (m_taskHandle != nullptr) {
        return false; // Already running
    }

    if (!m_initialized) {
        if (!initialize()) {
            return false;
        }
    }

    BaseType_t result;
    if (core >= 0) {
        result = xTaskCreatePinnedToCore(
            fftTaskWrapper,
            "FFT",
            3592,           // Stack size in words
            this,           // Parameter
            priority,       // Priority
            &m_taskHandle,  // Task handle
            core            // Core
        );
    } else {
        result = xTaskCreate(
            fftTaskWrapper,
            "FFT",
            3592,
            this,
            priority,
            &m_taskHandle
        );
    }

    return (result == pdPASS);
}

void AudioProcessor::stopTask() {
    if (m_taskHandle != nullptr) {
        vTaskDelete(m_taskHandle);
        m_taskHandle = nullptr;
    }
}

void AudioProcessor::fftTaskWrapper(void* parameter) {
    AudioProcessor* processor = static_cast<AudioProcessor*>(parameter);
    processor->fftTask();
}

void AudioProcessor::fftTask() {
    // This would contain the full FFT processing loop from FFTcode()
    // For now, this is a placeholder
    const TickType_t xFrequency = m_config.minCycle * portTICK_PERIOD_MS;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (true) {
        // Read samples from audio source
        // Apply filters
        // Process through AGC
        // Run FFT
        // Calculate bands
        // Detect peaks

        // Sleep until next cycle
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
#endif

void AudioProcessor::cleanup() {
#ifdef ARDUINO_ARCH_ESP32
    stopTask();
#endif
    freeBuffers();
    m_initialized = false;
}

