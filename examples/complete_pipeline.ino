/*
   Example: Complete Audio Processing Pipeline

   This example demonstrates using all three audio processing libraries together:
   - AudioFilters for signal preprocessing
   - AGCController for automatic gain control
   - AudioProcessor for FFT and frequency analysis

   Hardware required:
   - ESP32 (any variant)
   - I2S MEMS microphone (e.g., INMP441, SPH0645, ICS-43434)

   Connections (example for INMP441):
   - I2S WS  (LRCLK) -> GPIO 15
   - I2S SD  (DOUT)  -> GPIO 32
   - I2S SCK (BCLK)  -> GPIO 14
   - GND -> GND
   - VDD -> 3.3V
   - L/R -> GND (for left channel)
*/

#include <Arduino.h>
#include "audio_filters.h"
#include "agc_controller.h"
#include "audio_processor.h"

// Audio processing components
AudioFilters filters;
AGCController agc;
AudioProcessor processor;

// Configuration
const uint16_t SAMPLE_RATE = 18000;
const uint16_t FFT_SIZE = 512;

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

    Serial.println("====================================");
    Serial.println("  Complete Audio Processing Demo   ");
    Serial.println("====================================");
    Serial.println();

    // ========== Configure AudioFilters ==========
    AudioFilters::Config filterConfig;
    filterConfig.filterMode = 2;  // DC blocker (recommended for most I2S mics)
    filterConfig.micQuality = 1;  // Good quality
    filters.configure(filterConfig);
    Serial.println("✓ AudioFilters configured");
    Serial.println("  - Mode: DC Blocker");
    Serial.println("  - Quality: Good");

    // ========== Configure AGCController ==========
    AGCController::Config agcConfig;
    agcConfig.preset = AGCController::NORMAL;
    agcConfig.squelch = 10.0f;
    agcConfig.sampleGain = 60.0f;
    agcConfig.inputLevel = 128;
    agcConfig.micQuality = 1;
    agcConfig.micLevelMethod = 1;  // Freeze mode
    agcConfig.fastPath = false;
    agc.configure(agcConfig);
    agc.setEnabled(true);
    Serial.println("✓ AGCController configured");
    Serial.println("  - Preset: Normal");
    Serial.println("  - AGC: Enabled");
    Serial.println("  - Squelch: 10.0");

    // ========== Configure AudioProcessor ==========
    AudioProcessor::Config procConfig;
    procConfig.sampleRate = SAMPLE_RATE;
    procConfig.fftSize = FFT_SIZE;
    procConfig.numGEQChannels = NUM_GEQ_CHANNELS;
    procConfig.scalingMode = 3;           // Square root scaling
    procConfig.pinkIndex = 2;             // IMNP441 profile
    procConfig.useSlidingWindow = true;   // 50% overlap FFT
    procConfig.averageByRMS = true;       // RMS averaging
    procConfig.minCycle = 25;             // 25ms between FFT cycles
    processor.configure(procConfig);

    if (!processor.initialize()) {
        Serial.println("✗ Failed to initialize AudioProcessor!");
        Serial.println("  Check available memory.");
        while(1) delay(1000);
    }
    Serial.println("✓ AudioProcessor initialized");
    Serial.println("  - Sample Rate: 18kHz");
    Serial.println("  - FFT Size: 512");
    Serial.println("  - Scaling: Square Root");
    Serial.println("  - Profile: IMNP441");

    // Link components together
    processor.setAudioFilters(&filters);
    processor.setAGCController(&agc);
    Serial.println("✓ Components linked");

    // Start processing task (ESP32 only)
    #ifdef ARDUINO_ARCH_ESP32
    Serial.println();
    Serial.println("Starting FFT processing task...");
    if (processor.startTask(1, 0)) {  // Priority 1, Core 0
        Serial.println("✓ FFT task started on Core 0");
    } else {
        Serial.println("✗ Failed to start FFT task!");
        while(1) delay(1000);
    }
    #endif

    Serial.println();
    Serial.println("====================================");
    Serial.println("  Audio Processing Active!         ");
    Serial.println("====================================");
    Serial.println();
    Serial.println("Output Format:");
    Serial.println("Vol | Peak Freq | FFT Bands (16 channels) | Status");
    Serial.println("------------------------------------------------------------");

    delay(1000);
}

void loop() {
    // Auto-reset peak flag based on frame timing
    processor.autoResetPeak(50);  // 50ms minimum between peaks

    // Get results from AudioProcessor
    const uint8_t* fftBands = processor.getFFTResult();
    float volume = processor.getVolumeSmooth();
    float peakFreq = processor.getMajorPeak();
    float magnitude = processor.getMagnitude();
    bool peaked = processor.getSamplePeak();
    uint16_t zeroCrossings = processor.getZeroCrossingCount();

    // Get AGC info
    float sensitivity = agc.getSensitivity();
    float multiplier = agc.getMultiplier();
    float pressure = agc.estimatePressure(agc.getSampleReal(), 1);  // dmType=1 for I2S

    // Print results every 200ms
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 200) {
        // Volume and peak frequency
        Serial.printf("%3.0f | %5.0fHz | ", volume, peakFreq);

        // FFT bands (show as bar chart)
        for (int i = 0; i < NUM_GEQ_CHANNELS; i++) {
            // Scale to 0-9 for display
            int barHeight = map(fftBands[i], 0, 255, 0, 9);
            Serial.print(barHeight);
        }

        // Status indicators
        Serial.print(" | ");
        if (peaked) Serial.print("PEAK! ");
        if (volume > 100) Serial.print("LOUD ");
        if (magnitude > 500) Serial.print("STRONG ");

        Serial.println();
        lastPrint = millis();
    }

    // Show detailed stats every 5 seconds
    static unsigned long lastStats = 0;
    if (millis() - lastStats > 5000) {
        Serial.println();
        Serial.println("======= Audio Statistics =======");
        Serial.printf("Volume:       %.1f / 255\n", volume);
        Serial.printf("Peak Freq:    %.0f Hz\n", peakFreq);
        Serial.printf("Magnitude:    %.1f\n", magnitude);
        Serial.printf("Zero Cross:   %d\n", zeroCrossings);
        Serial.printf("Sensitivity:  %.1f / 255\n", sensitivity);
        Serial.printf("AGC Gain:     %.3fx (%.1fdB)\n", multiplier, 20.0f*log10(multiplier));
        Serial.printf("Pressure:     %.0f dB SPL (est)\n", map(pressure, 0, 255, 5, 105));
        Serial.println("================================");
        Serial.println();
        lastStats = millis();
    }

    // Show FFT band distribution every 10 seconds
    static unsigned long lastBands = 0;
    if (millis() - lastBands > 10000) {
        Serial.println();
        Serial.println("===== FFT Frequency Bands =====");
        const char* bandNames[] = {
            "Sub-Bass  ", "Bass      ", "Bass      ", "Bass/Mid  ",
            "Midrange  ", "Midrange  ", "Midrange  ", "Upper Mid ",
            "Upper Mid ", "Presence  ", "Presence  ", "High      ",
            "High      ", "High      ", "Very High ", "Very High "
        };

        for (int i = 0; i < NUM_GEQ_CHANNELS; i++) {
            Serial.printf("Ch%2d %s: ", i, bandNames[i]);
            int barLength = map(fftBands[i], 0, 255, 0, 40);
            for (int j = 0; j < barLength; j++) Serial.print("█");
            Serial.printf(" %d\n", fftBands[i]);
        }
        Serial.println("===============================");
        Serial.println();
        lastBands = millis();
    }

    // Small delay
    delay(10);
}

/*
 * Expected Output:
 * ================
 *
 * The serial monitor will show:
 * 1. Real-time volume, peak frequency, and FFT bands
 * 2. Peak detection markers
 * 3. Detailed statistics every 5 seconds
 * 4. FFT band visualization every 10 seconds
 *
 * Typical values:
 * - Silence:      Volume 0-10, no peaks
 * - Quiet music:  Volume 20-50, occasional peaks
 * - Normal music: Volume 50-150, regular peaks
 * - Loud music:   Volume 150-255, frequent peaks
 *
 * Peak frequency should track the dominant frequency in the audio
 * FFT bands show the distribution across the frequency spectrum
 */

