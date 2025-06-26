# decay~ Development Notes

This file documents the development process, architecture decisions, and technical insights from creating the `decay~` exponential decay envelope generator external for Max/MSP.

## Project Overview

**Objective**: Create a versatile exponential decay envelope generator with customizable curve shaping and optional click-free start smoothing for Max/MSP.

**Status**: ✅ **COMPLETED** - Production ready, universal binary with configurable smoothing

**Key Achievement**: Successful implementation of exponential decay envelope with curve shaping and user-configurable start smoothing to eliminate click artifacts while maintaining musical expressiveness.

---

## Development Timeline

### Phase 1: Core Exponential Decay Implementation
- **Goal**: Implement basic exponential decay envelope with bang triggering
- **Approach**: Mathematical exponential decay using coefficient calculation
- **Implementation**: `coeff = exp(-1.0 / (decay_time * sr))` per sample
- **Result**: Working exponential decay with sample-accurate timing

### Phase 2: Curve Shaping System
- **Goal**: Add curve parameter for exponential/linear/logarithmic envelope shapes
- **Approach**: Power function curve shaping applied to linear decay progress
- **Implementation**: `shaped = pow(linear_progress, curve_factor)` with curve-dependent exponents
- **Result**: Flexible envelope shapes from exponential to logarithmic curves

### Phase 3: lores~ Pattern Integration
- **Goal**: Add sample-accurate parameter control for time and curve
- **Implementation**: 3-inlet lores~ pattern with signal/float dual input
- **Benefits**: Real-time parameter modulation without artifacts
- **Result**: Professional-grade parameter control with connection detection

### Phase 4: Click Elimination (User Request)
- **Problem**: Bang-triggered envelope creates harsh click artifacts on start
- **Initial Solution**: Fixed 2-sample linear smoothing, later increased to 10 samples
- **User Feedback**: Request for configurable smoothing amount
- **Final Solution**: User-configurable smoothing samples as fourth argument (0-100 range)

---

## Technical Architecture

### Core Algorithm: Exponential Decay

**Mathematical Foundation**:
```c
// Exponential decay coefficient calculation
double time_constant = decay_time * sample_rate;
double decay_coeff = exp(-1.0 / time_constant);

// Per-sample envelope update
envelope *= decay_coeff;
```

**Decay Behavior**:
- **Exponential curve**: Natural decay following `y = peak * exp(-t/τ)` 
- **Time constant**: User-specified decay time in seconds
- **Sample-accurate**: Coefficient recalculated each sample for real-time control

### Curve Shaping System

**Curve Parameter Mapping**:
```c
double decay_apply_curve(double linear_value, double curve) {
    if (curve < 0.0) {
        // Exponential curve: y = x^(1 + |curve|)
        double exponent = 1.0 + fabs(curve);
        return pow(linear_value, exponent);
    } else {
        // Logarithmic curve: y = x^(1 / (1 + curve))
        double exponent = 1.0 / (1.0 + curve);
        return pow(linear_value, exponent);
    }
}
```

**Curve Characteristics**:
- **curve < 0**: Exponential (fast start, slow end) - sharper attack
- **curve = 0**: Linear decay - even progression
- **curve > 0**: Logarithmic (slow start, fast end) - gentler attack

### Start Smoothing System

**Configurable Smoothing**:
```c
// User-configurable smoothing length (0-100 samples)
int smooth_samples_setting;  // Set from fourth argument

// On bang trigger - detect level jumps
if (smooth_samples_setting > 0 && fabs(envelope - smooth_start_level) > 0.001) {
    smooth_samples = smooth_samples_setting;  // Enable smoothing
}

// Per-sample linear interpolation
if (smooth_samples > 0) {
    double progress = (smooth_samples_setting - smooth_samples) / smooth_samples_setting;
    output = start_level + (target_output - start_level) * progress;
    smooth_samples--;
}
```

**Smart Activation**:
- **Level jump detection**: Only activates when level change > 0.001
- **Zero overhead**: No processing when smoothing = 0 (default)
- **Preserves envelope shape**: Only affects initial samples

### Data Structure Design

**Optimized State Management**:
```c
typedef struct _decay {
    t_pxobject ob;              // MSP object header
    
    // Parameter storage (lores~ pattern)
    double decay_time_float;    // Decay time when no signal connected
    double curve_float;         // Curve when no signal connected
    double peak;                // Peak amplitude (0.0 to 1.0)
    
    // Signal connection status (lores~ pattern)
    short time_has_signal;      // 1 if time inlet has signal connection
    short curve_has_signal;     // 1 if curve inlet has signal connection
    
    // Envelope state
    double envelope;            // Current envelope value
    int is_active;              // 1 if envelope is running, 0 if finished
    int retrig_mode;            // 0 = retrig from current, 1 = retrig from peak
    
    // Smoothing state (for click-free starts)
    int smooth_samples_setting; // User-configured smoothing length
    int smooth_samples;         // Number of samples remaining for start smoothing
    double smooth_start_level;  // Starting level for smooth transition
} t_decay;
```

---

## Algorithm Design and Implementation

### Exponential Decay Mathematics

**Continuous vs Discrete**:
```c
// Continuous exponential decay: y(t) = peak * exp(-t / τ)
// Discrete implementation: y[n+1] = y[n] * exp(-1 / (τ * sr))

double decay_calculate_coefficient(double decay_time, double sample_rate) {
    if (decay_time <= 0.0) {
        return 0.0;  // Instant decay
    } else {
        double time_constant = decay_time * sample_rate;
        return exp(-1.0 / time_constant);
    }
}
```

**Benefits of This Approach**:
- Mathematically accurate exponential decay
- Sample-rate independent (time constant scales with sr)
- Real-time parameter changes without artifacts

### Curve Shaping Design Philosophy

**Power Function Approach**:
- Uses power functions to reshape the linear decay progress
- Maintains start and end points (always 0 to 1 progression)
- Intuitive parameter mapping: negative = sharp, positive = gentle

**Alternative Approaches Considered**:
- Lookup tables: Too memory-intensive for real-time parameter changes
- Polynomial curves: Less intuitive parameter control
- Exponential functions: Redundant with base exponential decay

### Retrigger Mode System

**Two Modes for Musical Flexibility**:
```c
void decay_bang(t_decay *x) {
    x->smooth_start_level = x->envelope;  // Store current level
    
    if (x->retrig_mode || !x->is_active) {
        // Mode 1: Retrig from peak (default)
        x->envelope = x->peak;
    }
    // Mode 0: Continue from current level (no reset)
    
    x->is_active = 1;
}
```

**Musical Applications**:
- **Mode 1 (from peak)**: Consistent envelope shape, good for percussion
- **Mode 0 (from current)**: Smooth retriggering, good for sustained sounds

---

## Development Challenges and Solutions

### Challenge 1: Click Artifacts on Envelope Start
**Problem**: Bang-triggered instant jump from current level to peak creates audible clicks
**Root Cause**: Discontinuous signal creates high-frequency content (clicks)
**Solution Evolution**:
1. **Fixed 2-sample smoothing**: Too brief for some applications
2. **Fixed 10-sample smoothing**: Better but inflexible
3. **User-configurable smoothing**: Perfect - users choose amount (0-100 samples)

**Final Implementation**:
- Fourth argument controls smoothing length
- Default 0 preserves original behavior
- Smart activation only when level jumps detected

### Challenge 2: Real-Time Parameter Control
**Problem**: Envelope parameters need sample-accurate control without artifacts
**Solution**: lores~ pattern implementation
**Benefits**:
- Seamless signal/float dual input on time and curve inlets
- No zipper noise or parameter artifacts
- Connection detection for optimal performance

### Challenge 3: Curve Shaping Mathematical Accuracy
**Problem**: Curve parameter needs intuitive control over envelope shape
**Solution**: Power function approach with symmetric parameter mapping
**Implementation**:
```c
// Symmetric curve mapping around 0
if (curve < 0.0) {
    exponent = 1.0 + fabs(curve);  // >1 = exponential curve
} else {
    exponent = 1.0 / (1.0 + curve);  // <1 = logarithmic curve
}
```

### Challenge 4: Envelope State Management
**Problem**: Complex state interactions between active/inactive, smoothing, and retrigger modes
**Solution**: Clear state separation and controlled transitions
**Benefits**:
- Predictable behavior in all operating modes
- Clean envelope termination with denormal protection
- Proper state cleanup on envelope completion

---

## Musical Context and Applications

### Envelope Types and Characteristics

**Classic Exponential Decay**:
- Natural decay behavior found in acoustic instruments
- Piano, plucked strings, mallet instruments
- Percussion with natural resonance decay

**Curve-Shaped Variants**:
- **Exponential curves (curve < 0)**: Punchy percussion, sharp attacks
- **Linear curves (curve = 0)**: Synthetic envelopes, electronic sounds
- **Logarithmic curves (curve > 0)**: Gentle attacks, pad sounds

### Integration with Max/MSP Workflow

**Drum Machine Applications**:
```
[decay~ 0.8 -1.5 1.0 5]  // Punchy kick: fast exponential, 5-sample smoothing
[decay~ 0.3 1.0 0.7 10]  // Gentle hi-hat: logarithmic, 10-sample smoothing
```

**Synthesis Applications**:
```
[decay~ 2.0 0.0 1.0 0]   // Clean sustain release: linear, no smoothing
[decay~ 0.1 -2.0 1.0 20] // Sharp pluck: very exponential, heavy smoothing
```

**Parameter Automation**:
```
[line~ 0.1 3.0 5000] → [decay~]  // Automated decay time sweep
[lfo~ 0.2] → [scale 0. 1. -2. 2.] → [decay~]  // LFO-controlled curve morphing
```

### Performance Considerations

**Computational Efficiency**:
- **Per-sample operations**: ~15 operations (exp, pow, multiply, conditional)
- **Smoothing overhead**: Only when active (negligible impact)
- **Memory usage**: Minimal state storage, no buffers

**Real-Time Safety**:
- No dynamic memory allocation in audio thread
- Predictable execution time
- Proper denormal handling

---

## Technical Specifications

### Parameter Ranges and Behavior

**Decay Time (0.001 - 60.0 seconds)**:
- **Minimum**: 1ms for sharp transients
- **Maximum**: 60 seconds for very long decays
- **Sample-accurate**: Changes applied immediately without artifacts

**Curve Shaping (-3.0 to 3.0)**:
- **-3.0**: Very sharp exponential (fastest attack)
- **0.0**: Linear decay (reference)
- **+3.0**: Very gentle logarithmic (slowest attack)

**Peak Amplitude (0.0 - 1.0)**:
- **0.0**: Silent envelope
- **1.0**: Full amplitude
- **Runtime adjustable**: via `peak` message

**Smoothing Samples (0 - 100)**:
- **0**: No smoothing (default, preserves original behavior)
- **1-10**: Light smoothing for minimal artifacts
- **11-50**: Medium smoothing for problematic content
- **51-100**: Heavy smoothing for very gentle starts

### Message Interface

**Bang Triggering**:
```
bang  // Trigger envelope (respects retrigger mode)
```

**Parameter Messages**:
```
peak 0.8        // Set peak amplitude to 0.8
retrig 0        // Set retrigger mode to "from current level"
retrig 1        // Set retrigger mode to "from peak" (default)
```

**Inlet Assignments**:
1. **Messages**: bang, peak, retrig
2. **Time**: signal/float (0.001-60.0 seconds)
3. **Curve**: signal/float (-3.0 to 3.0)

---

## Comparison with Other Envelope Generators

### vs Max Built-in Objects

**vs `adsr~`**:
- `adsr~`: Full ADSR envelope with sustain stage
- `decay~`: Specialized decay-only with curve shaping and smoothing

**vs `function~`**:
- `function~`: Arbitrary breakpoint envelopes
- `decay~`: Mathematical exponential decay with real-time control

### vs Third-Party Alternatives

**Advantages of `decay~`**:
- **Configurable click elimination**: User-controlled smoothing
- **Real-time curve morphing**: Sample-accurate curve parameter control
- **Mathematical precision**: True exponential decay behavior
- **Lightweight**: Minimal CPU and memory usage

**Unique Features**:
- Smart smoothing activation (only when needed)
- Symmetric curve parameter mapping
- Retrigger mode options
- Sample-accurate parameter modulation

---

## Future Enhancements

### Potential Feature Additions

**Additional Curve Types**:
- S-curve envelope shapes
- Custom curve lookup tables
- Bezier curve interpolation

**Advanced Triggering**:
- Velocity-sensitive peak control
- Multiple trigger modes (gate, retrigger, legato)
- Trigger probability/humanization

**Modulation Features**:
- Built-in LFO for parameter modulation
- Envelope following for dynamic response
- MIDI velocity scaling

### Technical Improvements

**Performance Optimizations**:
- SIMD vectorization for multiple instances
- Lookup table optimization for power functions
- Coefficient caching for static parameters

**Quality Enhancements**:
- Higher-order smoothing filters
- Anti-aliasing for rapid parameter changes
- Temperature-compensated timing

---

## Integration with Max SDK

This external demonstrates several important Max SDK patterns:

1. **Mathematical Precision**: Accurate implementation of exponential decay mathematics
2. **lores~ Pattern**: Perfect signal/float dual input handling for real-time control
3. **User-Configurable Features**: Flexible smoothing system controlled by creation arguments
4. **Smart Processing**: Conditional activation of expensive operations only when needed
5. **State Management**: Clean envelope lifecycle with proper termination conditions

The decay~ external serves as an example of creating specialized, high-quality envelope generators that combine mathematical accuracy with practical musical control and user customization options.

---

## References

- **Exponential Decay Mathematics**: Classical exponential function theory and discrete implementation
- **Envelope Generator Design**: Synthesis textbook envelope generation techniques
- **Click Elimination**: Digital audio processing anti-aliasing and smoothing techniques
- **Max SDK Documentation**: MSP object patterns and audio processing techniques
- **Musical Applications**: Drum machine and synthesizer envelope characteristics

---

*This external successfully provides professional-quality exponential decay envelope generation with configurable smoothing, demonstrating how mathematical precision and user customization can be combined for optimal musical results in Max/MSP environments.*