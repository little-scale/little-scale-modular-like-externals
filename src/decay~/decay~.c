/**
 * decay~ - Exponential Decay Envelope Generator for Max/MSP
 * 
 * A signal-rate exponential decay envelope generator with customizable curve shaping.
 * Perfect for drum envelopes, pluck synthesis, and any application requiring
 * exponential decay behavior with precise control over curve characteristics.
 * 
 * Features:
 * - Bang-triggered exponential decay from peak to zero
 * - Adjustable decay time in seconds (0.001 to 60.0 seconds)
 * - Curve parameter for exponential/linear/logarithmic shaping (-3.0 to 3.0)
 * - Peak amplitude control (0.0 to 1.0)
 * - Retrigger modes: from current level or from peak
 * - Sample-accurate timing and smooth curve generation
 * - Denormal protection and stability safeguards
 * 
 * Technical Details:
 * - Uses exponential coefficient calculation: coeff = exp(-1.0 / (decay_time * sr))
 * - Curve shaping via power function: output = pow(linear_decay, curve_factor)
 * - Linear decay: envelope *= decay_coefficient each sample
 * - Curve < 0: exponential (fast start, slow end)
 * - Curve = 0: linear decay
 * - Curve > 0: logarithmic (slow start, fast end)
 * 
 * Usage:
 * - Send bang to trigger decay from peak amplitude
 * - Inlet 1: Time control (signal/float, 0.001-60.0 seconds)
 * - Inlet 2: Curve control (signal/float, -3.0 to 3.0)
 * - Use peak and retrig messages for additional control
 * 
 * Inlets:
 *   1. Bang/messages (trigger, peak, retrig)
 *   2. Time (signal/float, 0.001-60.0 seconds)
 *   3. Curve (signal/float, -3.0 to 3.0)
 * 
 * Arguments: [decay_time] [curve] [peak]
 * Messages: bang, peak (float), retrig (0/1)
 */

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include <math.h>

#define PI 3.14159265358979323846
#define DENORMAL_THRESHOLD 1e-15
#define MIN_DECAY_TIME 0.001        // 1ms minimum
#define MAX_DECAY_TIME 60.0         // 60 seconds maximum
#define DEFAULT_DECAY_TIME 1.0      // 1 second default
#define DEFAULT_CURVE 0.0           // Linear curve default
#define DEFAULT_PEAK 1.0            // Full amplitude default

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
    
    // Sample rate
    double sr;                  // Sample rate
    double sr_inv;              // 1.0 / sample rate
    
    // Attribute parameters
    long envelope_mode;         // 0=decay, 1=AD, 2=gate-controlled (default 0)
    long curve_response;        // 0=exponential, 1=linear, 2=logarithmic (default 1)
    long click_protection;      // 0-100 samples click protection (default 0)
    long retrigger_mode;        // 0=from current, 1=from peak (default 1)
    
} t_decay;

// Function prototypes
void *decay_new(t_symbol *s, long argc, t_atom *argv);
void decay_free(t_decay *x);
void decay_dsp64(t_decay *x, t_object *dsp64, short *count, double samplerate, 
                 long maxvectorsize, long flags);
void decay_perform64(t_decay *x, t_object *dsp64, double **ins, long numins, 
                     double **outs, long numouts, long sampleframes, long flags, void *userparam);
void decay_bang(t_decay *x);
void decay_float(t_decay *x, double f);
void decay_peak(t_decay *x, double f);
void decay_retrig(t_decay *x, long n);
void decay_assist(t_decay *x, void *b, long m, long a, char *s);

// Envelope processing functions
double decay_calculate_coefficient(double decay_time, double sample_rate);
double decay_apply_curve(double linear_value, double curve);
double denormal_fix(double value);

// Attribute setter prototypes
t_max_err decay_attr_setenvelopemode(t_decay *x, void *attr, long argc, t_atom *argv);
t_max_err decay_attr_setcurveresponse(t_decay *x, void *attr, long argc, t_atom *argv);
t_max_err decay_attr_setclickprotection(t_decay *x, void *attr, long argc, t_atom *argv);
t_max_err decay_attr_setretriggermode(t_decay *x, void *attr, long argc, t_atom *argv);

// Class pointer
static t_class *decay_class = NULL;

//----------------------------------------------------------------------------------------------

void ext_main(void *r) {
    t_class *c;
    
    c = class_new("decay~", (method)decay_new, (method)decay_free,
                  sizeof(t_decay), 0L, A_GIMME, 0);
    
    class_addmethod(c, (method)decay_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)decay_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)decay_bang, "bang", 0);
    class_addmethod(c, (method)decay_float, "float", A_FLOAT, 0);
    class_addmethod(c, (method)decay_peak, "peak", A_FLOAT, 0);
    class_addmethod(c, (method)decay_retrig, "retrig", A_LONG, 0);
    
    // Attributes with Max 9 autocomplete support
    CLASS_ATTR_LONG(c, "envelope_mode", 0, t_decay, envelope_mode);
    CLASS_ATTR_BASIC(c, "envelope_mode", 0);
    CLASS_ATTR_LABEL(c, "envelope_mode", 0, "Envelope Mode");
    CLASS_ATTR_ACCESSORS(c, "envelope_mode", 0, decay_attr_setenvelopemode);
    CLASS_ATTR_ORDER(c, "envelope_mode", 0, "1");
    CLASS_ATTR_FILTER_CLIP(c, "envelope_mode", 0, 2);
    CLASS_ATTR_DEFAULT_SAVE(c, "envelope_mode", 0, "0");
    CLASS_ATTR_STYLE(c, "envelope_mode", 0, "enumindex");
    
    CLASS_ATTR_LONG(c, "curve_response", 0, t_decay, curve_response);
    CLASS_ATTR_BASIC(c, "curve_response", 0);
    CLASS_ATTR_LABEL(c, "curve_response", 0, "Curve Response");
    CLASS_ATTR_ACCESSORS(c, "curve_response", 0, decay_attr_setcurveresponse);
    CLASS_ATTR_ORDER(c, "curve_response", 0, "2");
    CLASS_ATTR_FILTER_CLIP(c, "curve_response", 0, 2);
    CLASS_ATTR_DEFAULT_SAVE(c, "curve_response", 0, "1");
    CLASS_ATTR_STYLE(c, "curve_response", 0, "enumindex");
    
    CLASS_ATTR_LONG(c, "click_protection", 0, t_decay, click_protection);
    CLASS_ATTR_BASIC(c, "click_protection", 0);
    CLASS_ATTR_LABEL(c, "click_protection", 0, "Click Protection");
    CLASS_ATTR_ACCESSORS(c, "click_protection", 0, decay_attr_setclickprotection);
    CLASS_ATTR_ORDER(c, "click_protection", 0, "3");
    CLASS_ATTR_FILTER_CLIP(c, "click_protection", 0, 100);
    CLASS_ATTR_DEFAULT_SAVE(c, "click_protection", 0, "0");
    CLASS_ATTR_STYLE(c, "click_protection", 0, "text");
    
    CLASS_ATTR_LONG(c, "retrigger_mode", 0, t_decay, retrigger_mode);
    CLASS_ATTR_BASIC(c, "retrigger_mode", 0);
    CLASS_ATTR_LABEL(c, "retrigger_mode", 0, "Retrigger Mode");
    CLASS_ATTR_ACCESSORS(c, "retrigger_mode", 0, decay_attr_setretriggermode);
    CLASS_ATTR_ORDER(c, "retrigger_mode", 0, "4");
    CLASS_ATTR_FILTER_CLIP(c, "retrigger_mode", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "retrigger_mode", 0, "1");
    CLASS_ATTR_STYLE(c, "retrigger_mode", 0, "onoff");
    
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    decay_class = c;
}

//----------------------------------------------------------------------------------------------

void *decay_new(t_symbol *s, long argc, t_atom *argv) {
    t_decay *x = (t_decay *)object_alloc(decay_class);
    
    if (x) {
        // lores~ pattern: 3 signal inlets (messages, time, curve)
        dsp_setup((t_pxobject *)x, 3);
        outlet_new(x, "signal");
        
        // Initialize sample rate
        x->sr = sys_getsr();
        x->sr_inv = 1.0 / x->sr;
        
        // Initialize parameter defaults
        x->decay_time_float = DEFAULT_DECAY_TIME;
        x->curve_float = DEFAULT_CURVE;
        x->peak = DEFAULT_PEAK;
        
        // Initialize connection status (assume no signals connected initially)
        x->time_has_signal = 0;
        x->curve_has_signal = 0;
        
        // Initialize attributes with defaults
        x->envelope_mode = 0;       // Decay mode
        x->curve_response = 1;      // Linear response
        x->click_protection = 0;    // No click protection
        x->retrigger_mode = 1;      // Retrig from peak
        
        // Initialize envelope state
        x->envelope = 0.0;          // Start at zero
        x->is_active = 0;           // Not running initially
        x->retrig_mode = 1;         // Default: retrig from peak
        
        // Initialize smoothing state
        x->smooth_samples_setting = 0;
        x->smooth_samples = 0;
        x->smooth_start_level = 0.0;
        
        
        // Process creation arguments
        post("decay~: argc=%d", argc);  // Debug: how many arguments?
        
        if (argc >= 1 && (atom_gettype(argv) == A_FLOAT || atom_gettype(argv) == A_LONG)) {
            x->decay_time_float = CLAMP(atom_getfloat(argv), MIN_DECAY_TIME, MAX_DECAY_TIME);
            post("decay~: arg 1 parsed as decay_time=%.3f", x->decay_time_float);
        }
        if (argc >= 2 && (atom_gettype(argv + 1) == A_FLOAT || atom_gettype(argv + 1) == A_LONG)) {
            x->curve_float = CLAMP(atom_getfloat(argv + 1), -3.0, 3.0);
            post("decay~: arg 2 parsed as curve=%.2f", x->curve_float);
        }
        if (argc >= 3 && (atom_gettype(argv + 2) == A_FLOAT || atom_gettype(argv + 2) == A_LONG)) {
            x->peak = CLAMP(atom_getfloat(argv + 2), 0.0, 1.0);
            post("decay~: arg 3 parsed as peak=%.3f", x->peak);
        }
        if (argc >= 4) {
            post("decay~: arg 4 type=%d (A_FLOAT=%d, A_LONG=%d)", atom_gettype(argv + 3), A_FLOAT, A_LONG);
            if (atom_gettype(argv + 3) == A_FLOAT || atom_gettype(argv + 3) == A_LONG) {
                int smooth_samples = (int)atom_getfloat(argv + 3);
                x->smooth_samples_setting = CLAMP(smooth_samples, 0, 100);
                post("decay~: arg 4 parsed as smooth_samples=%d", x->smooth_samples_setting);
            } else {
                post("decay~: arg 4 type not recognized, skipping");
            }
        }
        
        // Debug: Print final parameters
        post("decay~: final values - time=%.3f, curve=%.2f, peak=%.3f, smooth=%d", 
             x->decay_time_float, x->curve_float, x->peak, x->smooth_samples_setting);
        
        // Process attributes early for proper initialization
        long offset = attr_args_offset((short)argc, argv);
        attr_args_process(x, argc, argv);
        
        // Sync attributes with internal state
        x->retrig_mode = x->retrigger_mode;
        x->smooth_samples_setting = x->click_protection;
    }
    
    return x;
}

//----------------------------------------------------------------------------------------------

void decay_free(t_decay *x) {
    dsp_free((t_pxobject *)x);
}

//----------------------------------------------------------------------------------------------

void decay_dsp64(t_decay *x, t_object *dsp64, short *count, double samplerate, 
                 long maxvectorsize, long flags) {
    x->sr = samplerate;
    x->sr_inv = 1.0 / samplerate;
    
    // lores~ pattern: store signal connection status
    x->time_has_signal = count[1];     // Inlet 1 is time
    x->curve_has_signal = count[2];    // Inlet 2 is curve
    
    object_method(dsp64, gensym("dsp_add64"), x, decay_perform64, 0, NULL);
}

//----------------------------------------------------------------------------------------------

void decay_perform64(t_decay *x, t_object *dsp64, double **ins, long numins, 
                     double **outs, long numouts, long sampleframes, long flags, void *userparam) {
    // Input buffers (inlet 0 is messages, inlet 1 is time, inlet 2 is curve)
    double *time_in = ins[1];      // Time input
    double *curve_in = ins[2];     // Curve input
    
    // Output buffer
    double *out = outs[0];
    
    long n = sampleframes;
    
    while (n--) {
        // lores~ pattern: choose signal vs float for parameters
        double decay_time = x->time_has_signal ? *time_in++ : x->decay_time_float;
        double curve = x->curve_has_signal ? *curve_in++ : x->curve_float;
        
        // Clamp parameters to valid ranges
        decay_time = CLAMP(decay_time, MIN_DECAY_TIME, MAX_DECAY_TIME);
        curve = CLAMP(curve, -3.0, 3.0);
        
        double output = 0.0;
        
        
        if (x->is_active) {
            // Calculate decay coefficient for current time (sample-accurate)
            double decay_coeff = decay_calculate_coefficient(decay_time, x->sr);
            
            // Apply exponential decay
            x->envelope *= decay_coeff;
            
            // Apply curve shaping based on curve_response attribute
            double linear_progress = x->envelope / x->peak;  // Normalize to 0-1
            double final_curve = curve;
            
            // Override curve with response setting if not using signal input
            if (!x->curve_has_signal) {
                switch (x->curve_response) {
                    case 0: final_curve = -1.5; break;  // Exponential
                    case 1: final_curve = 0.0; break;   // Linear (default)
                    case 2: final_curve = 1.5; break;   // Logarithmic
                }
            }
            
            double shaped_progress = decay_apply_curve(linear_progress, final_curve);
            output = shaped_progress * x->peak;
            
            // Handle different envelope modes
            if (x->envelope_mode == 1) {  // AD mode - add attack phase
                // Simple attack ramp for AD mode (could be enhanced)
                static int attack_samples = 0;
                if (attack_samples < 44) {  // ~1ms attack at 44.1kHz
                    output *= (double)attack_samples / 44.0;
                    attack_samples++;
                } else {
                    attack_samples = 44;  // Stay at full level
                }
            }
            
            // Check if envelope has decayed sufficiently to stop
            if (x->envelope < DENORMAL_THRESHOLD || output < DENORMAL_THRESHOLD) {
                x->envelope = 0.0;
                x->is_active = 0;
                output = 0.0;
            }
        }
        
        
        // Apply click protection smoothing if active
        if (x->smooth_samples > 0) {
            double progress = (double)(x->click_protection - x->smooth_samples) / (double)x->click_protection;
            output = x->smooth_start_level + (output - x->smooth_start_level) * progress;
            x->smooth_samples--;
        }
        
        // Apply denormal fix and output
        *out++ = denormal_fix(output);
    }
}

//----------------------------------------------------------------------------------------------

void decay_bang(t_decay *x) {
    // Store current level for click protection smoothing
    x->smooth_start_level = x->envelope;
    
    // Trigger envelope based on retrigger mode attribute
    if (x->retrigger_mode || !x->is_active) {
        // Retrig from peak, or envelope not active
        x->envelope = x->peak;
    }
    // If retrigger_mode is 0 and envelope is active, continue from current level
    
    // Apply click protection if enabled and level jump detected
    if (x->click_protection > 0 && fabs(x->envelope - x->smooth_start_level) > 0.001) {
        x->smooth_samples = x->click_protection;
    } else {
        x->smooth_samples = 0;  // No smoothing needed
    }
    
    x->is_active = 1;
    
    post("decay~: triggered (mode=%s, peak=%.3f, time=%.3fs)", 
         x->envelope_mode == 0 ? "decay" : x->envelope_mode == 1 ? "AD" : "gate",
         x->peak, x->decay_time_float);
}

//----------------------------------------------------------------------------------------------

void decay_float(t_decay *x, double f) {
    // lores~ pattern: proxy_getinlet works on signal inlets for float routing
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 1: // Time inlet
            x->decay_time_float = CLAMP(f, MIN_DECAY_TIME, MAX_DECAY_TIME);
            post("decay~: decay time set to %.3f seconds", x->decay_time_float);
            break;
        case 2: // Curve inlet
            x->curve_float = CLAMP(f, -3.0, 3.0);
            post("decay~: curve set to %.2f", x->curve_float);
            break;
    }
}

//----------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------

void decay_peak(t_decay *x, double f) {
    // Set peak amplitude
    x->peak = CLAMP(f, 0.0, 1.0);
    
    post("decay~: peak amplitude set to %.3f", x->peak);
}

//----------------------------------------------------------------------------------------------

void decay_retrig(t_decay *x, long n) {
    // Set retrigger mode
    x->retrig_mode = n ? 1 : 0;
    
    post("decay~: retrigger mode: %s", x->retrig_mode ? "from peak" : "from current level");
}

//----------------------------------------------------------------------------------------------

double decay_calculate_coefficient(double decay_time, double sample_rate) {
    // Calculate exponential decay coefficient
    // envelope(t) = peak * exp(-t / tau)
    // For discrete samples: envelope[n+1] = envelope[n] * exp(-1 / (decay_time * sr))
    
    if (decay_time <= 0.0) {
        return 0.0;  // Instant decay
    } else {
        double time_constant = decay_time * sample_rate;
        return exp(-1.0 / time_constant);
    }
}

//----------------------------------------------------------------------------------------------

double decay_apply_curve(double linear_value, double curve) {
    // Apply curve shaping to linear 0-1 value
    // curve < 0: exponential (fast attack, slow decay)
    // curve = 0: linear
    // curve > 0: logarithmic (slow attack, fast decay)
    
    if (curve == 0.0 || linear_value <= 0.0) {
        return linear_value;  // Linear or edge case
    }
    
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

//----------------------------------------------------------------------------------------------

double denormal_fix(double value) {
    // Fix denormal numbers that can cause CPU spikes
    if (fabs(value) < DENORMAL_THRESHOLD) {
        return 0.0;
    }
    return value;
}

//----------------------------------------------------------------------------------------------

void decay_assist(t_decay *x, void *b, long m, long a, char *s) {
    if (m == ASSIST_INLET) {
        switch (a) {
            case 0:
                sprintf(s, "(bang/messages) Trigger envelope, peak, retrig");
                break;
            case 1:
                sprintf(s, "(signal/float) Decay time in seconds (0.001-60.0)");
                break;
            case 2:
                sprintf(s, "(signal/float) Curve shaping (-3.0 to 3.0)");
                break;
        }
    } else {  // ASSIST_OUTLET
        sprintf(s, "(signal) Exponential decay envelope output");
    }
}

//----------------------------------------------------------------------------------------------
// Attribute Setters
//----------------------------------------------------------------------------------------------

t_max_err decay_attr_setenvelopemode(t_decay *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->envelope_mode = CLAMP(atom_getlong(argv), 0, 2);
    }
    return 0;
}

t_max_err decay_attr_setcurveresponse(t_decay *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->curve_response = CLAMP(atom_getlong(argv), 0, 2);
    }
    return 0;
}

t_max_err decay_attr_setclickprotection(t_decay *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->click_protection = CLAMP(atom_getlong(argv), 0, 100);
        x->smooth_samples_setting = x->click_protection;  // Sync with old variable
    }
    return 0;
}

t_max_err decay_attr_setretriggermode(t_decay *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->retrigger_mode = CLAMP(atom_getlong(argv), 0, 1);
        x->retrig_mode = x->retrigger_mode;  // Sync with old variable
    }
    return 0;
}