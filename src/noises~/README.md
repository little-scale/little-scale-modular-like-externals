# noises~ - Multi-type Noise Generator

A professional noise generator external for Max/MSP featuring 7 distinct noise types with smooth morphing capabilities.

## Overview

`noises~` generates high-quality noise with seamless transitions between different spectral characteristics. It features click-free parameter changes, professional audio processing, and supports both signal and float inputs using the lores~ pattern. All noise types are carefully volume-balanced for consistent perceived loudness during morphing.

## Noise Types

| Type | Name | Spectral Characteristic | Description | Volume Scaling |
|------|------|------------------------|-------------|----------------|
| 0 | White | Flat spectrum | Equal energy per frequency bin | 1.0x (reference) |
| 1 | Pink | -3dB/octave | Equal energy per octave (1/f noise) | 0.578x (reduced) |
| 2 | Brown | -6dB/octave | Brownian motion, deeper low frequencies | 2.25x (boosted) |
| 3 | Blue | +3dB/octave | Brighter spectrum, more high frequencies | 0.6x (attenuated) |
| 4 | Violet | +6dB/octave | Very bright spectrum, emphasizes highs | 0.6x (attenuated) |
| 5 | Grey | Psychoacoustic flat | Inverse A-weighted, perceptually flat | 0.72x (slightly reduced) |
| 6 | Velvet | Sparse impulses | Random impulses at ~2205/sec density | 0.8x (boosted for audibility) |

## Usage

### Basic Syntax
```
noises~ [type] [amplitude]
```

### Parameters
- **type** (0.0-6.0): Noise type selector with continuous morphing
- **amplitude** (0.0-1.0): Output level control

### Inlets
1. **Type**: Signal or float input for noise type selection
2. **Amplitude**: Signal or float input for output level

### Outlets
1. **Signal**: Generated noise output

## Features

### Smooth Morphing
- Fractional type values enable smooth transitions between adjacent noise types
- Equal-power crossfading prevents volume dips during morphing
- All 7 noise types generated simultaneously for true morphing

### Click-Free Operation
- Adaptive parameter smoothing prevents audio artifacts
- Different smoothing rates for signal vs. float inputs (10x faster for signal)
- Continuous algorithm paths eliminate switching artifacts
- Exponential smoothing with ~10ms time constant

### Professional Audio Processing
- High-quality xorshift32 random number generation for excellent randomness
- Denormal number protection for consistent performance
- DC blocking filter for integrated noise types (brown noise)
- Careful volume balancing across all noise types
- 40% overall output scaling to prevent clipping during morphing

### lores~ Pattern Support
- Perfect signal/float dual input on both inlets
- Automatic detection of signal connections
- No proxy inlets required
- Seamless switching between signal and float inputs

## Examples

### Basic Noise Generation
```
noises~ 1.0 0.5  // Pink noise at 50% amplitude
```

### Smooth Morphing
```
// Morph from white to pink noise
noises~ 0.5      // 50% white, 50% pink
```

### Audio-Rate Modulation
```
// Connect LFO to type inlet for dynamic morphing
cycle~ 0.1 -> scale~ 0. 1. 0. 6. -> noises~
```

## Messages

- `type <float>`: Set noise type (0.0-6.0)
- `amp <float>`: Set amplitude (0.0-1.0)  
- `seed <int>`: Set random seed for reproducible noise

## Technical Implementation

### Noise Generation Algorithms

**White Noise**: Direct random number generation with uniform distribution

**Pink Noise**: Voss-McCartney algorithm with floating-point implementation for improved accuracy

**Brown Noise**: Leaky integrator applied to white noise with DC blocking

**Blue/Violet Noise**: First and second-order differentiation of white noise

**Grey Noise**: Inverse A-weighting filter approximation applied to white noise

**Velvet Noise**: Probability-based sparse impulse generation at 2205 impulses/second

### Architecture Features
- Universal binary support (x86_64 + ARM64)
- Real-time safe audio processing
- Memory-efficient simultaneous generation
- Optimized parameter smoothing

## Build Requirements

- Max SDK 8.2.0+
- CMake 3.19+
- C compiler with C99 support
- macOS 10.14+ (for universal binary)

## Installation

1. Place in Max package externals folder
2. Restart Max or use "Refresh Max File Browser"
3. Help file: `noises~.maxhelp`

## Version History

- **v1.0**: Initial release with 7 noise types and smooth morphing
- **v1.1**: Enhanced volume balancing and click-free transitions  
- **v1.2**: Improved pink noise algorithm accuracy (Voss-McCartney with floating-point precision)
- **v1.3**: Final volume balancing - pink noise reduced 15%, velvet noise increased 20%, overall 40% scaling
- **v1.4**: Comprehensive documentation and CPU optimization (maintained 7 types for efficiency)

## Credits

Developed for the Max SDK with professional audio processing standards.
Implements industry-standard noise generation algorithms with modern optimizations.