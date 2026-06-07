/*
   @title     Audio Processor Library
   @file      audio_processor.cpp
   @repo      https://github.com/MoonModules/WLED-MM
   @Authors   Extracted from audio_reactive.h
   @Copyright © 2024,2025 Github MoonModules Commit Authors
   @license   Licensed under the EUPL-1.2 or later
*/

#include "audio_processor.h"
#include "audio_filters.h"
#include "agc_controller.h"

#ifdef ARDUINO_ARCH_ESP32
#define sqrt(x) sqrtf(x)        // Optimization for ESP32
#define sqrt_internal sqrtf
#include <arduinoFFT.h>
#if defined(WLED_DEBUG) || defined(SR_DEBUG) || defined(SR_STATS)
#include <esp_timer.h>
#endif
#endif

// Helper function for float mapping
static inline float mapf(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

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

void AudioProcessor::setAudioSource(ISampleSource* source) {
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

void AudioProcessor::computeFrequencyBands(bool noiseGateOpen, bool fastpath, float wc) {
    // Mapping of FFT result bins to frequency channels
    if (noiseGateOpen) {
        if (m_config.freqDist == 0) {
            /* new mapping, optimized for 22050 Hz by softhack007 --- update: removed overlap */
            if (m_config.useInputFilter == 1) {
                // skip frequencies below 100hz
                m_fftCalc[ 0] = wc * 0.8f * fftAddAvg(3,3);
                m_fftCalc[ 1] = wc * 0.9f * fftAddAvg(4,4);
                m_fftCalc[ 2] = wc * fftAddAvg(5,5);
                m_fftCalc[ 3] = wc * fftAddAvg(6,6);
                // don't use the last bins from 206 to 255.
                m_fftCalc[15] = wc * fftAddAvg(165,205) * 0.75f;
            } else {
                m_fftCalc[ 0] = wc * fftAddAvg(1,1);               // 1    43 - 86   sub-bass
                m_fftCalc[ 1] = wc * fftAddAvg(2,2);               // 1    86 - 129  bass
                m_fftCalc[ 2] = wc * fftAddAvg(3,4);               // 2   129 - 216  bass
                m_fftCalc[ 3] = wc * fftAddAvg(5,6);               // 2   216 - 301  bass + midrange
                // don't use the last bins from 216 to 255. They are usually contaminated by aliasing (aka noise)
                m_fftCalc[15] = wc * fftAddAvg(165,215) * 0.70f;   // 50 7106 - 9259 high
            }
            m_fftCalc[ 4] = wc * fftAddAvg(7,9);                 // 3   301 - 430  midrange
            m_fftCalc[ 5] = wc * fftAddAvg(10,12);               // 3   430 - 560  midrange
            m_fftCalc[ 6] = wc * fftAddAvg(13,18);               // 5   560 - 818  midrange
            m_fftCalc[ 7] = wc * fftAddAvg(19,25);               // 7   818 - 1120 midrange -- 1Khz should always be the center !
            m_fftCalc[ 8] = wc * fftAddAvg(26,32);               // 7  1120 - 1421 midrange
            m_fftCalc[ 9] = wc * fftAddAvg(33,43);               // 9  1421 - 1895 midrange
            m_fftCalc[10] = wc * fftAddAvg(44,55);               // 12 1895 - 2412 midrange + high mid
            m_fftCalc[11] = wc * fftAddAvg(56,69);               // 14 2412 - 3015 high mid
            m_fftCalc[12] = wc * fftAddAvg(70,85);               // 16 3015 - 3704 high mid
            m_fftCalc[13] = wc * fftAddAvg(86,103);              // 18 3704 - 4479 high mid
            m_fftCalc[14] = wc * fftAddAvg(104,164) * 0.88f;     // 61 4479 - 7106 high mid + high
        } else if (m_config.freqDist == 1) { // Rightshift
            if (m_config.useInputFilter == 1) {
                // skip frequencies below 100hz
                m_fftCalc[ 0] = wc * 0.8f * fftAddAvg(1,1);
                m_fftCalc[ 1] = wc * 0.9f * fftAddAvg(2,2);
                m_fftCalc[ 2] = wc * fftAddAvg(3,3);
                m_fftCalc[ 3] = wc * fftAddAvg(4,4);
                m_fftCalc[15] = wc * fftAddAvg(165,205) * 0.75f;
            } else {
                m_fftCalc[ 0] = wc * fftAddAvg(1,1);
                m_fftCalc[ 1] = wc * fftAddAvg(2,2);
                m_fftCalc[ 2] = wc * fftAddAvg(3,3);
                m_fftCalc[ 3] = wc * fftAddAvg(4,4);
                m_fftCalc[15] = wc * fftAddAvg(165,215) * 0.70f;
            }
            m_fftCalc[ 4] = wc * fftAddAvg(5,6);
            m_fftCalc[ 5] = wc * fftAddAvg(7,8);
            m_fftCalc[ 6] = wc * fftAddAvg(9,10);
            m_fftCalc[ 7] = wc * fftAddAvg(11,13);
            m_fftCalc[ 8] = wc * fftAddAvg(14,18);
            m_fftCalc[ 9] = wc * fftAddAvg(19,25);
            m_fftCalc[10] = wc * fftAddAvg(26,36);
            m_fftCalc[11] = wc * fftAddAvg(37,45);
            m_fftCalc[12] = wc * fftAddAvg(46,66);
            m_fftCalc[13] = wc * fftAddAvg(67,97);
            m_fftCalc[14] = wc * fftAddAvg(98,164) * 0.88f;
        }
    } else {
        // noise gate closed - just decay old values
        for (int i=0; i < m_config.numGEQChannels; i++) {
            m_fftCalc[i] *= 0.85f;  // decay to zero
            if (m_fftCalc[i] < 4.0f) m_fftCalc[i] = 0.0f;
        }
    }
}

void AudioProcessor::postProcessFFT(bool noiseGateOpen, bool fastpath) {
    // Post-processing of frequency channels (pink noise adjustment, AGC, smoothing, scaling)
    for (int i = 0; i < m_config.numGEQChannels; i++) {
        if (noiseGateOpen) {
            // Adjustment for frequency curves
            m_fftCalc[i] *= fftResultPink[m_config.pinkIndex][i];

            if (m_config.scalingMode > 0) {
                m_fftCalc[i] *= FFT_DOWNSCALE;  // adjustment related to FFT windowing function
            }

            // Manual linear adjustment of gain using sampleGain adjustment for different input types
            if (m_agcController && m_agcController->isEnabled()) {
                m_fftCalc[i] *= m_agcController->getMultiplier();
            } else {
                // Manual gain: sampleGain/40.0 * inputLevel/128.0 + 1.0/16.0
                m_fftCalc[i] *= ((float)m_config.sampleGain/40.0f * (float)m_config.inputLevel/128.0f + 1.0f/16.0f);
            }

            if (m_fftCalc[i] < 0) m_fftCalc[i] = 0;
        }

        // Filter correction for sampling speed
        float speed = 1.0f;  // normal mode (43hz)
        if (fastpath) speed = 0.6931471805599453094f * 1.1f;  // ln(2) * 1.1 in fast mode (86hz)

        if (m_config.limiterOn) {
            // Limiter ON -> smooth results
            if (m_fftCalc[i] > m_fftAvg[i]) {  // rise fast
                m_fftAvg[i] += speed * 0.78f * (m_fftCalc[i] - m_fftAvg[i]);
            } else {  // fall slow - configurable decay time
                if (m_config.decayTime < 150)       m_fftAvg[i] += speed * 0.50f * (m_fftCalc[i] - m_fftAvg[i]);
                else if (m_config.decayTime < 250)  m_fftAvg[i] += speed * 0.40f * (m_fftCalc[i] - m_fftAvg[i]);
                else if (m_config.decayTime < 500)  m_fftAvg[i] += speed * 0.33f * (m_fftCalc[i] - m_fftAvg[i]);
                else if (m_config.decayTime < 1000) m_fftAvg[i] += speed * 0.22f * (m_fftCalc[i] - m_fftAvg[i]);
                else if (m_config.decayTime < 2000) m_fftAvg[i] += speed * 0.17f * (m_fftCalc[i] - m_fftAvg[i]);  // default
                else if (m_config.decayTime < 3000) m_fftAvg[i] += speed * 0.14f * (m_fftCalc[i] - m_fftAvg[i]);
                else if (m_config.decayTime < 4000) m_fftAvg[i] += speed * 0.10f * (m_fftCalc[i] - m_fftAvg[i]);
                else m_fftAvg[i] += speed * 0.05f * (m_fftCalc[i] - m_fftAvg[i]);
            }
        } else {
            // Limiter OFF
            if (fastpath) {
                // fast mode -> average last two results
                float tmp = m_fftCalc[i];
                m_fftCalc[i] = 0.7f * tmp + 0.3f * m_fftAvg[i];
                m_fftAvg[i] = tmp; // store current sample for next run
            } else {
                // normal mode -> no adjustments
                m_fftAvg[i] = m_fftCalc[i]; // keep filters up-to-date
            }
        }

        // Constrain internal vars
        m_fftCalc[i] = constrain(m_fftCalc[i], 0.0f, 1023.0f);
        m_fftAvg[i] = constrain(m_fftAvg[i], 0.0f, 1023.0f);

        // Continue with filtered result (limiter on) or unfiltered result (limiter off)
        float currentResult = m_config.limiterOn ? m_fftAvg[i] : m_fftCalc[i];

        switch (m_config.scalingMode) {
            case 1:
                // Logarithmic scaling
                currentResult *= 0.42f;                     // 42 is the answer ;-)
                currentResult -= 8.0f;                      // skip the lowest row
                if (currentResult > 1.0f) currentResult = logf(currentResult);
                else currentResult = 0.0f;
                currentResult *= 0.85f + (float(i)/18.0f);  // extra up-scaling for high frequencies
                currentResult = mapf(currentResult, 0.0f, LOG_256, 0.0f, 255.0f);
                break;
            case 2:
                // Linear scaling
                currentResult *= 0.30f;
                currentResult -= 2.0f;
                if (currentResult < 1.0f) currentResult = 0.0f;
                currentResult *= 0.85f + (float(i)/1.8f);
                break;
            case 3:
                // Square root scaling
                currentResult *= 0.38f;
                currentResult -= 6.0f;
                if (currentResult > 1.0f) currentResult = sqrtf(currentResult);
                else currentResult = 0.0f;
                currentResult *= 0.85f + (float(i)/4.5f);
                currentResult = mapf(currentResult, 0.0f, 16.0f, 0.0f, 255.0f);
                break;
            case 0:
            default:
                // no scaling - leave freq bins as-is
                currentResult -= 2.0f; // just a bit more room for peaks
                break;
        }

        // Apply extra "GEQ Gain" if AGC is enabled
        if (m_agcController && m_agcController->isEnabled()) {
            float post_gain = (float)m_config.inputLevel/128.0f;
            if (post_gain < 1.0f) post_gain = ((post_gain -1.0f) * 0.8f) +1.0f;
            currentResult *= post_gain;
        }

        // Store final result with proper rounding
        m_fftResult[i] = constrain((int)(currentResult + 0.5f), 0, 255);
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

void AudioProcessor::limitSampleDynamics(float& volumeSmth) {
    if (!m_config.limiterOn) return;

    constexpr float bigChange = 196.0f;  // Representative large sample value

    unsigned long currentTime = millis();
    long delta_time = currentTime - m_lastDynamicsTime;
    delta_time = constrain(delta_time, 1, 1000);  // 1ms to 1000ms

    float deltaSample = volumeSmth - m_lastVolumeSmth;

    // Attack limiting (matches main:1625-1628)
    if (m_config.attackTime > 0) {
        float maxAttack = bigChange * float(delta_time) / float(m_config.attackTime);
        if (deltaSample > maxAttack) deltaSample = maxAttack;
    }

    // Decay limiting (matches main:1629-1632)
    if (m_config.decayTime > 0) {
        float maxDecay = -bigChange * float(delta_time) / float(m_config.decayTime);
        if (deltaSample < maxDecay) deltaSample = maxDecay;
    }

    volumeSmth = m_lastVolumeSmth + deltaSample;

    m_lastVolumeSmth = volumeSmth;
    m_lastDynamicsTime = currentTime;
}

void AudioProcessor::limitGEQDynamics(bool gotNewSample) {
    if (!m_config.limiterOn) return;

    constexpr float bigChange = 202.0f;
    constexpr float smooth = 0.8f;

    // If new sample, copy fftResult to fftCalc and use fftAvg for output
    if (gotNewSample) {
        for (unsigned i = 0; i < m_config.numGEQChannels; i++) {
            m_fftCalc[i] = m_fftResult[i];
            m_fftResult[i] = m_fftAvg[i];
        }
    }

    unsigned long currentTime = millis();
    long delta_time = currentTime - m_lastGEQDynamicsTime;
    delta_time = constrain(delta_time, 1, 1000);

    float maxAttack = (m_config.decayTime <= 0) ? 255.0f :
                      (bigChange * float(delta_time) / float(m_config.decayTime));
    float maxDecay = (m_config.decayTime <= 0) ? -255.0f :
                     (-bigChange * float(delta_time) / float(m_config.decayTime));

    for (unsigned i = 0; i < m_config.numGEQChannels; i++) {
        float deltaSample = m_fftCalc[i] - m_fftAvg[i];

        if (deltaSample > maxAttack) deltaSample = maxAttack;
        if (deltaSample < maxDecay) deltaSample = maxDecay;

        deltaSample *= smooth;

        m_fftAvg[i] = constrain(m_fftAvg[i] + deltaSample, 0.0f, 255.0f);
        m_fftResult[i] = m_fftAvg[i];
    }

    m_lastGEQDynamicsTime = currentTime;
}

void AudioProcessor::processSamples(float* samples, size_t count) {
    if (!m_initialized || !samples || count == 0) return;

    // Ensure we have the right buffer size
    if (count != m_config.fftSize) {
        // Only process if we have exactly fftSize samples
        return;
    }

#ifdef ARDUINO_ARCH_ESP32
    // Copy samples to FFT buffer
    memcpy(m_vReal, samples, sizeof(float) * count);

    // Set imaginary parts to 0
    memset(m_vImag, 0, sizeof(float) * m_config.fftSize);

    // Apply filters if configured
    if (m_audioFilters && m_config.useInputFilter > 0) {
        m_audioFilters->applyFilter(count, m_vReal);
    }

    // Find max sample and count zero crossings
    float maxSample = 0.0f;
    uint_fast16_t newZeroCrossingCount = 0;
    for (int i = 0; i < m_config.fftSize; i++) {
        if ((m_vReal[i] <= (INT16_MAX - 1024)) && (m_vReal[i] >= (INT16_MIN + 1024))) {
            if (fabsf(m_vReal[i]) > maxSample) maxSample = fabsf(m_vReal[i]);
        }

        if (i < (m_config.fftSize - 1)) {
            if (__builtin_signbit(m_vReal[i]) != __builtin_signbit(m_vReal[i+1]))
                newZeroCrossingCount++;
        }
    }
    m_zeroCrossingCount = (newZeroCrossingCount * 2) / 3;

    // Update AGC with max sample
    if (m_agcController) {
        m_agcController->processSample(maxSample);
    }

    float wc = 1.0f;  // Window correction factor
    bool noiseGateOpen = m_agcController ? (m_agcController->getSampleAGC() > 10) : true;

    // Run FFT if noise gate is open
    if (noiseGateOpen) {
        // Create FFT object for this processing
        ArduinoFFT<float> FFT = ArduinoFFT<float>(m_vReal, m_vImag, m_config.fftSize, m_config.sampleRate, true);

        // Apply DC removal
        FFT.dcRemoval();

        // Apply windowing based on config
        switch(m_config.fftWindow) {
            case 1:
                FFT.windowing(FFTWindow::Hann, FFTDirection::Forward);
                wc = 0.66415918066f;
                break;
            case 2:
                FFT.windowing(FFTWindow::Nuttall, FFTDirection::Forward);
                wc = 0.9916873881f;
                break;
            case 3:
                FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
                wc = 0.664159180663f;
                break;
            case 4:
                FFT.windowing(FFTWindow::Flat_top, FFTDirection::Forward);
                wc = 1.276771793156f;
                break;
            case 5:
                FFT.windowing(FFTWindow::Blackman, FFTDirection::Forward);
                wc = 0.84762867875f;
                break;
            default:
                FFT.windowing(FFTWindow::Blackman_Harris, FFTDirection::Forward);
                wc = 1.0f;
                break;
        }

        // Compute FFT
        FFT.compute(FFTDirection::Forward);
        FFT.complexToMagnitude();
        m_vReal[0] = 0;  // Eliminate DC offset spike

        // Find major peak
        float last_majorpeak = m_fftMajorPeak;
        float last_magnitude = m_fftMagnitude;

        #if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)
        // Apply pink noise scaling for peak detection
        if (m_pinkFactors) {
            for (uint_fast16_t binInd = 0; binInd < m_config.fftSize; binInd++)
                m_vReal[binInd] *= m_pinkFactors[binInd];
        }
        #endif

        FFT.majorPeak(&m_fftMajorPeak, &m_fftMagnitude);
        m_fftMagnitude *= wc;

        if (m_fftMajorPeak < ((float)m_config.sampleRate / m_config.fftSize)) {
            m_fftMajorPeak = 1.0f;
            m_fftMagnitude = 0.0f;
        }
        if (m_fftMajorPeak > (0.42f * m_config.sampleRate)) {
            m_fftMajorPeak = last_majorpeak;
            m_fftMagnitude = last_magnitude;
        }

        #if !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)
        // Undo pink noise scaling
        if (m_pinkFactors) {
            for (uint_fast16_t binInd = 0; binInd < m_config.fftSize; binInd++)
                m_vReal[binInd] /= m_pinkFactors[binInd];

            // Fix peak magnitude
            if ((m_fftMajorPeak > 10.0f) && (m_fftMajorPeak < (m_config.sampleRate/2.2f)) && (m_fftMagnitude > 4.0f)) {
                float binWidth = (float)m_config.sampleRate / m_config.fftSize;
                unsigned peakBin = constrain((int)((m_fftMajorPeak + binWidth/2.0f) / binWidth), 0, m_config.fftSize - 1);
                m_fftMagnitude *= fmaxf(1.0f / m_pinkFactors[peakBin], 1.0f);
            }
        }
        #endif

        m_fftMajorPeak = constrain(m_fftMajorPeak, 1.0f, 11025.0f);
        // exponentially smooth peak (matches main:797 — "swooping peak")
        m_fftMajorPeakSmth = m_fftMajorPeakSmth + 0.42f * (m_fftMajorPeak - m_fftMajorPeakSmth);
    } else {
        // Noise gate closed
        m_fftMajorPeak = 1.0f;
        m_fftMagnitude = 0.001f;
    }

    // Scale FFT results
    for (int i = 0; i < m_config.fftSize; i++) {
        float t = fabsf(m_vReal[i]);
        m_vReal[i] = t / 16.0f;
    }

    // Compute frequency bands and post-process
    computeFrequencyBands(noiseGateOpen, false, wc);
    postProcessFFT(noiseGateOpen, false);

    // Detect peaks
    detectPeak();

    // Mark new FFT result available (for FASTPATH UDP-send latency reduction)
    m_haveNewFFTResult = true;

    // Update volume tracking
    if (m_agcController) {
        m_volumeSmth = m_agcController->getSampleAGC();
        m_volumeRaw = m_agcController->getSampleRaw();
    }
#else
    // Non-ESP32 platforms: basic processing only
    bool noiseGateOpen = m_agcController ? (m_agcController->getSampleAGC() > 10) : true;
    float wc = 1.0f;
    computeFrequencyBands(noiseGateOpen, false, wc);
    postProcessFFT(noiseGateOpen, false);
    detectPeak();
#endif
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
    // Full FFT processing loop migrated from FFTcode()
    const TickType_t xFrequency = m_config.minCycle * portTICK_PERIOD_MS;
    const TickType_t xFrequencyDouble = m_config.minCycle * portTICK_PERIOD_MS * 2;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    bool isFirstRun = false;
    bool haveOldSamples = false;
    bool usingOldSamples = false;

    // Create FFT object
    #if defined(ARDUINO_ARCH_ESP32)
    ArduinoFFT<float> FFT = ArduinoFFT<float>(m_vReal, m_vImag, m_config.fftSize, m_config.sampleRate, true);
    #endif

    // Pre-compute pink noise scaling factors
    #if defined(ARDUINO_ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)
    if (m_pinkFactors) {
        constexpr float pinkcenter = 23.66f;  // sqrt(560)
        float binWidth = (float)m_config.sampleRate / (float)m_config.fftSize;
        for (uint_fast16_t binInd = 0; binInd < m_config.fftSize; binInd++) {
            float binFreq = binInd * binWidth + binWidth/2.0f;
            if (binFreq > (m_config.sampleRate * 0.42f))
                binFreq = (m_config.sampleRate * 0.42f) - 0.25f * (binFreq - (m_config.sampleRate * 0.42f));
            m_pinkFactors[binInd] = sqrtf(binFreq) / pinkcenter;
        }
        m_pinkFactors[0] *= 0.5f;  // suppress 0-42hz bin
    }
    #endif

    while (true) {
        delay(1);  // Give IDLE task time and keep watchdog happy

        #if defined(WLED_DEBUG) || defined(SR_DEBUG) || defined(SR_STATS)
        uint64_t cycleStart = esp_timer_get_time();
        static uint64_t lastCycleStart = 0;
        static uint64_t lastLastCycleTime = 0;
        if ((lastCycleStart > 0) && (cycleStart > lastCycleStart)) {
            uint64_t taskTimeInMillis = ((cycleStart - lastCycleStart) + 5ULL) / 10ULL;
            m_stats.fftTaskCycle = (((taskTimeInMillis + lastLastCycleTime) / 2) * 4 + m_stats.fftTaskCycle * 6) / 10.0f;
            lastLastCycleTime = taskTimeInMillis;
        }
        lastCycleStart = cycleStart;
        uint64_t start = cycleStart;
        #endif

        // Get fresh samples from audio source
        memset(m_vReal, 0, sizeof(float) * m_config.fftSize);

        uint16_t readOffset = 0;
        #ifdef FFT_USE_SLIDING_WINDOW
        if (m_config.useSlidingWindow && haveOldSamples && m_oldSamples) {
            memcpy(m_vReal, m_oldSamples, sizeof(float) * (m_config.fftSize / 2));
            usingOldSamples = true;
            readOffset = m_config.fftSize / 2;
        } else {
            usingOldSamples = false;
        }

        // Read fresh samples
        do {
            if (m_audioSource) m_audioSource->getSamples(m_vReal + readOffset, m_config.fftSize / 2);
            readOffset += m_config.fftSize / 2;
        } while (readOffset < m_config.fftSize);
        #else
        if (m_audioSource) m_audioSource->getSamples(m_vReal, m_config.fftSize);
        #endif

        xLastWakeTime = xTaskGetTickCount();
        isFirstRun = !isFirstRun;

        #if defined(WLED_DEBUG) || defined(SR_DEBUG) || defined(SR_STATS)
        if (start < esp_timer_get_time()) {
            uint64_t t = (esp_timer_get_time() - start + 5ULL) / 10ULL;
            m_stats.sampleTime = (t * 3 + m_stats.sampleTime * 7) / 10.0f;
        }
        start = esp_timer_get_time();
        #endif

        // Determine which samples need filtering (in sliding window mode, only the new half)
        float *samplesStart = m_vReal;
        uint16_t sampleCount = m_config.fftSize;
        #ifdef FFT_USE_SLIDING_WINDOW
        if (usingOldSamples) {
            samplesStart = m_vReal + m_config.fftSize / 2;
            sampleCount = m_config.fftSize / 2;
        }
        #endif

        // Apply input filters (PDM bandpass or DC blocker) — sets doDCRemoval=false when a filter runs
        bool doDCRemoval = true;
        if (m_audioFilters && m_config.useInputFilter > 0) {
            m_audioFilters->applyFilter(sampleCount, samplesStart);
            doDCRemoval = false;
        }

        #if defined(WLED_DEBUG) || defined(SR_DEBUG) || defined(SR_STATS)
        if (start < esp_timer_get_time()) {
            uint64_t t = (esp_timer_get_time() - start + 5ULL) / 10ULL;
            m_stats.filterTime = (t * 3 + m_stats.filterTime * 7) / 10.0f;
        }
        start = esp_timer_get_time();
        #endif

        // Set imaginary parts to 0
        memset(m_vImag, 0, sizeof(float) * m_config.fftSize);

        #ifdef FFT_USE_SLIDING_WINDOW
        if (m_config.useSlidingWindow && m_oldSamples) {
            memcpy(m_oldSamples, m_vReal + m_config.fftSize / 2, sizeof(float) * (m_config.fftSize / 2));
            haveOldSamples = true;
        }
        #endif

        // Find max sample and count zero crossings
        float maxSample = 0.0f;
        uint_fast16_t newZeroCrossingCount = 0;
        for (int i = 0; i < m_config.fftSize; i++) {
            if ((m_vReal[i] <= (INT16_MAX - 1024)) && (m_vReal[i] >= (INT16_MIN + 1024))) {
                #ifdef FFT_USE_SLIDING_WINDOW
                if (usingOldSamples) {
                    if ((i >= m_config.fftSize / 2) && (fabsf(m_vReal[i]) > maxSample))
                        maxSample = fabsf(m_vReal[i]);
                } else
                #endif
                if (fabsf(m_vReal[i]) > maxSample) maxSample = fabsf(m_vReal[i]);
            }

            if (i < (m_config.fftSize - 1)) {
                if (__builtin_signbit(m_vReal[i]) != __builtin_signbit(m_vReal[i+1]))
                    newZeroCrossingCount++;
            }
        }
        m_zeroCrossingCount = (newZeroCrossingCount * 2) / 3;

        // Release peak sample to AGC / volume tracking
        if (m_agcController) {
            m_agcController->processSample(maxSample);
            m_volumeSmth = m_agcController->getSampleAGC();
            m_volumeRaw  = (uint16_t)m_agcController->getSampleRaw();
        } else {
            m_volumeSmth = maxSample;
            m_volumeRaw  = (uint16_t)maxSample;
        }

        float wc = 1.0f;  // Window correction factor
        bool noiseGateOpen = (m_volumeSmth > 0.25f);

        // Run FFT if noise gate is open
        if (noiseGateOpen) {
            // Apply DC removal if needed
            if (doDCRemoval) {
                #if defined(ARDUINO_ARCH_ESP32)
                FFT.dcRemoval();
                #endif
            }

            // Apply windowing
            #if defined(ARDUINO_ARCH_ESP32)
            switch(m_config.fftWindow) {
                case 1:
                    FFT.windowing(FFTWindow::Hann, FFTDirection::Forward);
                    wc = 0.66415918066f;
                    break;
                case 2:
                    FFT.windowing(FFTWindow::Nuttall, FFTDirection::Forward);
                    wc = 0.9916873881f;
                    break;
                case 3:
                    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
                    wc = 0.664159180663f;
                    break;
                case 4:
                    FFT.windowing(FFTWindow::Flat_top, FFTDirection::Forward);
                    wc = 1.276771793156f;
                    break;
                case 5:
                    FFT.windowing(FFTWindow::Blackman, FFTDirection::Forward);
                    wc = 0.84762867875f;
                    break;
                default:
                    FFT.windowing(FFTWindow::Blackman_Harris, FFTDirection::Forward);
                    wc = 1.0f;
                    break;
            }

            #ifdef FFT_USE_SLIDING_WINDOW
            if (usingOldSamples) wc = wc * 1.10f;
            #endif

            // Compute FFT
            FFT.compute(FFTDirection::Forward);
            FFT.complexToMagnitude();
            m_vReal[0] = 0;  // Eliminate DC offset spike

            #if defined(WLED_DEBUG) || defined(SR_DEBUG) || defined(SR_STATS)
            if (start < esp_timer_get_time()) {
                uint64_t t = (esp_timer_get_time() - start + 5ULL) / 10ULL;
                m_stats.fftTime = (t * 3 + m_stats.fftTime * 7) / 10.0f;
            }
            #endif

            // Find major peak
            float last_majorpeak = m_fftMajorPeak;
            float last_magnitude = m_fftMagnitude;

            #if defined(ARDUINO_ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)
            // Apply pink noise scaling for peak detection
            if (m_pinkFactors) {
                for (uint_fast16_t binInd = 0; binInd < m_config.fftSize; binInd++)
                    m_vReal[binInd] *= m_pinkFactors[binInd];
            }
            #endif

            FFT.majorPeak(&m_fftMajorPeak, &m_fftMagnitude);
            m_fftMagnitude *= wc;

            if (m_fftMajorPeak < ((float)m_config.sampleRate / m_config.fftSize)) {
                m_fftMajorPeak = 1.0f;
                m_fftMagnitude = 0.0f;
            }
            if (m_fftMajorPeak > (0.42f * m_config.sampleRate)) {
                m_fftMajorPeak = last_majorpeak;
                m_fftMagnitude = last_magnitude;
            }

            #if defined(ARDUINO_ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)
            // Undo pink noise scaling
            if (m_pinkFactors) {
                for (uint_fast16_t binInd = 0; binInd < m_config.fftSize; binInd++)
                    m_vReal[binInd] /= m_pinkFactors[binInd];

                // Fix peak magnitude
                if ((m_fftMajorPeak > 10.0f) && (m_fftMajorPeak < (m_config.sampleRate/2.2f)) && (m_fftMagnitude > 4.0f)) {
                    float binWidth = (float)m_config.sampleRate / m_config.fftSize;
                    unsigned peakBin = constrain((int)((m_fftMajorPeak + binWidth/2.0f) / binWidth), 0, m_config.fftSize - 1);
                    m_fftMagnitude *= fmaxf(1.0f / m_pinkFactors[peakBin], 1.0f);
                }
            }
            #endif

            m_fftMajorPeak = constrain(m_fftMajorPeak, 1.0f, 11025.0f);
            // exponentially smooth peak (matches main:797 — "swooping peak")
            m_fftMajorPeakSmth = m_fftMajorPeakSmth + 0.42f * (m_fftMajorPeak - m_fftMajorPeakSmth);
            #endif
        } else {
            // Noise gate closed
            memset(m_vReal, 0, sizeof(float) * m_config.fftSize);
            m_fftMajorPeak = 1.0f;
            m_fftMagnitude = 0.001f;
        }

        // Scale FFT results
        for (int i = 0; i < m_config.fftSize; i++) {
            float t = fabsf(m_vReal[i]);
            m_vReal[i] = t / 16.0f;
        }

        // Compute frequency bands and post-process
        computeFrequencyBands(noiseGateOpen, usingOldSamples, wc);
        postProcessFFT(noiseGateOpen, usingOldSamples);

        // Detect peaks
        detectPeak();
        autoResetPeak(50);

        // Mark new FFT result available (for FASTPATH UDP-send latency reduction)
        m_haveNewFFTResult = true;
        // Sleep until next cycle
        #ifdef FFT_USE_SLIDING_WINDOW
        if (!usingOldSamples) {
            vTaskDelayUntil(&xLastWakeTime, xFrequencyDouble);
        } else
        #endif
        if (isFirstRun) {
            vTaskDelayUntil(&xLastWakeTime, xFrequencyDouble);
        } else {
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
        }
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

