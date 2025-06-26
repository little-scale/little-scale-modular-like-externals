# decay~ - Exponential Decay Envelope Generator for Max/MSP

A versatile exponential decay envelope generator with customizable curve shaping and optional click-free start smoothing, perfect for drum envelopes, pluck synthesis, and any application requiring exponential decay behavior.

## Features

- **Exponential Decay**: Mathematically accurate exponential decay curves
- **Curve Shaping**: Exponential/linear/logarithmic envelope shapes (-3.0 to 3.0)
- **Click-Free Starts**: User-configurable smoothing (0-100 samples) eliminates artifacts
- **Real-Time Control**: Sample-accurate parameter modulation via signal inputs
- **Retrigger Modes**: From peak or from current level
- **lores~ Pattern**: Seamless signal/float dual input on time and curve parameters

## Quick Start

### Installation
1. Build the external using CMake:
   ```bash
   cd source/audio/decay~/build
   cmake -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" ..
   cmake --build .
   ```
2. The built `decay~.mxo` will be in the `externals/` directory
3. Codesign for Apple Silicon: `codesign --force --deep -s - decay~.mxo`

### Basic Usage
```
[bang] → [decay~ 1.0 0.0 1.0 10] → [*~ 0.5] → [dac~]
              ↑    ↑   ↑   ↑
           time curve peak smooth
```

## Parameters

### Creation Arguments
```
[decay~ decay_time curve peak smooth_samples]
```

1. **decay_time** (0.001-60.0 seconds, default 1.0)
   - Time for envelope to decay from peak to zero
   - 0.001s = 1ms for sharp transients
   - 60.0s = very long atmospheric decays

2. **curve** (-3.0 to 3.0, default 0.0)
   - -3.0 to -0.1: Exponential curves (fast start, slow end)
   - 0.0: Linear decay
   - 0.1 to 3.0: Logarithmic curves (slow start, fast end)

3. **peak** (0.0-1.0, default 1.0)
   - Maximum amplitude of the envelope
   - Can be changed runtime with `peak` message

4. **smooth_samples** (0-100, default 0)
   - Number of samples for start smoothing to eliminate clicks
   - 0 = no smoothing (original sharp attack)
   - 5-10 = light smoothing (~0.1-0.2ms at 44.1kHz)
   - 20-50 = heavy smoothing (~0.5-1.1ms at 44.1kHz)

### Inlets
1. **Messages** - bang (trigger), peak (float), retrig (0/1)
2. **Time** (signal/float) - Decay time in seconds
3. **Curve** (signal/float) - Curve shaping parameter

### Messages
```
bang           // Trigger envelope decay
peak 0.8       // Set peak amplitude to 0.8
retrig 0       // Retrigger from current level
retrig 1       // Retrigger from peak (default)
```

## Examples

### Drum Machine Envelopes

**Kick Drum** (punchy, short decay):
```
[decay~ 0.8 -1.5 1.0 5]
```
- 0.8s decay time
- Exponential curve (-1.5) for punch
- Full amplitude
- 5 samples smoothing to eliminate click

**Snare Drum** (medium decay with character):
```
[decay~ 0.3 -0.5 0.9 8]
```

**Hi-Hat** (very short, linear):
```
[decay~ 0.1 0.0 0.7 3]
```

### Synthesis Applications

**Piano/Pluck Envelope**:
```
[noise~] → [decay~ 2.0 -1.0 1.0 0] → [lores~ 2000] → [dac~]
```
- Natural exponential decay
- No smoothing for authentic pluck attack

**Pad Release Envelope**:
```
[saw~ 220] → [*~ envelope] → [decay~ 3.0 1.0 1.0 50] → [dac~]
```
- Long logarithmic decay
- Heavy smoothing for gentle release

### Parameter Automation

**Sweeping Decay Time**:
```
[line~ 0.1 5.0 10000] → [decay~ inlet_2]
              ↓
    [decay~ 1.0 0.0 1.0 10]
```

**LFO Curve Modulation**:
```
[lfo~ 0.5] → [scale 0. 1. -2. 2.] → [decay~ inlet_3] 
                        ↓
            [decay~ 1.5 0.0 1.0 15]
```

## Curve Shaping Guide

### Exponential Curves (curve < 0)
- **-0.5**: Slightly punchy attack
- **-1.0**: Moderate exponential (good for drums)
- **-2.0**: Sharp exponential (aggressive percussion)
- **-3.0**: Very sharp (electronic hits)

### Linear Curves (curve = 0)
- **0.0**: Even, mathematical decay (reference)

### Logarithmic Curves (curve > 0)
- **0.5**: Slightly gentle attack
- **1.0**: Moderate logarithmic (good for pads)
- **2.0**: Very gentle (atmospheric)
- **3.0**: Extremely gentle (ambient)

## Click Elimination Guide

### Smoothing Amount Selection

**No Smoothing (0 samples)**:
- Use when you want the sharpest possible attack
- Good for electronic percussion where click is desired
- Preserves original envelope character

**Light Smoothing (1-10 samples)**:
- Removes clicks while maintaining sharp character
- Good for most percussion applications
- ~0.02-0.23ms duration at 44.1kHz

**Medium Smoothing (11-30 samples)**:
- Noticeable but still quick attack softening
- Good for problematic source material
- ~0.25-0.68ms duration at 44.1kHz

**Heavy Smoothing (31-100 samples)**:
- Very gentle envelope starts
- Good for pad releases and atmospheric sounds
- ~0.7-2.3ms duration at 44.1kHz

## Technical Specifications

### Algorithm
- **Exponential decay**: `coeff = exp(-1.0 / (decay_time * sr))`
- **Per-sample update**: `envelope *= decay_coeff`
- **Curve shaping**: Power function applied to linear progress
- **Start smoothing**: Linear interpolation over N samples

### Performance
- **~15 operations per sample** (including curve shaping)
- **Minimal memory usage** (no buffers or lookup tables)
- **Real-time safe** (no allocations in audio thread)
- **Smart smoothing** (only processes when active)

### Frequency Response
- **No frequency coloration** (pure amplitude envelope)
- **Anti-click smoothing** doesn't affect frequency content
- **Sample-accurate timing** for precise musical control

## Musical Applications

### Electronic Music Production
- **Drum machine envelopes** with customizable punch
- **Bass pluck synthesis** with natural decay
- **Percussion layering** with varied curve shapes

### Sound Design
- **Impact sounds** with sharp exponential curves
- **Ambient textures** with long logarithmic decays
- **Granular synthesis** grain envelopes

### Live Performance
- **Real-time envelope morphing** via curve parameter
- **Dynamic decay time control** for expression
- **Click-free triggering** for clean live sets

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

- **Mathematical Precision**: Accurate exponential decay implementation
- **lores~ Pattern**: Perfect signal/float dual input handling
- **User Customization**: Configurable smoothing via creation arguments
- **Smart Processing**: Conditional smoothing activation only when needed
- **State Management**: Clean envelope lifecycle with proper termination

For detailed development history and technical analysis, see `CLAUDE.md` in this directory.

## Comparison with Max Built-ins

### vs `adsr~`
- **`adsr~`**: Full ADSR envelope with sustain stage
- **`decay~`**: Specialized decay-only with curve shaping and smoothing

### vs `function~`
- **`function~`**: Arbitrary breakpoint envelopes
- **`decay~`**: Mathematical exponential decay with real-time control

### Unique Features
- ✅ **User-configurable click elimination**
- ✅ **Real-time curve morphing**
- ✅ **Mathematical exponential accuracy**
- ✅ **Lightweight performance**

## License

This external is part of the Max SDK and follows the same licensing terms.

## Contributing

1. Follow the existing code style and patterns
2. Test thoroughly with various parameter ranges
3. Update documentation for any changes
4. Ensure universal binary compatibility

---

*The decay~ external provides professional-quality exponential decay envelope generation with configurable smoothing, bringing mathematical precision and musical flexibility to Max/MSP environments.*