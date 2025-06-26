# CLAUDE.md - noises~ External Development Guide

This file provides guidance to Claude Code when working on the `noises~` multi-type noise generator external for Max/MSP.

## Project Overview

**noises~** is a professional multi-type noise generator external featuring 7 distinct noise types with smooth morphing capabilities. It represents a completed, production-ready external with carefully balanced volume levels and CPU-optimized architecture.

## Current State: COMPLETED ✅

The external is **feature-complete** and **production-ready** with the following final specifications:

### Core Features
- **7 Noise Types**: White, Pink, Brown, Blue, Violet, Grey, Velvet (types 0-6)
- **Smooth Morphing**: Seamless transitions between adjacent noise types
- **lores~ Pattern**: Perfect signal/float dual input on both inlets
- **Click-Free Operation**: Adaptive parameter smoothing prevents audio artifacts
- **Volume Balanced**: All noise types carefully scaled for consistent perceived loudness

### Final Volume Scaling (DO NOT CHANGE)
```c
// These values were carefully calibrated through extensive testing
noise_types[NOISE_WHITE] = noises_generate_white(x);      // 1.0x (reference)
noise_types[NOISE_PINK] = noises_generate_pink(x);       // 0.578x (internal scaling)
noise_types[NOISE_BROWN] = noises_generate_brown(x);     // 2.25x (internal scaling) 
noise_types[NOISE_BLUE] = noises_generate_blue(x);       // 0.6x (internal scaling)
noise_types[NOISE_VIOLET] = noises_generate_violet(x);   // 0.6x (internal scaling)
noise_types[NOISE_GREY] = noises_generate_grey(x);       // 0.72x (internal scaling)
noise_types[NOISE_VELVET] = noises_generate_velvet(x);   // 0.8x (internal scaling)

// Overall output scaling: 40% for safe headroom during morphing
return (noise_a * mix_a + noise_b * mix_b) * 0.4;
```

## Architecture Decisions

### 1. 7-Type vs 22-Type Architecture
**DECISION**: Maintain 7 noise types for CPU efficiency
- **Tested**: 22-type experimental version (saved as `noises~_22types_backup.c`)
- **Result**: CPU usage too high for real-time operation
- **Final Choice**: 7 core noise types provide optimal quality/performance balance

### 2. Simultaneous Generation Pattern
**IMPLEMENTATION**: Generate all 7 types every sample
- **Benefit**: True morphing without phase discontinuities
- **Cost**: 7x RNG calls per sample (acceptable for 7 types)
- **Alternative Rejected**: On-demand generation (causes morphing artifacts)

### 3. Volume Balancing Strategy
**METHOD**: Individual scaling per noise type + overall scaling
- **Process**: Empirical testing with user feedback
- **Key Adjustments**: Pink noise reduced 15%, Velvet increased 20%, Overall 40%
- **Result**: Perceptually matched loudness across all types

## Critical Implementation Details

### Pink Noise Algorithm
**BREAKTHROUGH**: Complete algorithm rewrite was required
```c
// BEFORE: Catastrophic volume error (1.5M times too loud)
// AFTER: Proper Voss-McCartney with floating-point precision
return (x->pink_running_sum + white) * 0.578;  // Final scaling
```

### Parameter Smoothing
**PATTERN**: Adaptive smoothing rates
```c
// Signal inputs: 10x faster smoothing for responsiveness
double type_param = x->type_has_signal ? 
                   smooth_param(x->type_smooth, type_target, smooth_factor * 0.1) :
                   smooth_param(x->type_smooth, type_target, smooth_factor);
```

### Morphing Implementation
**KEY**: Equal-power crossfading with smoothstep interpolation
```c
double smooth_frac = type_frac * type_frac * (3.0 - 2.0 * type_frac);
double mix_a = cos(smooth_frac * PI * 0.5);
double mix_b = sin(smooth_frac * PI * 0.5);
```

## Development History

### Critical Fixes Applied
1. **Pink Noise Volume**: Rewrote entire algorithm (was 1.5M times too loud)
2. **Morphing Quality**: Changed from 2-type to 7-type simultaneous generation
3. **Universal Binary**: Fixed compilation for ARM64 + x86_64
4. **Volume Balancing**: Multiple rounds of user-guided adjustments
5. **CPU Optimization**: Reduced from 22-type experimental to 7-type final

### User Feedback Integration
- **"Pink noise WAY too loud"** → Algorithm rewrite
- **"Morphing too abrupt"** → Simultaneous generation architecture
- **"Type 6 still too quiet"** → Velvet noise volume increases
- **"We are hammering the CPU"** → Reduced to 7 types

## Future Development Guidelines

### DO NOT CHANGE
- Volume scaling values (carefully calibrated)
- 7-type architecture (CPU optimized)
- Simultaneous generation pattern (prevents artifacts)
- Core algorithm implementations (all working correctly)

### SAFE TO MODIFY
- Help file and documentation updates
- Additional message handlers (seed, etc.)
- Build system improvements
- Code comments and organization

### EXPERIMENTAL FEATURES
- 22-type version available in `noises~_22types_backup.c`
- Could be explored with type selection parameter (vs. simultaneous generation)
- Consider GPU acceleration for large-scale noise generation

## File Structure

```
source/audio/noises~/
├── noises~.c                    # Main implementation (7 types, production)
├── noises~_22types_backup.c     # Experimental 22-type version  
├── CMakeLists.txt              # Build configuration
├── README.md                   # User documentation
├── TECHNICAL.md                # Technical implementation details
├── CHANGELOG.md                # Development history
└── CLAUDE.md                   # This file (development guidance)

help/
└── noises~.maxhelp             # Interactive help file

externals/
└── noises~.mxo                 # Built external (universal binary)
```

## Build Commands

```bash
# Standard build (from max-sdk-main/build/)
make noises_tilde

# Clean rebuild
make clean && make noises_tilde

# Universal binary verification
file "../externals/noises~.mxo/Contents/MacOS/noises~"
# Should show: Mach-O universal binary with 2 architectures: [x86_64] [arm64]
```

## Testing Checklist

When making any changes, verify:
- [ ] Compilation succeeds without warnings
- [ ] Universal binary includes both x86_64 and ARM64
- [ ] All 7 noise types produce audible output
- [ ] Morphing is smooth between all adjacent types (test 0.5, 1.5, 2.5, etc.)
- [ ] No clicking artifacts during parameter changes
- [ ] Volume levels consistent across all types
- [ ] Signal and float inputs work on both inlets
- [ ] Help file opens correctly

## Success Metrics

The `noises~` external is considered successful based on:
1. **Audio Quality**: Professional-grade noise generation with no artifacts
2. **Performance**: Real-time operation without CPU spikes
3. **Usability**: Intuitive morphing parameter with predictable results
4. **Reliability**: No crashes, clicks, or unexpected behavior
5. **Completeness**: Comprehensive documentation and help files

---

**STATUS**: ✅ **PRODUCTION READY** - All objectives achieved, external ready for professional use.

The `noises~` external represents a perfect balance of functionality, quality, and performance for Max/MSP audio production environments.