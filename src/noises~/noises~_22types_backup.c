/**
 * noises~ - Advanced multi-type noise generator with smooth morphing for Max/MSP
 * 
 * A comprehensive noise generator featuring:
 * - 22 noise types: white, pink, brown, blue, violet, grey, velvet, perlin, simplex,
 *   worley, value, fractal, dither, gaussian, filtered, crackle, dust, telegraph,
 *   shot, flicker, quantization, bit-crushed
 * - Smooth morphing between adjacent noise types
 * - Click-free parameter transitions with adaptive smoothing
 * - lores~ pattern for perfect signal/float dual input on both inlets
 * - Professional audio processing with denormal protection
 * 
 * Inlets:
 *   1. Noise type (signal or float, 0.0-21.0) - continuous morphing parameter
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

// Noise type constants - Original 7 types
#define NOISE_WHITE         0
#define NOISE_PINK          1
#define NOISE_BROWN         2
#define NOISE_BLUE          3
#define NOISE_VIOLET        4
#define NOISE_GREY          5
#define NOISE_VELVET        6

// Digital/Computational noise types (7-11)
#define NOISE_PERLIN        7
#define NOISE_SIMPLEX       8
#define NOISE_WORLEY        9
#define NOISE_VALUE         10
#define NOISE_FRACTAL       11

// Audio-specific noise types (12-16)
#define NOISE_DITHER        12
#define NOISE_GAUSSIAN      13
#define NOISE_FILTERED      14
#define NOISE_CRACKLE       15
#define NOISE_DUST          16

// Specialized noise types (17-21)
#define NOISE_TELEGRAPH     17
#define NOISE_SHOT          18
#define NOISE_FLICKER       19
#define NOISE_QUANTIZATION  20
#define NOISE_BITCRUSHED    21

#define NUM_NOISE_TYPES 22

// Default parameter values
#define DEFAULT_TYPE 0.0
#define DEFAULT_AMPLITUDE 0.5

// Parameter limits
#define MIN_TYPE 0.0
#define MAX_TYPE 21.0
#define MIN_AMPLITUDE 0.0
#define MAX_AMPLITUDE 1.0

// Pink noise constants (from pinknoise~)
#define PINK_RANDOM_BITS    24
#define PINK_RANDOM_SHIFT   ((sizeof(long)*8)-PINK_RANDOM_BITS)
#define PINK_BITS           5

// Velvet noise constants
#define VELVET_IMPULSES_PER_SEC 2205.0  // Standard velvet noise density

// Perlin noise constants
#define PERLIN_TABLE_SIZE 256
#define PERLIN_OCTAVES 4

// Simplex noise constants  
#define SIMPLEX_GRAD_SIZE 12

// Worley noise constants
#define WORLEY_POINTS_PER_CELL 1
#define WORLEY_CELL_SIZE 64.0

// Value noise constants
#define VALUE_TABLE_SIZE 256

// Fractal noise constants
#define FRACTAL_OCTAVES 4
#define FRACTAL_LACUNARITY 2.0
#define FRACTAL_PERSISTENCE 0.5

// Gaussian noise constants (Box-Muller method)
#define GAUSSIAN_PAIRS 1

// Filtered noise constants
#define FILTER_ORDER 2

// Crackle noise constants
#define CRACKLE_IMPULSES_PER_SEC 50.0
#define CRACKLE_DECAY 0.95

// Dust noise constants  
#define DUST_LAMBDA 0.0002  // Much lower average impulse rate for sparsity

// Telegraph noise constants
#define TELEGRAPH_SWITCH_RATE 10.0

// Shot noise constants
#define SHOT_RATE 100.0

// Flicker noise constants (1/f^alpha)
#define FLICKER_ALPHA 1.0
#define FLICKER_OCTAVES 8

// Quantization noise constants
#define QUANTIZATION_BITS 8

// Bit-crushed noise constants
#define BITCRUSH_BITS 4
#define BITCRUSH_RATE_DIV 8

typedef struct _noises {
    t_pxobject x_obj;           // MSP object header (must be first)
    
    // Core state
    double sr;                  // Sample rate
    double sr_inv;              // 1.0 / sample rate (optimization)
    
    // Float parameter storage (for when no signal connected)
    double type_float;          // Default noise type (0.0-21.0)
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
    
    // Perlin noise state
    double perlin_time;         // Time counter for Perlin noise
    int perlin_perm[PERLIN_TABLE_SIZE * 2];  // Permutation table
    double perlin_grad[PERLIN_TABLE_SIZE * 3]; // Gradient table
    
    // Simplex noise state
    double simplex_time;        // Time counter for Simplex noise
    int simplex_perm[PERLIN_TABLE_SIZE * 2];  // Permutation table
    double simplex_grad[SIMPLEX_GRAD_SIZE * 3]; // Gradient table
    
    // Worley noise state  
    double worley_time;         // Time counter for Worley noise
    double worley_cell_x, worley_cell_y; // Current cell coordinates
    
    // Value noise state
    double value_time;          // Time counter for Value noise
    double value_table[VALUE_TABLE_SIZE]; // Value lookup table
    
    // Fractal noise state
    double fractal_time;        // Time counter for Fractal noise
    double fractal_amplitude;   // Current amplitude multiplier
    
    // Gaussian noise state (Box-Muller method)
    double gaussian_spare;      // Stored second value from Box-Muller
    int gaussian_has_spare;     // Flag for spare value availability
    
    // Filtered noise state (2nd order filter)
    double filter_x1, filter_x2; // Filter input history
    double filter_y1, filter_y2; // Filter output history
    double filter_a1, filter_a2; // Filter coefficients
    double filter_b0, filter_b1, filter_b2;
    
    // Crackle noise state
    double crackle_energy;      // Current crackle energy level
    double crackle_last_impulse; // Time since last impulse
    
    // Dust noise state (exponential distribution)
    double dust_accumulator;    // Accumulated probability
    
    // Telegraph noise state (binary switching)
    double telegraph_state;     // Current output state (+1 or -1)
    double telegraph_time;      // Time until next switch
    
    // Shot noise state (Poisson impulses)
    double shot_accumulator;    // Accumulated energy from impulses
    double shot_time;           // Time counter
    
    // Flicker noise state (1/f^alpha with multiple octaves)
    double flicker_octaves[FLICKER_OCTAVES]; // Octave amplitudes
    double flicker_phases[FLICKER_OCTAVES];  // Octave phases
    double flicker_freqs[FLICKER_OCTAVES];   // Octave frequencies
    
    // Quantization noise state
    double quantization_levels; // Number of quantization levels
    double quantization_step;   // Quantization step size
    
    // Bit-crushed noise state
    double bitcrush_accumulator; // Sample accumulator for rate reduction
    int bitcrush_counter;       // Sample counter
    double bitcrush_held_value; // Held sample value
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

// Noise generation utility functions
void noises_init_generators(t_noises *x);

// Original 7 noise types
double noises_generate_white(t_noises *x);
double noises_generate_pink(t_noises *x);
double noises_generate_brown(t_noises *x);
double noises_generate_blue(t_noises *x);
double noises_generate_violet(t_noises *x);
double noises_generate_grey(t_noises *x);
double noises_generate_velvet(t_noises *x);

// Digital/Computational noise types
double noises_generate_perlin(t_noises *x);
double noises_generate_simplex(t_noises *x);
double noises_generate_worley(t_noises *x);
double noises_generate_value(t_noises *x);
double noises_generate_fractal(t_noises *x);

// Audio-specific noise types
double noises_generate_dither(t_noises *x);
double noises_generate_gaussian(t_noises *x);
double noises_generate_filtered(t_noises *x);
double noises_generate_crackle(t_noises *x);
double noises_generate_dust(t_noises *x);

// Specialized noise types
double noises_generate_telegraph(t_noises *x);
double noises_generate_shot(t_noises *x);
double noises_generate_flicker(t_noises *x);
double noises_generate_quantization(t_noises *x);
double noises_generate_bitcrushed(t_noises *x);

// Morphing function
double noises_morph_types(t_noises *x, double type_param);

// Utility functions
double smooth_param(double current, double target, double smooth_factor);
double dc_block(t_noises *x, double input);
uint32_t xorshift32(uint32_t *state);
long pink_random(void);

// Noise-specific utility functions
double perlin_fade(double t);
double perlin_lerp(double t, double a, double b);
double perlin_grad(int hash, double x, double y, double z);
void init_perlin_tables(t_noises *x);
void init_simplex_tables(t_noises *x);
void init_value_table(t_noises *x);
double box_muller_gaussian(t_noises *x);
double exponential_random(t_noises *x, double lambda);

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
        
        // Initialize parameter smoothing
        x->type_smooth = x->type_float;
        x->amplitude_smooth = x->amplitude_float;
        x->smooth_factor = 0.001;  // ~10ms smoothing at 44.1kHz
        
        // Initialize random number generator with unique seed
        x->rng_state = (uint32_t)(((long)x) ^ ((long)systime_ms()));
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
    
    // Re-initialize generators with new seed
    noises_init_generators(x);
}

void noises_assist(t_noises *x, void *b, long m, long a, char *s) {
    if (m == ASSIST_INLET) {
        switch (a) {
            case 0:
                sprintf(s, "(signal/float) Noise type (0-21): 0=white, 1=pink, 2=brown, 3=blue, 4=violet, 5=grey, 6=velvet, 7=perlin, 8=simplex, 9=worley, 10=value, 11=fractal, 12=dither, 13=gaussian, 14=filtered, 15=crackle, 16=dust, 17=telegraph, 18=shot, 19=flicker, 20=quantization, 21=bitcrushed");
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
    
    // Initialize new noise types
    
    // Initialize Perlin noise
    x->perlin_time = 0.0;
    init_perlin_tables(x);
    
    // Initialize Simplex noise
    x->simplex_time = 0.0;
    init_simplex_tables(x);
    
    // Initialize Worley noise
    x->worley_time = 0.0;
    x->worley_cell_x = 0.0;
    x->worley_cell_y = 0.0;
    
    // Initialize Value noise
    x->value_time = 0.0;
    init_value_table(x);
    
    // Initialize Fractal noise
    x->fractal_time = 0.0;
    x->fractal_amplitude = 1.0;
    
    // Initialize Gaussian noise
    x->gaussian_spare = 0.0;
    x->gaussian_has_spare = 0;
    
    // Initialize filtered noise (bandpass filter at 1kHz)
    double fc_filter = 1000.0 / x->sr;
    double q_filter = 4.0;  // Higher Q for more pronounced filtering
    double omega_filter = 2.0 * PI * fc_filter;
    double cos_omega_filter = cos(omega_filter);
    double sin_omega_filter = sin(omega_filter);
    double alpha_filter = sin_omega_filter / (2.0 * q_filter);
    
    // Bandpass filter coefficients (corrected)
    double norm = 1.0 + alpha_filter;
    x->filter_b0 = alpha_filter / norm;
    x->filter_b1 = 0.0;
    x->filter_b2 = -alpha_filter / norm;
    x->filter_a1 = -2.0 * cos_omega_filter / norm;
    x->filter_a2 = (1.0 - alpha_filter) / norm;
    
    x->filter_x1 = x->filter_x2 = 0.0;
    x->filter_y1 = x->filter_y2 = 0.0;
    
    // Initialize Crackle noise
    x->crackle_energy = 0.0;
    x->crackle_last_impulse = 0.0;
    
    // Initialize Dust noise
    x->dust_accumulator = 0.0;
    
    // Initialize Telegraph noise
    x->telegraph_state = 1.0;  // Start with +1
    x->telegraph_time = exponential_random(x, TELEGRAPH_SWITCH_RATE);
    
    // Initialize Shot noise
    x->shot_accumulator = 0.0;
    x->shot_time = 0.0;
    
    // Initialize Flicker noise (1/f^alpha)
    for (int i = 0; i < FLICKER_OCTAVES; i++) {
        x->flicker_octaves[i] = 1.0 / pow(2.0, i * FLICKER_ALPHA);  // 1/f^alpha amplitude
        x->flicker_phases[i] = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX);
        x->flicker_freqs[i] = 10.0 * pow(2.0, i);  // Frequency progression
    }
    
    // Initialize Quantization noise
    x->quantization_levels = (1 << QUANTIZATION_BITS);
    x->quantization_step = 2.0 / x->quantization_levels;
    
    // Initialize Bit-crushed noise
    x->bitcrush_accumulator = 0.0;
    x->bitcrush_counter = 0;
    x->bitcrush_held_value = 0.0;
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
    
    // Apply DC blocking to remove any DC offset
    double brown_output = dc_block(x, x->brown_state);
    
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
    // Generate ALL noise types every sample for smooth morphing
    // This keeps all generators synchronized and allows true morphing
    double noise_types[NUM_NOISE_TYPES];
    
    // Original 7 noise types
    noise_types[NOISE_WHITE] = noises_generate_white(x);
    noise_types[NOISE_PINK] = noises_generate_pink(x);
    noise_types[NOISE_BROWN] = noises_generate_brown(x);
    noise_types[NOISE_BLUE] = noises_generate_blue(x);
    noise_types[NOISE_VIOLET] = noises_generate_violet(x);
    noise_types[NOISE_GREY] = noises_generate_grey(x);
    noise_types[NOISE_VELVET] = noises_generate_velvet(x);
    
    // Digital/Computational noise types
    noise_types[NOISE_PERLIN] = noises_generate_perlin(x);
    noise_types[NOISE_SIMPLEX] = noises_generate_simplex(x);
    noise_types[NOISE_WORLEY] = noises_generate_worley(x);
    noise_types[NOISE_VALUE] = noises_generate_value(x);
    noise_types[NOISE_FRACTAL] = noises_generate_fractal(x);
    
    // Audio-specific noise types
    noise_types[NOISE_DITHER] = noises_generate_dither(x);
    noise_types[NOISE_GAUSSIAN] = noises_generate_gaussian(x);
    noise_types[NOISE_FILTERED] = noises_generate_filtered(x);
    noise_types[NOISE_CRACKLE] = noises_generate_crackle(x);
    noise_types[NOISE_DUST] = noises_generate_dust(x);
    
    // Specialized noise types
    noise_types[NOISE_TELEGRAPH] = noises_generate_telegraph(x);
    noise_types[NOISE_SHOT] = noises_generate_shot(x);
    noise_types[NOISE_FLICKER] = noises_generate_flicker(x);
    noise_types[NOISE_QUANTIZATION] = noises_generate_quantization(x);
    noise_types[NOISE_BITCRUSHED] = noises_generate_bitcrushed(x);
    
    // Get integer and fractional parts
    int type_int = (int)type_param;
    double type_frac = type_param - type_int;
    
    // Clamp to valid range with 40% scaling
    if (type_param <= 0.0) {
        return noise_types[NOISE_WHITE] * 0.4;
    } else if (type_param >= NUM_NOISE_TYPES - 1) {
        return noise_types[NUM_NOISE_TYPES - 1] * 0.4;
    }
    
    // For exact integer values, return the exact type with 40% scaling
    if (type_frac < 0.0001) {
        return noise_types[type_int] * 0.4;
    }
    
    // Multi-way crossfading between adjacent types
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
// NEW NOISE GENERATION FUNCTIONS
//***********************************************************************************************

// Utility functions for noise generation
double perlin_fade(double t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}

double perlin_lerp(double t, double a, double b) {
    return a + t * (b - a);
}

double perlin_grad(int hash, double x, double y, double z) {
    int h = hash & 15;
    double u = h < 8 ? x : y;
    double v = h < 4 ? y : h == 12 || h == 14 ? x : z;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

void init_perlin_tables(t_noises *x) {
    // Initialize permutation table
    for (int i = 0; i < PERLIN_TABLE_SIZE; i++) {
        x->perlin_perm[i] = i;
    }
    
    // Shuffle permutation table
    for (int i = 0; i < PERLIN_TABLE_SIZE; i++) {
        int j = xorshift32(&x->rng_state) % PERLIN_TABLE_SIZE;
        int temp = x->perlin_perm[i];
        x->perlin_perm[i] = x->perlin_perm[j];
        x->perlin_perm[j] = temp;
    }
    
    // Duplicate for easy wrapping
    for (int i = 0; i < PERLIN_TABLE_SIZE; i++) {
        x->perlin_perm[PERLIN_TABLE_SIZE + i] = x->perlin_perm[i];
    }
}

void init_simplex_tables(t_noises *x) {
    // Initialize simplex gradient table
    double grad3[12][3] = {
        {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},
        {1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},
        {0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}
    };
    
    for (int i = 0; i < SIMPLEX_GRAD_SIZE; i++) {
        x->simplex_grad[i*3] = grad3[i][0];
        x->simplex_grad[i*3+1] = grad3[i][1];
        x->simplex_grad[i*3+2] = grad3[i][2];
    }
    
    // Initialize permutation table
    for (int i = 0; i < PERLIN_TABLE_SIZE; i++) {
        x->simplex_perm[i] = i;
    }
    
    // Shuffle
    for (int i = 0; i < PERLIN_TABLE_SIZE; i++) {
        int j = xorshift32(&x->rng_state) % PERLIN_TABLE_SIZE;
        int temp = x->simplex_perm[i];
        x->simplex_perm[i] = x->simplex_perm[j];
        x->simplex_perm[j] = temp;
    }
    
    // Duplicate
    for (int i = 0; i < PERLIN_TABLE_SIZE; i++) {
        x->simplex_perm[PERLIN_TABLE_SIZE + i] = x->simplex_perm[i];
    }
}

void init_value_table(t_noises *x) {
    for (int i = 0; i < VALUE_TABLE_SIZE; i++) {
        x->value_table[i] = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX) * 2.0 - 1.0;
    }
}

double box_muller_gaussian(t_noises *x) {
    if (x->gaussian_has_spare) {
        x->gaussian_has_spare = 0;
        return x->gaussian_spare;
    }
    
    x->gaussian_has_spare = 1;
    static double u, v, mag;
    do {
        u = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX) * 2.0 - 1.0;
        v = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX) * 2.0 - 1.0;
        mag = u * u + v * v;
    } while (mag >= 1.0 || mag == 0.0);
    
    mag = sqrt(-2.0 * log(mag) / mag);
    x->gaussian_spare = v * mag;
    return u * mag;
}

double exponential_random(t_noises *x, double lambda) {
    double u = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX);
    return -log(1.0 - u) / lambda;
}

// Digital/Computational noise types
double noises_generate_perlin(t_noises *x) {
    x->perlin_time += 0.01 / x->sr;  // Slow evolution
    
    double x_coord = x->perlin_time * 50.0;
    double y_coord = 0.0;
    double z_coord = 0.0;
    
    int X = (int)floor(x_coord) & 255;
    int Y = (int)floor(y_coord) & 255;
    int Z = (int)floor(z_coord) & 255;
    
    x_coord -= floor(x_coord);
    y_coord -= floor(y_coord);
    z_coord -= floor(z_coord);
    
    double u = perlin_fade(x_coord);
    double v = perlin_fade(y_coord);
    double w = perlin_fade(z_coord);
    
    int A = x->perlin_perm[X] + Y;
    int AA = x->perlin_perm[A] + Z;
    int AB = x->perlin_perm[A + 1] + Z;
    int B = x->perlin_perm[X + 1] + Y;
    int BA = x->perlin_perm[B] + Z;
    int BB = x->perlin_perm[B + 1] + Z;
    
    double res = perlin_lerp(w, 
        perlin_lerp(v, 
            perlin_lerp(u, perlin_grad(x->perlin_perm[AA], x_coord, y_coord, z_coord),
                          perlin_grad(x->perlin_perm[BA], x_coord - 1, y_coord, z_coord)),
            perlin_lerp(u, perlin_grad(x->perlin_perm[AB], x_coord, y_coord - 1, z_coord),
                          perlin_grad(x->perlin_perm[BB], x_coord - 1, y_coord - 1, z_coord))),
        perlin_lerp(v,
            perlin_lerp(u, perlin_grad(x->perlin_perm[AA + 1], x_coord, y_coord, z_coord - 1),
                          perlin_grad(x->perlin_perm[BA + 1], x_coord - 1, y_coord, z_coord - 1)),
            perlin_lerp(u, perlin_grad(x->perlin_perm[AB + 1], x_coord, y_coord - 1, z_coord - 1),
                          perlin_grad(x->perlin_perm[BB + 1], x_coord - 1, y_coord - 1, z_coord - 1))));
    
    return res * 3.0;  // Increased scaling for audibility
}

double noises_generate_simplex(t_noises *x) {
    x->simplex_time += 0.005 / x->sr;  // Even slower evolution
    
    double x_coord = x->simplex_time * 30.0;
    double y_coord = sin(x->simplex_time * 15.0) * 10.0;
    
    // 2D Simplex noise implementation
    const double F2 = 0.5 * (sqrt(3.0) - 1.0);
    const double G2 = (3.0 - sqrt(3.0)) / 6.0;
    
    double s = (x_coord + y_coord) * F2;
    int i = (int)floor(x_coord + s);
    int j = (int)floor(y_coord + s);
    
    double t = (i + j) * G2;
    double X0 = i - t;
    double Y0 = j - t;
    double x0 = x_coord - X0;
    double y0 = y_coord - Y0;
    
    int i1, j1;
    if (x0 > y0) { i1 = 1; j1 = 0; }
    else { i1 = 0; j1 = 1; }
    
    double x1 = x0 - i1 + G2;
    double y1 = y0 - j1 + G2;
    double x2 = x0 - 1.0 + 2.0 * G2;
    double y2 = y0 - 1.0 + 2.0 * G2;
    
    int ii = i & 255;
    int jj = j & 255;
    int gi0 = x->simplex_perm[ii + x->simplex_perm[jj]] % 12;
    int gi1 = x->simplex_perm[ii + i1 + x->simplex_perm[jj + j1]] % 12;
    int gi2 = x->simplex_perm[ii + 1 + x->simplex_perm[jj + 1]] % 12;
    
    double n0 = 0, n1 = 0, n2 = 0;
    double t0 = 0.5 - x0 * x0 - y0 * y0;
    if (t0 >= 0) {
        t0 *= t0;
        n0 = t0 * t0 * (x->simplex_grad[gi0*3] * x0 + x->simplex_grad[gi0*3+1] * y0);
    }
    
    double t1 = 0.5 - x1 * x1 - y1 * y1;
    if (t1 >= 0) {
        t1 *= t1;
        n1 = t1 * t1 * (x->simplex_grad[gi1*3] * x1 + x->simplex_grad[gi1*3+1] * y1);
    }
    
    double t2 = 0.5 - x2 * x2 - y2 * y2;
    if (t2 >= 0) {
        t2 *= t2;
        n2 = t2 * t2 * (x->simplex_grad[gi2*3] * x2 + x->simplex_grad[gi2*3+1] * y2);
    }
    
    return (n0 + n1 + n2) * 70.0 * 2.5;  // Increased scaling for audibility
}

double noises_generate_worley(t_noises *x) {
    x->worley_time += 0.02 / x->sr;
    
    double cell_size = WORLEY_CELL_SIZE;
    double x_coord = x->worley_time * 20.0;
    double y_coord = sin(x->worley_time * 8.0) * 5.0;
    
    int cell_x = (int)floor(x_coord / cell_size);
    int cell_y = (int)floor(y_coord / cell_size);
    
    double min_dist = 1e9;
    
    // Check 3x3 grid of cells
    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            int test_cell_x = cell_x + i;
            int test_cell_y = cell_y + j;
            
            // Generate point in this cell using cell coordinates as seed
            uint32_t seed = (uint32_t)(test_cell_x * 73856093 + test_cell_y * 19349663);
            uint32_t temp_state = seed;
            
            double point_x = test_cell_x * cell_size + ((double)xorshift32(&temp_state) / UINT32_MAX) * cell_size;
            double point_y = test_cell_y * cell_size + ((double)xorshift32(&temp_state) / UINT32_MAX) * cell_size;
            
            double dist = sqrt((x_coord - point_x) * (x_coord - point_x) + (y_coord - point_y) * (y_coord - point_y));
            min_dist = fmin(min_dist, dist);
        }
    }
    
    return ((1.0 - min_dist / cell_size) * 2.0 - 1.0) * 1.5;  // Increased amplitude
}

double noises_generate_value(t_noises *x) {
    x->value_time += 0.008 / x->sr;
    
    double coord = x->value_time * 100.0;
    int i = (int)floor(coord);
    double f = coord - i;
    
    // Get values from table
    double a = x->value_table[i & (VALUE_TABLE_SIZE - 1)];
    double b = x->value_table[(i + 1) & (VALUE_TABLE_SIZE - 1)];
    
    // Smooth interpolation
    double smooth_f = f * f * (3.0 - 2.0 * f);
    
    return perlin_lerp(smooth_f, a, b) * 1.8;  // Increased for better audibility
}

double noises_generate_fractal(t_noises *x) {
    x->fractal_time += 0.003 / x->sr;
    
    double result = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    
    for (int i = 0; i < FRACTAL_OCTAVES; i++) {
        // Use different phases for each octave
        double coord = x->fractal_time * frequency * 50.0 + i * 100.0;
        int table_index = (int)floor(coord);
        double frac = coord - table_index;
        
        double a = x->value_table[table_index & (VALUE_TABLE_SIZE - 1)];
        double b = x->value_table[(table_index + 1) & (VALUE_TABLE_SIZE - 1)];
        
        double smooth_frac = frac * frac * (3.0 - 2.0 * frac);
        double octave_value = perlin_lerp(smooth_frac, a, b);
        
        result += octave_value * amplitude;
        amplitude *= FRACTAL_PERSISTENCE;
        frequency *= FRACTAL_LACUNARITY;
    }
    
    return result * 1.2;  // Increased for better audibility
}

// Audio-specific noise types
double noises_generate_dither(t_noises *x) {
    // Triangular PDF dither noise - more subtle than white noise
    double u1 = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX);
    double u2 = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX);
    
    // Triangular distribution (0 to 1 range, then shifted to -1 to 1)
    double triangular = (u1 + u2 - 1.0);  // -1 to 1 with triangular PDF
    
    // Apply high-frequency emphasis to make it more distinctive
    static double prev_sample = 0.0;
    double output = triangular - prev_sample * 0.5;  // Mild high-pass
    prev_sample = triangular;
    
    return output * 0.7;
}

double noises_generate_gaussian(t_noises *x) {
    // Normal distribution - sounds "fuller" than white noise
    double gaussian = box_muller_gaussian(x);
    
    // Compress extreme values to make distribution more audible
    if (gaussian > 2.0) gaussian = 2.0 + (gaussian - 2.0) * 0.1;
    if (gaussian < -2.0) gaussian = -2.0 + (gaussian + 2.0) * 0.1;
    
    return gaussian * 0.4;
}

double noises_generate_filtered(t_noises *x) {
    // Bandpass filtered white noise
    double white = noises_generate_white(x);
    
    // Apply 2nd order bandpass filter (1kHz center, Q=2)
    double output = x->filter_b0 * white + x->filter_b1 * x->filter_x1 + x->filter_b2 * x->filter_x2
                   - x->filter_a1 * x->filter_y1 - x->filter_a2 * x->filter_y2;
    
    // Update filter state
    x->filter_x2 = x->filter_x1;
    x->filter_x1 = white;
    x->filter_y2 = x->filter_y1;
    x->filter_y1 = output;
    
    return output * 2.5;  // Increased gain to compensate for filter attenuation
}

double noises_generate_crackle(t_noises *x) {
    // Vinyl crackle-like sparse impulses
    double impulse_probability = CRACKLE_IMPULSES_PER_SEC / x->sr;
    double random_val = (double)xorshift32(&x->rng_state) / (double)UINT32_MAX;
    
    if (random_val < impulse_probability) {
        // Generate new impulse
        x->crackle_energy = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX) * 2.0 - 1.0;
        x->crackle_last_impulse = 0.0;
    }
    
    // Decay existing energy
    x->crackle_energy *= CRACKLE_DECAY;
    x->crackle_last_impulse += 1.0 / x->sr;
    
    // Add some randomness to the decay
    double decay_noise = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX) * 0.1 - 0.05;
    
    return x->crackle_energy * (1.0 + decay_noise) * 1.5;
}

double noises_generate_dust(t_noises *x) {
    // Very sparse exponentially distributed impulses
    double impulse_probability = DUST_LAMBDA;  // Much lower probability
    double random_val = (double)xorshift32(&x->rng_state) / (double)UINT32_MAX;
    
    double output = 0.0;
    if (random_val < impulse_probability) {
        // Generate strong impulse when it occurs
        output = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX) * 2.0 - 1.0;
        // Make impulses more prominent
        output = output > 0 ? 0.8 : -0.8;
    }
    
    return output * 3.0;  // Higher gain since impulses are so rare
}

// Specialized noise types
double noises_generate_telegraph(t_noises *x) {
    // Binary switching noise (like telegraph key)
    x->telegraph_time -= 1.0 / x->sr;
    
    if (x->telegraph_time <= 0.0) {
        // Time to switch state
        x->telegraph_state = -x->telegraph_state;  // Flip between +1 and -1
        
        // Random interval until next switch
        x->telegraph_time = exponential_random(x, TELEGRAPH_SWITCH_RATE);
    }
    
    return x->telegraph_state * 0.8;
}

double noises_generate_shot(t_noises *x) {
    // Poisson-distributed impulses (shot noise)
    x->shot_time += 1.0 / x->sr;
    
    double impulse_probability = SHOT_RATE / x->sr;
    double random_val = (double)xorshift32(&x->rng_state) / (double)UINT32_MAX;
    
    double output = 0.0;
    if (random_val < impulse_probability) {
        // Generate impulse with exponential decay
        double amplitude = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX);
        x->shot_accumulator += amplitude;
    }
    
    // Exponential decay of accumulated energy
    x->shot_accumulator *= 0.995;
    
    return x->shot_accumulator * 1.2;
}

double noises_generate_flicker(t_noises *x) {
    // 1/f^alpha noise using multiple octaves
    double result = 0.0;
    
    for (int i = 0; i < FLICKER_OCTAVES; i++) {
        x->flicker_phases[i] += x->flicker_freqs[i] / x->sr;
        if (x->flicker_phases[i] >= 1.0) x->flicker_phases[i] -= 1.0;
        
        // Use sine wave with random phase modulation for each octave
        double phase_noise = ((double)xorshift32(&x->rng_state) / (double)UINT32_MAX) * 0.1 - 0.05;
        double octave_out = sin(TWOPI * (x->flicker_phases[i] + phase_noise));
        
        result += octave_out * x->flicker_octaves[i];
    }
    
    return result * 0.4;
}

double noises_generate_quantization(t_noises *x) {
    // Quantization noise (simulates ADC/DAC artifacts)
    double white = noises_generate_white(x);
    
    // Quantize to specified bit depth
    double levels = x->quantization_levels;
    double step = x->quantization_step;
    
    // Quantize
    double quantized = floor(white * levels + 0.5) * step;
    
    // Return quantization error
    double error = white - quantized;
    
    return error * 1.5;  // Amplify error for audibility
}

double noises_generate_bitcrushed(t_noises *x) {
    // Bit-crushed noise with sample rate reduction
    x->bitcrush_counter++;
    
    if (x->bitcrush_counter >= BITCRUSH_RATE_DIV) {
        // Time for new sample
        x->bitcrush_counter = 0;
        
        // Generate new noise
        double white = noises_generate_white(x);
        
        // Bit crush (reduce bit depth)
        double levels = (1 << BITCRUSH_BITS);
        x->bitcrush_held_value = floor(white * levels + 0.5) / levels;
    }
    
    return x->bitcrush_held_value * 0.9;
}