#ifndef AUDIO_FILTERS_H
#define AUDIO_FILTERS_H

/*
   @title     Audio Filters Library
   @file      audio_filters.h
   @repo      https://github.com/MoonModules/WLED-MM
   @Authors   Extracted from audio_reactive.h
   @Copyright © 2024,2025 Github MoonModules Commit Authors
   @license   Licensed under the EUPL-1.2 or later

   Audio filtering utilities for audio processing:
   - DC blocker filter
   - Bandpass filter for PDM microphones
   - Sample smoothing
*/

#include <Arduino.h>

// High-resolution type for input filters
#if defined(ARDUINO_ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)
  typedef double SR_HIRES_TYPE;  // ESP32 and ESP32-S3 (with FPU) are fast enough to use "double"
#else
  typedef float SR_HIRES_TYPE;   // prefer faster type on slower boards (-S2, -C3)
#endif

/**
 * @brief Audio filtering utilities
 *
 * Provides various audio filters for preprocessing raw audio samples:
 * - DC blocker to remove DC offset
 * - Bandpass filter for PDM microphones
 * - Sample smoothing and noise reduction
 */
class AudioFilters {
public:
    /**
     * @brief Filter configuration
     */
    struct Config {
        uint8_t filterMode = 2;  // 0=none, 1=PDM bandpass, 2=DC blocker
        uint8_t micQuality = 1;  // 0=normal, 1=good, 2=best (affects smoothing)
    };

    /**
     * @brief Constructor
     */
    AudioFilters();

    /**
     * @brief Configure the filters
     * @param config Filter configuration
     */
    void configure(const Config& config);

    /**
     * @brief Apply DC blocker filter to remove DC offset
     * @param numSamples Number of samples in buffer
     * @param sampleBuffer Sample buffer to filter (modified in place)
     */
    void applyDCBlocker(uint_fast16_t numSamples, float* sampleBuffer);

    /**
     * @brief Apply bandpass filter (for PDM microphones)
     * @param numSamples Number of samples in buffer
     * @param sampleBuffer Sample buffer to filter (modified in place)
     */
    void applyBandpassFilter(uint16_t numSamples, float* sampleBuffer);

    /**
     * @brief Apply appropriate filter based on configuration
     * @param numSamples Number of samples in buffer
     * @param sampleBuffer Sample buffer to filter (modified in place)
     */
    void applyFilter(uint16_t numSamples, float* sampleBuffer);

    /**
     * @brief Reset filter state
     */
    void reset();

private:
    Config m_config;

    // DC blocker state (see https://www.dsprelated.com/freebooks/filters/DC_Blocker.html)
    SR_HIRES_TYPE dcFilterState_in = 0.0;
    SR_HIRES_TYPE dcFilterState_out = 0.0;

    // Bandpass filter state
    SR_HIRES_TYPE bandpassFilterState_in[2] = {0.0, 0.0};
    SR_HIRES_TYPE bandpassFilterState_out[2] = {0.0, 0.0};
};

#endif // audio_filters.h

