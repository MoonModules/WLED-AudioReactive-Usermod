#ifndef AGC_CONTROLLER_H
#define AGC_CONTROLLER_H

/*
   @title     AGC Controller Library
   @file      agc_controller.h
   @repo      https://github.com/MoonModules/WLED-MM
   @Authors   Extracted from audio_reactive.h
   @Copyright © 2024,2025 Github MoonModules Commit Authors
   @license   Licensed under the EUPL-1.2 or later

   Automatic Gain Control (AGC) using PI controller for audio processing
*/

#include <Arduino.h>

/**
 * @brief Automatic Gain Control (AGC) controller
 *
 * Implements a PI (Proportional-Integral) controller to automatically
 * adjust audio gain based on signal level. Uses dual setpoints and
 * emergency zones for robust control.
 */
class AGCController {
public:
    /**
     * @brief AGC preset modes
     */
    enum Preset {
        NORMAL = 0,  // Balanced response
        VIVID = 1,   // Fast, aggressive response
        LAZY = 2     // Slow, gentle response
    };

    /**
     * @brief AGC configuration
     */
    struct Config {
        Preset preset = NORMAL;
        float squelch = 0.0f;        // Noise gate threshold
        float sampleGain = 60.0f;    // Manual gain (when AGC is off)
        uint8_t inputLevel = 128;    // Input level adjustment
        uint8_t micQuality = 1;      // 0=normal, 1=good, 2=best (affects smoothing)
        uint8_t micLevelMethod = 0;  // 0=floating, 1=freeze, 2=fast freeze
        bool fastPath = false;       // Use faster smoothing coefficients
    };

    /**
     * @brief Constructor
     */
    AGCController();

    /**
     * @brief Configure the AGC controller
     * @param config AGC configuration
     */
    void configure(const Config& config);

    /**
     * @brief Enable or disable AGC
     * @param enabled true to enable AGC, false for manual gain
     */
    void setEnabled(bool enabled);

    /**
     * @brief Check if AGC is enabled
     * @return true if AGC is enabled
     */
    bool isEnabled() const { return m_enabled; }

    /**
     * @brief Process AGC for a raw sample
     * @param rawSample The raw audio sample (before gain)
     * @param timestamp Current timestamp in milliseconds (0 = auto)
     */
    void processAGC(float rawSample, unsigned long timestamp = 0);

    /**
     * @brief Process a microphone sample with DC level tracking and noise gate
     * @param micDataReal Raw microphone data value
     */
    void processSample(float micDataReal);

    /**
     * @brief Get the AGC-processed sample (smooth)
     * @return Smoothed AGC sample value
     */
    float getSampleAGC() const { return m_sampleAgc; }

    /**
     * @brief Get the raw AGC-processed sample (less smoothing)
     * @return Raw AGC sample value
     */
    float getRawSampleAGC() const { return m_rawSampleAgc; }

    /**
     * @brief Get the processed sample as int16
     * @return Sample value as int16_t
     */
    int16_t getSampleRaw() const { return m_sampleRaw; }

    /**
     * @brief Get the average sample value
     * @return Average sample value
     */
    float getSampleAvg() const { return m_sampleAvg; }

    /**
     * @brief Get the real (unprocessed) sample value
     * @return Real sample value
     */
    float getSampleReal() const { return m_sampleReal; }

    /**
     * @brief Get the current AGC gain multiplier
     * @return Current AGC multiplier (1/64 to 32)
     */
    float getMultiplier() const { return m_multAgc; }

    /**
     * @brief Get the current sensitivity value (0-255)
     * @return Sensitivity value scaled for UI display
     */
    float getSensitivity() const;

    /**
     * @brief Estimate sound pressure level (0-255)
     * @param micDataReal Current mic sample
     * @param dmType Device/microphone type
     * @return Estimated sound pressure (0=5dB, 255=105dB)
     */
    float estimatePressure(float micDataReal, uint8_t dmType) const;

    /**
     * @brief Reset AGC state
     */
    void reset();

private:
    // Configuration
    Config m_config;
    bool m_enabled = false;
    int m_lastPreset = -1;

    // AGC state
    float m_multAgc = 1.0f;           // Current AGC multiplier
    double m_controlIntegrated = 0.0;  // PI controller integrator
    double m_sampleMax = 0.0;          // Maximum sample seen
    double m_micLev = 0.0;             // DC level tracker

    // Sample values
    float m_sampleReal = 0.0f;         // Real sample (before gain)
    int16_t m_sampleRaw = 0;           // Processed sample (after gain)
    float m_sampleAvg = 0.0f;          // Average sample
    float m_sampleAgc = 0.0f;          // AGC processed (smooth)
    float m_rawSampleAgc = 0.0f;       // AGC processed (raw)
    float m_expAdjF = 0.0f;            // Exponential filter state

    // Timing
    unsigned long m_lastTime = 0;

    // DC level tracking state
    bool m_isFrozen = false;
    bool m_haveSilence = true;
    unsigned long m_lastSoundTime = 0;
    unsigned long m_startupTime = 0;

    // AGC preset tables (indexed by Preset enum)
    static constexpr double AGC_SAMPLE_DECAY[3]  = { 0.9994, 0.9985, 0.9997 };
    static constexpr float AGC_ZONE_LOW[3]       = { 32.0f, 28.0f, 36.0f };
    static constexpr float AGC_ZONE_HIGH[3]      = { 240.0f, 240.0f, 248.0f };
    static constexpr float AGC_ZONE_STOP[3]      = { 336.0f, 448.0f, 304.0f };
    static constexpr float AGC_TARGET0[3]        = { 112.0f, 144.0f, 164.0f };
    static constexpr float AGC_TARGET0_UP[3]     = { 88.0f, 64.0f, 116.0f };
    static constexpr float AGC_TARGET1[3]        = { 220.0f, 224.0f, 216.0f };
    static constexpr double AGC_FOLLOW_FAST[3]  = { 1/192.0, 1/128.0, 1/256.0 };
    static constexpr double AGC_FOLLOW_SLOW[3]  = { 1/6144.0, 1/4096.0, 1/8192.0 };
    static constexpr double AGC_CONTROL_KP[3]   = { 0.6, 1.5, 0.65 };
    static constexpr double AGC_CONTROL_KI[3]   = { 1.7, 1.85, 1.2 };
};

#endif // agc_controller.h

