# Audio Processing Examples

This directory contains example sketches demonstrating how to use the audio processing libraries.

## Examples

### 1. example_standalone.ino
**Difficulty**: Beginner  
**Components Used**: AudioFilters, AGCController  
**Hardware**: ESP32 + I2S microphone

**Description**: Basic example showing how to use filtering and AGC independently. Perfect for learning the basics of each component.

**Features**:
- I2S microphone input
- DC blocker filtering
- AGC with normal preset
- Real-time volume display
- Sensitivity monitoring
- Sound pressure estimation

**Best For**:
- Learning the library basics
- Testing your microphone setup
- Understanding AGC behavior
- Simple audio level monitoring

---

### 2. complete_pipeline.ino
**Difficulty**: Intermediate  
**Components Used**: AudioFilters, AGCController, AudioProcessor  
**Hardware**: ESP32 + I2S microphone

**Description**: Complete audio processing pipeline with FFT analysis. Shows how all three libraries work together for full audio visualization.

**Features**:
- Full FFT processing (512 samples)
- 16-channel frequency bands (GEQ)
- Peak frequency detection
- Peak detection with auto-reset
- Multiple visualization outputs
- Detailed statistics
- FreeRTOS task management

**Best For**:
- LED audio visualization projects
- VU meter applications
- Music visualization
- Understanding frequency analysis
- Full-featured audio processing

---

## Hardware Requirements

### Minimum (example_standalone.ino)
- ESP32 (any variant)
- I2S MEMS microphone (INMP441, SPH0645, etc.)
- 3.3V power supply
- USB for programming/serial monitor

### Recommended (complete_pipeline.ino)
- ESP32 (classic or S3 for better performance)
- High-quality I2S microphone (INMP441, ICS-43434)
- Adequate power supply (500mA+)
- LED strip for visualization (optional)

### Pin Connections (Typical)
```
Microphone -> ESP32
-----------------------
WS  (LRCLK) -> GPIO 15
SD  (DOUT)  -> GPIO 32
SCK (BCLK)  -> GPIO 14
GND         -> GND
VDD         -> 3.3V
L/R         -> GND (left channel)
```

**Note**: Pin numbers can be changed in the code. Some ESP32 boards have restrictions on certain pins.

## Getting Started

### Option 1: Using PlatformIO (Recommended)

**Easiest way to get started!**

1. **Install PlatformIO**
   - VSCode: Install PlatformIO IDE extension
   - Or: `pip install platformio`

2. **Open Project**
   ```bash
   cd WLED-AudioReactive-Usermod
   code .  # Or open in VSCode
   ```

3. **Build and Upload**
   ```bash
   # Build for ESP32
   pio run -e esp32dev
   
   # Upload and monitor
   pio run -e esp32dev -t upload -t monitor
   ```

4. **Done!** 
   - Libraries are auto-included
   - Dependencies auto-installed
   - No manual configuration needed

See **[QUICKSTART.md](../QUICKSTART.md)** for detailed PlatformIO instructions!

### Option 2: Using Arduino IDE

1. **Install Libraries**
   Copy these files to your project:
   - `audio_filters.h/cpp`
   - `agc_controller.h/cpp`
   - `audio_processor.h/cpp` (for complete_pipeline only)

2. **Install Dependencies**
   For complete_pipeline.ino, you need ArduinoFFT:
   ```
   Arduino IDE: Install "arduinoFFT" via Library Manager (version 1.9.2)
   ```

3. **Choose an Example**
   - **Just starting?** → example_standalone.ino
   - **Want full features?** → complete_pipeline.ino

4. **Configure Hardware**
   Update pin definitions in the example to match your hardware.

5. **Upload and Monitor**
   - Upload the sketch
   - Open Serial Monitor (115200 baud)
   - Make some noise and watch the output!

## Understanding the Output

### example_standalone.ino Output
```
Volume(AGC) | Volume(Raw) | Sensitivity | Pressure | Peak
-----------------------------------------------------------------------
   45.2     |    123      |    87.3     |   62.1   | PEAK!
   52.1     |    145      |    91.2     |   68.5   |      
```

- **Volume(AGC)**: AGC-processed volume (0-255)
- **Volume(Raw)**: Raw sample value
- **Sensitivity**: Current AGC sensitivity
- **Pressure**: Estimated sound pressure (dB SPL)
- **Peak**: Peak detection marker

### complete_pipeline.ino Output
```
Vol | Peak Freq | FFT Bands (16 channels) | Status
------------------------------------------------------------
 87 | 523Hz | 5427836542134679 | PEAK! LOUD
```

- **Vol**: Overall volume
- **Peak Freq**: Dominant frequency in Hz
- **FFT Bands**: Histogram of frequency content (0-9 scale)
- **Status**: Peak/loud/strong indicators

Plus detailed statistics every 5-10 seconds.

## Troubleshooting

### No Audio Detected
1. Check microphone connections
2. Verify 3.3V power to microphone
3. Try swapping L/R pin (connect to GND or VDD)
4. Check I2S pin assignments
5. Increase input level / decrease squelch

### Noisy Output
1. Use DC blocker filter (mode 2)
2. Increase squelch threshold
3. Check power supply quality
4. Keep wires short
5. Use shielded cables if possible

### Peak Frequency Wrong
1. Verify sample rate matches hardware
2. Check FFT size configuration
3. Try different pink noise profile
4. Ensure microphone has clear path to sound

### Memory Errors (complete_pipeline)
1. Reduce FFT size (512 → 256)
2. Disable sliding window
3. Use smaller ESP32 variant settings
4. Close other tasks/services

### Compilation Errors
1. Install ArduinoFFT library
2. Check ESP32 Arduino core version (>= 2.0)
3. Verify all library files are present
4. Enable C++11 or later

## Configuration Tips

### AGC Presets
- **Normal**: Balanced, good for most uses
- **Vivid**: Fast response, good for beat detection
- **Lazy**: Slow response, good for ambient/smooth

### Filter Modes
- **0 (None)**: Pre-filtered input, line-in
- **1 (PDM)**: PDM microphones only
- **2 (DC Blocker)**: I2S digital mics (recommended)

### FFT Scaling Modes
- **1 (Log)**: Wide dynamic range, emphasizes changes
- **2 (Linear)**: Proportional, predictable
- **3 (Sqrt)**: Balanced, good for visualization

### Pink Noise Profiles
- **0**: Default (SR WLED)
- **1**: Line-in
- **2**: INMP441 default
- **3-4**: INMP441 variants (bass/voice)
- **5-6**: ICS-43434 variants
- **7**: SPM1423
- **10**: Flat (no correction)

## Advanced Usage

### Custom FFT Task Priority
```cpp
// Higher priority (1-4) for better responsiveness
processor.startTask(3, 0);  // Priority 3, Core 0
```

### Custom Frequency Bands
Modify `computeFrequencyBands()` in `audio_processor.cpp` to create your own frequency mappings.

### Integration with LED Effects
```cpp
const uint8_t* bands = processor.getFFTResult();
for (int i = 0; i < NUM_LEDS; i++) {
    int bandIndex = map(i, 0, NUM_LEDS, 0, 16);
    uint8_t brightness = bands[bandIndex];
    leds[i] = CHSV(hue, 255, brightness);
}
```

## Performance Notes

### ESP32 Classic
- Sample Rate: 18kHz recommended
- FFT Size: 512 works well
- Can run FFT task on Core 0
- ~10-15% CPU usage

### ESP32-S3
- Sample Rate: Up to 22kHz
- FFT Size: 512 with sliding window
- Faster FFT computation
- ~8-12% CPU usage

### ESP32-S2/C3
- Sample Rate: 16-18kHz recommended
- FFT Size: 256-512
- Slower without FPU
- Disable sliding window for better performance

## Further Reading

- **docs/README_REFACTORED.md**: Complete API documentation
- **docs/ARCHITECTURE.md**: System architecture and design
- **docs/MIGRATION_GUIDE.md**: Migrating from old code
- **audio_filters.h**: Filtering API details
- **agc_controller.h**: AGC API details
- **audio_processor.h**: FFT API details

## Contributing Examples

Have a cool example? We'd love to see it!

1. Create a new .ino file
2. Add clear comments explaining what it does
3. Include hardware requirements
4. Test on real hardware
5. Submit a pull request

Good example ideas:
- BLE audio streaming
- Multi-microphone setup
- Custom frequency mappings
- Integration with specific LED libraries
- MQTT publishing of audio data
- Web-based visualization

## Support

- **Issues**: GitHub issue tracker
- **Discord**: WLED MM community
- **Documentation**: See docs/ folder

---

**Last Updated**: February 26, 2026  
**Maintainer**: WLED MM Contributors  
**License**: EUPL-1.2 or later

