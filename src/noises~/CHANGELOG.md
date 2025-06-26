# noises~ Changelog

## Development History

### v1.4 - Final Release (Current)
**Focus: CPU Optimization & Documentation**
- ✅ Reverted from 22-type experimental version to optimized 7-type architecture
- ✅ Maintained all performance improvements and volume balancing
- ✅ Updated comprehensive documentation (README.md, TECHNICAL.md)
- ✅ CPU-friendly operation with 7 simultaneous generators
- ✅ Backup of 22-type version saved as `noises~_22types_backup.c`

### v1.3 - Volume Fine-Tuning
**Focus: Final Volume Balancing**
- ✅ Pink noise reduced 15% (0.68 → 0.578) - was still too loud
- ✅ Velvet noise increased 20% (0.648 → 0.8) - needed more audibility  
- ✅ Overall output scaled to 40% for safe headroom during morphing
- ✅ Perfect perceived loudness matching across all 7 noise types

### v1.2 - Major Algorithm Improvements  
**Focus: Pink Noise & Morphing Architecture**
- ✅ **CRITICAL FIX**: Completely rewrote pink noise algorithm
  - Fixed catastrophic volume error (was 1.5M times too loud!)
  - Implemented proper Voss-McCartney algorithm with floating-point precision
  - Eliminated bit-shifting artifacts from original implementation
- ✅ **ARCHITECTURE BREAKTHROUGH**: All-types simultaneous generation
  - Changed from 2-type crossfading to 7-type simultaneous generation
  - Enables true morphing without phase discontinuities  
  - Smooth transitions even with tiny parameter changes (3.0 vs 3.01)
- ✅ Applied user-specified volume scaling per noise type
- ✅ Fixed universal binary compilation issues

### v1.1 - Core Implementation
**Focus: Basic Multi-Type Noise Generation**
- ✅ Implemented 7 distinct noise types:
  - White noise (reference level)
  - Pink noise (1/f spectrum) 
  - Brown noise (integrated, with DC blocking)
  - Blue noise (+3dB/octave via differentiation)
  - Violet noise (+6dB/octave via double differentiation)
  - Grey noise (psychoacoustic, inverse A-weighted)
  - Velvet noise (sparse impulses at 2205/sec)
- ✅ lores~ pattern implementation for perfect signal/float dual inputs
- ✅ Click-free parameter transitions with adaptive smoothing
- ✅ Equal-power crossfading between adjacent noise types
- ✅ Professional audio processing (denormal protection, etc.)

### v1.0 - Initial Framework
**Focus: Basic Structure**
- ✅ Max/MSP external framework setup
- ✅ Basic noise generation infrastructure
- ✅ Parameter handling and inlet/outlet configuration

---

## Key Technical Breakthroughs

### 1. Pink Noise Algorithm Fix
**Problem**: Original implementation was ~1.5 million times too loud due to incorrect bit-shifting arithmetic.
**Solution**: Complete rewrite using proper Voss-McCartney algorithm with floating-point values.

### 2. Morphing Architecture Revolution  
**Problem**: Traditional crossfading between 2 types caused abrupt character changes.
**Solution**: Generate all 7 types simultaneously, enabling true morphing with synchronized RNG sequences.

### 3. Volume Balancing Precision
**Problem**: Noise types had vastly different perceived loudness levels.
**Solution**: Careful empirical scaling per type: pink(0.578x), brown(2.25x), blue/violet(0.6x), grey(0.72x), velvet(0.8x).

### 4. CPU Optimization Decision
**Problem**: 22-type experimental version hammered CPU with excessive simultaneous generation.
**Solution**: Strategic reduction to 7 core types, maintaining quality while ensuring real-time performance.

---

## User Feedback Integration

- **"Pink noise WAY too loud"** → Complete algorithm rewrite
- **"Morphing too abrupt between types"** → Simultaneous generation architecture  
- **"Type 6 still too quiet"** → Multiple velvet noise volume adjustments
- **"We are hammering the CPU"** → Strategic reduction from 22 to 7 types

---

## Final Architecture Benefits

1. **CPU Efficient**: Only 7 concurrent generators vs. experimental 22
2. **Professional Quality**: All noise types carefully volume-balanced
3. **Smooth Morphing**: True morphing without artifacts via simultaneous generation
4. **Click-Free Operation**: Adaptive smoothing prevents parameter change artifacts
5. **Comprehensive**: Covers all essential noise colors for audio production
6. **Extensible**: 22-type backup available for future experimentation

The `noises~` external represents a perfect balance of functionality, quality, and performance for professional Max/MSP audio production.