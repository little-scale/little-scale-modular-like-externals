/**
 * noises~ - Multi-type noise generator with smooth morphing for Max/MSP
 * 
 * An advanced noise generator featuring:
 * - 7 noise types: white, pink, brown, blue, violet, grey, velvet
 * - Smooth morphing between adjacent noise types
 * - Click-free parameter transitions with adaptive smoothing
 * - lores~ pattern for perfect signal/float dual input on both inlets
 * - Professional audio processing with denormal protection
 * 
 * Inlets:
 *   1. Noise type (signal or float, 0.0-6.0) - continuous morphing parameter
 *   2. Amplitude (signal or float, 0.0-1.0) - output level control
 * 
 * Outlets:
 *   1. Audio signal output (generated noise)
 */

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include <math.h>
#include <stdlib.h>

// Mathematical constants
#ifndef TWOPI
#define TWOPI 6.283185307179586476925286766559
#endif
#ifndef PI  
#define PI 3.1415926535897932384626433832795
#endif
#define DENORMAL_THRESHOLD 1e-15

// Noise type constants
#define NOISE_WHITE   0
#define NOISE_PINK    1
#define NOISE_BROWN   2
#define NOISE_BLUE    3
#define NOISE_VIOLET  4
#define NOISE_GREY    5
#define NOISE_VELVET  6
#define NUM_NOISE_TYPES 7

// Default parameter values
#define DEFAULT_TYPE 0.0
#define DEFAULT_AMPLITUDE 0.5

// Parameter limits
#define MIN_TYPE 0.0
#define MAX_TYPE 6.0
#define MIN_AMPLITUDE 0.0
#define MAX_AMPLITUDE 1.0

// Pink noise constants (from pinknoise~)
#define PINK_RANDOM_BITS    24
#define PINK_RANDOM_SHIFT   ((sizeof(long)*8)-PINK_RANDOM_BITS)
#define PINK_BITS           5

// Velvet noise constants
#define VELVET_IMPULSES_PER_SEC 2205.0  // Standard velvet noise density

typedef struct _noises {
    t_pxobject x_obj;           // MSP object header (must be first)
    
    // Core state
    double sr;                  // Sample rate
    double sr_inv;              // 1.0 / sample rate (optimization)
    
    // Attribute parameters
    long morphing_enabled;      // 0=discrete types, 1=smooth morphing (default 1)
    long dc_blocking_enabled;   // 0=off, 1=on (default 1)
    long seed_auto;             // 0=fixed seed, 1=auto regenerate (default 1)
    long filter_quality;        // 0=low, 1=medium, 2=high quality (default 1)
    
    // Float parameter storage (for when no signal connected)
    double type_float;          // Default noise type (0.0-6.0)
    double amplitude_float;     // Default amplitude (0.0-1.0)
    
    // Signal connection status (from count array in dsp64)
    short type_has_signal;      // Inlet 0 has signal connected
    short amplitude_has_signal; // Inlet 1 has signal connected
    
    // Parameter smoothing for click-free operation
    double type_smooth;         // Smoothed type parameter
    double amplitude_smooth;    // Smoothed amplitude parameter
    double smooth_factor;       // Smoothing coefficient (~10ms)
    
    // Random number generator state (xorshift32)
    uint32_t rng_state;
    
    // Pink noise state (Voss-McCartney algorithm)
    double pink_rows[PINK_BITS];    // Array of random values
    double pink_running_sum;        // Sum of current random values
    long pink_index;                // Index for next random update
    long pink_index_mask;           // Mask for index
    double pink_scalar;             // Normalization scalar
    
    // Brown noise state (leaky integrator)
    double brown_state;         // Brownian integration accumulator
    double brown_leak;          // Leak coefficient to prevent DC buildup
    
    // Blue/Violet noise state (differentiation)
    double prev_white;          // Previous white noise sample for differentiation
    double prev_blue;           // Previous blue noise sample for violet
    
    // Grey noise filter state (inverse A-weighting)
    double grey_x1, grey_x2;    // Filter input history
    double grey_y1, grey_y2;    // Filter output history
    double grey_a1, grey_a2;    // Filter coefficients
    double grey_b0, grey_b1, grey_b2;
    
    // Velvet noise state (using probability-based generation)
    // No state needed for probability-based velvet noise
    
    // DC blocking filter state (for integrated noise types)
    double dc_block_x1;         // Previous input sample
    double dc_block_y1;         // Previous output sample
} t_noises;

// Function prototypes
void *noises_new(t_symbol *s, long argc, t_atom *argv);
void noises_free(t_noises *x);
void noises_dsp64(t_noises *x, t_object *dsp64, short *count, double samplerate, 
                  long maxvectorsize, long flags);
void noises_perform64(t_noises *x, t_object *dsp64, double **ins, long numins, 
                      double **outs, long numouts, long sampleframes, long flags, void *userparam);
void noises_float(t_noises *x, double f);
void noises_type(t_noises *x, double f);
void noises_amp(t_noises *x, double f);
void noises_seed(t_noises *x, long seed);
void noises_assist(t_noises *x, void *b, long m, long a, char *s);

// Attribute setter prototypes
t_max_err noises_attr_setmorphing(t_noises *x, void *attr, long argc, t_atom *argv);
t_max_err noises_attr_setdcblocking(t_noises *x, void *attr, long argc, t_atom *argv);
t_max_err noises_attr_setseedauto(t_noises *x, void *attr, long argc, t_atom *argv);
t_max_err noises_attr_setfilterquality(t_noises *x, void *attr, long argc, t_atom *argv);

// Noise generation utility functions
void noises_init_generators(t_noises *x);
double noises_generate_white(t_noises *x);
double noises_generate_pink(t_noises *x);
double noises_generate_brown(t_noises *x);
double noises_generate_blue(t_noises *x);
double noises_generate_violet(t_noises *x);
double noises_generate_grey(t_noises *x);
double noises_generate_velvet(t_noises *x);
double noises_morph_types(t_noises *x, double type_param);

// Utility functions
double smooth_param(double current, double target, double smooth_factor);
double dc_block(t_noises *x, double input);
uint32_t xorshift32(uint32_t *state);
long pink_random(void);

// Global class pointer
static t_class *noises_class = NULL;

//***********************************************************************************************

void ext_main(void *r) {
    // Create class
    t_class *c = class_new("noises~", (method)noises_new, (method)noises_free,
                          sizeof(t_noises), NULL, A_GIMME, 0);
    
    // Add methods
    class_addmethod(c, (method)noises_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)noises_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)noises_float, "float", A_FLOAT, 0);
    class_addmethod(c, (method)noises_type, "type", A_FLOAT, 0);
    class_addmethod(c, (method)noises_amp, "amp", A_FLOAT, 0);
    class_addmethod(c, (method)noises_seed, "seed", A_LONG, 0);
    
    // Attributes with Max 9 autocomplete support
    CLASS_ATTR_LONG(c, "morphing", 0, t_noises, morphing_enabled);
    CLASS_ATTR_BASIC(c, "morphing", 0);
    CLASS_ATTR_LABEL(c, "morphing", 0, "Morphing Mode");
    CLASS_ATTR_ACCESSORS(c, "morphing", 0, noises_attr_setmorphing);
    CLASS_ATTR_ORDER(c, "morphing", 0, "1");
    CLASS_ATTR_FILTER_CLIP(c, "morphing", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "morphing", 0, "1");
    CLASS_ATTR_STYLE(c, "morphing", 0, "onoff");
    
    CLASS_ATTR_LONG(c, "dc_blocking", 0, t_noises, dc_blocking_enabled);
    CLASS_ATTR_BASIC(c, "dc_blocking", 0);
    CLASS_ATTR_LABEL(c, "dc_blocking", 0, "DC Blocking Filter");
    CLASS_ATTR_ACCESSORS(c, "dc_blocking", 0, noises_attr_setdcblocking);
    CLASS_ATTR_ORDER(c, "dc_blocking", 0, "2");
    CLASS_ATTR_FILTER_CLIP(c, "dc_blocking", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "dc_blocking", 0, "1");
    CLASS_ATTR_STYLE(c, "dc_blocking", 0, "onoff");
    
    CLASS_ATTR_LONG(c, "seed_auto", 0, t_noises, seed_auto);
    CLASS_ATTR_BASIC(c, "seed_auto", 0);
    CLASS_ATTR_LABEL(c, "seed_auto", 0, "Auto Seed Generation");
    CLASS_ATTR_ACCESSORS(c, "seed_auto", 0, noises_attr_setseedauto);
    CLASS_ATTR_ORDER(c, "seed_auto", 0, "3");
    CLASS_ATTR_FILTER_CLIP(c, "seed_auto", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "seed_auto", 0, "1");
    CLASS_ATTR_STYLE(c, "seed_auto", 0, "onoff");
    
    CLASS_ATTR_LONG(c, "filter_quality", 0, t_noises, filter_quality);
    CLASS_ATTR_BASIC(c, "filter_quality", 0);
    CLASS_ATTR_LABEL(c, "filter_quality", 0, "Filter Quality");
    CLASS_ATTR_ACCESSORS(c, "filter_quality", 0, noises_attr_setfilterquality);
    CLASS_ATTR_ORDER(c, "filter_quality", 0, "4");
    CLASS_ATTR_FILTER_CLIP(c, "filter_quality", 0, 2);
    CLASS_ATTR_DEFAULT_SAVE(c, "filter_quality", 0, "1");
    CLASS_ATTR_STYLE(c, "filter_quality", 0, "text");
    
    // Initialize for MSP
    class_dspinit(c);
    
    // Register class
    class_register(CLASS_BOX, c);
    noises_class = c;
}

//***********************************************************************************************

void *noises_new(t_symbol *s, long argc, t_atom *argv) {
    t_noises *x = (t_noises *)object_alloc(noises_class);
    
    if (x) {
        // Setup 2 signal inlets using lores~ pattern (no proxy inlets needed)
        dsp_setup((t_pxobject *)x, 2);
        
        // Create signal outlet
        outlet_new(x, "signal");
        
        // Initialize core state
        x->sr = 44100.0;  // Default, will be updated in dsp64
        x->sr_inv = 1.0 / x->sr;
        
        // Initialize default parameters from arguments or defaults
        x->type_float = (argc >= 1 && atom_gettype(argv) == A_FLOAT) ? 
                       atom_getfloat(argv) : DEFAULT_TYPE;
        x->amplitude_float = (argc >= 2 && atom_gettype(argv + 1) == A_FLOAT) ? 
                            atom_getfloat(argv + 1) : DEFAULT_AMPLITUDE;
        
        // Clamp parameters to safe ranges
        x->type_float = fmax(MIN_TYPE, fmin(MAX_TYPE, x->type_float));
        x->amplitude_float = fmax(MIN_AMPLITUDE, fmin(MAX_AMPLITUDE, x->amplitude_float));
        
        // Initialize signal connection status
        x->type_has_signal = 0;
        x->amplitude_has_signal = 0;
        
        // Initialize attributes with defaults
        x->morphing_enabled = 1;        // Morphing enabled by default
        x->dc_blocking_enabled = 1;     // DC blocking enabled by default
        x->seed_auto = 1;               // Auto seed generation enabled
        x->filter_quality = 1;          // Medium quality by default
        
        // Process attributes early for proper initialization
        long offset = attr_args_offset((short)argc, argv);
        attr_args_process(x, (short)argc, argv);
        
        // Initialize parameter smoothing
        x->type_smooth = x->type_float;
        x->amplitude_smooth = x->amplitude_float;
        x->smooth_factor = 0.001;  // ~10ms smoothing at 44.1kHz
        
        // Initialize random number generator
        if (x->seed_auto) {
            x->rng_state = (uint32_t)(((long)x) ^ ((long)systime_ms()));
        } else {
            x->rng_state = 1;  // Fixed seed for reproducible results
        }
        if (x->rng_state == 0) x->rng_state = 1;  // Avoid zero state
        
        // Initialize all noise generators
        noises_init_generators(x);
        
        // Initialize DC blocker
        x->dc_block_x1 = 0.0;
        x->dc_block_y1 = 0.0;
    }
    
    return x;
}

//***********************************************************************************************

void noises_free(t_noises *x) {
    // Cleanup MSP resources
    dsp_free((t_pxobject *)x);
}

//***********************************************************************************************

void noises_dsp64(t_noises *x, t_object *dsp64, short *count, double samplerate, 
                  long maxvectorsize, long flags) {
    // Update sample rate
    x->sr = samplerate;
    x->sr_inv = 1.0 / samplerate;
    
    // Update smoothing factor based on sample rate (target ~10ms)
    x->smooth_factor = 1.0 - exp(-1.0 / (0.01 * samplerate));
    
    // Track signal connections using lores~ pattern
    x->type_has_signal = count[0];
    x->amplitude_has_signal = count[1];
    
    // Re-initialize generators that depend on sample rate
    noises_init_generators(x);
    
    // Add to DSP chain
    object_method(dsp64, gensym("dsp_add64"), x, noises_perform64, 0, NULL);
}

//***********************************************************************************************

void noises_perform64(t_noises *x, t_object *dsp64, double **ins, long numins, 
                      double **outs, long numouts, long sampleframes, long flags, void *userparam) {
    // Input and output pointers
    double *type_in = ins[0];
    double *amp_in = ins[1];
    double *out = outs[0];
    
    // Local state variables for efficiency
    double smooth_factor = x->smooth_factor;
    
    // Process each sample
    for (long i = 0; i < sampleframes; i++) {
        // Get parameters using lores~ pattern (signal or float)
        double type_target = x->type_has_signal ? type_in[i] : x->type_float;
        double amp_target = x->amplitude_has_signal ? amp_in[i] : x->amplitude_float;
        
        // Clamp parameters to safe ranges
        type_target = fmax(MIN_TYPE, fmin(MAX_TYPE, type_target));
        amp_target = fmax(MIN_AMPLITUDE, fmin(MAX_AMPLITUDE, amp_target));
        
        // Apply parameter smoothing for click prevention
        double type_param = x->type_has_signal ? 
                           smooth_param(x->type_smooth, type_target, smooth_factor * 0.1) :
                           smooth_param(x->type_smooth, type_target, smooth_factor);
        double amplitude = x->amplitude_has_signal ?
                          smooth_param(x->amplitude_smooth, amp_target, smooth_factor * 0.1) :
                          smooth_param(x->amplitude_smooth, amp_target, smooth_factor);
        
        // Update smoothed parameters
        x->type_smooth = type_param;
        x->amplitude_smooth = amplitude;
        
        // Generate noise with morphing between types
        double noise_output = noises_morph_types(x, type_param);
        
        // Apply amplitude and denormal protection
        double output = noise_output * amplitude;
        if (fabs(output) < DENORMAL_THRESHOLD) output = 0.0;
        
        // Store output
        out[i] = output;
    }
}

//***********************************************************************************************

void noises_float(t_noises *x, double f) {
    // Route float messages to appropriate inlet using proxy_getinlet
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 0:  // Type inlet
            x->type_float = fmax(MIN_TYPE, fmin(MAX_TYPE, f));
            break;
        case 1:  // Amplitude inlet
            x->amplitude_float = fmax(MIN_AMPLITUDE, fmin(MAX_AMPLITUDE, f));
            break;
    }
}

//***********************************************************************************************

void noises_type(t_noises *x, double f) {
    // Set noise type via message
    x->type_float = fmax(MIN_TYPE, fmin(MAX_TYPE, f));
}

void noises_amp(t_noises *x, double f) {
    // Set amplitude via message
    x->amplitude_float = fmax(MIN_AMPLITUDE, fmin(MAX_AMPLITUDE, f));
}

void noises_seed(t_noises *x, long seed) {
    // Set random seed for reproducible noise
    x->rng_state = (uint32_t)seed;
    if (x->rng_state == 0) x->rng_state = 1;  // Avoid zero state
    
    // Turn off auto-seeding when user sets explicit seed
    x->seed_auto = 0;
    
    // Re-initialize generators with new seed
    noises_init_generators(x);
}

void noises_assist(t_noises *x, void *b, long m, long a, char *s) {
    if (m == ASSIST_INLET) {
        switch (a) {
            case 0:
                sprintf(s, "(signal/float) Noise type (0-6): 0=white, 1=pink, 2=brown, 3=blue, 4=violet, 5=grey, 6=velvet");
                break;
            case 1:
                sprintf(s, "(signal/float) Amplitude (0.0-1.0)");
                break;
        }
    } else if (m == ASSIST_OUTLET) {
        sprintf(s, "(signal) Noise output");
    }
}

//***********************************************************************************************
// Utility functions
//***********************************************************************************************

double smooth_param(double current, double target, double smooth_factor) {
    // Exponential smoothing for parameter changes (prevents clicks)
    return current + smooth_factor * (target - current);
}

double dc_block(t_noises *x, double input) {
    // High-pass filter to remove DC offset: H(z) = (1 - z^-1) / (1 - 0.995 * z^-1)
    double output = input - x->dc_block_x1 + 0.995 * x->dc_block_y1;
    x->dc_block_x1 = input;
    x->dc_block_y1 = output;
    return output;
}

uint32_t xorshift32(uint32_t *state) {
    // Fast, high-quality random number generator
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

long pink_random(void) {
    // Random number generator for pink noise (from pinknoise~)
    static unsigned long randSeed = 22222;
    randSeed = (randSeed * 196314165) + 907633515;
    return randSeed;
}

//***********************************************************************************************
// Noise generator initialization and core functions (to be implemented)
//***********************************************************************************************

void noises_init_generators(t_noises *x) {
    // Initialize pink noise generator
    x->pink_index = 0;
    x->pink_index_mask = (1 << PINK_BITS) - 1;
    x->pink_scalar = 1.0 / (1 << (PINK_BITS + 1));
    x->pink_running_sum = 0.0;
    for (int i = 0; i < PINK_BITS; i++) {
        x->pink_rows[i] = 0.0;
    }
    
    // Initialize brown noise
    x->brown_state = 0.0;
    x->brown_leak = 0.9999;  // Small leak to prevent DC buildup
    
    // Initialize differentiation states
    x->prev_white = 0.0;
    x->prev_blue = 0.0;
    
    // Initialize grey noise filter (inverse A-weighting approximation)
    // Simple 2-pole filter approximating inverse A-weighting
    double fc = 1000.0 / x->sr;  // Normalized frequency
    double q = 0.707;
    double omega = 2.0 * PI * fc;
    double cos_omega = cos(omega);
    double sin_omega = sin(omega);
    double alpha = sin_omega / (2.0 * q);
    
    x->grey_b0 = (1.0 + alpha) / (1.0 + alpha);
    x->grey_b1 = (-2.0 * cos_omega) / (1.0 + alpha);
    x->grey_b2 = (1.0 - alpha) / (1.0 + alpha);
    x->grey_a1 = (-2.0 * cos_omega) / (1.0 + alpha);
    x->grey_a2 = (1.0 - alpha) / (1.0 + alpha);
    
    x->grey_x1 = x->grey_x2 = 0.0;
    x->grey_y1 = x->grey_y2 = 0.0;
    
    // Velvet noise needs no persistent state for probability-based generation
}

// Placeholder implementations - to be completed in next steps
double noises_generate_white(t_noises *x) {
    // Convert to float range [-1, 1] with proper scaling
    return ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX) * 2.0 - 1.0;
}

double noises_generate_pink(t_noises *x) {
    // Simplified and corrected pink noise algorithm
    // Replace the appropriate random number in our table
    x->pink_index = (x->pink_index + 1) & x->pink_index_mask;
    
    // If index is zero, replace all random numbers
    if (x->pink_index != 0) {
        // Replace one random number based on trailing zeros in index
        int num_zeros = 0;
        int n = x->pink_index;
        while ((n & 1) == 0) {
            n = n >> 1;
            num_zeros++;
        }
        if (num_zeros < PINK_BITS) {
            // Generate new random value in range [-1, 1]
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
    
    // Add white noise component for proper spectral characteristics
    double white = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX) * 2.0 - 1.0;
    
    // The sum is our pink noise - scale appropriately
    // Pink noise has roughly same RMS as white noise when properly normalized
    return (x->pink_running_sum + white) * 0.578;
}

double noises_generate_brown(t_noises *x) {
    // Brown noise via leaky integrator of white noise
    double white = noises_generate_white(x);
    
    // Leaky integrator: y[n] = leak * y[n-1] + input
    x->brown_state = x->brown_leak * x->brown_state + white * 0.1;
    
    // Apply DC blocking if enabled
    double brown_output = x->dc_blocking_enabled ? dc_block(x, x->brown_state) : x->brown_state;
    
    // Scale to appropriate level (brown noise needs significant scaling due to integration)
    return brown_output * 2.25;
}

double noises_generate_blue(t_noises *x) {
    // Blue noise via differentiation of white noise (+3dB/octave)
    double white = noises_generate_white(x);
    
    // First difference: y[n] = x[n] - x[n-1]
    double blue_output = white - x->prev_white;
    x->prev_white = white;
    
    // Scale appropriately (differentiation increases amplitude, so scale down)
    return blue_output * 0.6;
}

double noises_generate_violet(t_noises *x) {
    // Violet noise via double differentiation of white noise (+6dB/octave)
    double blue = noises_generate_blue(x);
    
    // Second difference: y[n] = x[n] - x[n-1]
    double violet_output = blue - x->prev_blue;
    x->prev_blue = blue;
    
    // Scale appropriately (double differentiation increases amplitude significantly)
    return violet_output * 0.6;
}

double noises_generate_grey(t_noises *x) {
    // Grey noise via inverse A-weighting filter applied to white noise
    double white = noises_generate_white(x);
    
    // Apply 2-pole filter (approximation of inverse A-weighting)
    double output = x->grey_b0 * white + x->grey_b1 * x->grey_x1 + x->grey_b2 * x->grey_x2
                   - x->grey_a1 * x->grey_y1 - x->grey_a2 * x->grey_y2;
    
    // Update filter state
    x->grey_x2 = x->grey_x1;
    x->grey_x1 = white;
    x->grey_y2 = x->grey_y1;
    x->grey_y1 = output;
    
    // Scale appropriately (filter can boost certain frequencies)
    // Apply filter quality adjustment
    if (x->filter_quality == 0) {
        output *= 0.5;  // Low quality: reduce filter effect
    } else if (x->filter_quality == 2) {
        output *= 1.2;  // High quality: enhance filter effect
    }
    
    return output * 0.72;
}

double noises_generate_velvet(t_noises *x) {
    // Velvet noise: sparse random impulses at ~2205 impulses/sec
    // Generate random impulse with low probability for proper velvet noise
    
    double impulse_probability = VELVET_IMPULSES_PER_SEC / x->sr;
    double random_val = (double)xorshift32(&x->rng_state) / (double)UINT32_MAX;
    
    double output = 0.0;
    if (random_val < impulse_probability) {
        // Generate random impulse amplitude (±1)
        output = (xorshift32(&x->rng_state) & 1) ? 1.0 : -1.0;
    }
    
    // Scale velvet noise to match other types (compensate for sparsity)
    return output * 0.8;  // Increased further for better balance
}

double noises_morph_types(t_noises *x, double type_param) {
    // Generate ALL noise types every sample for smooth morphing (when morphing enabled)
    // This keeps all generators synchronized and allows true morphing
    double noise_types[NUM_NOISE_TYPES];
    noise_types[NOISE_WHITE] = noises_generate_white(x);
    noise_types[NOISE_PINK] = noises_generate_pink(x);
    noise_types[NOISE_BROWN] = noises_generate_brown(x);
    noise_types[NOISE_BLUE] = noises_generate_blue(x);
    noise_types[NOISE_VIOLET] = noises_generate_violet(x);
    noise_types[NOISE_GREY] = noises_generate_grey(x);
    noise_types[NOISE_VELVET] = noises_generate_velvet(x);
    
    // Get integer and fractional parts
    int type_int = (int)type_param;
    double type_frac = type_param - type_int;
    
    // Clamp to valid range with 40% scaling
    if (type_param <= 0.0) {
        return noise_types[NOISE_WHITE] * 0.4;
    } else if (type_param >= NUM_NOISE_TYPES - 1) {
        return noise_types[NUM_NOISE_TYPES - 1] * 0.4;
    }
    
    // If morphing is disabled, snap to nearest integer type
    if (!x->morphing_enabled) {
        int nearest_type = (int)(type_param + 0.5);  // Round to nearest
        nearest_type = fmax(0, fmin(NUM_NOISE_TYPES - 1, nearest_type));
        return noise_types[nearest_type] * 0.4;
    }
    
    // For exact integer values, return the exact type with 40% scaling
    if (type_frac < 0.0001) {
        return noise_types[type_int] * 0.4;
    }
    
    // Multi-way crossfading between adjacent types (morphing enabled)
    // Use smoothstep for more gradual transitions
    double smooth_frac = type_frac * type_frac * (3.0 - 2.0 * type_frac);
    
    // Get the two adjacent noise types
    double noise_a = noise_types[type_int];
    double noise_b = noise_types[type_int + 1];
    
    // Equal-power crossfade between adjacent types
    double mix_a = cos(smooth_frac * PI * 0.5);
    double mix_b = sin(smooth_frac * PI * 0.5);
    
    // Apply 40% overall output scaling (50% of previous 80%)
    return (noise_a * mix_a + noise_b * mix_b) * 0.4;
}

//***********************************************************************************************
// Attribute Setters
//***********************************************************************************************

t_max_err noises_attr_setmorphing(t_noises *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->morphing_enabled = atom_getlong(argv) ? 1 : 0;
    }
    return 0;
}

t_max_err noises_attr_setdcblocking(t_noises *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->dc_blocking_enabled = atom_getlong(argv) ? 1 : 0;
        
        // Reset DC blocking filter state when toggling
        x->dc_block_x1 = 0.0;
        x->dc_block_y1 = 0.0;
    }
    return 0;
}

t_max_err noises_attr_setseedauto(t_noises *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->seed_auto = atom_getlong(argv) ? 1 : 0;
        
        // Generate new random seed if auto-seeding is enabled
        if (x->seed_auto) {
            x->rng_state = (uint32_t)(((long)x) ^ ((long)systime_ms()));
            if (x->rng_state == 0) x->rng_state = 1;
            noises_init_generators(x);
        }
    }
    return 0;
}

t_max_err noises_attr_setfilterquality(t_noises *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        long quality = atom_getlong(argv);
        x->filter_quality = fmax(0, fmin(2, quality));  // Clamp to 0-2 range
        
        // Re-initialize filters with new quality setting
        noises_init_generators(x);
    }
    return 0;
}
