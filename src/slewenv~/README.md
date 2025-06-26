# slewenv~

A Max/MSP external that recreates the function generator capabilities of the Make Noise Maths module. This audio-rate integrator provides configurable rise and fall times with exponential-to-logarithmic curve shaping and looping functionality.

## Features

- **5-inlet design** for comprehensive real-time control
- **Integrator-based algorithm** mimicking analog capacitor charging/discharging
- **Variable curve shapes**: Exponential (punchy/plucky) to logarithmic (smooth/drawn-out)
- **Flexible timing range**: 10ms to 10+ seconds per phase
- **Loop mode**: Continuous envelope cycling
- **Audio-rate processing**: Signal-rate parameter modulation supported
- **Universal binary**: Compatible with Intel and Apple Silicon Macs

## Installation

1. Build the external using CMake:
   ```bash
   cd source/audio/slewenv~
   mkdir build && cd build
   cmake -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" ..
   cmake --build .
   ```

2. The compiled external will appear in `externals/slewenv~.mxo`

3. Copy to your Max externals folder or use directly from the SDK directory

## Usage

### Creation Arguments and Attributes

#### Creation Arguments
```
[slewenv~ rise_time fall_time linearity]
```

- **rise_time** (float, 0.001-1.0): Default rise time (default: 0.1)
- **fall_time** (float, 0.001-1.0): Default fall time (default: 0.1)  
- **linearity** (float, -1.0-1.0): Default curve shape (default: 0.0 = linear)

#### Attributes
```
[slewenv~ @looping 1]
[slewenv~ 0.1 0.2 @looping 1]
[message @looping 1( → [slewenv~]
```

- **@looping** (0 or 1): Loop mode control, same as inlet 2 (aliases: @loop)
  - 0 = one-shot mode (default)
  - 1 = continuous looping mode
  - Appears in Max Inspector as "Loop Mode" checkbox
  - Bidirectionally synced with inlet 2 messages

### Inlets

1. **Trigger** (float): Triggers envelope output. Value > 0.0 starts rise from current position and scales output amplitude
2. **Loop Mode** (float/signal): Non-zero enables looping, zero disables
3. **Rise Time** (float/signal): Normalized 0-1 value controlling attack duration
4. **Fall Time** (float/signal): Normalized 0-1 value controlling decay duration  
5. **Linearity** (float/signal): Curve shape from -1 (exponential) through 0 (linear) to 1 (logarithmic)

### Outlets

1. **Envelope Output** (signal): Generated envelope scaled by trigger amplitude

## Behavior

### Make Noise Maths Emulation

The external emulates the integrator behavior of the Make Noise Maths module:

- **Trigger**: Starts rise phase from current output level (not from zero)
- **Rise Phase**: Integrates upward at rate determined by rise time
- **Peak**: Automatically transitions to fall phase when reaching maximum (1.0)
- **Fall Phase**: Integrates downward at rate determined by fall time
- **Bottom**: Stops at minimum (0.0) in one-shot mode, or restarts cycle in loop mode

### Loop Modes

- **One-shot mode** (loop = 0): Trigger → Rise → Fall → Stop at zero
- **Loop mode** (loop ≠ 0): Trigger → Rise → Fall → Rise → Fall → (continues cycling)

## Usage Examples

### Basic Triggered Envelope
```
[1.0( → [slewenv~] → [*~ 0.5] → [dac~]
```

### Creation with Arguments (fast exponential pluck)
```
[slewenv~ 0.01 0.05 -0.8]
[1.0( → [slewenv~ 0.01 0.05 -0.8] → [*~ 0.5] → [dac~]
```

### Looping with Parameter Control
```
[1.0( → [inlet 1] (trigger)
[1.0( → [inlet 2] (loop on)
[0.2( → [inlet 3] (moderate rise)
[0.8( → [inlet 4] (slow fall)
[0.5( → [inlet 5] (logarithmic curve)
```

### Signal-Rate Modulation
```
[phasor~ 0.1] → [*~ 0.5] → [+~ 0.25] → [inlet 3] (modulated rise time)
[lfo~ 0.05] → [*~ 0.3] → [inlet 5] (modulated curve shape)
```

## Curve Characteristics

### Exponential Mode (linearity < 0)
- **Use case**: Punchy, plucky envelopes
- **Character**: Fast initial change, then slower
- **Timing**: Good for percussion, transients, impacts
- **Range**: -1.0 (very exponential) to -0.1 (slightly exponential)

### Linear Mode (linearity ≈ 0)  
- **Use case**: Smooth, predictable envelopes
- **Character**: Constant rate of change
- **Timing**: Even progression through time
- **Range**: -0.1 to 0.1

### Logarithmic Mode (linearity > 0)
- **Use case**: Slow, drawn-out evolution
- **Character**: Slow initial change, then faster
- **Timing**: Extended, smooth transitions
- **Range**: 0.1 (slightly logarithmic) to 1.0 (very logarithmic)
- **Best for**: Ambient textures, long-form composition, subtle changes

## Timing Ranges

The rise and fall time controls use exponential scaling to provide access to a wide range of durations:

- **0.001**: ~10ms (very fast, good for clicks/transients)
- **0.01**: ~100ms (fast envelope, percussion)
- **0.1**: ~1 second (moderate envelope, musical phrases)
- **0.3**: ~3 seconds (slow envelope)
- **0.5**: ~5 seconds (very slow)
- **1.0**: ~10 seconds (extremely slow, ambient)

**Note**: Logarithmic curves (linearity > 0) extend these times further for even longer evolution.

## Technical Notes

- **Sample Rate Adaptive**: Automatically adjusts to host sample rate
- **Real-time Safe**: No allocations or blocking operations in audio thread
- **Integrator Algorithm**: True capacitor-like charging/discharging behavior
- **Parameter Smoothing**: Instant parameter updates without artifacts
- **CPU Efficient**: Optimized integration suitable for multiple instances
- **Thread Safe**: Message handlers update parameters atomically

## Algorithm Details

### Integrator Model

Unlike traditional ADSR envelopes that use phase-based calculations, `slewenv~` implements a true integrator model:

```
output(n) = output(n-1) + rate_per_sample

where rate_per_sample = 1.0 / (time_seconds * sample_rate)
```

### Curve Shaping

Curves are applied by modifying the integration rate based on current output level:

- **Exponential**: Rate decreases as output approaches target (fast start, slow end)
- **Linear**: Constant rate throughout
- **Logarithmic**: Rate increases as output approaches target (slow start, fast end)

This creates natural-feeling curves that respond organically to parameter changes.

## Build Requirements

- Max SDK 8.2 or later
- CMake 3.19 or later
- macOS with Xcode command line tools
- Optional: Max/MSP for testing

## Inspiration

This external recreates the functionality of the Make Noise Maths module's function generator, providing Max users with the same powerful envelope shaping capabilities in software form. The integrator approach and extreme timing range make it suitable for everything from percussive transients to long-form ambient evolution.

## Future Development

**TODO**: Check timing and curve values against a real Make Noise Maths module for accuracy. Current timing ranges and curve characteristics are approximations based on documentation and should be verified against hardware measurements for precise emulation.

## License

Part of the Max SDK - see Max SDK license for usage terms.

---

*Built with the Max SDK using proven patterns from working codec externals. Demonstrates advanced MSP inlet handling and real-time parameter control.*