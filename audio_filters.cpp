/*
   @title     Audio Filters Library
   @file      audio_filters.cpp
   @repo      https://github.com/MoonModules/WLED-MM
   @Authors   Extracted from audio_reactive.h
   @Copyright © 2024,2025 Github MoonModules Commit Authors
   @license   Licensed under the EUPL-1.2 or later
*/

#include "audio_filters.h"
#include <math.h>

AudioFilters::AudioFilters() {
    reset();
}

void AudioFilters::configure(const Config& config) {
    m_config = config;
}

void AudioFilters::reset() {
    dcFilterState_in = 0.0;
    dcFilterState_out = 0.0;
    bandpassFilterState_in[0] = 0.0;
    bandpassFilterState_in[1] = 0.0;
    bandpassFilterState_out[0] = 0.0;
    bandpassFilterState_out[1] = 0.0;
}

void AudioFilters::applyDCBlocker(uint_fast16_t numSamples, float* sampleBuffer) {
    // High-Pass "DC blocker" filter
    // see https://www.dsprelated.com/freebooks/filters/DC_Blocker.html
    constexpr float filterR = 0.990f;  // around 40hz

    for (unsigned i = 0; i < numSamples; i++) {
        float value = sampleBuffer[i];
        SR_HIRES_TYPE filtered = (SR_HIRES_TYPE)(value - dcFilterState_in) + filterR * dcFilterState_out;
        dcFilterState_in = value;
        dcFilterState_out = filtered;
        sampleBuffer[i] = filtered;
    }
}

void AudioFilters::applyBandpassFilter(uint16_t numSamples, float* sampleBuffer) {
    // PDM bandpass filter - IIR Butterworth 4th order Bandpass filter, based on
    // https://www-users.cs.york.ac.uk/~fisher/cgi-bin/mkfscript
    // Sampling Freq  = 20Khz
    // Low freq  = 200hz
    // High freq = 8000hz
    // a0=1.0; y = a0*x[n] + a1*x[n-1] + a2*x[n-2] - b1*y[n-1] - b2*y[n-2];
    constexpr SR_HIRES_TYPE a0 =  0.2928893542;
    constexpr SR_HIRES_TYPE a1 =  0.0;
    constexpr SR_HIRES_TYPE a2 = -0.2928893542;
    constexpr SR_HIRES_TYPE b1 = -0.0316892352;
    constexpr SR_HIRES_TYPE b2 = -0.3814225903;

    for (int i = 0; i < numSamples; i++) {
        SR_HIRES_TYPE xn = sampleBuffer[i];
        SR_HIRES_TYPE yn = a0*xn + a1*bandpassFilterState_in[0] + a2*bandpassFilterState_in[1]
                          - b1*bandpassFilterState_out[0] - b2*bandpassFilterState_out[1];

        // Shift history
        bandpassFilterState_in[1] = bandpassFilterState_in[0];
        bandpassFilterState_in[0] = xn;
        bandpassFilterState_out[1] = bandpassFilterState_out[0];
        bandpassFilterState_out[0] = yn;

        sampleBuffer[i] = yn;
    }

    // Additional FIR lowpass and IIR highpass (from runMicFilter)
    constexpr float beta1 = 0.12589412;   // FIR lowpass alpha, 0 < alpha < 0.5 (originally 0.125)
    constexpr float beta2 = (0.5f - beta1) / 2.0f;
    constexpr float alpha = 0.03;         // IIR highpass alpha, 0 < alpha < 1

    SR_HIRES_TYPE lowfilt = 0.0;
    float last_vals[2] = {0.0f, 0.0f};

    for (int i = 0; i < numSamples; i++) {
        // FIR lowpass, to remove high frequency noise
        float highFilteredSample;
        if (i < (numSamples-1))
            highFilteredSample = beta1*sampleBuffer[i] + beta2*last_vals[0] + beta2*sampleBuffer[i+1];
        else
            highFilteredSample = beta1*sampleBuffer[i] + beta2*last_vals[0] + beta2*last_vals[1];

        last_vals[1] = last_vals[0];
        last_vals[0] = sampleBuffer[i];
        sampleBuffer[i] = highFilteredSample;

        // IIR highpass, to remove low frequency noise
        lowfilt += alpha * (sampleBuffer[i] - lowfilt);
        sampleBuffer[i] = sampleBuffer[i] - lowfilt;
    }
}

void AudioFilters::applyFilter(uint16_t numSamples, float* sampleBuffer) {
    switch (m_config.filterMode) {
        case 0:
            // No filtering
            break;
        case 1:
            // PDM bandpass filter
            applyBandpassFilter(numSamples, sampleBuffer);
            break;
        case 2:
        default:
            // DC blocker (default)
            applyDCBlocker(numSamples, sampleBuffer);
            break;
    }
}

