# physicslfo~ - Physics-Based LFO for Max/MSP

A signal-rate LFO external that generates modulation curves using realistic physics simulations, providing organic and musical modulation shapes that evolve naturally over time.

## Features

- **6 Physics Simulation Types**: Bounce, damped decay, enhanced spin, overshoot, multi-bounce, and wobble
- **lores~ Pattern**: 4 signal inlets accept both signals and floats with proper signal/float handling
- **Looping vs Envelope Modes**: Continuous cycling or one-shot physics simulations
- **Natural Physics Range**: 0.0 to 1.0 output (unipolar) representing natural physics behavior
- **Bang Trigger Support**: Phase reset and envelope triggering functionality
- **Real-Time Parameter Control**: All parameters modifiable during playback
- **Universal Binary**: Compatible with Intel and Apple Silicon Macs

## Physics Types

### Type 0: Bounce (Gravity)
- **Behavior**: Simple gravitational bounce with variable curve
- **Parameter**: Bounce curve (0=droopy/slow, 1=sharp/fast)
- **Damping**: Energy loss rate per bounce
- **Physics**: Parabolic trajectory with continuous energy decay

### Type 1: Damped Decay (Ring-out)
- **Behavior**: Exponential decay envelope with oscillations inside (like struck bell)
- **Parameter**: Vibration frequency inside decay (3-15 Hz range)
- **Damping**: Decay rate (0=long ring, 1=quick stop)
- **Physics**: True damped oscillation with frequency drift

### Type 2: Enhanced Bounce with Spin
- **Behavior**: Complex bouncing ball with multi-frequency spin and wobble
- **Parameter**: Complex spin rate & intensity (0=simple, 1=complex harmonics)
- **Damping**: Energy loss rate affecting both bounce and spin
- **Physics**: Primary/secondary spin frequencies with wobble modulation

### Type 3: Overshoot (Settles to Equilibrium)
- **Behavior**: Step response with overshoot that eventually settles
- **Parameter**: Overshoot intensity (0=gentle, 1=dramatic)
- **Damping**: Settling speed and oscillation decay
- **Physics**: Approaches equilibrium with decaying oscillations

### Type 4: Multi-bounce (Comes to Complete Rest)
- **Behavior**: Multiple bounces with realistic decay, eventually stops
- **Parameter**: Number of bounces per cycle (2-8 range)
- **Damping**: How quickly energy dissipates and motion stops
- **Physics**: Exponential energy loss per bounce with complete stop

### Type 5: Wobble (Settles to Equilibrium)
- **Behavior**: Frequency beating that eventually stabilizes
- **Parameter**: Frequency spread for beating complexity (0=simple, 1=complex)
- **Damping**: Settling speed to equilibrium position
- **Physics**: Dual-frequency beating with exponential decay

## Usage

### Basic Instantiation
```
[physicslfo~]                    // Default: bounce type, 0.5 physics param, 0.1 damping
[physicslfo~ 1]                  // Damped decay type
[physicslfo~ 2 0.7 0.3]          // Enhanced spin with high intensity, medium damping
```

### Signal Connections
```
[sig~ 1.0]  [2]  [0.6]  [0.2]    // 1Hz, enhanced spin, high intensity, light damping
     |       |     |      |
   [physicslfo~]               // Physics LFO
     |
   [scale 0. 1. 200. 8000.]     // Scale to filter frequency range
     |
   [lores~ 0.7]                 // Apply modulation
```

### Mode Control
```
[looping 1]                      // Enable looping mode (default)
[looping 0]                      // Enable envelope mode
[bang(                          // Reset/trigger physics simulation
[phase 0.5]                     // Set phase (looping mode only)
```

## Parameters

### Inlets
1. **Frequency/Trigger** (signal/float/bang)
   - Signal: Frequency in Hz (0-1000 Hz)
   - Float: Set default frequency
   - Bang: Reset phase and physics state

2. **Physics Type** (signal/float/int, 0-5)
   - Select physics simulation type
   - Prints parameter info to Max console when changed

3. **Physics Parameter** (signal/float, 0.0-1.0)
   - Type-specific parameter (see physics types above)
   - Controls intensity, frequency, or curve characteristics

4. **Damping** (signal/float, 0.0-1.0)
   - Energy loss rate or settling speed
   - Higher values = faster decay/settling

### Messages
- **looping 1/0**: Switch between looping and envelope modes
- **phase \<float\>**: Set phase position (0.0-1.0) in looping mode only

### Output
- **Signal Range**: 0.0 to 1.0 (unipolar, natural physics range)
- **Behavior**: Physics simulations evolve naturally over time

## Modes

### Looping Mode (Default)
- Continuous physics simulation
- Phase resets when reaching 1.0
- Bang resets phase and physics state
- Suitable for ongoing modulation

### Envelope Mode
- One-shot physics simulation triggered by bang
- Physics play out naturally until energy dissipates
- No artificial cutoffs - simulations settle naturally
- Perfect for transient modulation and envelope generation

## Musical Applications

### Filter Sweeps with Character
```
[sig~ 0.3]                      // Slow sweep
|
[physicslfo~ 1]                 // Damped decay with vibrations
|
[scale 0. 1. 300. 3000.]        // Map to filter range
|
[lores~ 0.8]                    // Apply to audio
```

### Organic Tremolo
```
[sig~ 4.5]                      // Tremolo rate
|
[physicslfo~ 3]                 // Overshoot settling
|
[scale 0. 1. 0.7 1.3]           // Amplitude modulation range
|
[*~ audio_signal]               // Apply to audio
```

### Envelope Generation
```
[looping 0]                     // Envelope mode
|
[physicslfo~ 1 0.3 0.4]         // Damped decay, medium freq, medium decay
|
[bang(                          // Trigger envelope
|
[scale 0. 1. 0. 1.]             // Use as amplitude envelope
```

### Complex Rhythmic Modulation
```
[sig~ 2.0]                      // 2Hz base rate
|
[physicslfo~ 2 0.8 0.2]         // Enhanced spin, high complexity
|
[*~ 100]                        // Scale for frequency modulation
|
[+~ 440]                        // Add to carrier
```

## Technical Specifications

### Performance
- **CPU Usage**: Minimal (~0.1% per instance at 48kHz)
- **Latency**: Zero-latency signal processing
- **Precision**: 64-bit floating point throughout
- **Real-time Safe**: All physics calculations optimized for audio thread

### Physics Accuracy
- **Envelope Mode**: Natural physics evolution, no artificial cutoffs
- **Energy Conservation**: Realistic energy dissipation models
- **Frequency Drift**: Simulates real-world frequency changes during decay
- **Settling Behavior**: Proper convergence to equilibrium or complete stop

### Input Specifications
- **Frequency Range**: 0.0 to 1000.0 Hz
- **Parameter Range**: 0.0 to 1.0 (all physics parameters)
- **Signal Compatibility**: Full signal-rate modulation support

## Build Instructions

### Requirements
- CMake 3.19 or later
- Xcode command line tools (macOS)
- Max SDK base (included in repository)

### Building
```bash
cd source/audio/physicslfo~
mkdir build && cd build
cmake -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" ..
cmake --build .

# For Apple Silicon Macs
codesign --force --deep -s - ../../../externals/physicslfo~.mxo
```

### Verification
```bash
# Check universal binary
file ../../../externals/physicslfo~.mxo/Contents/MacOS/physicslfo~

# Test in Max
# Load physicslfo~.maxhelp to verify functionality
```

## Development History

This external demonstrates advanced Max SDK patterns:

1. **lores~ Pattern**: Proper signal/float inlet handling with connection detection
2. **Physics Simulation**: Real-time physics calculations optimized for audio
3. **Mode Switching**: Runtime behavior changes without audio artifacts
4. **Natural Evolution**: Physics simulations that settle or stop naturally
5. **Universal Binary**: Cross-architecture compatibility

### Key Discoveries
- **MSP Signal/Float Conflicts**: Max provides zero-signals to all `dsp_setup()` inlets, requiring careful parameter handling
- **Natural Physics Behavior**: Envelope mode should let physics play out rather than artificially cutting off
- **Energy Dissipation**: Proper stop conditions prevent perpetual motion artifacts

## Files

- `physicslfo~.c` - Main external implementation with 6 physics types
- `CMakeLists.txt` - Build configuration for universal binary
- `README.md` - This comprehensive documentation
- `physicslfo~.maxhelp` - Interactive help file with examples for all physics types

## Compatibility

- **Max/MSP**: Version 8.0 or later
- **macOS**: 10.14 or later (universal binary)
- **Windows**: Not currently supported

## See Also

- **Max Objects**: `lfo~`, `cycle~`, `phasor~` for basic modulation
- **Related Externals**: `easelfo~` for easing function LFOs, `tide~` for complex waveshaping
- **Help File**: `physicslfo~.maxhelp` for interactive examples and presets

---

*Physics-based LFO generation brings the natural evolution and organic character of real physical systems to Max/MSP modulation, creating more musical and expressive parameter automation.*