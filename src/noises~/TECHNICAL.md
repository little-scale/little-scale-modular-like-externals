# noises~ Technical Documentation

## Architecture Overview

The `noises~` external implements a multi-type noise generator with seamless morphing capabilities using professional audio processing techniques. The final version generates 7 noise types simultaneously for optimal CPU/quality balance.

## Core Design Principles

### 1. Simultaneous Generation Architecture
Unlike traditional approaches that generate only the needed noise types, `noises~` generates all 7 noise types every sample. This enables:
- True morphing without phase discontinuities
- Synchronized random number sequences  
- Consistent spectral characteristics during transitions
- CPU-efficient operation (7 generators vs. potential 22+ in experimental versions)

### 2. lores~ Pattern Implementation
Perfect signal/float dual input support:
```c
// Parameter acquisition using lores~ pattern
double type_target = x->type_has_signal ? type_in[i] : x->type_float;
double amp_target = x->amplitude_has_signal ? amp_in[i] : x->amplitude_float;
```

### 3. Click-Free Parameter Transitions
Adaptive smoothing with different rates for signal vs. float inputs:
```c
double type_param = x->type_has_signal ? 
                   smooth_param(x->type_smooth, type_target, smooth_factor * 0.1) :
                   smooth_param(x->type_smooth, type_target, smooth_factor);
```

## Noise Generation Algorithms

### White Noise
```c
double noises_generate_white(t_noises *x) {
    return ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX) * 2.0 - 1.0;
}
```
- Direct uniform random number generation
- Range: [-1, 1]
- Flat spectrum across all frequencies

### Pink Noise (1/f Noise)
Implements the Voss-McCartney algorithm with floating-point precision:

```c
double noises_generate_pink(t_noises *x) {
    x->pink_index = (x->pink_index + 1) & x->pink_index_mask;
    
    if (x->pink_index != 0) {
        // Replace one random number based on trailing zeros
        int num_zeros = 0;
        int n = x->pink_index;
        while ((n & 1) == 0) {
            n = n >> 1;
            num_zeros++;
        }
        if (num_zeros < PINK_BITS) {
            double new_random = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX) * 2.0 - 1.0;
            x->pink_running_sum -= x->pink_rows[num_zeros];
            x->pink_running_sum += new_random;
            x->pink_rows[num_zeros] = new_random;
        }
    } else {
        // Replace all random numbers
        x->pink_running_sum = 0.0;
        for (int i = 0; i < PINK_BITS; i++) {
            double new_random = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX) * 2.0 - 1.0;
            x->pink_running_sum += new_random;
            x->pink_rows[i] = new_random;
        }
    }
    
    double white = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX) * 2.0 - 1.0;
    return (x->pink_running_sum + white) * 0.578;
}
```

**Key Features:**
- 5-bit table (PINK_BITS = 5) for 32 samples period
- Floating-point values instead of integer scaling
- Additional white noise component for proper spectral characteristics
- Scaling factor of 0.578 for volume balance (reduced 15% from original 0.68)

### Brown Noise (Brownian/Red Noise)
```c
double noises_generate_brown(t_noises *x) {
    double white = noises_generate_white(x);
    x->brown_state = x->brown_leak * x->brown_state + white * 0.1;
    double brown_output = dc_block(x, x->brown_state);
    return brown_output * 2.25;
}
```

**Implementation Details:**
- Leaky integrator with leak coefficient 0.9999
- DC blocking to prevent offset accumulation
- Scaling factor 2.25 to compensate for integration attenuation

### Blue Noise
```c
double noises_generate_blue(t_noises *x) {
    double white = noises_generate_white(x);
    double blue_output = white - x->prev_white;
    x->prev_white = white;
    return blue_output * 0.6;
}
```

**Characteristics:**
- First-order differentiation of white noise
- +3dB/octave spectral slope
- Scaling factor 0.6 to compensate for differentiation gain

### Violet Noise
```c
double noises_generate_violet(t_noises *x) {
    double blue = noises_generate_blue(x);
    double violet_output = blue - x->prev_blue;
    x->prev_blue = blue;
    return violet_output * 0.6;
}
```

**Characteristics:**
- Second-order differentiation (differentiation of blue noise)
- +6dB/octave spectral slope
- Scaling factor 0.6 for amplitude control

### Grey Noise
```c
double noises_generate_grey(t_noises *x) {
    double white = noises_generate_white(x);
    double output = x->grey_b0 * white + x->grey_b1 * x->grey_x1 + x->grey_b2 * x->grey_x2
                   - x->grey_a1 * x->grey_y1 - x->grey_a2 * x->grey_y2;
    
    // Update filter state
    x->grey_x2 = x->grey_x1;
    x->grey_x1 = white;
    x->grey_y2 = x->grey_y1;
    x->grey_y1 = output;
    
    return output * 0.72;
}
```

**Filter Design:**
- 2-pole IIR filter approximating inverse A-weighting
- Center frequency: 1000 Hz
- Q factor: 0.707 (Butterworth response)
- Psychoacoustically flat perceived spectrum

### Velvet Noise
```c
double noises_generate_velvet(t_noises *x) {
    double impulse_probability = VELVET_IMPULSES_PER_SEC / x->sr;
    double random_val = (double)xorshift32(&x->rng_state) / (double)UINT32_MAX;
    
    double output = 0.0;
    if (random_val < impulse_probability) {
        output = (xorshift32(&x->rng_state) & 1) ? 1.0 : -1.0;
    }
    
    return output * 0.648;
}
```

**Parameters:**
- Impulse density: 2205 impulses/second (standard velvet noise)
- Amplitude: ±1.0 (bipolar impulses)
- Scaling factor: 0.8 for audibility (increased 20% from original 0.648)

## Morphing Algorithm

### Equal-Power Crossfading
```c
double noises_morph_types(t_noises *x, double type_param) {
    // Generate ALL noise types every sample
    double noise_types[NUM_NOISE_TYPES];
    noise_types[NOISE_WHITE] = noises_generate_white(x);
    noise_types[NOISE_PINK] = noises_generate_pink(x);
    // ... (all types generated)
    
    // Get integer and fractional parts
    int type_int = (int)type_param;
    double type_frac = type_param - type_int;
    
    // Smoothstep interpolation
    double smooth_frac = type_frac * type_frac * (3.0 - 2.0 * type_frac);
    
    // Equal-power crossfade
    double mix_a = cos(smooth_frac * PI * 0.5);
    double mix_b = sin(smooth_frac * PI * 0.5);
    
    return (noise_a * mix_a + noise_b * mix_b) * 0.4;
}
```

**Key Features:**
- Smoothstep function for gradual transitions
- Equal-power crossfading (constant perceived loudness)
- Overall scaling factor 0.4 (40% of maximum amplitude for safe headroom during morphing)

## Random Number Generation

### xorshift32 Algorithm
```c
uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}
```

**Advantages:**
- Fast execution (3 XOR operations)
- Excellent statistical properties
- Long period (2^32 - 1)
- Suitable for audio applications

## Parameter Smoothing

### Exponential Smoothing
```c
double smooth_param(double current, double target, double smooth_factor) {
    return current + smooth_factor * (target - current);
}
```

**Smoothing Factor Calculation:**
```c
x->smooth_factor = 1.0 - exp(-1.0 / (0.01 * samplerate));  // ~10ms time constant
```

**Adaptive Rates:**
- Signal inputs: `smooth_factor * 0.1` (faster response)
- Float inputs: `smooth_factor` (standard response)

## DC Blocking Filter

```c
double dc_block(t_noises *x, double input) {
    double output = input - x->dc_block_x1 + 0.995 * x->dc_block_y1;
    x->dc_block_x1 = input;
    x->dc_block_y1 = output;
    return output;
}
```

**Transfer Function:** H(z) = (1 - z^-1) / (1 - 0.995 * z^-1)
- High-pass filter with cutoff ~3.5 Hz at 44.1 kHz
- Removes DC offset from integrated noise types

## Performance Optimizations

### Memory Layout
- Contiguous struct layout for cache efficiency
- Minimal state variables per noise type (only 7 types vs. experimental 22)
- Pre-computed coefficients where possible

### Computational Efficiency
- Single random number generator shared across all types
- Simultaneous generation of 7 types amortizes RNG cost
- Optimized parameter smoothing with local variables
- CPU-friendly architecture (7 concurrent generators maximum)

### Real-Time Safety
- No dynamic memory allocation in audio thread
- No file I/O or blocking operations
- Bounded execution time per sample
- Consistent performance regardless of morphing parameter

## Universal Binary Support

### Build Configuration
- Automatic x86_64 + ARM64 compilation
- Optimized assembly code generation for both architectures
- Consistent floating-point behavior across platforms

## Quality Assurance

### Anti-Aliasing
- Frequency-dependent processing where applicable
- Nyquist-aware filter design
- Oversampling considerations for future enhancement

### Denormal Protection
```c
if (fabs(output) < DENORMAL_THRESHOLD) output = 0.0;
```
- Threshold: 1e-15
- Prevents CPU performance degradation on Intel processors

### Dynamic Range
- Full 24-bit dynamic range utilization
- Careful scaling to prevent clipping
- Balanced levels across all noise types

## Testing Methodology

### Spectral Analysis
- FFT analysis of each noise type
- Verification of expected spectral slopes
- Cross-correlation analysis for independence

### Morphing Validation
- Smooth transitions verified via spectrogram analysis
- No clicking artifacts during parameter changes
- Equal-power crossfading validation

### Performance Profiling
- CPU usage measurement across all noise types
- Memory allocation analysis
- Real-time performance validation at various buffer sizes