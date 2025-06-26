/**
 * ramplfo~ - Ramping LFO with asymmetric rise/fall times and curve shaping for Max/MSP
 * 
 * This external generates a ramping LFO with independent control over rise and fall
 * portions, curve shaping with extended curve types, and organic jitter.
 * 
 * Inlets:
 *   1. Frequency in Hz (signal or float) + bang for phase reset
 *   2. Shape - rise/fall ratio (signal or float, 0.0-1.0)
 *   3. Rise linearity - curve shaping for rise (signal or float, -3.0-3.0)
 *   4. Fall linearity - curve shaping for fall (signal or float, -3.0-3.0)
 *   5. Jitter - randomness amount (signal or float, 0.0-1.0)
 * 
 * Outlets:
 *   1. LFO output (signal, 0.0 to 1.0)
 */

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include <math.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

typedef struct _ramplfo {
    t_pxobject ob;              // MSP object header
    double phase;               // Current phase (0.0 to 1.0)
    double sr;                  // Sample rate
    double sr_inv;              // 1.0 / sample rate
    
    // Parameter storage for when no signal connected
    double freq_float;          // Default frequency (Hz)
    double shape_float;         // Default shape (0.0-1.0)
    double rise_curve_float;    // Default rise curve (-3.0-3.0)
    double fall_curve_float;    // Default fall curve (-3.0-3.0)
    double jitter_float;        // Default jitter (0.0-1.0)
    double phase_offset_float;  // Default phase offset (0.0-1.0)
    
    // Signal connection status (from count array in dsp64)
    short freq_has_signal;      // Inlet 0 has signal connected
    short shape_has_signal;     // Inlet 1 has signal connected
    short rise_has_signal;      // Inlet 2 has signal connected
    short fall_has_signal;      // Inlet 3 has signal connected
    short jitter_has_signal;    // Inlet 4 has signal connected
    short phase_has_signal;     // Inlet 5 has signal connected
    
    // Proxy inlets for parameters 2-6
    void *proxy1;               // Shape
    void *proxy2;               // Rise curve
    void *proxy3;               // Fall curve
    void *proxy4;               // Jitter
    void *proxy5;               // Phase offset
    long proxy_inletnum;        // Inlet number tracker
    
    // Jitter state
    double jitter_state;        // Current jitter value
    double jitter_target;       // Target jitter value
    long jitter_counter;        // Sample counter for jitter updates
    long jitter_interval;       // Samples between jitter updates
    
    // Attribute parameters
    long curve_type;            // 0=power, 1=exponential, 2=logarithmic, 3=s-curve, 4=mixed (default 4)
    double jitter_rate;         // 0.0-1.0 jitter update rate (default 0.5)
    long symmetry;              // 0=asymmetric, 1=symmetric (default 0)
} t_ramplfo;

// Function prototypes
void *ramplfo_new(t_symbol *s, long argc, t_atom *argv);
void ramplfo_free(t_ramplfo *x);
void ramplfo_dsp64(t_ramplfo *x, t_object *dsp64, short *count, double samplerate, 
                   long maxvectorsize, long flags);
void ramplfo_perform64(t_ramplfo *x, t_object *dsp64, double **ins, long numins, 
                       double **outs, long numouts, long sampleframes, long flags, void *userparam);
void ramplfo_float(t_ramplfo *x, double f);
void ramplfo_bang(t_ramplfo *x);
void ramplfo_freq(t_ramplfo *x, double f);
void ramplfo_shape(t_ramplfo *x, double f);
void ramplfo_rise(t_ramplfo *x, double f);
void ramplfo_fall(t_ramplfo *x, double f);
void ramplfo_jitter(t_ramplfo *x, double f);
void ramplfo_phase(t_ramplfo *x, double f);
void ramplfo_assist(t_ramplfo *x, void *b, long m, long a, char *s);

// Curve shaping function prototypes
double apply_curve(double local_phase, double linearity);
double generate_jitter_sample(t_ramplfo *x);

// Attribute setter prototypes
t_max_err ramplfo_attr_setcurvetype(t_ramplfo *x, void *attr, long argc, t_atom *argv);
t_max_err ramplfo_attr_setjitterrate(t_ramplfo *x, void *attr, long argc, t_atom *argv);
t_max_err ramplfo_attr_setsymmetry(t_ramplfo *x, void *attr, long argc, t_atom *argv);

// Class pointer
static t_class *ramplfo_class = NULL;

// Main function
void ext_main(void *r) {
    t_class *c;
    
    c = class_new("ramplfo~", (method)ramplfo_new, (method)ramplfo_free,
                  sizeof(t_ramplfo), 0L, A_GIMME, 0);
    
    class_addmethod(c, (method)ramplfo_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)ramplfo_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)ramplfo_float, "float", A_FLOAT, 0);
    class_addmethod(c, (method)ramplfo_bang, "bang", A_CANT, 0);
    class_addmethod(c, (method)ramplfo_freq, "freq", A_FLOAT, 0);
    class_addmethod(c, (method)ramplfo_shape, "shape", A_FLOAT, 0);
    class_addmethod(c, (method)ramplfo_rise, "rise", A_FLOAT, 0);
    class_addmethod(c, (method)ramplfo_fall, "fall", A_FLOAT, 0);
    class_addmethod(c, (method)ramplfo_jitter, "jitter", A_FLOAT, 0);
    class_addmethod(c, (method)ramplfo_phase, "phase", A_FLOAT, 0);
    
    // Attributes with Max 9 autocomplete support
    CLASS_ATTR_LONG(c, "curve_type", 0, t_ramplfo, curve_type);
    CLASS_ATTR_BASIC(c, "curve_type", 0);
    CLASS_ATTR_LABEL(c, "curve_type", 0, "Curve Type");
    CLASS_ATTR_ACCESSORS(c, "curve_type", 0, ramplfo_attr_setcurvetype);
    CLASS_ATTR_ORDER(c, "curve_type", 0, "1");
    CLASS_ATTR_FILTER_CLIP(c, "curve_type", 0, 4);
    CLASS_ATTR_DEFAULT_SAVE(c, "curve_type", 0, "4");
    CLASS_ATTR_STYLE(c, "curve_type", 0, "enumindex");
    
    CLASS_ATTR_DOUBLE(c, "jitter_rate", 0, t_ramplfo, jitter_rate);
    CLASS_ATTR_BASIC(c, "jitter_rate", 0);
    CLASS_ATTR_LABEL(c, "jitter_rate", 0, "Jitter Rate");
    CLASS_ATTR_ACCESSORS(c, "jitter_rate", 0, ramplfo_attr_setjitterrate);
    CLASS_ATTR_ORDER(c, "jitter_rate", 0, "2");
    CLASS_ATTR_FILTER_CLIP(c, "jitter_rate", 0.0, 1.0);
    CLASS_ATTR_DEFAULT_SAVE(c, "jitter_rate", 0, "0.5");
    CLASS_ATTR_STYLE(c, "jitter_rate", 0, "text");
    
    CLASS_ATTR_LONG(c, "symmetry", 0, t_ramplfo, symmetry);
    CLASS_ATTR_BASIC(c, "symmetry", 0);
    CLASS_ATTR_LABEL(c, "symmetry", 0, "Shape Symmetry");
    CLASS_ATTR_ACCESSORS(c, "symmetry", 0, ramplfo_attr_setsymmetry);
    CLASS_ATTR_ORDER(c, "symmetry", 0, "3");
    CLASS_ATTR_FILTER_CLIP(c, "symmetry", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "symmetry", 0, "0");
    CLASS_ATTR_STYLE(c, "symmetry", 0, "onoff");
    
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    ramplfo_class = c;
}

// Constructor
void *ramplfo_new(t_symbol *s, long argc, t_atom *argv) {
    t_ramplfo *x = (t_ramplfo *)object_alloc(ramplfo_class);
    
    if (x) {
        dsp_setup((t_pxobject *)x, 6);  // 6 signal inlets for all parameters
        outlet_new(x, "signal");         // 1 signal outlet
        
        // Initialize default values
        x->phase = 0.0;
        x->sr = sys_getsr();
        x->sr_inv = 1.0 / x->sr;
        x->freq_float = 1.0;        // 1 Hz default
        x->shape_float = 0.5;       // 50/50 rise/fall
        x->rise_curve_float = 0.0;  // Linear rise
        x->fall_curve_float = 0.0;  // Linear fall
        x->jitter_float = 0.0;      // No jitter
        x->phase_offset_float = 0.0; // No phase offset
        
        // Initialize attributes with defaults
        x->curve_type = 4;          // Mixed mode (all curve types available)
        x->jitter_rate = 0.5;       // Moderate jitter rate
        x->symmetry = 0;            // Asymmetric by default
        
        // Initialize jitter state
        x->jitter_state = 0.0;
        x->jitter_target = 0.0;
        x->jitter_counter = 0;
        x->jitter_interval = 512;   // Update jitter every 512 samples (~10ms at 48kHz)
        
        // No proxy inlets needed - all 6 inlets are signal inlets (like lores~)
        x->proxy1 = NULL;
        x->proxy2 = NULL;
        x->proxy3 = NULL;
        x->proxy4 = NULL;
        x->proxy5 = NULL;
        
        // Process arguments: freq, shape, rise_curve, fall_curve, jitter, phase_offset
        if (argc >= 1 && (atom_gettype(argv) == A_FLOAT || atom_gettype(argv) == A_LONG)) {
            x->freq_float = atom_getfloat(argv);
            if (x->freq_float <= 0.0) x->freq_float = 1.0;
        }
        if (argc >= 2 && (atom_gettype(argv + 1) == A_FLOAT || atom_gettype(argv + 1) == A_LONG)) {
            x->shape_float = atom_getfloat(argv + 1);
            if (x->shape_float < 0.0) x->shape_float = 0.0;
            if (x->shape_float > 1.0) x->shape_float = 1.0;
        }
        if (argc >= 3 && (atom_gettype(argv + 2) == A_FLOAT || atom_gettype(argv + 2) == A_LONG)) {
            x->rise_curve_float = atom_getfloat(argv + 2);
            if (x->rise_curve_float < -3.0) x->rise_curve_float = -3.0;
            if (x->rise_curve_float > 3.0) x->rise_curve_float = 3.0;
        }
        if (argc >= 4 && (atom_gettype(argv + 3) == A_FLOAT || atom_gettype(argv + 3) == A_LONG)) {
            x->fall_curve_float = atom_getfloat(argv + 3);
            if (x->fall_curve_float < -3.0) x->fall_curve_float = -3.0;
            if (x->fall_curve_float > 3.0) x->fall_curve_float = 3.0;
        }
        if (argc >= 5 && (atom_gettype(argv + 4) == A_FLOAT || atom_gettype(argv + 4) == A_LONG)) {
            x->jitter_float = atom_getfloat(argv + 4);
            if (x->jitter_float < 0.0) x->jitter_float = 0.0;
            if (x->jitter_float > 1.0) x->jitter_float = 1.0;
        }
        if (argc >= 6 && (atom_gettype(argv + 5) == A_FLOAT || atom_gettype(argv + 5) == A_LONG)) {
            x->phase_offset_float = atom_getfloat(argv + 5);
            if (x->phase_offset_float < 0.0) x->phase_offset_float = 0.0;
            if (x->phase_offset_float > 1.0) x->phase_offset_float = 1.0;
        }
        
        // Process attributes early for proper initialization
        long offset = attr_args_offset((short)argc, argv);
        attr_args_process(x, argc, argv);
    }
    
    return x;
}

// Destructor
void ramplfo_free(t_ramplfo *x) {
    dsp_free((t_pxobject *)x);
    // No proxy inlets to free
}

// DSP setup
void ramplfo_dsp64(t_ramplfo *x, t_object *dsp64, short *count, double samplerate, 
                   long maxvectorsize, long flags) {
    x->sr = samplerate;
    x->sr_inv = 1.0 / samplerate;
    
    // Store connection status for all 6 signal inlets (like lores~)
    x->freq_has_signal = count[0];
    x->shape_has_signal = count[1];
    x->rise_has_signal = count[2];
    x->fall_has_signal = count[3];
    x->jitter_has_signal = count[4];
    x->phase_has_signal = count[5];
    
    object_method(dsp64, gensym("dsp_add64"), x, ramplfo_perform64, 0, NULL);
}

// Extended curve shaping function
double apply_curve(double local_phase, double linearity) {
    // Clamp input phase to safe range
    if (local_phase <= 0.0) return 0.0;
    if (local_phase >= 1.0) return 1.0;
    
    // Clamp linearity to our extended range
    if (linearity < -3.0) linearity = -3.0;
    if (linearity > 3.0) linearity = 3.0;
    
    double abs_lin = fabs(linearity);
    
    if (linearity >= -1.0 && linearity <= 1.0) {
        // Enhanced power function approach (-1 to 1) with more extreme curves
        if (linearity == 0.0) {
            return local_phase;  // Linear
        } else if (linearity < 0.0) {
            // Concave curve - very extreme exponents
            double exponent = 1.0 + (-linearity * 6.0);  // 1.0 to 7.0 (was 1.0 to 5.0)
            return pow(local_phase, exponent);
        } else {
            // Convex curve - very extreme exponents  
            double exponent = 1.0 + (linearity * 6.0);   // 1.0 to 7.0 (was 1.0 to 5.0)
            return 1.0 - pow(1.0 - local_phase, exponent);
        }
    }
    else if (linearity < -1.0) {
        // Exponential curves (-3 to -1)
        double strength = (abs_lin - 1.0) / 2.0;  // 0.0 to 1.0
        if (strength <= 0.0) return local_phase;
        double exp_val = exp(strength * local_phase);
        double exp_max = exp(strength);
        return (exp_val - 1.0) / (exp_max - 1.0);
    }
    else if (linearity > 1.0 && linearity <= 2.0) {
        // Logarithmic curves (1 to 2)
        double strength = linearity - 1.0;  // 0.0 to 1.0
        if (strength <= 0.0) return local_phase;
        return log(1.0 + strength * local_phase) / log(1.0 + strength);
    }
    else {
        // S-curves (2 to 3)
        double strength = (linearity - 2.0) / 1.0;  // 0.0 to 1.0
        if (strength <= 0.0) return local_phase;
        double tanh_str = tanh(strength);
        if (tanh_str == 0.0) return local_phase;
        return 0.5 * (1.0 + tanh(strength * (2.0 * local_phase - 1.0)) / tanh_str);
    }
}

// Generate smoothed jitter sample
double generate_jitter_sample(t_ramplfo *x) {
    // Calculate dynamic jitter interval based on jitter_rate attribute
    long dynamic_interval = (long)(128 + (1.0 - x->jitter_rate) * 1024);  // 128-1152 samples
    
    // Update jitter target periodically
    x->jitter_counter++;
    if (x->jitter_counter >= dynamic_interval) {
        x->jitter_counter = 0;
        // Generate new random target between -1.0 and 1.0
        x->jitter_target = ((double)rand() / RAND_MAX) * 2.0 - 1.0;
    }
    
    // Smooth interpolation towards target (also affected by jitter_rate)
    double alpha = 0.005 + (x->jitter_rate * 0.02);  // 0.005-0.025 smoothing
    x->jitter_state += alpha * (x->jitter_target - x->jitter_state);
    
    return x->jitter_state;
}

// Signal processing
void ramplfo_perform64(t_ramplfo *x, t_object *dsp64, double **ins, long numins, 
                       double **outs, long numouts, long sampleframes, long flags, void *userparam) {
    double *freq_in = ins[0];       // Frequency input  
    double *shape_in = ins[1];      // Shape input
    double *rise_in = ins[2];       // Rise curve input
    double *fall_in = ins[3];       // Fall curve input
    double *jitter_in = ins[4];     // Jitter input
    double *phase_in = ins[5];      // Phase offset input
    double *out = outs[0];          // Output
    long n = sampleframes;
    double phase = x->phase;
    double sr_inv = x->sr_inv;
    
    while (n--) {
        // Get all parameters (signal if connected, float if not) - like lores~
        double freq = x->freq_has_signal ? *freq_in++ : x->freq_float;
        if (freq <= 0.0) freq = x->freq_float;
        
        double shape = x->shape_has_signal ? *shape_in++ : x->shape_float;
        if (shape < 0.0) shape = 0.0;
        if (shape > 1.0) shape = 1.0;
        
        double rise_curve = x->rise_has_signal ? *rise_in++ : x->rise_curve_float;
        if (rise_curve < -3.0) rise_curve = -3.0;
        if (rise_curve > 3.0) rise_curve = 3.0;
        
        double fall_curve = x->fall_has_signal ? *fall_in++ : x->fall_curve_float;
        if (fall_curve < -3.0) fall_curve = -3.0;
        if (fall_curve > 3.0) fall_curve = 3.0;
        
        double jitter_amount = x->jitter_has_signal ? *jitter_in++ : x->jitter_float;
        if (jitter_amount < 0.0) jitter_amount = 0.0;
        if (jitter_amount > 1.0) jitter_amount = 1.0;
        
        double phase_offset = x->phase_has_signal ? *phase_in++ : x->phase_offset_float;
        if (phase_offset < 0.0) phase_offset = 0.0;
        if (phase_offset > 1.0) phase_offset = 1.0;
        
        // Update phase
        phase += freq * sr_inv;
        
        // Wrap phase
        while (phase >= 1.0) phase -= 1.0;
        while (phase < 0.0) phase += 1.0;
        
        // Apply phase offset
        double offset_phase = phase + phase_offset;
        while (offset_phase >= 1.0) offset_phase -= 1.0;
        while (offset_phase < 0.0) offset_phase += 1.0;
        
        // Apply symmetry if enabled
        double final_shape = shape;
        if (x->symmetry) {
            final_shape = 0.5;  // Force symmetric timing
        }
        
        // Determine if we're in rise or fall portion (using offset phase)
        double output;
        if (final_shape <= 0.0) {
            // Pure fall (shape = 0)
            double local_phase = offset_phase;
            output = 1.0 - apply_curve(local_phase, fall_curve);
        } else if (final_shape >= 1.0) {
            // Pure rise (shape = 1)
            double local_phase = offset_phase;
            output = apply_curve(local_phase, rise_curve);
        } else if (offset_phase < final_shape) {
            // Rise portion
            double local_phase = offset_phase / final_shape;
            output = apply_curve(local_phase, rise_curve);
        } else {
            // Fall portion
            double local_phase = (offset_phase - final_shape) / (1.0 - final_shape);
            output = 1.0 - apply_curve(local_phase, fall_curve);
        }
        
        // Apply jitter if enabled
        if (jitter_amount > 0.0) {
            double jitter_sample = generate_jitter_sample(x);
            output = output * (1.0 + (jitter_sample * jitter_amount * 0.2));  // ±20% max variation
        }
        
        // Clamp output to 0.0-1.0 range
        if (output < 0.0) output = 0.0;
        if (output > 1.0) output = 1.0;
        
        *out++ = output;
    }
    
    x->phase = phase;
}

// Handle float input
void ramplfo_float(t_ramplfo *x, double f) {
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 0:  // Frequency
            if (f > 0.0) x->freq_float = f;
            break;
        case 1:  // Shape
            x->shape_float = f;
            if (x->shape_float < 0.0) x->shape_float = 0.0;
            if (x->shape_float > 1.0) x->shape_float = 1.0;
            break;
        case 2:  // Rise curve
            x->rise_curve_float = f;
            if (x->rise_curve_float < -3.0) x->rise_curve_float = -3.0;
            if (x->rise_curve_float > 3.0) x->rise_curve_float = 3.0;
            break;
        case 3:  // Fall curve
            x->fall_curve_float = f;
            if (x->fall_curve_float < -3.0) x->fall_curve_float = -3.0;
            if (x->fall_curve_float > 3.0) x->fall_curve_float = 3.0;
            break;
        case 4:  // Jitter
            x->jitter_float = f;
            if (x->jitter_float < 0.0) x->jitter_float = 0.0;
            if (x->jitter_float > 1.0) x->jitter_float = 1.0;
            break;
        case 5:  // Phase offset
            x->phase_offset_float = f;
            if (x->phase_offset_float < 0.0) x->phase_offset_float = 0.0;
            if (x->phase_offset_float > 1.0) x->phase_offset_float = 1.0;
            break;
    }
}

// Specific parameter message handlers
void ramplfo_freq(t_ramplfo *x, double f) {
    if (f > 0.0) x->freq_float = f;
}

void ramplfo_shape(t_ramplfo *x, double f) {
    x->shape_float = f;
    if (x->shape_float < 0.0) x->shape_float = 0.0;
    if (x->shape_float > 1.0) x->shape_float = 1.0;
}

void ramplfo_rise(t_ramplfo *x, double f) {
    x->rise_curve_float = f;
    if (x->rise_curve_float < -3.0) x->rise_curve_float = -3.0;
    if (x->rise_curve_float > 3.0) x->rise_curve_float = 3.0;
}

void ramplfo_fall(t_ramplfo *x, double f) {
    x->fall_curve_float = f;
    if (x->fall_curve_float < -3.0) x->fall_curve_float = -3.0;
    if (x->fall_curve_float > 3.0) x->fall_curve_float = 3.0;
}

void ramplfo_jitter(t_ramplfo *x, double f) {
    x->jitter_float = f;
    if (x->jitter_float < 0.0) x->jitter_float = 0.0;
    if (x->jitter_float > 1.0) x->jitter_float = 1.0;
}

void ramplfo_phase(t_ramplfo *x, double f) {
    x->phase_offset_float = f;
    if (x->phase_offset_float < 0.0) x->phase_offset_float = 0.0;
    if (x->phase_offset_float > 1.0) x->phase_offset_float = 1.0;
}

// Handle bang input - reset phase
void ramplfo_bang(t_ramplfo *x) {
    long inlet = proxy_getinlet((t_object *)x);
    
    if (inlet == 0) {  // First inlet - reset phase
        x->phase = 0.0;
    }
}

// Assist method
void ramplfo_assist(t_ramplfo *x, void *b, long m, long a, char *s) {
    if (m == ASSIST_INLET) {
        switch (a) {
            case 0:
                sprintf(s, "(signal/float/bang) Frequency in Hz, bang to reset phase");
                break;
            case 1:
                sprintf(s, "(signal/float) Shape - rise/fall ratio (0.0-1.0)");
                break;
            case 2:
                sprintf(s, "(signal/float) Rise curve shaping (-3.0-3.0)");
                break;
            case 3:
                sprintf(s, "(signal/float) Fall curve shaping (-3.0-3.0)");
                break;
            case 4:
                sprintf(s, "(signal/float) Jitter amount (0.0-1.0)");
                break;
            case 5:
                sprintf(s, "(signal/float) Phase offset (0.0-1.0)");
                break;
        }
    } else {  // ASSIST_OUTLET
        sprintf(s, "(signal) Ramping LFO output (0.0 to 1.0)");
    }
}

//----------------------------------------------------------------------------------------------
// Attribute Setters
//----------------------------------------------------------------------------------------------

t_max_err ramplfo_attr_setcurvetype(t_ramplfo *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->curve_type = CLAMP(atom_getlong(argv), 0, 4);
    }
    return 0;
}

t_max_err ramplfo_attr_setjitterrate(t_ramplfo *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->jitter_rate = CLAMP(atom_getfloat(argv), 0.0, 1.0);
    }
    return 0;
}

t_max_err ramplfo_attr_setsymmetry(t_ramplfo *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->symmetry = atom_getlong(argv) ? 1 : 0;
    }
    return 0;
}