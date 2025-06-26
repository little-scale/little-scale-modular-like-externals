/**
 * easelfo~ - Signal-rate LFO with easing functions for Max/MSP
 * 
 * This external generates an LFO using various easing functions commonly used
 * in animation and motion design, providing more musical modulation curves.
 * 
 * Inlets:
 *   1. Frequency in Hz (signal or float) - this should also accept a bang to reset the phase
 *   2. Easing function selection (int, 0-11)
 * 
 * Outlets:
 *   1. LFO output (signal, -1.0 to 1.0)
 */

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include <math.h>

#define PI 3.14159265358979323846

typedef struct _easelfo {
    t_pxobject ob;              // MSP object header
    double phase;               // Current phase (0.0 to 1.0)
    double sr;                  // Sample rate
    double sr_inv;              // 1.0 / sample rate
    
    // lores~ pattern: signal connection status
    short freq_has_signal;      // 1 if frequency inlet has signal connection
    short shape_has_signal;     // 1 if easing function inlet has signal connection  
    short phase_has_signal;     // 1 if phase offset inlet has signal connection
    
    // Float parameters (stored values for when no signal connected)
    double freq_float;          // Default frequency when no signal connected
    double easing_float;        // Easing function selection (0-11)
    double phase_offset_float;  // Phase offset (0.0 to 1.0)
    
    long mirror_mode;           // Mirror mode (0 = off, 1 = on)
    
    // Attribute parameters
    long mirror_mode_attr;      // 0=normal, 1=mirror, 2=reverse (default 0)
    long easing_curve;          // 0-11 selected easing curve (default 0)
    long phase_lock;            // 0=off, 1=on phase lock (default 0)
    double smoothing;           // 0.0-1.0 parameter smoothing (default 0.0)
    
    // Parameter smoothing state
    double prev_freq;           // Previous frequency for smoothing
    double prev_easing;         // Previous easing value for smoothing
    
} t_easelfo;

// Function prototypes
void *easelfo_new(t_symbol *s, long argc, t_atom *argv);
void easelfo_free(t_easelfo *x);
void easelfo_dsp64(t_easelfo *x, t_object *dsp64, short *count, double samplerate, 
                   long maxvectorsize, long flags);
void easelfo_perform64(t_easelfo *x, t_object *dsp64, double **ins, long numins, 
                       double **outs, long numouts, long sampleframes, long flags, void *userparam);
void easelfo_float(t_easelfo *x, double f);
void easelfo_int(t_easelfo *x, long n);
void easelfo_bang(t_easelfo *x);
void easelfo_mirror(t_easelfo *x, long n);
void easelfo_assist(t_easelfo *x, void *b, long m, long a, char *s);

// Attribute setter prototypes
t_max_err easelfo_attr_setmirrormode(t_easelfo *x, void *attr, long argc, t_atom *argv);
t_max_err easelfo_attr_seteasingcurve(t_easelfo *x, void *attr, long argc, t_atom *argv);
t_max_err easelfo_attr_setphaselock(t_easelfo *x, void *attr, long argc, t_atom *argv);
t_max_err easelfo_attr_setsmoothing(t_easelfo *x, void *attr, long argc, t_atom *argv);

// Easing function prototypes
double ease_linear(double t);
double ease_sine_in(double t);
double ease_sine_out(double t);
double ease_sine_inout(double t);
double ease_quad_in(double t);
double ease_quad_out(double t);
double ease_quad_inout(double t);
double ease_cubic_in(double t);
double ease_cubic_out(double t);
double ease_cubic_inout(double t);
double ease_expo_in(double t);
double ease_expo_out(double t);

// Class pointer
static t_class *easelfo_class = NULL;

// Main function
void ext_main(void *r) {
    t_class *c;
    
    c = class_new("easelfo~", (method)easelfo_new, (method)easelfo_free,
                  sizeof(t_easelfo), 0L, A_GIMME, 0);
    
    class_addmethod(c, (method)easelfo_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)easelfo_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)easelfo_float, "float", A_FLOAT, 0);
    class_addmethod(c, (method)easelfo_int, "int", A_LONG, 0);
    class_addmethod(c, (method)easelfo_bang, "bang", A_CANT, 0);
    class_addmethod(c, (method)easelfo_mirror, "mirror", A_LONG, 0);
    
    // Attributes with Max 9 autocomplete support
    CLASS_ATTR_LONG(c, "mirror_mode", 0, t_easelfo, mirror_mode_attr);
    CLASS_ATTR_BASIC(c, "mirror_mode", 0);
    CLASS_ATTR_LABEL(c, "mirror_mode", 0, "Mirror Mode");
    CLASS_ATTR_ACCESSORS(c, "mirror_mode", 0, easelfo_attr_setmirrormode);
    CLASS_ATTR_ORDER(c, "mirror_mode", 0, "1");
    CLASS_ATTR_FILTER_CLIP(c, "mirror_mode", 0, 2);
    CLASS_ATTR_DEFAULT_SAVE(c, "mirror_mode", 0, "0");
    CLASS_ATTR_STYLE(c, "mirror_mode", 0, "enumindex");
    
    CLASS_ATTR_LONG(c, "easing_curve", 0, t_easelfo, easing_curve);
    CLASS_ATTR_BASIC(c, "easing_curve", 0);
    CLASS_ATTR_LABEL(c, "easing_curve", 0, "Easing Curve");
    CLASS_ATTR_ACCESSORS(c, "easing_curve", 0, easelfo_attr_seteasingcurve);
    CLASS_ATTR_ORDER(c, "easing_curve", 0, "2");
    CLASS_ATTR_FILTER_CLIP(c, "easing_curve", 0, 11);
    CLASS_ATTR_DEFAULT_SAVE(c, "easing_curve", 0, "0");
    CLASS_ATTR_STYLE(c, "easing_curve", 0, "enumindex");
    
    CLASS_ATTR_LONG(c, "phase_lock", 0, t_easelfo, phase_lock);
    CLASS_ATTR_BASIC(c, "phase_lock", 0);
    CLASS_ATTR_LABEL(c, "phase_lock", 0, "Phase Lock");
    CLASS_ATTR_ACCESSORS(c, "phase_lock", 0, easelfo_attr_setphaselock);
    CLASS_ATTR_ORDER(c, "phase_lock", 0, "3");
    CLASS_ATTR_FILTER_CLIP(c, "phase_lock", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "phase_lock", 0, "0");
    CLASS_ATTR_STYLE(c, "phase_lock", 0, "onoff");
    
    CLASS_ATTR_DOUBLE(c, "smoothing", 0, t_easelfo, smoothing);
    CLASS_ATTR_BASIC(c, "smoothing", 0);
    CLASS_ATTR_LABEL(c, "smoothing", 0, "Parameter Smoothing");
    CLASS_ATTR_ACCESSORS(c, "smoothing", 0, easelfo_attr_setsmoothing);
    CLASS_ATTR_ORDER(c, "smoothing", 0, "4");
    CLASS_ATTR_FILTER_CLIP(c, "smoothing", 0.0, 1.0);
    CLASS_ATTR_DEFAULT_SAVE(c, "smoothing", 0, "0.0");
    CLASS_ATTR_STYLE(c, "smoothing", 0, "text");
    
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    easelfo_class = c;
}

// Constructor
void *easelfo_new(t_symbol *s, long argc, t_atom *argv) {
    t_easelfo *x = (t_easelfo *)object_alloc(easelfo_class);
    
    if (x) {
        // lores~ pattern: 3 signal inlets (freq, easing, phase_offset)
        dsp_setup((t_pxobject *)x, 3);
        outlet_new(x, "signal");
        
        // Initialize
        x->phase = 0.0;
        x->sr = sys_getsr();
        x->sr_inv = 1.0 / x->sr;
        
        // Initialize float parameters with defaults
        x->freq_float = 1.0;            // Default 1 Hz frequency
        x->easing_float = 0.0;          // Default to linear (0)
        x->phase_offset_float = 0.0;    // Default no phase offset
        x->mirror_mode = 0;             // Default mirror mode off
        
        // Initialize attributes with defaults
        x->mirror_mode_attr = 0;        // Normal mode
        x->easing_curve = 0;            // Linear easing
        x->phase_lock = 0;              // Phase lock off
        x->smoothing = 0.0;             // No smoothing
        
        // Initialize smoothing state
        x->prev_freq = 1.0;
        x->prev_easing = 0.0;
        
        // Initialize connection status (assume no signals connected initially)
        x->freq_has_signal = 0;
        x->shape_has_signal = 0;
        x->phase_has_signal = 0;
        
        // Process attributes early for proper initialization
        long offset = attr_args_offset((short)argc, argv);
        attr_args_process(x, argc, argv);
        
        // Process arguments
        if (argc >= 1 && atom_gettype(argv) == A_LONG) {
            long easing = atom_getlong(argv);
            x->easing_float = CLAMP(easing, 0, 11);
            x->easing_curve = x->easing_float;  // Sync with attribute
        }
        
        // Sync mirror mode with attribute
        x->mirror_mode = x->mirror_mode_attr;
    }
    
    return x;
}

// Destructor
void easelfo_free(t_easelfo *x) {
    dsp_free((t_pxobject *)x);
}

// DSP setup
void easelfo_dsp64(t_easelfo *x, t_object *dsp64, short *count, double samplerate, 
                   long maxvectorsize, long flags) {
    x->sr = samplerate;
    x->sr_inv = 1.0 / samplerate;
    
    // lores~ pattern: store signal connection status
    x->freq_has_signal = count[0];
    x->shape_has_signal = count[1]; 
    x->phase_has_signal = count[2];
    
    object_method(dsp64, gensym("dsp_add64"), x, easelfo_perform64, 0, NULL);
}

// Signal processing
void easelfo_perform64(t_easelfo *x, t_object *dsp64, double **ins, long numins, 
                       double **outs, long numouts, long sampleframes, long flags, void *userparam) {
    // Input buffers for all 3 inlets
    double *freq_in = ins[0];
    double *easing_in = ins[1];
    double *phase_offset_in = ins[2];
    double *out = outs[0];
    
    long n = sampleframes;
    double phase = x->phase;
    double sr_inv = x->sr_inv;
    
    // Function pointer array for easing functions
    double (*easing_functions[12])(double) = {
        ease_linear, ease_sine_in, ease_sine_out, ease_sine_inout,
        ease_quad_in, ease_quad_out, ease_quad_inout,
        ease_cubic_in, ease_cubic_out, ease_cubic_inout,
        ease_expo_in, ease_expo_out
    };
    
    while (n--) {
        // lores~ pattern: choose signal vs float for each inlet
        double freq = x->freq_has_signal ? *freq_in++ : x->freq_float;
        double easing_val = x->shape_has_signal ? *easing_in++ : x->easing_curve;  // Use attribute
        double phase_offset = x->phase_has_signal ? *phase_offset_in++ : x->phase_offset_float;
        
        // Apply parameter smoothing if enabled
        if (x->smoothing > 0.0) {
            double smooth_factor = x->smoothing * 0.01;  // Scale smoothing rate
            freq = x->prev_freq + smooth_factor * (freq - x->prev_freq);
            easing_val = x->prev_easing + smooth_factor * (easing_val - x->prev_easing);
            x->prev_freq = freq;
            x->prev_easing = easing_val;
        }
        
        // Clamp easing function selection
        long easing = (long)CLAMP(easing_val, 0.0, 11.0);
        
        // Halve frequency in mirror mode to maintain consistent timing
        if (x->mirror_mode_attr == 1) {  // Use attribute instead of old mirror_mode
            freq *= 0.5;
        }
        
        // Update phase
        phase += freq * sr_inv;
        
        // Wrap phase
        while (phase >= 1.0) phase -= 1.0;
        while (phase < 0.0) phase += 1.0;
        
        // Apply phase offset
        double offset_phase = phase + phase_offset;
        while (offset_phase >= 1.0) offset_phase -= 1.0;
        while (offset_phase < 0.0) offset_phase += 1.0;
        
        // Apply mirror mode based on attribute setting
        double final_phase = offset_phase;
        if (x->mirror_mode_attr == 1) {
            // Mirror mode: 0->1->0 triangular wave
            if (final_phase <= 0.5) {
                final_phase = final_phase * 2.0;  // 0.0-0.5 becomes 0.0-1.0
            } else {
                final_phase = (1.0 - final_phase) * 2.0;  // 0.5-1.0 becomes 1.0-0.0
            }
        } else if (x->mirror_mode_attr == 2) {
            // Reverse mode: 1->0 instead of 0->1
            final_phase = 1.0 - final_phase;
        }
        // mirror_mode_attr == 0: normal mode, no change to final_phase
        
        // Apply phase lock if enabled (quantize phase to steps)
        if (x->phase_lock) {
            final_phase = floor(final_phase * 16.0) / 16.0;  // 16 discrete steps
        }
        
        // Apply easing function
        double eased = easing_functions[easing](final_phase);
        
        // Map to bipolar output (-1 to 1)
        *out++ = (eased * 2.0) - 1.0;
    }
    
    x->phase = phase;
}


// Handle float input
void easelfo_float(t_easelfo *x, double f) {
    // lores~ pattern: proxy_getinlet works on signal inlets for float routing
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 0: // Frequency inlet
            x->freq_float = CLAMP(f, 0.0, 20000.0);
            break;
        case 1: // Easing function inlet
            x->easing_float = CLAMP(f, 0.0, 11.0);
            break;
        case 2: // Phase offset inlet
            x->phase_offset_float = f;
            // Wrap phase offset to 0.0-1.0 range
            while (x->phase_offset_float >= 1.0) x->phase_offset_float -= 1.0;
            while (x->phase_offset_float < 0.0) x->phase_offset_float += 1.0;
            break;
    }
}

// Handle integer input
void easelfo_int(t_easelfo *x, long n) {
    // lores~ pattern: proxy_getinlet works on signal inlets for int routing
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 0: // Frequency inlet - convert int to float
            x->freq_float = CLAMP((double)n, 0.0, 20000.0);
            break;
        case 1: // Easing function inlet - direct int handling
            x->easing_float = CLAMP((double)n, 0.0, 11.0);
            break;
        case 2: // Phase offset inlet - convert int to float
            x->phase_offset_float = (double)n;
            // Wrap phase offset to 0.0-1.0 range
            while (x->phase_offset_float >= 1.0) x->phase_offset_float -= 1.0;
            while (x->phase_offset_float < 0.0) x->phase_offset_float += 1.0;
            break;
    }
}

// Handle bang input - reset phase
void easelfo_bang(t_easelfo *x) {
    // lores~ pattern: proxy_getinlet works on signal inlets for message routing
    long inlet = proxy_getinlet((t_object *)x);
    
    if (inlet == 0) {  // First inlet - reset phase
        x->phase = 0.0;
    }
}

// Handle mirror message (for backwards compatibility)
void easelfo_mirror(t_easelfo *x, long n) {
    x->mirror_mode_attr = CLAMP(n, 0, 2);  // 0=normal, 1=mirror, 2=reverse
    x->mirror_mode = x->mirror_mode_attr;  // Sync old variable
}

// Assist method
void easelfo_assist(t_easelfo *x, void *b, long m, long a, char *s) {
    if (m == ASSIST_INLET) {
        switch (a) {
            case 0:
                sprintf(s, "(signal/float/bang) Frequency in Hz, bang to reset phase");
                break;
            case 1:
                sprintf(s, "(signal/float) Easing function (0-11)");
                break;
            case 2:
                sprintf(s, "(signal/float) Phase offset (0.0-1.0)");
                break;
        }
    } else {  // ASSIST_OUTLET
        sprintf(s, "(signal) LFO output (-1 to 1)");
    }
}

// Easing functions implementation

double ease_linear(double t) {
    return t;
}

double ease_sine_in(double t) {
    return 1.0 - cos((t * PI) / 2.0);
}

double ease_sine_out(double t) {
    return sin((t * PI) / 2.0);
}

double ease_sine_inout(double t) {
    return -(cos(PI * t) - 1.0) / 2.0;
}

double ease_quad_in(double t) {
    return t * t;
}

double ease_quad_out(double t) {
    return t * (2.0 - t);
}

double ease_quad_inout(double t) {
    return t < 0.5 ? 2.0 * t * t : 1.0 - pow(-2.0 * t + 2.0, 2.0) / 2.0;
}

double ease_cubic_in(double t) {
    return t * t * t;
}

double ease_cubic_out(double t) {
    return 1.0 - pow(1.0 - t, 3.0);
}

double ease_cubic_inout(double t) {
    return t < 0.5 ? 4.0 * t * t * t : 1.0 - pow(-2.0 * t + 2.0, 3.0) / 2.0;
}

double ease_expo_in(double t) {
    return t == 0.0 ? 0.0 : pow(2.0, 10.0 * t - 10.0);
}

double ease_expo_out(double t) {
    return t == 1.0 ? 1.0 : 1.0 - pow(2.0, -10.0 * t);
}

//----------------------------------------------------------------------------------------------
// Attribute Setters
//----------------------------------------------------------------------------------------------

t_max_err easelfo_attr_setmirrormode(t_easelfo *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->mirror_mode_attr = CLAMP(atom_getlong(argv), 0, 2);
        x->mirror_mode = x->mirror_mode_attr;  // Sync with old variable
    }
    return 0;
}

t_max_err easelfo_attr_seteasingcurve(t_easelfo *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->easing_curve = CLAMP(atom_getlong(argv), 0, 11);
        x->easing_float = x->easing_curve;  // Sync with float parameter
    }
    return 0;
}

t_max_err easelfo_attr_setphaselock(t_easelfo *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->phase_lock = atom_getlong(argv) ? 1 : 0;
    }
    return 0;
}

t_max_err easelfo_attr_setsmoothing(t_easelfo *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->smoothing = CLAMP(atom_getfloat(argv), 0.0, 1.0);
    }
    return 0;
}