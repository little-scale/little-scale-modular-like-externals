# little-scale modular-like externals

A comprehensive collection of modular synthesis externals for Max/MSP, featuring generators, LFOs, modulation sources, filters, and dynamics processors designed for creative sound synthesis and modular-style patching.

## Overview

This package provides 13 professional modular synthesis externals that bring the flexibility and creativity of modular synthesis to Max/MSP. Each external is designed to work seamlessly with others, enabling complex modular-style patches with professional sound quality.

**Enhanced with Max 9 Attribute System** - All externals feature autocomplete-compatible attributes for modern Max workflow integration.

## Generators (4 externals)

### **harmosc~** - Harmonic oscillator with additive synthesis
Computationally efficient harmonic oscillator with variable harmonic count and dynamic control.

**Key Features:**
- 1-64 harmonics with wavetable-based synthesis
- Automatic falloff curves and selective harmonic activation
- Custom amplitude arrays via list input
- Musical detuning with ±50 cents per harmonic

**Attributes:**
- `harmonic_mode` (0-2) - Harmonic selection (0=all, 1=odd, 2=even)
- `falloff_curve` (-1.0-1.0) - Automatic amplitude distribution
- `detune_amount` (0.0-1.0) - Harmonic detuning amount
- `amplitude_control` (0-1) - Amplitude control mode (0=auto, 1=custom)

**Usage:**
```
[harmosc~ 440 16 @harmonic_mode 1 @falloff_curve -0.3]
```

### **cycle.fold~** - Wavefolder with antialiasing
High-quality wavefolder with multiple folding algorithms and antialiasing.

**Key Features:**
- Multiple folding algorithms (classic, smooth, asymmetric)
- Antialiasing for artifact-free folding
- DC blocking for clean output
- Warp mode for additional timbral variation

**Attributes:**
- `folding_algorithm` (0-2) - Folding type (0=classic, 1=smooth, 2=asymmetric)
- `antialiasing` (0-1) - Enable antialiasing filter
- `dc_blocking` (0-1) - Enable DC blocking filter
- `warp_mode` (0-2) - Additional warp processing

**Usage:**
```
[cycle.fold~ @folding_algorithm 1 @antialiasing 1]
```

### **cycle-2d~** - 2D wavetable oscillator
Advanced wavetable oscillator with 2D interpolation and morphing.

**Key Features:**
- 2D wavetable with smooth interpolation
- Corner morphing modes for complex timbres
- Variable table size for memory vs quality trade-offs
- Real-time wavetable loading and manipulation

**Attributes:**
- `interpolation` (0-2) - Interpolation type (0=linear, 1=cubic, 2=spline)
- `corner_mode` (0-1) - Corner interpolation (0=smooth, 1=sharp)
- `table_size` (0-2) - Table resolution (0=256, 1=512, 2=1024)

**Usage:**
```
[cycle-2d~ @interpolation 2 @table_size 2]
```

### **noises~** - Multi-type noise generator with morphing
Professional noise generator with 7 distinct noise types and smooth morphing.

**Key Features:**
- 7 noise types: white, pink, brown, blue, violet, grey, velvet
- Smooth morphing between noise types with equal-power crossfading
- Professional volume balancing across all types
- Automatic and manual seed control

**Attributes:**
- `morphing` (0-1) - Enable smooth morphing between types
- `dc_blocking` (0-1) - Enable DC blocking filter
- `seed_auto` (0-1) - Automatic seed generation
- `filter_quality` (0-2) - Filter quality (0=fast, 1=good, 2=best)

**Usage:**
```
[noises~ @morphing 1 @dc_blocking 1]
```

## LFOs & Modulation (6 externals)

### **tide~** - Tidal LFO with multiple waveforms
Advanced LFO with tidal waveform generation and comprehensive modulation features.

**Key Features:**
- Multiple waveform modes with tidal characteristics
- Sync capabilities for rhythmic modulation
- Quantization for stepped modulation
- Slew rate limiting for smooth transitions

**Attributes:**
- `mode` (0-3) - Waveform mode (0=sine, 1=triangle, 2=saw, 3=tidal)
- `sync` (0-1) - Enable sync input
- `quantize` (0-1) - Enable quantization
- `slew_rate` (0.0-1.0) - Slew limiting amount

**Usage:**
```
[tide~ 0.5 @mode 3 @quantize 1]
```

### **easelfo~** - Easing-based LFO with curves
LFO with easing curve functions for smooth, natural modulation.

**Key Features:**
- Multiple easing curves (linear, ease-in, ease-out, ease-in-out)
- Mirror mode for symmetrical modulation
- Phase lock for synchronized multiple instances
- Smoothing filter for ultra-smooth modulation

**Attributes:**
- `mirror_mode` (0-1) - Enable mirror symmetry
- `easing_curve` (0-3) - Easing type (0=linear, 1=ease-in, 2=ease-out, 3=ease-in-out)
- `phase_lock` (0-1) - Enable phase synchronization
- `smoothing` (0.0-1.0) - Output smoothing amount

**Usage:**
```
[easelfo~ 1.0 @easing_curve 3 @smoothing 0.3]
```

### **ramplfo~** - Ramp-based LFO with jitter
Versatile LFO with ramp generation, curve shaping, and built-in jitter.

**Key Features:**
- Multiple curve types for varied modulation shapes
- Built-in jitter generator for organic movement
- Symmetry control for asymmetric waveforms
- High-precision ramp generation

**Attributes:**
- `curve_type` (0-2) - Curve shape (0=linear, 1=exponential, 2=logarithmic)
- `jitter_rate` (0.0-1.0) - Jitter intensity
- `symmetry` (0.0-1.0) - Waveform symmetry (0.5=symmetric)

**Usage:**
```
[ramplfo~ 2.0 @curve_type 1 @jitter_rate 0.2]
```

### **physicslfo~** - Physics-based LFO simulation
LFO based on physics simulation for natural, organic modulation.

**Key Features:**
- Multiple physics models (pendulum, spring, bouncing ball)
- Gravity and air resistance parameters
- Looping modes for continuous vs one-shot operation
- Realistic physics behavior for natural modulation

**Attributes:**
- `looping` (0-1) - Enable continuous looping
- `physics_type` (0-2) - Physics model (0=pendulum, 1=spring, 2=ball)
- `gravity` (0.0-2.0) - Gravity strength
- `air_resistance` (0.0-1.0) - Air resistance/damping

**Usage:**
```
[physicslfo~ @physics_type 0 @gravity 1.0 @air_resistance 0.1]
```

### **decay~** - Envelope generator with multiple modes
Versatile envelope generator with multiple decay curves and trigger modes.

**Key Features:**
- Multiple envelope modes (exponential, linear, logarithmic)
- Curve response shaping for natural decay
- Click protection for smooth triggering
- Retrigger modes for overlapping envelopes

**Attributes:**
- `envelope_mode` (0-2) - Decay curve (0=exponential, 1=linear, 2=logarithmic)
- `curve_response` (0.1-10.0) - Curve shaping factor
- `click_protection` (0-1) - Enable click-free triggering
- `retrigger_mode` (0-1) - Retrigger behavior (0=reset, 1=continue)

**Usage:**
```
[decay~ 1000 @envelope_mode 0 @curve_response 2.0]
```

### **slewenv~** - Slew envelope limiter
Envelope follower with slew rate limiting for smooth parameter control.

**Key Features:**
- Separate attack and decay slew rates
- Envelope following with customizable response
- Slew limiting for smooth parameter automation
- Low CPU usage for multiple instances

**Attributes:**
- `attack_slew` (0.1-1000.0) - Attack slew rate (ms)
- `decay_slew` (0.1-1000.0) - Decay slew rate (ms)
- `response_curve` (0.1-10.0) - Envelope response curve
- `smoothing` (0-1) - Additional output smoothing

**Usage:**
```
[slewenv~ @attack_slew 10.0 @decay_slew 100.0]
```

## Filters & Dynamics (3 externals)

### **ssm2044~** - Analog filter emulation
Accurate emulation of the classic SSM2044 analog filter chip.

**Key Features:**
- Authentic analog character with nonlinear behavior
- Self-oscillation capability for pitched resonance
- Warmth control for analog saturation
- Resonance compensation for consistent levels

**Attributes:**
- `character` (0-2) - Analog character (0=clean, 1=warm, 2=saturated)
- `self_oscillation` (0-1) - Enable self-oscillation
- `warmth` (0.0-1.0) - Analog warmth amount
- `resonance_compensation` (0-1) - Automatic level compensation

**Usage:**
```
[ssm2044~ @character 1 @self_oscillation 1 @warmth 0.7]
```

### **vactrol~** - Vactrol dynamics simulation
Simulation of vactrol (light-dependent resistor) dynamics processing.

**Key Features:**
- Authentic vactrol response curves
- Multiple pole configurations
- Calibration settings for different vactrol types
- Temperature drift simulation for realism

**Attributes:**
- `poles` (1-4) - Number of poles in response
- `response_curve` (0-2) - Response characteristic (0=fast, 1=medium, 2=slow)
- `calibration` (0.5-2.0) - Vactrol calibration factor
- `temperature_drift` (0-1) - Enable temperature simulation

**Usage:**
```
[vactrol~ @poles 2 @response_curve 1 @calibration 1.0]
```

### **modvca~** - Modular VCA with saturation
Professional voltage-controlled amplifier with analog-style saturation.

**Key Features:**
- Saturation modes for analog VCA character
- Character amount for subtle to extreme saturation
- Response curve shaping for linear to exponential control
- Warmth factor for analog coloration

**Attributes:**
- `saturation_mode` (0-2) - Saturation type (0=clean, 1=tube, 2=transistor)
- `character_amount` (0.0-1.0) - Saturation intensity
- `response_curve` (0.0-2.0) - Control response curve
- `warmth_factor` (0.0-1.0) - Analog warmth amount

**Usage:**
```
[modvca~ @saturation_mode 1 @character_amount 0.4]
```

## Technical Features

### Max 9 Integration
- **Autocomplete Support**: All attributes appear in Max's autocomplete system
- **Parameter Validation**: Real-time validation and clamping of attribute values
- **Backward Compatibility**: Message-based control maintained alongside attributes
- **Documentation Integration**: Complete `.maxref.xml` files for Max's reference system

### Performance Characteristics
- **Real-Time Safe**: No memory allocation or blocking operations in audio thread
- **Universal Binary**: Native support for both Intel and Apple Silicon Macs
- **Low CPU Usage**: Optimized algorithms for multiple simultaneous instances
- **Signal/Float Duality**: lores~ pattern for seamless signal and float input handling

## Modular Patching Examples

### Basic Oscillator Patch
```
[noises~ white] → [ssm2044~] → [modvca~] → [dac~]
     ↑               ↑           ↑
[easelfo~] ──→ [*~ 0.3] ─→ [tide~]
```

### Complex Modulation Setup
```
[harmosc~ 110 8] → [cycle.fold~] → [vactrol~] → [dac~]
     ↑                  ↑              ↑
[physicslfo~] ──→ [ramplfo~] ──→ [decay~]
     ↑                  ↑              ↑
[metro 250] ─→ [slewenv~] ──→ [bang]
```

### Rhythmic Pattern Generator
```
[tide~ 4 @quantize 1] → [decay~] → [modvca~] → [cycle-2d~] → [dac~]
                           ↑         ↑
                    [easelfo~] ─→ [*~ 0.8]
```

## Installation

1. Place externals in your Max package's `externals/` folder
2. Restart Max or refresh the file browser
3. Externals will appear in Max's object browser under "Audio > Synthesis"

### Build Requirements
```bash
# Build all modular externals
mkdir build && cd build
cmake -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" ..
cmake --build . -j4
```

## Quick Reference

### External Categories

| Category | Externals | Primary Use |
|----------|-----------|-------------|
| **Generators** | harmosc~, cycle.fold~, cycle-2d~, noises~ | Sound sources, oscillators |
| **LFOs & Modulation** | tide~, easelfo~, ramplfo~, physicslfo~, decay~, slewenv~ | Modulation sources, envelopes |
| **Filters & Dynamics** | ssm2044~, vactrol~, modvca~ | Processing, dynamics |

### Common Attribute Patterns

| Attribute Type | Range | Description |
|---------------|-------|-------------|
| Mode Selection | 0-2 or 0-3 | Enumerated choices |
| Amount/Intensity | 0.0-1.0 | Normalized amount |
| Time Values | 0.1-1000.0 | Milliseconds |
| Enable/Disable | 0-1 | Boolean switch |
| Curve Shaping | 0.1-10.0 | Response curves |

## Help Files

Each external includes comprehensive Max help patches demonstrating features and usage:
- **Generators**: Waveform generation, harmonic content, noise types
- **LFOs**: Modulation techniques, sync, quantization
- **Filters/Dynamics**: Processing examples, character settings

## Creative Workflow Tips

### Modular Approach
- Start with a generator (harmosc~, cycle-2d~, noises~)
- Add modulation sources (LFOs, envelopes)
- Process through filters and dynamics
- Use multiple instances for complex patches

### Parameter Automation
- Use attributes for Max's automation system
- Combine with modulation for dynamic changes
- Save attribute states with patch presets

### Performance Optimization
- Use bypass attributes for A/B comparison
- Adjust quality settings based on CPU usage
- Group related parameters for easy control

## Resources

- [Max/MSP Modular Synthesis Guide](https://cycling74.com)
- [Max SDK Documentation](https://cycling74.com/sdk)
- [Modular Synthesis Techniques](https://en.wikipedia.org/wiki/Modular_synthesizer)

## License

These externals are provided as part of the Max SDK and follow the Max SDK licensing terms.

---

*little-scale modular-like externals - Modular synthesis tools for Max/MSP*