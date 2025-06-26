/**
 * vactrol~ - VTL5C3 vactrol emulation Max external
 * 
 * Implements a smooth low pass gate using authentic vactrol characteristics
 * with exponential decay behavior and tube saturation.
 * 
 * Features:
 * - VTL5C3 resistance curve (100Ω to 1MΩ+)
 * - Exponential decay timing (2-5ms attack, 50-200ms decay)
 * - CV signal input (0-1) for direct vactrol control
 * - 1-pole or 2-pole low pass filtering
 * - Asymmetric tube saturation
 * - Bang triggering with retriggering support
 */

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include <math.h>

// Constants for vactrol behavior
#define VACTROL_MIN_RESISTANCE 100.0        // Minimum resistance in ohms (bright)
#define VACTROL_MAX_RESISTANCE 1000000.0    // Maximum resistance in ohms (dark) 
#define VACTROL_CAPACITANCE 47e-9           // 47nF capacitor value
#define DEFAULT_DECAY_TIME 0.15             // 150ms default decay time (Buchla-style)
#define PI 3.14159265358979323846

// External object structure
typedef struct _vactrol {
    t_pxobject ob;                  // Audio object base
    
    // Vactrol state
    double resistance;              // Current resistance value
    double decay_time;              // Decay time constant (tau)
    long triggered;                 // Trigger state flag
    double trigger_time;            // Time since trigger
    
    // Attribute parameters
    long poles;                     // 1 or 2 pole filter (default 1)
    long response_curve;            // 0=exponential, 1=linear, 2=logarithmic (default 0)
    double calibration;             // 0.1-2.0 resistance scaling (default 1.0)
    long temperature_drift;         // 0=off, 1=on temperature simulation (default 0)
    
    // Filter state (for 1-pole and 2-pole)
    double filter_state1;           // First pole state
    double filter_state2;           // Second pole state
    long pole_count;                // 1 or 2 pole filter
    
    // Tube saturation
    double tube_drive;              // Tube drive amount (0-1)
    double tube_character;          // Asymmetric character (0-1)
    
    // Sample rate and timing
    double sample_rate;
    double inv_sample_rate;
    
    // Proxy inlet for bang messages
    void *m_proxy_bang;             // Bang inlet proxy
    long m_inletnum;
    
} t_vactrol;

// Function prototypes
void *vactrol_new(t_symbol *s, long argc, t_atom *argv);
void vactrol_free(t_vactrol *x);
void vactrol_assist(t_vactrol *x, void *b, long m, long a, char *s);
void vactrol_dsp64(t_vactrol *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void vactrol_perform64(t_vactrol *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam);

// Message handlers
void vactrol_bang(t_vactrol *x);
void vactrol_decay(t_vactrol *x, double decay);
void vactrol_poles(t_vactrol *x, long poles);
void vactrol_drive(t_vactrol *x, double drive);
void vactrol_character(t_vactrol *x, double character);

// Attribute setter prototypes
t_max_err vactrol_attr_setpoles(t_vactrol *x, void *attr, long argc, t_atom *argv);
t_max_err vactrol_attr_setresponse(t_vactrol *x, void *attr, long argc, t_atom *argv);
t_max_err vactrol_attr_setcalibration(t_vactrol *x, void *attr, long argc, t_atom *argv);
t_max_err vactrol_attr_settempdrift(t_vactrol *x, void *attr, long argc, t_atom *argv);

// DSP utility functions
double vactrol_calculate_resistance(t_vactrol *x, double time_elapsed);
double vactrol_calculate_cutoff(double resistance);
double vactrol_onepole_filter(double input, double cutoff, double *state, double sample_rate);
double vactrol_tube_saturation(double input, double drive, double character);

// Class pointer
static t_class *vactrol_class;

//--------------------------------------------------------------------------

void ext_main(void *r) {
    t_class *c = class_new("vactrol~", (method)vactrol_new, (method)vactrol_free, 
                          (long)sizeof(t_vactrol), 0L, A_GIMME, 0);
    
    class_addmethod(c, (method)vactrol_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)vactrol_assist, "assist", A_CANT, 0);
    
    // Message handlers
    class_addmethod(c, (method)vactrol_bang, "bang", 0);
    class_addmethod(c, (method)vactrol_decay, "decay", A_FLOAT, 0);
    class_addmethod(c, (method)vactrol_poles, "poles", A_LONG, 0);
    class_addmethod(c, (method)vactrol_drive, "drive", A_FLOAT, 0);
    class_addmethod(c, (method)vactrol_character, "character", A_FLOAT, 0);
    
    // Attributes with Max 9 autocomplete support
    CLASS_ATTR_LONG(c, "poles", 0, t_vactrol, poles);
    CLASS_ATTR_BASIC(c, "poles", 0);
    CLASS_ATTR_LABEL(c, "poles", 0, "Filter Poles");
    CLASS_ATTR_ACCESSORS(c, "poles", 0, vactrol_attr_setpoles);
    CLASS_ATTR_ORDER(c, "poles", 0, "1");
    CLASS_ATTR_FILTER_CLIP(c, "poles", 1, 2);
    CLASS_ATTR_DEFAULT_SAVE(c, "poles", 0, "1");
    CLASS_ATTR_STYLE(c, "poles", 0, "enumindex");
    
    CLASS_ATTR_LONG(c, "response_curve", 0, t_vactrol, response_curve);
    CLASS_ATTR_BASIC(c, "response_curve", 0);
    CLASS_ATTR_LABEL(c, "response_curve", 0, "Response Curve");
    CLASS_ATTR_ACCESSORS(c, "response_curve", 0, vactrol_attr_setresponse);
    CLASS_ATTR_ORDER(c, "response_curve", 0, "2");
    CLASS_ATTR_FILTER_CLIP(c, "response_curve", 0, 2);
    CLASS_ATTR_DEFAULT_SAVE(c, "response_curve", 0, "0");
    CLASS_ATTR_STYLE(c, "response_curve", 0, "enumindex");
    
    CLASS_ATTR_DOUBLE(c, "calibration", 0, t_vactrol, calibration);
    CLASS_ATTR_BASIC(c, "calibration", 0);
    CLASS_ATTR_LABEL(c, "calibration", 0, "Resistance Calibration");
    CLASS_ATTR_ACCESSORS(c, "calibration", 0, vactrol_attr_setcalibration);
    CLASS_ATTR_ORDER(c, "calibration", 0, "3");
    CLASS_ATTR_FILTER_CLIP(c, "calibration", 0.1, 2.0);
    CLASS_ATTR_DEFAULT_SAVE(c, "calibration", 0, "1.0");
    CLASS_ATTR_STYLE(c, "calibration", 0, "text");
    
    CLASS_ATTR_LONG(c, "temperature_drift", 0, t_vactrol, temperature_drift);
    CLASS_ATTR_BASIC(c, "temperature_drift", 0);
    CLASS_ATTR_LABEL(c, "temperature_drift", 0, "Temperature Drift");
    CLASS_ATTR_ACCESSORS(c, "temperature_drift", 0, vactrol_attr_settempdrift);
    CLASS_ATTR_ORDER(c, "temperature_drift", 0, "4");
    CLASS_ATTR_FILTER_CLIP(c, "temperature_drift", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "temperature_drift", 0, "0");
    CLASS_ATTR_STYLE(c, "temperature_drift", 0, "onoff");
    
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    vactrol_class = c;
}

//--------------------------------------------------------------------------

void *vactrol_new(t_symbol *s, long argc, t_atom *argv) {
    t_vactrol *x = (t_vactrol *)object_alloc(vactrol_class);
    
    if (x) {
        // Initialize DSP with 2 signal inlets: audio and CV
        dsp_setup((t_pxobject *)x, 2);  // Audio (inlet 0) and CV (inlet 1)
        
        // Create proxy inlet for bang messages (inlet 2)
        x->m_proxy_bang = proxy_new(x, 2, &x->m_inletnum);
        
        // Create audio outlet
        outlet_new(x, "signal");
        
        // Initialize parameters with defaults
        x->resistance = VACTROL_MAX_RESISTANCE;  // Start in dark state
        x->decay_time = DEFAULT_DECAY_TIME;
        x->triggered = 0;
        x->trigger_time = 0.0;
        x->pole_count = 1;                       // Default to 1-pole
        x->tube_drive = 0.7;                     // More saturation (Buchla-style)
        x->tube_character = 0.7;                 // Asymmetric character
        
        // Initialize attributes with defaults
        x->poles = 1;                            // 1-pole by default
        x->response_curve = 0;                   // Exponential response
        x->calibration = 1.0;                    // Standard calibration
        x->temperature_drift = 0;                // No temperature drift
        
        // Process attributes early for proper initialization
        long offset = attr_args_offset((short)argc, argv);
        attr_args_process(x, (short)argc, argv);
        
        // Sync pole_count with poles attribute
        x->pole_count = x->poles;
        
        // Initialize filter states
        x->filter_state1 = 0.0;
        x->filter_state2 = 0.0;
        
        // Process arguments
        if (argc >= 1 && atom_gettype(argv) == A_LONG) {
            x->pole_count = CLAMP(atom_getlong(argv), 1, 2);
        }
        if (argc >= 2 && atom_gettype(argv + 1) == A_FLOAT) {
            x->decay_time = CLAMP(atom_getfloat(argv + 1), 0.05, 0.5);  // 50ms to 500ms
        }
        if (argc >= 3 && atom_gettype(argv + 2) == A_FLOAT) {
            x->tube_drive = CLAMP(atom_getfloat(argv + 2), 0.0, 1.0);
        }
        if (argc >= 4 && atom_gettype(argv + 3) == A_FLOAT) {
            x->tube_character = CLAMP(atom_getfloat(argv + 3), 0.01, 1.0);  // Clamp just above 0
        }
    }
    
    return x;
}

void vactrol_free(t_vactrol *x) {
    dsp_free((t_pxobject *)x);
    if (x->m_proxy_bang) {
        object_free(x->m_proxy_bang);
    }
}

void vactrol_assist(t_vactrol *x, void *b, long m, long a, char *s) {
    if (m == ASSIST_INLET) {
        switch (a) {
            case 0: sprintf(s, "(signal) Audio Input"); break;
            case 1: sprintf(s, "(signal) CV Input (0-1)"); break;
            case 2: sprintf(s, "(bang) Trigger Vactrol"); break;
        }
    }
    else {
        sprintf(s, "(signal) Filtered Audio Output");
    }
}

//--------------------------------------------------------------------------
// DSP Methods
//--------------------------------------------------------------------------

void vactrol_dsp64(t_vactrol *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags) {
    x->sample_rate = samplerate;
    x->inv_sample_rate = 1.0 / samplerate;
    
    object_method(dsp64, gensym("dsp_add64"), x, vactrol_perform64, 0, NULL);
}

void vactrol_perform64(t_vactrol *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam) {
    double *audio_in = ins[0];
    double *cv_in = (numins > 1) ? ins[1] : NULL;  // CV input (0-1)
    double *out = outs[0];
    
    for (long i = 0; i < sampleframes; i++) {
        // Calculate envelope resistance (from bang trigger)
        double envelope_resistance = VACTROL_MAX_RESISTANCE;  // Default to dark
        
        if (x->triggered) {
            x->trigger_time += x->inv_sample_rate;
            envelope_resistance = vactrol_calculate_resistance(x, x->trigger_time);
        }
        
        // Calculate CV resistance (from signal input)
        double cv_resistance = VACTROL_MAX_RESISTANCE;  // Default to dark
        if (cv_in) {
            double cv_value = CLAMP(cv_in[i], 0.0, 1.0);  // Clamp CV to 0-1
            // CV 0 = dark (max resistance), CV 1 = bright (min resistance)
            cv_resistance = VACTROL_MAX_RESISTANCE - cv_value * (VACTROL_MAX_RESISTANCE - VACTROL_MIN_RESISTANCE);
        }
        
        // Use the brighter (lower resistance) of the two sources
        x->resistance = fmin(envelope_resistance, cv_resistance);
        
        // Calculate current cutoff frequency from resistance
        double cutoff = vactrol_calculate_cutoff(x->resistance);
        
        // Apply calibration to resistance before cutoff calculation
        double calibrated_resistance = x->resistance * x->calibration;
        
        // Apply temperature drift if enabled
        if (x->temperature_drift) {
            // Simulate temperature drift (±2% variation)
            static double drift_phase = 0.0;
            drift_phase += 0.0001;  // Slow drift
            double drift_factor = 1.0 + 0.02 * sin(drift_phase);
            calibrated_resistance *= drift_factor;
        }
        
        // Recalculate cutoff with calibrated resistance
        cutoff = vactrol_calculate_cutoff(calibrated_resistance);
        
        // Calculate effective cutoff (adjust for pole count)
        double effective_cutoff = cutoff;
        if (x->poles == 2) {
            // In 2-pole mode, reduce the cutoff frequency more aggressively
            effective_cutoff = cutoff * 0.8;  // 20% lower for more dramatic filtering
            effective_cutoff = CLAMP(effective_cutoff, 20.0, 20000.0);
        }
        
        // Apply filtering (1-pole or 2-pole)
        double filtered = audio_in[i];
        
        // First pole (always applied)
        filtered = vactrol_onepole_filter(filtered, effective_cutoff, &x->filter_state1, x->sample_rate);
        
        // Second pole (if 2-pole mode)
        if (x->poles == 2) {
            filtered = vactrol_onepole_filter(filtered, effective_cutoff, &x->filter_state2, x->sample_rate);
        }
        
        // Apply vactrol amplitude envelope (VCA behavior)
        // Lower resistance = higher amplitude (brighter = louder)
        double amplitude = 1.0 - ((x->resistance - VACTROL_MIN_RESISTANCE) / (VACTROL_MAX_RESISTANCE - VACTROL_MIN_RESISTANCE));
        amplitude = CLAMP(amplitude, 0.0, 1.0);
        filtered *= amplitude;
        
        // Apply tube saturation
        filtered = vactrol_tube_saturation(filtered, x->tube_drive, x->tube_character);
        
        out[i] = filtered;
    }
}

//--------------------------------------------------------------------------
// Message Handlers
//--------------------------------------------------------------------------

void vactrol_bang(t_vactrol *x) {
    // Check which inlet received the bang
    long inlet = proxy_getinlet((t_object *)x);
    
    if (inlet == 2) {  // Bang inlet (trigger)
        x->triggered = 1;
        x->trigger_time = 0.0;
        
        // Reset filter states for clean retrigger
        x->filter_state1 = 0.0;
        x->filter_state2 = 0.0;
        
        post("vactrol~: triggered");
    }
}

void vactrol_decay(t_vactrol *x, double decay) {
    x->decay_time = CLAMP(decay, 0.05, 0.5);  // 50ms to 500ms
    post("vactrol~: decay time set to %.1f ms", x->decay_time * 1000.0);
}

void vactrol_poles(t_vactrol *x, long poles) {
    x->pole_count = CLAMP(poles, 1, 2);
    x->poles = x->pole_count;  // Sync with attribute
    post("vactrol~: filter poles set to %ld", x->pole_count);
}

void vactrol_drive(t_vactrol *x, double drive) {
    x->tube_drive = CLAMP(drive, 0.0, 1.0);
    post("vactrol~: tube drive set to %.2f", x->tube_drive);
}

void vactrol_character(t_vactrol *x, double character) {
    x->tube_character = CLAMP(character, 0.01, 1.0);  // Clamp just above 0 to prevent division by zero
    post("vactrol~: tube character set to %.2f", x->tube_character);
}

//--------------------------------------------------------------------------
// DSP Utility Functions
//--------------------------------------------------------------------------

double vactrol_calculate_resistance(t_vactrol *x, double time_elapsed) {
    // Calculate decay factor based on response curve
    double decay_factor;
    
    if (x->response_curve == 0) {
        // Exponential (default vactrol behavior)
        decay_factor = exp(-time_elapsed / x->decay_time);
    } else if (x->response_curve == 1) {
        // Linear
        decay_factor = fmax(0.0, 1.0 - (time_elapsed / x->decay_time));
    } else {
        // Logarithmic
        double t_norm = time_elapsed / x->decay_time;
        if (t_norm >= 1.0) {
            decay_factor = 0.0;
        } else {
            decay_factor = 1.0 - log(1.0 + t_norm * 9.0) / log(10.0);  // Log base 10
        }
    }
    
    // Calculate resistance: Start bright (low resistance), decay to dark (high resistance)
    double resistance = VACTROL_MAX_RESISTANCE - 
                       (VACTROL_MAX_RESISTANCE - VACTROL_MIN_RESISTANCE) * decay_factor;
    
    return resistance;
}

double vactrol_calculate_cutoff(double resistance) {
    // fc = 1/(2π * R * C)
    double cutoff = 1.0 / (2.0 * PI * resistance * VACTROL_CAPACITANCE);
    
    // Clamp to reasonable audio range
    return CLAMP(cutoff, 20.0, 20000.0);
}

double vactrol_onepole_filter(double input, double cutoff, double *state, double sample_rate) {
    // One-pole low-pass filter
    // y[n] = y[n-1] + α * (x[n] - y[n-1])
    // where α = 1 - e^(-2π * fc / fs)
    
    double alpha = 1.0 - exp(-2.0 * PI * cutoff / sample_rate);
    alpha = CLAMP(alpha, 0.0, 1.0);
    
    *state = *state + alpha * (input - *state);
    return *state;
}

double vactrol_tube_saturation(double input, double drive, double character) {
    if (drive <= 0.0) {
        return input;  // No saturation
    }
    
    // Scale drive (0-1 maps to 1-10 for tanh input scaling)
    double scaled_drive = 1.0 + drive * 9.0;
    
    // Asymmetric character: different drive for positive/negative
    double positive_drive = scaled_drive;
    double negative_drive = scaled_drive * character;  // character 0-1 affects negative peaks
    
    double output;
    if (input >= 0.0) {
        output = tanh(input * positive_drive) / positive_drive;
    } else {
        output = tanh(input * negative_drive) / negative_drive;
    }
    
    // Apply overall drive scaling to maintain reasonable levels
    return output * (1.0 - drive * 0.3);  // Slight level compensation
}

//--------------------------------------------------------------------------
// Attribute Setters
//--------------------------------------------------------------------------

t_max_err vactrol_attr_setpoles(t_vactrol *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        long new_poles = CLAMP(atom_getlong(argv), 1, 2);
        x->poles = new_poles;
        x->pole_count = new_poles;  // Sync with internal variable
    }
    return 0;
}

t_max_err vactrol_attr_setresponse(t_vactrol *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->response_curve = CLAMP(atom_getlong(argv), 0, 2);
    }
    return 0;
}

t_max_err vactrol_attr_setcalibration(t_vactrol *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->calibration = CLAMP(atom_getfloat(argv), 0.1, 2.0);
    }
    return 0;
}

t_max_err vactrol_attr_settempdrift(t_vactrol *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->temperature_drift = atom_getlong(argv) ? 1 : 0;
    }
    return 0;
}