# vactrol~ - Project CLAUDE.md

## Project Overview

**vactrol~** is a Max/MSP external that emulates the VTL5C3 vactrol (voltage-controlled resistor) used in classic low pass gates. This project demonstrates advanced MSP audio processing techniques, real-time parameter control, and authentic analog circuit modeling.

## Key Technical Achievements

### 🎯 **Core Algorithm Implementation**
- **Exponential Decay Model**: Authentic VTL5C3 resistance curve R(t) = R_max - (R_max - R_min) * e^(-t/τ)
- **RC Filter Modeling**: Cutoff frequency calculation fc = 1/(2π * R * C) with 47nF capacitance
- **Asymmetric Tube Saturation**: Variable positive/negative drive characteristics
- **Sample-Accurate Triggering**: Bang-based envelope triggering with retriggering support

### 🔧 **MSP Architecture Patterns**
- **Multi-Inlet Structure**: 3-inlet design (audio, CV, bang) using `dsp_setup()` + `proxy_new()`
- **Real-Time Parameter Control**: Message handlers for `decay`, `poles`, `drive`, `character`
- **Signal/Float Inlet Handling**: Proper CV signal processing with 0-1 range mapping
- **Proxy Inlet Management**: Bang messages routed to inlet 2 via proxy system

### 🎛️ **Parameter System**
- **Poles**: 1-pole (6dB/octave) or 2-pole (12dB/octave) filtering with enhanced 2-pole behavior
- **Decay Time**: 50ms to 500ms exponential decay with musical responsiveness
- **Tube Drive**: 0.0-1.0 saturation amount with level compensation
- **Character**: 0.01-1.0 asymmetric clipping (clamped above 0 to prevent division by zero)

## Development History & Lessons Learned

### 🐛 **Critical Bug Fixes**
1. **Decay Direction Issue**: Initial implementation had reversed decay curve
   - **Problem**: Started dark, went bright (opposite of vactrol behavior)
   - **Solution**: Corrected formula to R(t) = R_max - (R_max - R_min) * e^(-t/τ)

2. **Inlet Description Swap**: Assist strings for inlets 2 and 3 were swapped
   - **Problem**: CV inlet showed as "Trigger", Bang inlet showed as "CV"
   - **Solution**: Corrected `vactrol_assist()` function order

3. **2-Pole Decay Responsiveness**: 2-pole mode wasn't dramatically different from 1-pole
   - **Problem**: Same cutoff frequency for both modes
   - **Solution**: Reduce effective cutoff by 20% in 2-pole mode for more pronounced effect

4. **Character Parameter Instability**: Division by zero when character = 0.0
   - **Problem**: `negative_drive = scaled_drive * character` caused division by zero
   - **Solution**: Clamp character parameter to minimum 0.01 instead of 0.0

### 🎨 **Design Decisions**
- **Default to 1-pole**: Changed from 2-pole to 1-pole default for classic LPG behavior
- **Buchla-style Defaults**: 150ms decay, 0.7 tube drive, 0.7 character for vintage warmth
- **CV Input Integration**: Parallel CV and envelope control with "brighter wins" logic
- **Message System**: Reliable message handlers instead of Max attribute system

## Build Configuration

### **CMakeLists.txt**
Standard Max SDK configuration with universal binary support:
```cmake
cmake_minimum_required(VERSION 3.19)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)
# ... standard Max SDK build pattern
```

### **Build Commands**
```bash
cd source/audio/vactrol~/build
cmake -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" ..
cmake --build .
```

## File Structure

```
source/audio/vactrol~/
├── vactrol~.c          # Main implementation (331 lines)
├── CMakeLists.txt      # Build configuration
├── README.md           # User documentation
├── CLAUDE.md           # This project guide
└── build/              # Build directory
```

```
help/
└── vactrol~.maxhelp    # Max patch demonstrating usage
```

## Technical Specifications

### **Performance Characteristics**
- **CPU Usage**: Optimized for real-time performance
- **Latency**: Zero latency processing
- **Sample Rate**: Any supported by Max/MSP
- **Architecture**: Universal binary (Intel + Apple Silicon)

### **Signal Processing Details**
- **Resistance Range**: 100Ω (bright) to 1MΩ+ (dark)
- **Capacitance**: 47nF (authentic VTL5C3 specification)
- **Filter Types**: 1-pole and cascaded 2-pole low-pass
- **Saturation**: Asymmetric tanh with positive/negative drive scaling

## Usage Patterns

### **Basic Low Pass Gate**
```
vactrol~ 1 0.1 0.5
```

### **Buchla-Style Processing**
```
vactrol~ 2 0.15 0.7
```

### **CV Control Integration**
- Connect audio to inlet 1
- Connect 0-1 CV signal to inlet 2
- Send bang messages to inlet 3
- Both CV and envelope control work simultaneously

## Future Development Notes

### **Potential Enhancements**
- **Multiple Vactrol Types**: Add LDR, VTL5C1, VTL5C2 models
- **Attack Time Control**: Currently fixed at ~2-5ms
- **Stereo Processing**: Dual-channel version
- **Preset System**: Save/recall parameter combinations

### **Performance Optimizations**
- **SIMD Processing**: Vector optimization for multiple instances
- **Lookup Tables**: Pre-computed exponential curves
- **Parameter Smoothing**: Reduce zipper noise on parameter changes

## Development Environment

- **Max SDK**: Standard Max SDK patterns and best practices
- **Universal Binary**: Apple Silicon + Intel compatibility
- **Zero Dependencies**: Pure C implementation with Max SDK only
- **Real-time Safe**: No allocations or system calls in perform routine

---

**Status**: Production ready - comprehensive testing completed, universal binary built, full documentation provided.

**Sound Quality**: Authentic vactrol behavior with musical responsiveness and vintage character. The exponential decay curve and tube saturation create the characteristic "bloom" and harmonic warmth of classic analog low pass gates.