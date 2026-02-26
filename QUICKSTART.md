# AudioReactive Library - Quick Start Guide

This guide helps you quickly get started with the AudioReactive libraries using PlatformIO.

## 🚀 Quick Start

### 1. Install PlatformIO

If you haven't already, install PlatformIO:
- **VSCode**: Install the PlatformIO IDE extension
- **Command Line**: `pip install platformio`

### 2. Clone or Download

```bash
cd /your/projects/directory
git clone <this-repo>
cd WLED-AudioReactive-Usermod
```

### 3. Build and Upload

```bash
# Build for ESP32 (default)
pio run -e esp32dev

# Upload to ESP32
pio run -e esp32dev -t upload

# Monitor serial output
pio device monitor
```

Or use the PlatformIO GUI in VSCode!

## 📋 Available Environments

### Production Builds

- **esp32dev** - ESP32 classic (recommended)
- **esp32-s3** - ESP32-S3 (best performance)
- **esp32-s2** - ESP32-S2 (limited, no dual-core)
- **esp32-c3** - ESP32-C3 (RISC-V, limited)

### Development Builds

- **esp32dev-debug** - With debug logging enabled
- **esp32dev-fast** - Optimized for performance

### Upload Options

- **esp32dev-ota** - Over-the-air updates (set IP first!)

## 🔧 Hardware Setup

### Microphone Connections

#### INMP441 (recommended)
```
INMP441 → ESP32
─────────────────
WS      → GPIO 15
SD      → GPIO 32
SCK     → GPIO 14
L/R     → GND
VDD     → 3.3V
GND     → GND
```

#### SPH0645
```
SPH0645 → ESP32
─────────────────
LRCL    → GPIO 15
DOUT    → GPIO 32
BCLK    → GPIO 14
SEL     → GND (or 3.3V)
VDD     → 3.3V
GND     → GND
```

#### ICS-43434
```
ICS-43434 → ESP32
─────────────────
WS      → GPIO 15
SD      → GPIO 32
SCK     → GPIO 14
L/R     → GND
VDD     → 3.3V
GND     → GND
```

### Custom Pin Configuration

Edit `example_standalone.ino` or `complete_pipeline.ino`:
```cpp
#define I2S_WS_PIN 15  // Change these
#define I2S_SD_PIN 32
#define I2S_SCK_PIN 14
```

## 📊 Expected Output

### example_standalone.ino
```
AudioReactive Library Example
=============================
✓ AudioFilters configured (DC blocker)
✓ AGCController configured (Normal preset, AGC enabled)
✓ I2S microphone initialized

Starting audio processing...
Columns: Volume(AGC) | Volume(Raw) | Sensitivity | Pressure | Peak
-----------------------------------------------------------------------
  45.2 |    123  |   87.3  |   62.1  | PEAK!
  52.1 |    145  |   91.2  |   68.5  |
```

### complete_pipeline.ino
```
====================================
  Complete Audio Processing Demo
====================================
✓ AudioFilters configured
✓ AGCController configured
✓ AudioProcessor initialized
✓ FFT task started on Core 0

Output Format:
Vol | Peak Freq | FFT Bands (16 channels) | Status
------------------------------------------------------------
 87 | 523Hz | 5427836542134679 | PEAK! LOUD
```

## 🔍 Troubleshooting

### No Audio Detected
1. Check microphone power (3.3V)
2. Verify pin connections
3. Try swapping L/R pin (GND ↔ 3.3V)
4. Increase serial baud rate: `monitor_speed = 115200`

### Compilation Errors
```bash
# Clean build
pio run -e esp32dev -t clean

# Update platform
pio pkg update

# Install dependencies
pio pkg install
```

### Upload Issues
```bash
# Find your port
pio device list

# Specify port manually
pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0
```

### Memory Issues
If you see memory errors, try:
- Use ESP32 classic (more RAM than S2/C3)
- Reduce FFT size in code
- Use `esp32dev` environment instead of debug

## 🎛️ Configuration Options

### Build Flags

Add to `platformio.ini` under your environment:
```ini
build_flags = 
    -DSAMPLE_RATE=22050      ; Change sample rate
    -DFFT_SIZE=256           ; Smaller FFT
    -DMIC_LOGGER             ; Enable audio logging
    -DSR_DEBUG               ; Enable debug output
```

### Monitor Filters

```ini
monitor_filters = 
    esp32_exception_decoder  ; Decode crashes
    log2file                 ; Save to file
    time                     ; Add timestamps
```

## 📚 Examples Included

### 1. example_standalone.ino
**Difficulty**: Beginner  
**Components**: AudioFilters + AGCController  
**Best for**: Learning basics, testing setup

### 2. complete_pipeline.ino
**Difficulty**: Intermediate  
**Components**: All three libraries  
**Best for**: Full audio visualization, production use

## 🔗 Useful Commands

```bash
# Build specific example
pio run -e esp32dev

# Upload and monitor in one command
pio run -e esp32dev -t upload -t monitor

# Clean everything
pio run -t clean

# Update all packages
pio pkg update

# Check environment
pio run -e esp32dev -t envdump

# Build all environments
pio run

# Run specific target
pio run -e esp32-s3 -t upload
```

## 🎯 Next Steps

1. ✅ Test `example_standalone.ino` first
2. ✅ Verify audio input works
3. ✅ Try `complete_pipeline.ino`
4. ✅ Experiment with settings
5. ✅ Build your own project!

## 📖 Documentation

- **Library API**: See `docs/README_REFACTORED.md`
- **Architecture**: See `docs/ARCHITECTURE.md`
- **Examples Guide**: See `examples/README.md`
- **Migration**: See `docs/MIGRATION_GUIDE.md`

## 💡 Tips

### Performance
- ESP32-S3 offers best performance (FPU + dual-core)
- Use `esp32dev-fast` for optimized builds
- Adjust SAMPLE_RATE if lagging

### Development
- Use `esp32dev-debug` for troubleshooting
- Monitor with `pio device monitor`
- Check serial plotter for visualizations

### Production
- Use `esp32dev` or `esp32-s3`
- Disable debug flags
- Test with actual music/sound

## 🐛 Common Issues

### Issue: "No module named 'serial'"
**Solution**: `pip install pyserial`

### Issue: "Upload failed"
**Solution**: 
- Hold BOOT button while uploading
- Check USB cable (data not just power)
- Try different USB port

### Issue: "Out of memory"
**Solution**:
- Use ESP32 classic (not S2/C3)
- Reduce FFT_SIZE
- Check `board_build.partitions`

### Issue: "I2S initialization failed"
**Solution**:
- Check pin numbers
- Verify microphone power
- Try different I2S pins

## 📞 Support

- **GitHub Issues**: Report bugs
- **Discord**: Community help
- **Documentation**: See `docs/` folder

## 🎉 Success!

If you see audio levels changing when you make noise, congratulations! The setup is working correctly. Now you can:

- Modify the code for your needs
- Connect LED strips for visualization
- Integrate into larger projects
- Contribute improvements!

Happy coding! 🎵✨

