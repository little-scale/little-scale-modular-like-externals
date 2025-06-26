/**
 * cycle.fold~ - Wave folding oscillator with phase warping for Max/MSP
 * 
 * An advanced oscillator featuring:
 * - Clean sine wave generation with phase accumulation
 * - Musical phase warping (horizontal wave distortion)
 * - Progressive threshold wave folding with reflection algorithm
 * - Anti-aliasing protection
 * - DC offset removal (always applied for consistency)
 * - Parameter smoothing for click-free transitions (both signal and float inputs)
 * - lores~ pattern for perfect signal/float dual input on all 3 inlets
 * 
 * Inlets:
 *   1. Frequency in Hz (signal or float) + bang for phase reset
 *   2. Fold amount (signal or float, 0.0-1.0) - wave folding intensity
 *   3. Warp amount (signal or float, -1.0-1.0) - phase distortion amount
 * 
 * Outlets:
 *   1. Audio signal output (folded and warped sine wave)
 */

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include <math.h>

// Mathematical constants (use Max SDK definitions)
#ifndef TWOPI
#define TWOPI 6.283185307179586476925286766559
#endif
#ifndef PI  
#define PI 3.1415926535897932384626433832795
#endif
#define DENORMAL_THRESHOLD 1e-15

// Default parameter values
#define DEFAULT_FREQUENCY 440.0
#define DEFAULT_FOLD_AMOUNT 0.0
#define DEFAULT_WARP_AMOUNT 0.0

// Parameter limits
#define MIN_FREQUENCY 0.001
#define MAX_FREQUENCY 20000.0
#define MIN_FOLD_AMOUNT 0.0
#define MAX_FOLD_AMOUNT 1.0
#define MIN_WARP_AMOUNT -1.0
#define MAX_WARP_AMOUNT 1.0

typedef struct _cyclefold {
    t_pxobject x_obj;           // MSP object header (must be first)
    
    // Core oscillator state
    double phase;               // Current phase (0.0 to 1.0)
    double sr;                  // Sample rate
    double sr_inv;              // 1.0 / sample rate (optimization)
    
    // Attribute parameters
    long folding_algorithm;     // 0=reflection, 1=tanh, 2=hybrid (default 0)
    long antialiasing_enabled;  // 0=off, 1=on (default 1)
    long dc_blocking_enabled;   // 0=off, 1=on (default 1)
    long warp_mode;             // 0=symmetric, 1=asymmetric (default 0)
    
    // Float parameter storage (for when no signal connected)
    double frequency_float;     // Default frequency (Hz)
    double fold_amount_float;   // Default fold amount (0.0-1.0)
    double warp_amount_float;   // Default warp amount (-1.0-1.0)
    
    // Signal connection status (from count array in dsp64)
    short frequency_has_signal; // Inlet 0 has signal connected
    short fold_has_signal;      // Inlet 1 has signal connected
    short warp_has_signal;      // Inlet 2 has signal connected
    
    // Parameter smoothing for float inputs (prevents clicks)
    double fold_smooth;         // Smoothed fold parameter
    double warp_smooth;         // Smoothed warp parameter
    double smooth_factor;       // Smoothing coefficient (~10ms)
    
    // DC blocker state (removes DC offset from folded output)
    double dc_block_x1;         // Previous input sample
    double dc_block_y1;         // Previous output sample
} t_cyclefold;

// Function prototypes
void *cyclefold_new(t_symbol *s, long argc, t_atom *argv);
void cyclefold_free(t_cyclefold *x);
void cyclefold_dsp64(t_cyclefold *x, t_object *dsp64, short *count, double samplerate, 
                     long maxvectorsize, long flags);
void cyclefold_perform64(t_cyclefold *x, t_object *dsp64, double **ins, long numins, 
                         double **outs, long numouts, long sampleframes, long flags, void *userparam);
void cyclefold_float(t_cyclefold *x, double f);
void cyclefold_bang(t_cyclefold *x);
void cyclefold_assist(t_cyclefold *x, void *b, long m, long a, char *s);

// Attribute setter prototypes
t_max_err cyclefold_attr_setfolding(t_cyclefold *x, void *attr, long argc, t_atom *argv);
t_max_err cyclefold_attr_setantialiasing(t_cyclefold *x, void *attr, long argc, t_atom *argv);
t_max_err cyclefold_attr_setdcblocking(t_cyclefold *x, void *attr, long argc, t_atom *argv);
t_max_err cyclefold_attr_setwarpmode(t_cyclefold *x, void *attr, long argc, t_atom *argv);

// Mathematical utility functions
double warp_phase_improved(double phase, double warp_amount, long warp_mode);
double fold_wave_improved(double input, double fold_amount, double frequency, double sr, t_cyclefold *x);
double dc_block(t_cyclefold *x, double input);
double smooth_param(double current, double target, double smooth_factor);

// Global class pointer
static t_class *cyclefold_class = NULL;

//***********************************************************************************************

void ext_main(void *r) {
    // Create class
    t_class *c = class_new("cycle.fold~", (method)cyclefold_new, (method)cyclefold_free,
                          sizeof(t_cyclefold), NULL, A_GIMME, 0);
    
    // Add methods
    class_addmethod(c, (method)cyclefold_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)cyclefold_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)cyclefold_float, "float", A_FLOAT, 0);
    class_addmethod(c, (method)cyclefold_bang, "bang", 0);
    
    // Attributes with Max 9 autocomplete support
    CLASS_ATTR_LONG(c, "folding_algorithm", 0, t_cyclefold, folding_algorithm);
    CLASS_ATTR_BASIC(c, "folding_algorithm", 0);
    CLASS_ATTR_LABEL(c, "folding_algorithm", 0, "Folding Algorithm");
    CLASS_ATTR_ACCESSORS(c, "folding_algorithm", 0, cyclefold_attr_setfolding);
    CLASS_ATTR_ORDER(c, "folding_algorithm", 0, "1");
    CLASS_ATTR_FILTER_CLIP(c, "folding_algorithm", 0, 2);
    CLASS_ATTR_DEFAULT_SAVE(c, "folding_algorithm", 0, "0");
    CLASS_ATTR_STYLE(c, "folding_algorithm", 0, "enumindex");
    
    CLASS_ATTR_LONG(c, "antialiasing", 0, t_cyclefold, antialiasing_enabled);
    CLASS_ATTR_BASIC(c, "antialiasing", 0);
    CLASS_ATTR_LABEL(c, "antialiasing", 0, "Anti-aliasing Protection");
    CLASS_ATTR_ACCESSORS(c, "antialiasing", 0, cyclefold_attr_setantialiasing);
    CLASS_ATTR_ORDER(c, "antialiasing", 0, "2");
    CLASS_ATTR_FILTER_CLIP(c, "antialiasing", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "antialiasing", 0, "1");
    CLASS_ATTR_STYLE(c, "antialiasing", 0, "onoff");
    
    CLASS_ATTR_LONG(c, "dc_blocking", 0, t_cyclefold, dc_blocking_enabled);
    CLASS_ATTR_BASIC(c, "dc_blocking", 0);
    CLASS_ATTR_LABEL(c, "dc_blocking", 0, "DC Blocking Filter");
    CLASS_ATTR_ACCESSORS(c, "dc_blocking", 0, cyclefold_attr_setdcblocking);
    CLASS_ATTR_ORDER(c, "dc_blocking", 0, "3");
    CLASS_ATTR_FILTER_CLIP(c, "dc_blocking", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "dc_blocking", 0, "1");
    CLASS_ATTR_STYLE(c, "dc_blocking", 0, "onoff");
    
    CLASS_ATTR_LONG(c, "warp_mode", 0, t_cyclefold, warp_mode);
    CLASS_ATTR_BASIC(c, "warp_mode", 0);
    CLASS_ATTR_LABEL(c, "warp_mode", 0, "Warp Mode");
    CLASS_ATTR_ACCESSORS(c, "warp_mode", 0, cyclefold_attr_setwarpmode);
    CLASS_ATTR_ORDER(c, "warp_mode", 0, "4");
    CLASS_ATTR_FILTER_CLIP(c, "warp_mode", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "warp_mode", 0, "0");
    CLASS_ATTR_STYLE(c, "warp_mode", 0, "onoff");
    
    // Initialize for MSP
    class_dspinit(c);
    
    // Register class
    class_register(CLASS_BOX, c);
    cyclefold_class = c;
}

//***********************************************************************************************

void *cyclefold_new(t_symbol *s, long argc, t_atom *argv) {
    t_cyclefold *x = (t_cyclefold *)object_alloc(cyclefold_class);
    
    if (x) {
        // Setup 3 signal inlets using lores~ pattern (no proxy inlets needed)
        dsp_setup((t_pxobject *)x, 3);
        
        // Create signal outlet
        outlet_new(x, "signal");
        
        // Initialize core state
        x->phase = 0.0;
        x->sr = 44100.0;  // Default, will be updated in dsp64
        x->sr_inv = 1.0 / x->sr;
        
        // Initialize default parameters from arguments or defaults
        x->frequency_float = (argc >= 1 && atom_gettype(argv) == A_FLOAT) ? 
                            atom_getfloat(argv) : DEFAULT_FREQUENCY;
        x->fold_amount_float = (argc >= 2 && atom_gettype(argv + 1) == A_FLOAT) ? 
                              atom_getfloat(argv + 1) : DEFAULT_FOLD_AMOUNT;
        x->warp_amount_float = (argc >= 3 && atom_gettype(argv + 2) == A_FLOAT) ? 
                              atom_getfloat(argv + 2) : DEFAULT_WARP_AMOUNT;
        
        // Clamp parameters to safe ranges
        x->frequency_float = fmax(MIN_FREQUENCY, fmin(MAX_FREQUENCY, x->frequency_float));
        x->fold_amount_float = fmax(MIN_FOLD_AMOUNT, fmin(MAX_FOLD_AMOUNT, x->fold_amount_float));
        x->warp_amount_float = fmax(MIN_WARP_AMOUNT, fmin(MAX_WARP_AMOUNT, x->warp_amount_float));
        
        // Initialize signal connection status
        x->frequency_has_signal = 0;
        x->fold_has_signal = 0;
        x->warp_has_signal = 0;
        
        // Initialize attributes with defaults
        x->folding_algorithm = 0;       // Reflection folding by default
        x->antialiasing_enabled = 1;    // Anti-aliasing enabled by default
        x->dc_blocking_enabled = 1;     // DC blocking enabled by default
        x->warp_mode = 0;               // Symmetric warping by default
        
        // Process attributes early for proper initialization
        long offset = attr_args_offset((short)argc, argv);
        attr_args_process(x, (short)argc, argv);
        
        // Initialize parameter smoothing
        x->fold_smooth = x->fold_amount_float;
        x->warp_smooth = x->warp_amount_float;
        x->smooth_factor = 0.001;  // ~10ms smoothing at 44.1kHz
        
        // Initialize DC blocker
        x->dc_block_x1 = 0.0;
        x->dc_block_y1 = 0.0;
    }
    
    return x;
}

//***********************************************************************************************

void cyclefold_free(t_cyclefold *x) {
    // Cleanup MSP resources
    dsp_free((t_pxobject *)x);
}

//***********************************************************************************************

void cyclefold_dsp64(t_cyclefold *x, t_object *dsp64, short *count, double samplerate, 
                     long maxvectorsize, long flags) {
    // Update sample rate
    x->sr = samplerate;
    x->sr_inv = 1.0 / samplerate;
    
    // Update smoothing factor based on sample rate (target ~10ms)
    x->smooth_factor = 1.0 - exp(-1.0 / (0.01 * samplerate));
    
    // Track signal connections using lores~ pattern
    x->frequency_has_signal = count[0];
    x->fold_has_signal = count[1];
    x->warp_has_signal = count[2];
    
    // Add to DSP chain
    object_method(dsp64, gensym("dsp_add64"), x, cyclefold_perform64, 0, NULL);
}

//***********************************************************************************************

void cyclefold_perform64(t_cyclefold *x, t_object *dsp64, double **ins, long numins, 
                         double **outs, long numouts, long sampleframes, long flags, void *userparam) {
    // Input and output pointers
    double *freq_in = ins[0];
    double *fold_in = ins[1];
    double *warp_in = ins[2];
    double *out = outs[0];
    
    // Local state variables for efficiency
    double phase = x->phase;
    double sr_inv = x->sr_inv;
    double sr = x->sr;
    double smooth_factor = x->smooth_factor;
    
    // Process each sample
    for (long i = 0; i < sampleframes; i++) {
        // Get parameters using lores~ pattern (signal or float)
        double frequency = x->frequency_has_signal ? freq_in[i] : x->frequency_float;
        double fold_target = x->fold_has_signal ? fold_in[i] : x->fold_amount_float;
        double warp_target = x->warp_has_signal ? warp_in[i] : x->warp_amount_float;
        
        // Clamp parameters to safe ranges
        frequency = fmax(MIN_FREQUENCY, fmin(MAX_FREQUENCY, frequency));
        fold_target = fmax(MIN_FOLD_AMOUNT, fmin(MAX_FOLD_AMOUNT, fold_target));
        warp_target = fmax(MIN_WARP_AMOUNT, fmin(MAX_WARP_AMOUNT, warp_target));
        
        // Apply parameter smoothing for click prevention
        // Float inputs get heavy smoothing, signal inputs get light smoothing
        double fold_amount, warp_amount;
        if (x->fold_has_signal) {
            // Light smoothing for signal inputs (prevents extreme jumps)
            fold_amount = smooth_param(x->fold_smooth, fold_target, smooth_factor * 0.1);
        } else {
            // Heavy smoothing for float inputs (prevents clicks)
            fold_amount = smooth_param(x->fold_smooth, fold_target, smooth_factor);
        }
        
        if (x->warp_has_signal) {
            // Light smoothing for signal inputs
            warp_amount = smooth_param(x->warp_smooth, warp_target, smooth_factor * 0.1);
        } else {
            // Heavy smoothing for float inputs
            warp_amount = smooth_param(x->warp_smooth, warp_target, smooth_factor);
        }
        
        // Update smoothed parameters (for both signal and float inputs)
        x->fold_smooth = fold_amount;
        x->warp_smooth = warp_amount;
        
        // Apply phase warping with configurable mode
        double warped_phase = warp_phase_improved(phase, warp_amount, x->warp_mode);
        
        // Generate sine wave
        double sine_wave = sin(warped_phase * TWOPI);
        
        // Apply wave folding with configurable algorithm and anti-aliasing
        double folded_wave = fold_wave_improved(sine_wave, fold_amount, frequency, sr, x);
        
        // Apply DC blocking if enabled
        double output = x->dc_blocking_enabled ? dc_block(x, folded_wave) : folded_wave;
        
        // Store output
        out[i] = output;
        
        // Update phase accumulator
        phase += frequency * sr_inv;
        
        // Wrap phase and apply denormal protection
        if (phase >= 1.0) phase -= 1.0;
        if (phase < 0.0) phase += 1.0;
        if (fabs(phase) < DENORMAL_THRESHOLD) phase = 0.0;
    }
    
    // Store phase back to object
    x->phase = phase;
}

//***********************************************************************************************

void cyclefold_float(t_cyclefold *x, double f) {
    // Route float messages to appropriate inlet using proxy_getinlet
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 0:  // Frequency inlet
            x->frequency_float = fmax(MIN_FREQUENCY, fmin(MAX_FREQUENCY, f));
            break;
        case 1:  // Fold amount inlet
            x->fold_amount_float = fmax(MIN_FOLD_AMOUNT, fmin(MAX_FOLD_AMOUNT, f));
            break;
        case 2:  // Warp amount inlet
            x->warp_amount_float = fmax(MIN_WARP_AMOUNT, fmin(MAX_WARP_AMOUNT, f));
            break;
    }
}

//***********************************************************************************************

void cyclefold_bang(t_cyclefold *x) {
    // Reset phase to 0 for sync functionality
    x->phase = 0.0;
}

//***********************************************************************************************

void cyclefold_assist(t_cyclefold *x, void *b, long m, long a, char *s) {
    if (m == ASSIST_INLET) {
        switch (a) {
            case 0:
                sprintf(s, "(signal/float) Frequency (Hz), bang to reset phase");
                break;
            case 1:
                sprintf(s, "(signal/float) Fold amount (0.0-1.0)");
                break;
            case 2:
                sprintf(s, "(signal/float) Warp amount (-1.0-1.0)");
                break;
        }
    } else if (m == ASSIST_OUTLET) {
        sprintf(s, "(signal) Folded and warped sine wave output");
    }
}

//***********************************************************************************************
// Mathematical utility functions
//***********************************************************************************************

double warp_phase_improved(double phase, double warp_amount, long warp_mode) {
    // Enhanced phase warping with musical exponential curves
    if (fabs(warp_amount) < 0.0001) return phase;  // Tighter threshold
    
    if (warp_mode == 0) {
        // Symmetric mode (original algorithm)
        if (warp_amount > 0) {
            // Exponential warping: squish to right with musical scaling
            double curve = 1.0 + warp_amount * 3.0;  // 1-4 range for musical control
            return pow(phase, 1.0 / curve);
        } else {
            // Inverse exponential warping: squish to left
            double curve = 1.0 + (-warp_amount) * 3.0;
            return 1.0 - pow(1.0 - phase, 1.0 / curve);
        }
    } else {
        // Asymmetric mode - different algorithm for more extreme warping
        double curve = 1.0 + fabs(warp_amount) * 5.0;  // Extended range for asymmetric
        if (warp_amount > 0) {
            return pow(phase, curve);  // More extreme right warping
        } else {
            return 1.0 - pow(1.0 - phase, curve);  // More extreme left warping
        }
    }
}

//***********************************************************************************************

double fold_wave_improved(double input, double fold_amount, double frequency, double sr, t_cyclefold *x) {
    // Calculate anti-aliasing protection if enabled
    double safe_fold_amount = fmax(0.0, fold_amount);  // Ensure non-negative
    
    if (x->antialiasing_enabled && frequency > 20.0) {
        double nyquist = sr * 0.5;
        double max_harmonics = nyquist / frequency;
        double safe_fold_limit = fmin(1.0, max_harmonics / 10.0);
        safe_fold_amount = fmin(safe_fold_amount, safe_fold_limit);
    }
    
    // Apply folding algorithm based on attribute setting
    if (x->folding_algorithm == 0) {
        // Reflection-based folding (original algorithm)
        double threshold = 1.0 - safe_fold_amount * 0.99;
        if (threshold < 0.01) threshold = 0.01;
        
        double output = input;
        while (output > threshold || output < -threshold) {
            if (output > threshold) {
                output = 2.0 * threshold - output;
            } else if (output < -threshold) {
                output = -2.0 * threshold - output;
            }
        }
        return output;
        
    } else if (x->folding_algorithm == 1) {
        // Tanh-based soft folding
        if (safe_fold_amount <= 0.0) return input;
        double drive = 1.0 + safe_fold_amount * 8.0;
        return tanh(input * drive) / tanh(drive);
        
    } else {
        // Hybrid: reflection + tanh blend
        double threshold = 1.0 - safe_fold_amount * 0.5;  // Less aggressive threshold
        double reflected = input;
        
        // Light reflection
        if (reflected > threshold) {
            reflected = threshold + (reflected - threshold) * 0.5;
        } else if (reflected < -threshold) {
            reflected = -threshold + (reflected + threshold) * 0.5;
        }
        
        // Apply soft saturation
        double drive = 1.0 + safe_fold_amount * 4.0;
        double soft = tanh(reflected * drive) / tanh(drive);
        
        // Blend between reflection and tanh
        return reflected * (1.0 - safe_fold_amount * 0.5) + soft * (safe_fold_amount * 0.5);
    }
}

//***********************************************************************************************

double dc_block(t_cyclefold *x, double input) {
    // High-pass filter to remove DC offset from folded signal
    // Transfer function: H(z) = (1 - z^-1) / (1 - 0.995 * z^-1)
    double output = input - x->dc_block_x1 + 0.995 * x->dc_block_y1;
    x->dc_block_x1 = input;
    x->dc_block_y1 = output;
    return output;
}

//***********************************************************************************************

double smooth_param(double current, double target, double smooth_factor) {
    // Exponential smoothing for parameter changes (prevents clicks)
    return current + smooth_factor * (target - current);
}

//***********************************************************************************************
// Attribute Setters
//***********************************************************************************************

t_max_err cyclefold_attr_setfolding(t_cyclefold *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        long algorithm = atom_getlong(argv);
        x->folding_algorithm = fmax(0, fmin(2, algorithm));  // Clamp to 0-2 range
    }
    return 0;
}

t_max_err cyclefold_attr_setantialiasing(t_cyclefold *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->antialiasing_enabled = atom_getlong(argv) ? 1 : 0;
    }
    return 0;
}

t_max_err cyclefold_attr_setdcblocking(t_cyclefold *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->dc_blocking_enabled = atom_getlong(argv) ? 1 : 0;
        
        // Reset DC blocking filter state when toggling
        x->dc_block_x1 = 0.0;
        x->dc_block_y1 = 0.0;
    }
    return 0;
}

t_max_err cyclefold_attr_setwarpmode(t_cyclefold *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->warp_mode = atom_getlong(argv) ? 1 : 0;
    }
    return 0;
}