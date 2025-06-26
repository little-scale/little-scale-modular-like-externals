# vactrol~ - VTL5C3 Vactrol Emulation

A Max/MSP external that emulates the smooth low pass gate behavior of a VTL5C3 vactrol with authentic exponential decay characteristics and tube saturation.

## Features

- **Authentic Vactrol Behavior**: Based on VTL5C3 characteristics with resistance range from ~100Ω (bright) to ~1MΩ+ (dark)
- **Exponential Decay Timing**: Fast attack (2-5ms) when triggered, slow exponential decay (50-200ms configurable)
- **Variable Filter Response**: 1-pole (6dB/octave) or 2-pole (12dB/octave) low-pass filtering
- **Tube Saturation**: Asymmetric tanh-based saturation with configurable drive and character
- **Bang Triggering**: Sample-accurate triggering with instant retriggering support
- **CV Signal Input**: Direct 0-1 signal control for voltage-controlled low pass gate behavior
- **Musical Response**: Percussive sounds evolve naturally into sustained drones with harmonic warmth

## Technical Implementation

### Signal Processing Chain
1. **Trigger Detection**: Bang message triggers LED current to maximum brightness
2. **Resistance Modeling**: R(t) = 100 + (1M-100) * e^(-t/τ_decay)
3. **Filter Response**: Variable cutoff frequency fc = 1/(2π * R * C) where C = 47nF
4. **Amplitude Control**: Simultaneous VCA behavior following same resistance curve
5. **Tube Saturation**: Asymmetric clipping with different gain for positive/negative peaks
6. **Retriggering**: New bang resets decay state instantly for musical playability

### Parameters
- **Poles**: 1 or 2 pole filter response (1 = 6dB/octave, 2 = 12dB/octave)
- **Decay Time**: Adjustable τ_decay from 50ms to 500ms
- **Tube Drive**: Saturation amount from 0.0 (clean) to 1.0 (heavily saturated)
- **Tube Character**: Asymmetric clipping behavior (0.0 = symmetric, 1.0 = asymmetric)

## Usage

### Basic Syntax
```
vactrol~ [poles] [decay_time] [tube_drive]
```

### Arguments
- `poles` (int): Number of filter poles (1 or 2, default: 1)
- `decay_time` (float): Decay time constant in seconds (0.05-0.5, default: 0.15)
- `tube_drive` (float): Tube saturation amount (0.0-1.0, default: 0.7)
- `character` (float): Tube asymmetric character (0.01-1.0, default: 0.7)

### Messages
- `bang`: Trigger the vactrol (send to right inlet)
- `decay [float]`: Set decay time in seconds (0.05 - 0.5)
- `poles [int]`: Set filter poles (1 or 2)
- `drive [float]`: Set tube drive amount (0.0 - 1.0)
- `character [float]`: Set tube asymmetric character (0.01 - 1.0)

### Inlets and Outlets
- **Left Inlet**: Audio signal input
- **Middle Inlet**: CV signal input (0-1) for direct vactrol control
- **Right Inlet**: Bang messages for triggering
- **Outlet**: Filtered audio with vactrol envelope and tube saturation

## Examples

### Basic Low Pass Gate
```
vactrol~ 1 0.1 0.3
```
Single-pole filter with 100ms decay and light tube saturation.

### Two-Pole Buchla Style
```
vactrol~ 2 0.15 0.7
```
Two-pole filter with 150ms decay and moderate tube drive for classic Buchla 292 character.

### Fast Percussive Gate
```
vactrol~ 1 0.05 0.8
```
Fast 50ms decay with heavy tube saturation for snappy percussive processing.

## Technical Specifications

- **Sample Rate**: Any supported by Max/MSP
- **Architecture**: Universal binary (x86_64 + arm64)
- **Requirements**: Max 8.2 or later
- **CPU Usage**: Optimized for real-time performance
- **Latency**: Zero latency processing

## Implementation Notes

The external follows authentic VTL5C3 vactrol characteristics:
- Resistance curve uses actual exponential decay mathematics
- Filter cutoff calculation based on RC time constants
- Tube saturation models asymmetric valve behavior
- Sample-accurate triggering ensures musical timing precision

Built using the Max SDK with proven patterns from the Max external development community.