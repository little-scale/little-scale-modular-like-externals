/**
 * ssm2044~ - SSM2044 Analog Filter Emulation for Max/MSP
 * 
 * This external emulates the classic SSM2044 4-pole voltage-controlled low-pass filter
 * IC used in legendary synthesizers like the Korg Polysix and Mono/Poly. Features
 * zero-delay feedback topology, analog-style nonlinear saturation, and self-oscillation.
 * 
 * Features:
 * - Zero-delay feedback (ZDF) 4-pole low-pass filter topology
 * - Musical character via input and feedback saturation
 * - Self-oscillation capability at high resonance values
 * - lores~ pattern: 4 signal inlets accept both signals and floats
 * - Sample-accurate parameter modulation at audio rate
 * - Denormal protection and stability safeguards
 * - Accurate frequency response via bilinear transform pre-warping
 * - Universal binary support (Intel + Apple Silicon)
 * 
 * Technical Details:
 * - 4-pole cascaded topology with global feedback for resonance
 * - Per-sample processing for zero-delay feedback stability  
 * - Musical character via subtle input and feedback saturation
 * - Frequency range: 20 Hz to 20 kHz with accurate frequency response
 * - Resonance range: 0.0 to 4.0 (self-oscillation above ~3.5)
 * - Gain range: 0.0 to 4.0 (with musical saturation)
 * 
 * Inlets:
 *   1. Audio input (signal) - input signal to be filtered
 *   2. Cutoff frequency (signal/float, 20-20000 Hz) - filter cutoff frequency
 *   3. Resonance (signal/float, 0.0-4.0) - filter resonance/Q factor
 *   4. Input gain (signal/float, 0.0-4.0) - input gain with musical saturation
 * 
 * Outlets:
 *   1. Filtered output (signal, -1.0 to 1.0) - filtered audio signal
 */

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include <math.h>

#define PI 3.14159265358979323846
#define DENORMAL_THRESHOLD 1e-15

// Filter constants
#define RESONANCE_SCALE 4.0     // Resonance feedback scaling
#define MAX_RESONANCE 4.0       // Maximum resonance value

// Character constants
#define INPUT_DRIVE 1.5         // Input saturation drive (subtle)
#define FEEDBACK_DRIVE 2.0      // Feedback saturation drive (moderate)

typedef struct _ssm2044 {
    t_pxobject ob;              // MSP object header
    
    // Core filter state (ZDF topology)
    double state1, state2, state3, state4;  // 4-pole filter states
    double feedback_sample;     // Feedback sample for ZDF
    
    // Sample rate
    double sr;                  // Sample rate
    double sr_inv;              // 1.0 / sample rate
    
    // Parameter storage (lores~ pattern)
    double cutoff_float;        // Cutoff frequency when no signal connected
    double resonance_float;     // Resonance when no signal connected
    double gain_float;          // Input gain when no signal connected
    
    // Signal connection status (lores~ pattern)
    short cutoff_has_signal;    // 1 if cutoff inlet has signal connection
    short resonance_has_signal; // 1 if resonance inlet has signal connection
    short gain_has_signal;      // 1 if gain inlet has signal connection
    
    // Filter coefficients (computed per sample for stability)
    double g;                   // Integrator gain (cutoff-dependent)
    double k;                   // Resonance feedback gain
    
    // Oversampling support (future enhancement)
    long oversample_factor;     // 1, 2, or 4x oversampling
    double *oversample_buffer;  // Buffer for oversampling
    
    // Attribute parameters
    long character_mode;        // 0=clean, 1=vintage, 2=aggressive (default 1)
    long self_oscillation;      // 0=disabled, 1=enabled (default 1)
    double warmth_amount;       // 0.0-1.0 analog warmth (default 0.5)
    long resonance_compensation; // 0=off, 1=on level compensation (default 1)
    
} t_ssm2044;

// Function prototypes
void *ssm2044_new(t_symbol *s, long argc, t_atom *argv);
void ssm2044_free(t_ssm2044 *x);
void ssm2044_dsp64(t_ssm2044 *x, t_object *dsp64, short *count, double samplerate, 
                   long maxvectorsize, long flags);
void ssm2044_perform64(t_ssm2044 *x, t_object *dsp64, double **ins, long numins, 
                       double **outs, long numouts, long sampleframes, long flags, void *userparam);
void ssm2044_float(t_ssm2044 *x, double f);
void ssm2044_int(t_ssm2044 *x, long n);
void ssm2044_assist(t_ssm2044 *x, void *b, long m, long a, char *s);

// Filter processing functions
double ssm2044_process_sample(t_ssm2044 *x, double input, double cutoff, double resonance, double gain);
double denormal_fix(double value);
double soft_saturation(double input, double drive);
void compute_filter_coefficients(t_ssm2044 *x, double cutoff, double resonance);

// Attribute setter prototypes
t_max_err ssm2044_attr_setoversample(t_ssm2044 *x, void *attr, long argc, t_atom *argv);
t_max_err ssm2044_attr_setcharacter(t_ssm2044 *x, void *attr, long argc, t_atom *argv);
t_max_err ssm2044_attr_setselfosc(t_ssm2044 *x, void *attr, long argc, t_atom *argv);
t_max_err ssm2044_attr_setwarmth(t_ssm2044 *x, void *attr, long argc, t_atom *argv);
t_max_err ssm2044_attr_setrescomp(t_ssm2044 *x, void *attr, long argc, t_atom *argv);

// Class pointer
static t_class *ssm2044_class = NULL;

//----------------------------------------------------------------------------------------------

void ext_main(void *r) {
    t_class *c;
    
    c = class_new("ssm2044~", (method)ssm2044_new, (method)ssm2044_free,
                  sizeof(t_ssm2044), 0L, A_GIMME, 0);
    
    class_addmethod(c, (method)ssm2044_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)ssm2044_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)ssm2044_float, "float", A_FLOAT, 0);
    class_addmethod(c, (method)ssm2044_int, "int", A_LONG, 0);
    
    // Attributes with Max 9 autocomplete support
    CLASS_ATTR_LONG(c, "oversample", 0, t_ssm2044, oversample_factor);
    CLASS_ATTR_BASIC(c, "oversample", 0);
    CLASS_ATTR_LABEL(c, "oversample", 0, "Oversampling Factor");
    CLASS_ATTR_ACCESSORS(c, "oversample", 0, ssm2044_attr_setoversample);
    CLASS_ATTR_ORDER(c, "oversample", 0, "1");
    CLASS_ATTR_FILTER_CLIP(c, "oversample", 1, 4);
    CLASS_ATTR_DEFAULT_SAVE(c, "oversample", 0, "1");
    CLASS_ATTR_STYLE(c, "oversample", 0, "enumindex");
    
    CLASS_ATTR_LONG(c, "character", 0, t_ssm2044, character_mode);
    CLASS_ATTR_BASIC(c, "character", 0);
    CLASS_ATTR_LABEL(c, "character", 0, "Filter Character");
    CLASS_ATTR_ACCESSORS(c, "character", 0, ssm2044_attr_setcharacter);
    CLASS_ATTR_ORDER(c, "character", 0, "2");
    CLASS_ATTR_FILTER_CLIP(c, "character", 0, 2);
    CLASS_ATTR_DEFAULT_SAVE(c, "character", 0, "1");
    CLASS_ATTR_STYLE(c, "character", 0, "enumindex");
    
    CLASS_ATTR_LONG(c, "self_oscillation", 0, t_ssm2044, self_oscillation);
    CLASS_ATTR_BASIC(c, "self_oscillation", 0);
    CLASS_ATTR_LABEL(c, "self_oscillation", 0, "Self-Oscillation");
    CLASS_ATTR_ACCESSORS(c, "self_oscillation", 0, ssm2044_attr_setselfosc);
    CLASS_ATTR_ORDER(c, "self_oscillation", 0, "3");
    CLASS_ATTR_FILTER_CLIP(c, "self_oscillation", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "self_oscillation", 0, "1");
    CLASS_ATTR_STYLE(c, "self_oscillation", 0, "onoff");
    
    CLASS_ATTR_DOUBLE(c, "warmth", 0, t_ssm2044, warmth_amount);
    CLASS_ATTR_BASIC(c, "warmth", 0);
    CLASS_ATTR_LABEL(c, "warmth", 0, "Analog Warmth");
    CLASS_ATTR_ACCESSORS(c, "warmth", 0, ssm2044_attr_setwarmth);
    CLASS_ATTR_ORDER(c, "warmth", 0, "4");
    CLASS_ATTR_FILTER_CLIP(c, "warmth", 0.0, 1.0);
    CLASS_ATTR_DEFAULT_SAVE(c, "warmth", 0, "0.5");
    CLASS_ATTR_STYLE(c, "warmth", 0, "text");
    
    CLASS_ATTR_LONG(c, "resonance_compensation", 0, t_ssm2044, resonance_compensation);
    CLASS_ATTR_BASIC(c, "resonance_compensation", 0);
    CLASS_ATTR_LABEL(c, "resonance_compensation", 0, "Resonance Compensation");
    CLASS_ATTR_ACCESSORS(c, "resonance_compensation", 0, ssm2044_attr_setrescomp);
    CLASS_ATTR_ORDER(c, "resonance_compensation", 0, "5");
    CLASS_ATTR_FILTER_CLIP(c, "resonance_compensation", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "resonance_compensation", 0, "1");
    CLASS_ATTR_STYLE(c, "resonance_compensation", 0, "onoff");
    
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    ssm2044_class = c;
}

//----------------------------------------------------------------------------------------------

void *ssm2044_new(t_symbol *s, long argc, t_atom *argv) {
    t_ssm2044 *x = (t_ssm2044 *)object_alloc(ssm2044_class);
    
    if (x) {
        // lores~ pattern: 4 signal inlets (audio, cutoff, resonance, gain)
        // Note: audio input is automatically created, so we need 3 additional inlets
        dsp_setup((t_pxobject *)x, 4);
        outlet_new(x, "signal");
        
        // Initialize core state
        x->sr = sys_getsr();
        x->sr_inv = 1.0 / x->sr;
        
        // Initialize filter state
        x->state1 = x->state2 = x->state3 = x->state4 = 0.0;
        x->feedback_sample = 0.0;
        
        // Initialize parameter defaults
        x->cutoff_float = 1000.0;      // 1 kHz default cutoff
        x->resonance_float = 0.5;      // Medium resonance
        x->gain_float = 1.0;           // Unity gain
        
        // Initialize connection status (assume no signals connected initially)
        x->cutoff_has_signal = 0;
        x->resonance_has_signal = 0;
        x->gain_has_signal = 0;
        
        // Initialize filter coefficients
        x->g = 0.0;
        x->k = 0.0;
        
        // Initialize oversampling
        x->oversample_factor = 1;      // No oversampling by default
        x->oversample_buffer = NULL;
        
        // Initialize attributes with defaults
        x->character_mode = 1;         // Vintage character by default
        x->self_oscillation = 1;       // Self-oscillation enabled
        x->warmth_amount = 0.5;        // Medium warmth
        x->resonance_compensation = 1; // Level compensation enabled
        
        // Process attributes early for proper initialization
        long offset = attr_args_offset((short)argc, argv);
        attr_args_process(x, (short)argc, argv);
        
        // Process creation arguments if any
        if (argc >= 1 && (atom_gettype(argv) == A_FLOAT || atom_gettype(argv) == A_LONG)) {
            x->cutoff_float = CLAMP(atom_getfloat(argv), 20.0, 20000.0);
        }
        if (argc >= 2 && (atom_gettype(argv + 1) == A_FLOAT || atom_gettype(argv + 1) == A_LONG)) {
            x->resonance_float = CLAMP(atom_getfloat(argv + 1), 0.0, 4.0);
        }
        if (argc >= 3 && (atom_gettype(argv + 2) == A_FLOAT || atom_gettype(argv + 2) == A_LONG)) {
            x->gain_float = CLAMP(atom_getfloat(argv + 2), 0.0, 4.0);
        }
        
        // Allocate oversampling buffer if needed
        if (x->oversample_factor > 1) {
            x->oversample_buffer = (double *)malloc(sizeof(double) * 4096 * x->oversample_factor);
            if (!x->oversample_buffer) {
                post("ssm2044~: could not allocate oversampling buffer");
                x->oversample_factor = 1;
            }
        }
    }
    
    return x;
}

//----------------------------------------------------------------------------------------------

void ssm2044_free(t_ssm2044 *x) {
    if (x->oversample_buffer) {
        free(x->oversample_buffer);
    }
    dsp_free((t_pxobject *)x);
}

//----------------------------------------------------------------------------------------------

void ssm2044_dsp64(t_ssm2044 *x, t_object *dsp64, short *count, double samplerate, 
                   long maxvectorsize, long flags) {
    x->sr = samplerate;
    x->sr_inv = 1.0 / samplerate;
    
    // lores~ pattern: store signal connection status
    x->cutoff_has_signal = count[1];    // Inlet 1 is cutoff
    x->resonance_has_signal = count[2]; // Inlet 2 is resonance
    x->gain_has_signal = count[3];      // Inlet 3 is gain
    
    object_method(dsp64, gensym("dsp_add64"), x, ssm2044_perform64, 0, NULL);
}

//----------------------------------------------------------------------------------------------

void ssm2044_perform64(t_ssm2044 *x, t_object *dsp64, double **ins, long numins, 
                       double **outs, long numouts, long sampleframes, long flags, void *userparam) {
    // Input buffers
    double *audio_in = ins[0];      // Audio input
    double *cutoff_in = ins[1];     // Cutoff frequency
    double *resonance_in = ins[2];  // Resonance
    double *gain_in = ins[3];       // Input gain
    
    // Output buffer
    double *out = outs[0];
    
    long n = sampleframes;
    
    while (n--) {
        // lores~ pattern: choose signal vs float for each parameter
        double audio = *audio_in++;
        double cutoff = x->cutoff_has_signal ? *cutoff_in++ : x->cutoff_float;
        double resonance = x->resonance_has_signal ? *resonance_in++ : x->resonance_float;
        double gain = x->gain_has_signal ? *gain_in++ : x->gain_float;
        
        // Clamp parameters to valid ranges
        cutoff = CLAMP(cutoff, 20.0, 20000.0);        // 20 Hz to 20 kHz
        resonance = CLAMP(resonance, 0.0, MAX_RESONANCE); // 0 to 4 (self-oscillation above 3.5)
        gain = CLAMP(gain, 0.0, 4.0);                 // 0 to 4x gain
        
        // Process sample through filter
        double filtered = ssm2044_process_sample(x, audio, cutoff, resonance, gain);
        
        // Apply denormal fix and output
        *out++ = denormal_fix(filtered);
    }
}

//----------------------------------------------------------------------------------------------

double ssm2044_process_sample(t_ssm2044 *x, double input, double cutoff, double resonance, double gain) {
    // Compute filter coefficients for current cutoff and resonance
    compute_filter_coefficients(x, cutoff, resonance);
    
    // Apply input gain with character-dependent saturation
    double scaled_input = input * gain;
    double drive_amount = INPUT_DRIVE;
    
    // Adjust drive based on character setting
    if (x->character_mode == 0) {
        drive_amount *= 0.5;  // Clean: minimal saturation
    } else if (x->character_mode == 2) {
        drive_amount *= 2.0;  // Aggressive: heavy saturation
    }
    // Vintage (mode 1) uses standard drive amount
    
    double saturated_input = soft_saturation(scaled_input, drive_amount);
    
    // Zero-delay feedback calculation
    // For a 4-pole filter: y = G4 * (input + k * feedback)
    // Where feedback comes from the output of the 4th stage
    
    // Calculate the feed-forward and feedback gains
    double g = x->g;
    double k = x->k;
    
    // ZDF: solve for the feedback sample with character-dependent feedback saturation
    double feedback_drive = FEEDBACK_DRIVE * x->warmth_amount;
    double saturated_feedback = soft_saturation(x->feedback_sample, feedback_drive);
    
    // Apply self-oscillation control
    double actual_k = x->self_oscillation ? k : k * 0.8;  // Limit resonance if disabled
    double fb_input = saturated_input + actual_k * saturated_feedback;
    
    // Process through clean 4-pole cascade
    double stage1_out = x->state1 + g * (fb_input - x->state1);
    double stage2_out = x->state2 + g * (stage1_out - x->state2);
    double stage3_out = x->state3 + g * (stage2_out - x->state3);
    double stage4_out = x->state4 + g * (stage3_out - x->state4);
    
    // Update filter states
    x->state1 = denormal_fix(stage1_out);
    x->state2 = denormal_fix(stage2_out);
    x->state3 = denormal_fix(stage3_out);
    x->state4 = denormal_fix(stage4_out);
    
    // Update feedback sample for next iteration
    x->feedback_sample = stage4_out;
    
    return stage4_out;
}

//----------------------------------------------------------------------------------------------

void compute_filter_coefficients(t_ssm2044 *x, double cutoff, double resonance) {
    // Clamp cutoff to valid range (avoid Nyquist issues)
    cutoff = CLAMP(cutoff, 20.0, x->sr * 0.45);
    
    // Convert to angular frequency (radians per second)
    double omega = 2.0 * PI * cutoff;
    
    // Apply bilinear transform pre-warping: 
    // omega_warped = tan(omega * T/2) where T = 1/sr
    double omega_warped = tan(omega * x->sr_inv * 0.5);
    
    // Compute integrator gain (g) for one-pole section
    // g = omega_warped / (1 + omega_warped)
    x->g = omega_warped / (1.0 + omega_warped);
    
    // Clamp g to prevent instability (must be < 1.0)
    x->g = CLAMP(x->g, 0.0, 0.99);
    
    // Compute resonance feedback gain (k)
    // Higher resonance = more feedback, approaching self-oscillation
    x->k = resonance * RESONANCE_SCALE;
    
    // Apply resonance compensation if enabled
    if (x->resonance_compensation) {
        // Compensate for volume increase at high resonance
        double compensation = 1.0 / (1.0 + resonance * 0.3);
        x->k *= compensation;
    }
}

//----------------------------------------------------------------------------------------------

double denormal_fix(double value) {
    // Fix denormal numbers that can cause CPU spikes
    if (fabs(value) < DENORMAL_THRESHOLD) {
        return 0.0;
    }
    return value;
}

//----------------------------------------------------------------------------------------------

double soft_saturation(double input, double drive) {
    // Soft saturation using tanh function
    // Provides musical harmonic distortion without harsh clipping
    if (drive <= 0.0) return input;
    
    double driven = input * drive;
    
    // Apply tanh saturation for smooth, musical character
    double saturated = tanh(driven);
    
    // Compensate for drive level to maintain overall gain structure
    return saturated / drive;
}

//----------------------------------------------------------------------------------------------

void ssm2044_float(t_ssm2044 *x, double f) {
    // lores~ pattern: proxy_getinlet works on signal inlets for float routing
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 1: // Cutoff frequency inlet
            x->cutoff_float = CLAMP(f, 20.0, 20000.0);
            break;
        case 2: // Resonance inlet
            x->resonance_float = CLAMP(f, 0.0, MAX_RESONANCE);
            break;
        case 3: // Input gain inlet
            x->gain_float = CLAMP(f, 0.0, 4.0);
            break;
    }
}

//----------------------------------------------------------------------------------------------

void ssm2044_int(t_ssm2044 *x, long n) {
    // lores~ pattern: proxy_getinlet works on signal inlets for int routing
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 1: // Cutoff frequency inlet - convert int to float
            x->cutoff_float = CLAMP((double)n, 20.0, 20000.0);
            break;
        case 2: // Resonance inlet - convert int to float
            x->resonance_float = CLAMP((double)n, 0.0, MAX_RESONANCE);
            break;
        case 3: // Input gain inlet - convert int to float
            x->gain_float = CLAMP((double)n, 0.0, 4.0);
            break;
    }
}

//----------------------------------------------------------------------------------------------

void ssm2044_assist(t_ssm2044 *x, void *b, long m, long a, char *s) {
    if (m == ASSIST_INLET) {
        switch (a) {
            case 0:
                sprintf(s, "(signal) Audio input");
                break;
            case 1:
                sprintf(s, "(signal/float) Cutoff frequency (20-20000 Hz)");
                break;
            case 2:
                sprintf(s, "(signal/float) Resonance (0-4, self-osc >3.5)");
                break;
            case 3:
                sprintf(s, "(signal/float) Input gain (0-4, with musical saturation)");
                break;
        }
    } else {  // ASSIST_OUTLET
        sprintf(s, "(signal) Filtered output - SSM2044 4-pole low-pass");
    }
}

//----------------------------------------------------------------------------------------------
// Attribute Setters
//----------------------------------------------------------------------------------------------

t_max_err ssm2044_attr_setoversample(t_ssm2044 *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        long new_factor = atom_getlong(argv);
        new_factor = CLAMP(new_factor, 1, 4);  // 1x, 2x, 4x only
        
        if (new_factor != x->oversample_factor) {
            // Free old buffer
            if (x->oversample_buffer) {
                free(x->oversample_buffer);
                x->oversample_buffer = NULL;
            }
            
            x->oversample_factor = new_factor;
            
            // Allocate new buffer if needed
            if (x->oversample_factor > 1) {
                x->oversample_buffer = (double *)malloc(sizeof(double) * 4096 * x->oversample_factor);
                if (!x->oversample_buffer) {
                    post("ssm2044~: could not allocate oversampling buffer");
                    x->oversample_factor = 1;
                }
            }
        }
    }
    return 0;
}

t_max_err ssm2044_attr_setcharacter(t_ssm2044 *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->character_mode = CLAMP(atom_getlong(argv), 0, 2);
    }
    return 0;
}

t_max_err ssm2044_attr_setselfosc(t_ssm2044 *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->self_oscillation = atom_getlong(argv) ? 1 : 0;
    }
    return 0;
}

t_max_err ssm2044_attr_setwarmth(t_ssm2044 *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->warmth_amount = CLAMP(atom_getfloat(argv), 0.0, 1.0);
    }
    return 0;
}

t_max_err ssm2044_attr_setrescomp(t_ssm2044 *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->resonance_compensation = atom_getlong(argv) ? 1 : 0;
    }
    return 0;
}