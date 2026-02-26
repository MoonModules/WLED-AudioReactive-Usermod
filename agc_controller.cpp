/*
   @title     AGC Controller Library
   @file      agc_controller.cpp
   @repo      https://github.com/MoonModules/WLED-MM
   @Authors   Extracted from audio_reactive.h
   @Copyright © 2024,2025 Github MoonModules Commit Authors
   @license   Licensed under the EUPL-1.2 or later
*/

#include "agc_controller.h"
#include <math.h>

// Define static constexpr arrays
constexpr double AGCController::AGC_SAMPLE_DECAY[3];
constexpr float AGCController::AGC_ZONE_LOW[3];
constexpr float AGCController::AGC_ZONE_HIGH[3];
constexpr float AGCController::AGC_ZONE_STOP[3];
constexpr float AGCController::AGC_TARGET0[3];
constexpr float AGCController::AGC_TARGET0_UP[3];
constexpr float AGCController::AGC_TARGET1[3];
constexpr double AGCController::AGC_FOLLOW_FAST[3];
constexpr double AGCController::AGC_FOLLOW_SLOW[3];
constexpr double AGCController::AGC_CONTROL_KP[3];
constexpr double AGCController::AGC_CONTROL_KI[3];

AGCController::AGCController() {
    reset();
}

void AGCController::configure(const Config& config) {
    m_config = config;
}

void AGCController::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (enabled && m_lastPreset != (int)m_config.preset) {
        // Reset integrator when changing presets
        m_controlIntegrated = 0.0;
        m_lastPreset = (int)m_config.preset;
    }
}

void AGCController::reset() {
    m_multAgc = 1.0f;
    m_controlIntegrated = 0.0;
    m_sampleMax = 0.0;
    m_micLev = 0.0;
    m_sampleReal = 0.0f;
    m_sampleRaw = 0;
    m_sampleAvg = 0.0f;
    m_sampleAgc = 0.0f;
    m_rawSampleAgc = 0.0f;
    m_expAdjF = 0.0f;
    m_lastTime = 0;
    m_isFrozen = false;
    m_haveSilence = true;
    m_lastSoundTime = 0;
    m_startupTime = 0;
    m_lastPreset = -1;
}

void AGCController::processAGC(float rawSample, unsigned long timestamp) {
    if (!m_enabled) return;

    const int preset = (int)m_config.preset;
    m_sampleReal = rawSample;

    float lastMultAgc = m_multAgc;
    float multAgcTemp = m_multAgc;
    float tmpAgc = m_sampleReal * m_multAgc;

    float control_error;

    if (m_lastPreset != preset) {
        m_controlIntegrated = 0.0;  // Reset integrator on preset change
        m_lastPreset = preset;
    }

    // For PI controller, maintain constant frequency
    unsigned long time_now = (timestamp > 0) ? timestamp : millis();
    if ((time_now - m_lastTime) > 2) {
        m_lastTime = time_now;

        if ((fabsf(m_sampleReal) < 2.0f) || (m_sampleMax < 1.0f)) {
            // Signal is squelched - deliver silence
            tmpAgc = 0;
            // Spin down the integrated error buffer
            if (fabs(m_controlIntegrated) < 0.01)
                m_controlIntegrated = 0.0;
            else
                m_controlIntegrated *= 0.91;
        } else {
            // Compute new setpoint
            if (tmpAgc <= AGC_TARGET0_UP[preset])
                multAgcTemp = AGC_TARGET0[preset] / m_sampleMax;
            else
                multAgcTemp = AGC_TARGET1[preset] / m_sampleMax;
        }

        // Limit amplification
        multAgcTemp = constrain(multAgcTemp, 1.0f/64.0f, 32.0f);

        // Compute error terms
        control_error = multAgcTemp - lastMultAgc;

        // Integrator with anti-windup
        if ((multAgcTemp > 0.085f) && (multAgcTemp < 6.5f) &&
            (m_multAgc * m_sampleMax < AGC_ZONE_STOP[preset])) {
            m_controlIntegrated += control_error * 0.002 * 0.25;
        } else {
            m_controlIntegrated *= 0.9;
        }

        // Apply PI Control
        tmpAgc = m_sampleReal * lastMultAgc;
        if ((tmpAgc > AGC_ZONE_HIGH[preset]) ||
            (tmpAgc < m_config.squelch + AGC_ZONE_LOW[preset])) {
            // Emergency zone
            multAgcTemp = lastMultAgc + AGC_FOLLOW_FAST[preset] * AGC_CONTROL_KP[preset] * control_error;
            multAgcTemp += AGC_FOLLOW_FAST[preset] * AGC_CONTROL_KI[preset] * m_controlIntegrated;
        } else {
            // Normal zone
            multAgcTemp = lastMultAgc + AGC_FOLLOW_SLOW[preset] * AGC_CONTROL_KP[preset] * control_error;
            multAgcTemp += AGC_FOLLOW_SLOW[preset] * AGC_CONTROL_KI[preset] * m_controlIntegrated;
        }

        // Limit amplification again
        multAgcTemp = constrain(multAgcTemp, 1.0f/64.0f, 32.0f);
    }

    // Apply gain to signal
    tmpAgc = m_sampleReal * multAgcTemp;
    if (fabsf(m_sampleReal) < 2.0f) tmpAgc = 0.0f;
    tmpAgc = constrain(tmpAgc, 0.0f, 255.0f);

    // Update multiplier
    m_multAgc = multAgcTemp;

    // Update samples with smoothing based on quality
    if (m_config.micQuality > 0) {
        if (m_config.micQuality > 1) {
            // Best quality
            m_rawSampleAgc = 0.95f * tmpAgc + 0.05f * m_rawSampleAgc;
            m_sampleAgc += 0.95f * (tmpAgc - m_sampleAgc);
        } else {
            // Good quality
            m_rawSampleAgc = 0.70f * tmpAgc + 0.30f * m_rawSampleAgc;
            m_sampleAgc += 0.70f * (tmpAgc - m_sampleAgc);
        }
    } else {
        // Normal quality
        float smoothFactor = m_config.fastPath ? 0.65f : 0.8f;
        m_rawSampleAgc = smoothFactor * tmpAgc + (1.0f - smoothFactor) * m_rawSampleAgc;

        if (fabsf(tmpAgc) < 1.0f) {
            m_sampleAgc = 0.5f * tmpAgc + 0.5f * m_sampleAgc;  // Fast path to zero
        } else {
            // Get smoothing factor from config
            float agcSmooth = m_config.fastPath ? 1/8.0f : 1/12.0f;
            if (preset == VIVID) agcSmooth = m_config.fastPath ? 1/5.0f : 1/6.0f;
            else if (preset == LAZY) agcSmooth = m_config.fastPath ? 1/12.0f : 1/16.0f;

            m_sampleAgc += agcSmooth * (tmpAgc - m_sampleAgc);
        }
    }

    m_sampleAgc = fabsf(m_sampleAgc);
}

void AGCController::processSample(float micDataReal) {
    const int preset = (int)m_config.preset;
    const float weighting = 0.18f;   // Exponential filter weighting
    const float weighting2 = 0.073f; // For rising signal

    // Initialize startup time on first call
    if (m_startupTime == 0) m_startupTime = millis();

    // Track DC level (unless frozen)
    if ((m_config.micLevelMethod < 1) || !m_isFrozen) {
        m_micLev += (micDataReal - m_micLev) / 12288.0f;
    }

    // Check if DC level is above signal
    if (micDataReal < (m_micLev - 0.24)) {
        m_micLev = ((m_micLev * 31.0f) + micDataReal) / 32.0f;
        if (!m_haveSilence) m_isFrozen = true;
    }

    // Remove DC offset
    float micInNoDC = fabsf(micDataReal - m_micLev);

    // Apply exponential smoothing
    if ((micInNoDC > m_expAdjF) && (m_expAdjF > m_config.squelch)) {
        m_expAdjF = weighting2 * micInNoDC + (1.0f - weighting2) * m_expAdjF;  // Rise slower
    } else {
        m_expAdjF = weighting * micInNoDC + (1.0f - weighting) * m_expAdjF;    // Fall faster
    }
    m_expAdjF = fabsf(m_expAdjF);

    // Fast freeze mode
    if ((m_config.micLevelMethod == 2) && !m_haveSilence &&
        (m_expAdjF >= (1.5f * m_config.squelch))) {
        m_isFrozen = true;
    }

    // Noise gate
    if ((m_expAdjF <= m_config.squelch) ||
        ((m_config.squelch == 0) && (m_expAdjF < 0.25f))) {
        m_expAdjF = 0.0f;
        micInNoDC = 0.0f;
    }

    // Track silence
    if (m_expAdjF <= 0.5f) {
        m_haveSilence = true;
    } else {
        m_lastSoundTime = millis();
        m_haveSilence = false;
    }

    // Un-freeze micLev
    if (m_config.micLevelMethod == 0) {
        m_isFrozen = false;
    } else if ((m_config.micLevelMethod == 1) && m_isFrozen && m_haveSilence &&
               ((millis() - m_lastSoundTime) > 4000)) {
        m_isFrozen = false;  // 4 seconds silence
    } else if ((m_config.micLevelMethod == 2) && m_isFrozen && m_haveSilence &&
               ((millis() - m_lastSoundTime) > 6000)) {
        m_isFrozen = false;  // 6 seconds silence
    } else if ((m_config.micLevelMethod == 2) &&
               ((millis() - m_startupTime) < 12000)) {
        m_isFrozen = false;  // First 12 seconds
    }

    float tmpSample = m_expAdjF;
    float sampleAdj;

    // Apply gain
    if (m_config.micQuality > 0) {
        sampleAdj = micInNoDC * m_config.sampleGain / 40.0f * m_config.inputLevel/128.0f + micInNoDC / 16.0f;
        m_sampleReal = micInNoDC;
    } else {
        sampleAdj = tmpSample * m_config.sampleGain / 40.0f * m_config.inputLevel/128.0f + tmpSample / 16.0f;
        m_sampleReal = tmpSample;
    }

    sampleAdj = constrain(sampleAdj, 0.0f, 255.0f);
    m_sampleRaw = (int16_t)sampleAdj;

    // Update sampleMax with decay
    if ((m_sampleMax < m_sampleReal) && (m_sampleReal > 0.5f)) {
        m_sampleMax = m_sampleMax + 0.5f * (m_sampleReal - m_sampleMax);
    } else {
        if (m_enabled && (m_multAgc * m_sampleMax > AGC_ZONE_STOP[preset])) {
            m_sampleMax += 0.5f * (m_sampleReal - m_sampleMax);
        } else {
            m_sampleMax *= AGC_SAMPLE_DECAY[preset];
        }
    }
    if (m_sampleMax < 0.5f) m_sampleMax = 0.0f;

    // Update average
    if (m_config.micQuality > 0) {
        if (m_config.micQuality > 1) {
            m_sampleAvg += 0.95f * (sampleAdj - m_sampleAvg);
        } else {
            m_sampleAvg += 0.70f * (sampleAdj - m_sampleAvg);
        }
    } else {
        float avgFactor = m_config.fastPath ? 11.0f/12.0f : 15.0f/16.0f;
        m_sampleAvg = (m_sampleAvg * avgFactor) + sampleAdj * (1.0f - avgFactor);
    }
    m_sampleAvg = fabsf(m_sampleAvg);

    // Process AGC if enabled
    if (m_enabled) {
        processAGC(m_sampleReal, 0);
    }
}

float AGCController::getSensitivity() const {
    float tmpSound = m_multAgc;

    if (!m_enabled) {
        // AGC off - use non-AGC gain
        tmpSound = (m_config.sampleGain/40.0f * m_config.inputLevel/128.0f) + 1.0f/16.0f;
    } else {
        // AGC ON - scale value
        tmpSound /= (m_config.sampleGain/40.0f + 1.0f/16.0f);
    }

    // Scale to 0..255
    if (tmpSound > 1.0f) tmpSound = sqrtf(tmpSound);
    if (tmpSound > 1.25f) tmpSound = ((tmpSound - 1.25f)/3.42f) + 1.25f;

    return constrain(128.0f * tmpSound - 6.0f, 0.0f, 255.0f);
}

float AGCController::estimatePressure(float micDataReal, uint8_t dmType) const {
    // Constants for logarithmic scaling
    constexpr float logMinSample = 0.8329091229351f;  // ln(2.3)
    constexpr float sampleRangeMin = 2.3f;
    constexpr float logMaxSample = 10.1895683436f;    // ln(32767 - 6144)
    constexpr float sampleRangeMax = 32767.0f - 6144.0f;

    // Use appropriate sample
    float micSampleMax = fabsf(m_sampleReal);

    // Apply corrections for different mic types
    if (dmType == 0) micSampleMax *= 2.0f;       // ADC analog
    if (dmType == 5) micSampleMax *= 2.0f;       // PDM
    if (dmType == 4) {                            // I2S Line-In
        micSampleMax /= 11.0f;
        micSampleMax *= micSampleMax;
    }

    // Check ranges
    if (micSampleMax <= sampleRangeMin) return 0.0f;
    if (micSampleMax >= sampleRangeMax) return 255.0f;

    // Apply logarithmic scaling
    float scaledValue = logf(micSampleMax);
    scaledValue = (scaledValue - logMinSample) / (logMaxSample - logMinSample);

    return constrain(256.0f * scaledValue, 0.0f, 255.0f);
}

