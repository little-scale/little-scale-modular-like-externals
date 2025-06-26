/**
 * modvca~ - Make Noise MODDEMIX-Style VCA for Max/MSP
 * 
 * This external emulates the unique VCA section from the Make Noise MODDEMIX module,
 * featuring amplitude-dependent distortion where low levels exhibit more harmonic
 * content and high levels remain clean. This creates musical "tail character" where
 * sine wave tails develop pleasing harmonic coloration.
 * 
 * Features:
 * - Linear VCA response curve (direct amplitude control)
 * - Smooth linear distortion-to-amplitude relationship 
 * - Single-stage tanh saturation with linear drive interpolation
 * - lores~ pattern: dual signal/float inputs for audio and CV
 * - Sample-accurate parameter modulation at audio rate
 * - Unity gain passthrough at full CV levels
 * - Denormal protection and stability safeguards
 * 
 * Technical Details:
 * - Linear amplitude calculation: amplitude = cv_level (0-1 direct mapping)
 * - Linear drive interpolation: drive = max_drive * (1 - amplitude) + min_drive
 * - Smooth saturation transition from clean highs to rich harmonic lows
 * - Drive compensation maintains consistent output levels across amplitude range
 * - No hard thresholds or discontinuities in saturation behavior
 * 
 * Amplitude-Dependent Behavior:
 * - CV = 1.0, High amplitude: Clean, minimal saturation (drive ≈ 0.1)
 * - CV = 0.5, Medium amplitude: Moderate saturation (drive ≈ 4.0) 
 * - CV = 0.1, Low amplitude: Rich harmonic saturation (drive ≈ 7.3)
 * - CV = 0.0, Zero amplitude: Maximum saturation (drive = 8.0)
 * 
 * Inlets:
 *   1. Audio input (signal) - audio signal to be processed through VCA
 *   2. CV/Level (signal/float, 0.0-1.0) - linear amplitude control (0=closed, 1=open)
 * 
 * Outlets:
 *   1. VCA output (signal, variable level) - processed audio with amplitude-dependent character
 */

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include <math.h>

#define PI 3.14159265358979323846
#define DENORMAL_THRESHOLD 1e-15

// VCA modeling constants
#define MAX_SATURATION_DRIVE 8.0    // Maximum drive at zero amplitude (heaviest saturation)
#define MIN_SATURATION_DRIVE 0.1    // Minimum drive at full amplitude (cleanest signal)
#define OUTPUT_COMPENSATION 1.0     // Output level compensation (unity gain at full level)

typedef struct _moddemix_vca {
    t_pxobject ob;              // MSP object header
    
    // Parameter storage (lores~ pattern)
    double level_float;         // CV/level when no signal connected
    
    // Signal connection status (lores~ pattern)
    short level_has_signal;     // 1 if level inlet has signal connection
    
    // VCA state
    double envelope_follower;   // Smoothed amplitude measurement
    double previous_output;     // For DC blocking
    
    // Sample rate
    double sr;                  // Sample rate
    double sr_inv;              // 1.0 / sample rate
    
    // Attribute parameters
    long saturation_mode;       // 0=linear drive, 1=exponential drive, 2=asymmetric (default 0)
    double character_amount;    // 0.0-2.0 saturation intensity scaling (default 1.0)
    long response_curve;        // 0=linear, 1=exponential, 2=logarithmic (default 0)
    double warmth_factor;       // 0.0-1.0 additional harmonic warmth (default 0.0)
    
} t_moddemix_vca;

// Function prototypes
void *moddemix_vca_new(t_symbol *s, long argc, t_atom *argv);
void moddemix_vca_free(t_moddemix_vca *x);
void moddemix_vca_dsp64(t_moddemix_vca *x, t_object *dsp64, short *count, double samplerate, 
                        long maxvectorsize, long flags);
void moddemix_vca_perform64(t_moddemix_vca *x, t_object *dsp64, double **ins, long numins, 
                            double **outs, long numouts, long sampleframes, long flags, void *userparam);
void moddemix_vca_float(t_moddemix_vca *x, double f);
void moddemix_vca_int(t_moddemix_vca *x, long n);
void moddemix_vca_assist(t_moddemix_vca *x, void *b, long m, long a, char *s);

// VCA processing functions
double moddemix_vca_process_sample(t_moddemix_vca *x, double input, double level);
double exponential_vca_curve(double level);
double amplitude_dependent_distortion(double input, double amplitude);
double amplitude_dependent_distortion_enhanced(t_moddemix_vca *x, double input, double amplitude);
double harmonic_saturation(double input, double drive, int stage);
double denormal_fix(double value);

// Attribute setter prototypes
t_max_err modvca_attr_setsaturationmode(t_moddemix_vca *x, void *attr, long argc, t_atom *argv);
t_max_err modvca_attr_setcharacteramount(t_moddemix_vca *x, void *attr, long argc, t_atom *argv);
t_max_err modvca_attr_setresponsecurve(t_moddemix_vca *x, void *attr, long argc, t_atom *argv);
t_max_err modvca_attr_setwarmthfactor(t_moddemix_vca *x, void *attr, long argc, t_atom *argv);

// Class pointer
static t_class *moddemix_vca_class = NULL;

//----------------------------------------------------------------------------------------------

void ext_main(void *r) {
    t_class *c;
    
    c = class_new("modvca~", (method)moddemix_vca_new, (method)moddemix_vca_free,
                  sizeof(t_moddemix_vca), 0L, A_GIMME, 0);
    
    class_addmethod(c, (method)moddemix_vca_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)moddemix_vca_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)moddemix_vca_float, "float", A_FLOAT, 0);
    class_addmethod(c, (method)moddemix_vca_int, "int", A_LONG, 0);
    
    // Attributes with Max 9 autocomplete support
    CLASS_ATTR_LONG(c, "saturation_mode", 0, t_moddemix_vca, saturation_mode);
    CLASS_ATTR_BASIC(c, "saturation_mode", 0);
    CLASS_ATTR_LABEL(c, "saturation_mode", 0, "Saturation Mode");
    CLASS_ATTR_ACCESSORS(c, "saturation_mode", 0, modvca_attr_setsaturationmode);
    CLASS_ATTR_ORDER(c, "saturation_mode", 0, "1");
    CLASS_ATTR_FILTER_CLIP(c, "saturation_mode", 0, 2);
    CLASS_ATTR_DEFAULT_SAVE(c, "saturation_mode", 0, "0");
    CLASS_ATTR_STYLE(c, "saturation_mode", 0, "enumindex");
    
    CLASS_ATTR_DOUBLE(c, "character_amount", 0, t_moddemix_vca, character_amount);
    CLASS_ATTR_BASIC(c, "character_amount", 0);
    CLASS_ATTR_LABEL(c, "character_amount", 0, "Character Amount");
    CLASS_ATTR_ACCESSORS(c, "character_amount", 0, modvca_attr_setcharacteramount);
    CLASS_ATTR_ORDER(c, "character_amount", 0, "2");
    CLASS_ATTR_FILTER_CLIP(c, "character_amount", 0.0, 2.0);
    CLASS_ATTR_DEFAULT_SAVE(c, "character_amount", 0, "1.0");
    CLASS_ATTR_STYLE(c, "character_amount", 0, "text");
    
    CLASS_ATTR_LONG(c, "response_curve", 0, t_moddemix_vca, response_curve);
    CLASS_ATTR_BASIC(c, "response_curve", 0);
    CLASS_ATTR_LABEL(c, "response_curve", 0, "Response Curve");
    CLASS_ATTR_ACCESSORS(c, "response_curve", 0, modvca_attr_setresponsecurve);
    CLASS_ATTR_ORDER(c, "response_curve", 0, "3");
    CLASS_ATTR_FILTER_CLIP(c, "response_curve", 0, 2);
    CLASS_ATTR_DEFAULT_SAVE(c, "response_curve", 0, "0");
    CLASS_ATTR_STYLE(c, "response_curve", 0, "enumindex");
    
    CLASS_ATTR_DOUBLE(c, "warmth_factor", 0, t_moddemix_vca, warmth_factor);
    CLASS_ATTR_BASIC(c, "warmth_factor", 0);
    CLASS_ATTR_LABEL(c, "warmth_factor", 0, "Warmth Factor");
    CLASS_ATTR_ACCESSORS(c, "warmth_factor", 0, modvca_attr_setwarmthfactor);
    CLASS_ATTR_ORDER(c, "warmth_factor", 0, "4");
    CLASS_ATTR_FILTER_CLIP(c, "warmth_factor", 0.0, 1.0);
    CLASS_ATTR_DEFAULT_SAVE(c, "warmth_factor", 0, "0.0");
    CLASS_ATTR_STYLE(c, "warmth_factor", 0, "text");
    
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    moddemix_vca_class = c;
}

//----------------------------------------------------------------------------------------------

void *moddemix_vca_new(t_symbol *s, long argc, t_atom *argv) {
    t_moddemix_vca *x = (t_moddemix_vca *)object_alloc(moddemix_vca_class);
    
    if (x) {
        // lores~ pattern: 2 signal inlets (audio, level/CV)
        dsp_setup((t_pxobject *)x, 2);
        outlet_new(x, "signal");
        
        // Initialize sample rate
        x->sr = sys_getsr();
        x->sr_inv = 1.0 / x->sr;
        
        // Initialize parameter defaults
        x->level_float = 0.0;          // VCA closed by default
        
        // Initialize connection status (assume no signals connected initially)
        x->level_has_signal = 0;
        
        // Initialize attributes with defaults
        x->saturation_mode = 0;     // Linear drive mode
        x->character_amount = 1.0;  // Normal character intensity
        x->response_curve = 0;      // Linear VCA response
        x->warmth_factor = 0.0;     // No additional warmth
        
        // Initialize VCA state
        x->envelope_follower = 0.0;
        x->previous_output = 0.0;
        
        // Process creation arguments if any
        if (argc >= 1 && (atom_gettype(argv) == A_FLOAT || atom_gettype(argv) == A_LONG)) {
            x->level_float = CLAMP(atom_getfloat(argv), 0.0, 1.0);
        }
        
        // Process attributes early for proper initialization
        long offset = attr_args_offset((short)argc, argv);
        attr_args_process(x, argc, argv);
    }
    
    return x;
}

//----------------------------------------------------------------------------------------------

void moddemix_vca_free(t_moddemix_vca *x) {
    dsp_free((t_pxobject *)x);
}

//----------------------------------------------------------------------------------------------

void moddemix_vca_dsp64(t_moddemix_vca *x, t_object *dsp64, short *count, double samplerate, 
                        long maxvectorsize, long flags) {
    x->sr = samplerate;
    x->sr_inv = 1.0 / samplerate;
    
    // lores~ pattern: store signal connection status
    x->level_has_signal = count[1];    // Inlet 1 is level/CV
    
    object_method(dsp64, gensym("dsp_add64"), x, moddemix_vca_perform64, 0, NULL);
}

//----------------------------------------------------------------------------------------------

void moddemix_vca_perform64(t_moddemix_vca *x, t_object *dsp64, double **ins, long numins, 
                            double **outs, long numouts, long sampleframes, long flags, void *userparam) {
    // Input buffers
    double *audio_in = ins[0];      // Audio input
    double *level_in = ins[1];      // Level/CV input
    
    // Output buffer
    double *out = outs[0];
    
    long n = sampleframes;
    
    while (n--) {
        // lores~ pattern: choose signal vs float for level parameter
        double audio = *audio_in++;
        double level = x->level_has_signal ? *level_in++ : x->level_float;
        
        // Clamp level to valid range (0.0 = closed, 1.0 = fully open)
        level = CLAMP(level, 0.0, 1.0);
        
        // Process sample through MODDEMIX-style VCA
        double vca_output = moddemix_vca_process_sample(x, audio, level);
        
        // Apply denormal fix and output
        *out++ = denormal_fix(vca_output);
    }
}

//----------------------------------------------------------------------------------------------

double moddemix_vca_process_sample(t_moddemix_vca *x, double input, double level) {
    // Apply response curve to level based on attribute
    double vca_amplitude;
    switch (x->response_curve) {
        case 1: // Exponential
            vca_amplitude = level * level * level * level;  // x^4 curve
            break;
        case 2: // Logarithmic  
            vca_amplitude = level > 0.0 ? sqrt(sqrt(level)) : 0.0;  // x^0.25 curve
            break;
        default: // Linear
            vca_amplitude = level;
            break;
    }
    
    // Apply basic VCA operation
    double vca_output = input * vca_amplitude;
    
    // Calculate amplitude-dependent distortion with character scaling
    double processed_output = amplitude_dependent_distortion_enhanced(x, vca_output, vca_amplitude);
    
    // Add warmth factor if enabled
    if (x->warmth_factor > 0.0) {
        double warmth = x->warmth_factor * 0.15 * tanh(processed_output * 1.5);
        processed_output = (1.0 - x->warmth_factor) * processed_output + x->warmth_factor * (processed_output + warmth);
    }
    
    // Apply output compensation to maintain consistent levels
    processed_output *= OUTPUT_COMPENSATION;
    
    // Update envelope follower for amplitude tracking (with smoothing)
    double envelope_coeff = 0.99;  // Smooth envelope following
    x->envelope_follower = x->envelope_follower * envelope_coeff + 
                          fabs(processed_output) * (1.0 - envelope_coeff);
    
    return processed_output;
}

//----------------------------------------------------------------------------------------------

double exponential_vca_curve(double level) {
    // Linear VCA response curve for predictable amplitude control
    // This creates a direct 1:1 relationship between CV and amplitude
    // NOTE: Function name kept for compatibility, but now implements linear response
    
    if (level <= 0.0) return 0.0;
    
    // Linear curve: amplitude = level (direct relationship)
    // CV 0.0 = 0% amplitude, CV 1.0 = 100% amplitude
    // This ensures predictable saturation behavior throughout amplitude range
    double amplitude = level;
    
    return amplitude;
}

//----------------------------------------------------------------------------------------------

double amplitude_dependent_distortion(double input, double amplitude) {
    // The key characteristic: distortion amount is inversely related to amplitude
    // Low amplitude = high distortion (rich harmonics in tails)
    // High amplitude = low distortion (clean loud signals)
    
    if (amplitude <= 0.0) return 0.0;
    
    // Linear amplitude-dependent drive approach:
    // Calculate drive amount that decreases linearly with amplitude
    // Drive = max_drive * (1 - amplitude) + min_drive
    // This creates smooth transition: high amplitude = clean, low amplitude = saturated
    
    // Linear interpolation: high amplitude = low drive, low amplitude = high drive  
    double drive = MAX_SATURATION_DRIVE * (1.0 - amplitude) + MIN_SATURATION_DRIVE;
    
    // Apply saturation with calculated drive amount
    double driven_signal = input * drive;
    double saturated = tanh(driven_signal);
    
    // Compensate for drive to maintain consistent output level
    double output = saturated / drive;
    
    return output;
}

//----------------------------------------------------------------------------------------------

double amplitude_dependent_distortion_enhanced(t_moddemix_vca *x, double input, double amplitude) {
    // Enhanced version with attribute-based control
    
    if (amplitude <= 0.0) return 0.0;
    
    // Scale drive based on character_amount attribute
    double max_drive = MAX_SATURATION_DRIVE * x->character_amount;
    double min_drive = MIN_SATURATION_DRIVE;
    
    double drive;
    
    // Calculate drive based on saturation_mode attribute
    switch (x->saturation_mode) {
        case 1: // Exponential drive
            {
                double inv_amp = 1.0 - amplitude;
                drive = max_drive * (inv_amp * inv_amp) + min_drive;
            }
            break;
        case 2: // Asymmetric drive
            {
                double inv_amp = 1.0 - amplitude;
                if (input > 0.0) {
                    drive = max_drive * inv_amp + min_drive;
                } else {
                    drive = max_drive * inv_amp * 0.7 + min_drive;  // Less saturation on negative
                }
            }
            break;
        default: // Linear drive (original behavior)
            drive = max_drive * (1.0 - amplitude) + min_drive;
            break;
    }
    
    // Apply saturation with calculated drive amount
    double driven_signal = input * drive;
    double saturated = tanh(driven_signal);
    
    // Compensate for drive to maintain consistent output level
    double output = saturated / drive;
    
    return output;
}

//----------------------------------------------------------------------------------------------

double harmonic_saturation(double input, double drive, int stage) {
    // Multi-stage harmonic saturation to create complex harmonic content
    // Each stage contributes different harmonic characteristics
    
    if (drive <= 0.0) return input;
    
    double driven = input * drive;
    double saturated;
    
    switch (stage) {
        case 1: // Primary saturation - odd harmonics
            saturated = tanh(driven);
            break;
            
        case 2: // Even harmonic generation
            saturated = tanh(driven * 0.7);
            saturated += 0.15 * tanh(driven * 0.5) * tanh(driven * 0.5);  // Even harmonics
            break;
            
        case 3: // Higher-order complexity
            saturated = tanh(driven * 0.5);
            saturated += 0.08 * sin(driven * 2.0) * tanh(driven);  // Higher-order content
            break;
            
        default:
            saturated = tanh(driven);
            break;
    }
    
    // Compensate for drive level to maintain consistent output scaling
    return saturated / (1.0 + drive * 0.2);
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

void moddemix_vca_float(t_moddemix_vca *x, double f) {
    // lores~ pattern: proxy_getinlet works on signal inlets for float routing
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 1: // Level/CV inlet
            x->level_float = CLAMP(f, 0.0, 1.0);
            break;
    }
}

//----------------------------------------------------------------------------------------------

void moddemix_vca_int(t_moddemix_vca *x, long n) {
    // lores~ pattern: proxy_getinlet works on signal inlets for int routing
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 1: // Level/CV inlet - convert int to float
            x->level_float = CLAMP((double)n, 0.0, 1.0);
            break;
    }
}

//----------------------------------------------------------------------------------------------

void moddemix_vca_assist(t_moddemix_vca *x, void *b, long m, long a, char *s) {
    if (m == ASSIST_INLET) {
        switch (a) {
            case 0:
                sprintf(s, "(signal) Audio input");
                break;
            case 1:
                sprintf(s, "(signal/float) Level/CV (0-1, 0=closed, 1=open)");
                break;
        }
    } else {  // ASSIST_OUTLET
        sprintf(s, "(signal) VCA output - MODDEMIX-style amplitude-dependent character");
    }
}

//----------------------------------------------------------------------------------------------
// Attribute Setters
//----------------------------------------------------------------------------------------------

t_max_err modvca_attr_setsaturationmode(t_moddemix_vca *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->saturation_mode = CLAMP(atom_getlong(argv), 0, 2);
    }
    return 0;
}

t_max_err modvca_attr_setcharacteramount(t_moddemix_vca *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->character_amount = CLAMP(atom_getfloat(argv), 0.0, 2.0);
    }
    return 0;
}

t_max_err modvca_attr_setresponsecurve(t_moddemix_vca *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->response_curve = CLAMP(atom_getlong(argv), 0, 2);
    }
    return 0;
}

t_max_err modvca_attr_setwarmthfactor(t_moddemix_vca *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->warmth_factor = CLAMP(atom_getfloat(argv), 0.0, 1.0);
    }
    return 0;
}