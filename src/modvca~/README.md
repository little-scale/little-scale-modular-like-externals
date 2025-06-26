# modvca~ - MODDEMIX-Style VCA for Max/MSP

A Make Noise MODDEMIX-style VCA external featuring amplitude-dependent distortion where low levels exhibit rich harmonic content and high levels remain clean, creating musical "tail character" during signal decay.

## Features

- **Linear VCA Response**: Direct 1:1 CV-to-amplitude control (CV 0.5 = 50% volume)
- **Amplitude-Dependent Distortion**: Inverse relationship between amplitude and saturation
- **Smooth Saturation Transition**: Linear drive interpolation eliminates discontinuities
- **Unity Gain Passthrough**: Clean signal at full CV levels
- **Real-Time Control**: Sample-accurate parameter modulation
- **lores~ Pattern**: Seamless signal/float dual input on CV inlet

## Quick Start

### Installation
1. Build the external using CMake:
   ```bash
   cd source/audio/modvca~/build
   cmake -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" ..
   cmake --build .
   ```
2. The built `modvca~.mxo` will be in the `externals/` directory
3. Codesign for Apple Silicon: `codesign --force --deep -s - modvca~.mxo`

### Basic Usage
```
[sine~ 440] → [modvca~ 0.8] → [dac~]
                     |
              [line~ 1000 0]
```

## How It Works

### Amplitude-Dependent Behavior
- **High Amplitude** (CV near 1.0): Clean, minimal saturation
- **Medium Amplitude** (CV around 0.5): Moderate harmonic content  
- **Low Amplitude** (CV near 0.0): Rich harmonic saturation

### Technical Implementation
The external uses **linear drive interpolation** for smooth saturation:

```c
// Calculate drive amount inversely proportional to amplitude
drive = MAX_DRIVE * (1.0 - amplitude) + MIN_DRIVE;

// Apply saturation with drive compensation
saturated = tanh(input * drive) / drive;
```

This creates a perfectly smooth transition from clean highs to saturated lows without discontinuities.

## Parameters

### Inlets
1. **Audio Input** (signal) - Audio signal to process through VCA
2. **CV/Level** (signal/float, 0.0-1.0) - Amplitude control
   - 0.0 = VCA closed, maximum saturation
   - 1.0 = VCA fully open, minimal saturation

### Outlets  
1. **VCA Output** (signal) - Processed audio with amplitude-dependent character

## Musical Applications

### Percussive Instruments
- **Clean attacks** with **harmonic decay tails**
- Natural evolution of timbre during note decay
- Adds warmth without affecting transients

### Sustained Tones
- **Dynamic character changes** with volume automation
- Crossfades between clean and harmonically rich textures
- Musical response to envelope followers and LFOs

### Mix Processing
- **Level-dependent analog warmth**
- Dynamic saturation that responds to music dynamics
- Enhances quiet details while keeping loud parts clean

## Examples

### Envelope-Controlled VCA
```
[noise~] → [modvca~] → [*~ 0.3] → [dac~]
               |
        [adsr~ 10 100 0.3 500]
```
Creates natural decay with increasing harmonic content as the envelope fades.

### LFO Tremolo with Character
```
[saw~ 220] → [modvca~] → [dac~]
                 |
           [lfo~ 2 0.5 0.5]  
```
Tremolo effect where quiet portions develop rich harmonics.

### Dynamic Mix Processing
```
[adc~] → [modvca~] → [dac~]
             |
      [envelope~ 512]
```
Adds harmonic character that follows the input signal's dynamics.

## Technical Specifications

### Performance
- **~8 operations per sample** (highly optimized)
- **Minimal memory usage** (no buffers or lookup tables)
- **Real-time safe** (no allocations in audio thread)

### Frequency Response
- **High Amplitude**: Flat response, unity gain passthrough
- **Low Amplitude**: Rich harmonic content with soft limiting

### Drive Range
- **Maximum Drive**: 8.0 (at zero amplitude)
- **Minimum Drive**: 0.1 (at full amplitude)
- **Linear interpolation** between extremes

## Build Requirements

- **Max/MSP SDK** (included in max-sdk-main)
- **CMake 3.19+**
- **macOS**: Xcode Command Line Tools
- **Windows**: Visual Studio with C++ support

### Universal Binary (macOS)
The external builds as a universal binary supporting both Intel and Apple Silicon:
```bash
cmake -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" ..
```

## Development Notes

This external demonstrates several advanced Max SDK patterns:

- **Linear Algorithm Design**: Simple relationships for predictable musical control
- **lores~ Pattern**: Perfect signal/float dual input handling
- **Drive Compensation**: Maintaining consistent levels across amplitude range
- **Smooth Transitions**: Eliminating discontinuities in saturation behavior

For detailed development history and technical analysis, see `CLAUDE.md` in this directory.

## Comparison with Original Hardware

### MODDEMIX VCA Characteristics Retained
- ✅ Amplitude-dependent distortion behavior
- ✅ Clean highs, saturated lows
- ✅ Musical tail character during decay
- ✅ Smooth saturation transitions

### Digital Enhancements
- **Perfect parameter recall** and automation
- **Sample-accurate CV control** 
- **No noise floor** or component drift
- **Multiple instances** without additional cost

## License

This external is part of the Max SDK and follows the same licensing terms.

## Contributing

1. Follow the existing code style and patterns
2. Test thoroughly with various input signals and CV sources
3. Update documentation for any changes
4. Ensure universal binary compatibility

---

*The modvca~ external brings the unique character of the Make Noise MODDEMIX VCA to the digital domain, providing smooth amplitude-dependent saturation that enhances musical expression in Max/MSP environments.*