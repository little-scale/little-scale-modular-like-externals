/**
    @file
    slewenv~: A function generator envelope that mimics Make Noise Maths module
    Provides configurable rise/fall times with exponential-to-logarithmic curve shaping
    @ingroup examples
*/

#include "ext.h"        // standard Max include, always required
#include "ext_obex.h"   // required for "new" style objects
#include "z_dsp.h"      // required for MSP objects
#include <math.h>       // for curve shaping calculations

// Utility macros (CLAMP is provided by Max SDK)
#define MIN_TIME_MS 1.0
#define MAX_TIME_MS (20.0 * 60.0 * 1000.0) // 20 minutes in milliseconds

// struct to represent the object's state
typedef struct _slewenv
{
    t_pxobject ob;                  // the object itself (t_pxobject for MSP)
    
    // lores~ pattern: signal connection status
    short trigger_has_signal;       // 1 if trigger inlet has signal connection
    short loop_has_signal;          // 1 if loop inlet has signal connection
    short rise_has_signal;          // 1 if rise inlet has signal connection
    short fall_has_signal;          // 1 if fall inlet has signal connection
    short linearity_has_signal;     // 1 if linearity inlet has signal connection
    
    // Float parameters (stored values for when no signal connected)
    double loop_float;             // Float inlet value for loop mode
    double rise_float;             // Float inlet value for rise time
    double fall_float;             // Float inlet value for fall time
    double linearity_float;        // Float inlet value for linearity
    
    // Integrator state (like Make Noise Maths)
    double current_output;          // Current integrator output value (0.0 to 1.0)
    int state;                     // 0=idle, 1=rising, 2=falling
    int loop_enabled;              // Looping enabled/disabled
    
    // Parameters
    double trigger_amplitude;      // Scaling factor from trigger input
    double rise_time;              // Rise time (0-1 normalized)
    double fall_time;              // Fall time (0-1 normalized)  
    double linearity;              // Shape control (-1 to 1)
    
    // Sample rate and timing
    double sample_rate;            // Current sample rate
    double samples_per_ms;         // For time calculations
    
    // Previous trigger state for edge detection
    double prev_trigger;           // For detecting new triggers
    
} t_slewenv;

// method prototypes
void* slewenv_new(t_symbol* s, long argc, t_atom* argv);
void slewenv_free(t_slewenv* x);
void slewenv_assist(t_slewenv* x, void* b, long m, long a, char* s);
void slewenv_float(t_slewenv* x, double f);
void slewenv_int(t_slewenv* x, long n);
void slewenv_dsp64(t_slewenv* x, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags);
void slewenv_perform64(t_slewenv* x, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam);

// attribute setter prototypes
t_max_err slewenv_attr_setlooping(t_slewenv* x, void* attr, long argc, t_atom* argv);

// integrator computation functions
double apply_curve_shaping(double linear_value, double linearity);
void trigger_integrator(t_slewenv* x, double amplitude);
void update_integrator(t_slewenv* x, double rise_time, double fall_time, double linearity);

// global class pointer variable
static t_class* slewenv_class = NULL;

//***********************************************************************************************

C74_EXPORT void ext_main(void* r)
{
    // object initialization, using custom freemethod for proxy cleanup
    t_class* c = class_new("slewenv~", (method)slewenv_new, (method)slewenv_free, 
                          (long)sizeof(t_slewenv), 0L, A_GIMME, 0);

    class_addmethod(c, (method)slewenv_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)slewenv_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)slewenv_float, "float", A_FLOAT, 0);
    class_addmethod(c, (method)slewenv_int, "int", A_LONG, 0);

    // Attribute: looping (0 or 1) - enhanced with Max 9 metadata
    CLASS_ATTR_LONG(c, "looping", 0, t_slewenv, loop_enabled);
    CLASS_ATTR_BASIC(c, "looping", 0);
    CLASS_ATTR_ALIAS(c, "looping", "loop");
    CLASS_ATTR_LABEL(c, "looping", 0, "Loop Mode");
    CLASS_ATTR_ACCESSORS(c, "looping", 0, slewenv_attr_setlooping);
    CLASS_ATTR_ORDER(c, "looping", 0, "1");
    CLASS_ATTR_FILTER_CLIP(c, "looping", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "looping", 0, "0");
    CLASS_ATTR_STYLE(c, "looping", 0, "onoff");

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    slewenv_class = c;
}

void* slewenv_new(t_symbol* s, long argc, t_atom* argv)
{
    t_slewenv* x = (t_slewenv*)object_alloc(slewenv_class);
    long offset;
    
    // CRITICAL: Process attributes EARLY like lores~
    offset = attr_args_offset((short)argc, argv);
    dsp_setup((t_pxobject*)x, 5);

    // Process creation arguments first (before attributes)
    if (offset >= 1 && atom_gettype(argv) == A_FLOAT) {
        x->rise_time = x->rise_float = CLAMP(atom_getfloat(argv), 0.001, 1.0);
    } else {
        x->rise_time = x->rise_float = 0.1;  // Default
    }
    if (offset >= 2 && atom_gettype(argv + 1) == A_FLOAT) {
        x->fall_time = x->fall_float = CLAMP(atom_getfloat(argv + 1), 0.001, 1.0);
    } else {
        x->fall_time = x->fall_float = 0.1;  // Default
    }
    if (offset >= 3 && atom_gettype(argv + 2) == A_FLOAT) {
        x->linearity = x->linearity_float = CLAMP(atom_getfloat(argv + 2), -1.0, 1.0);
    } else {
        x->linearity = x->linearity_float = 0.0;  // Default
    }
    
    // Initialize other parameters
    x->loop_enabled = 0;
    x->loop_float = 0.0;
    
    // CRITICAL: Process attributes EARLY like lores~
    attr_args_process(x, (short)argc, argv);
    
    // Initialize integrator state
    x->current_output = 0.0;
    x->state = 0;  // 0=idle, 1=rising, 2=falling
    x->trigger_amplitude = 1.0;
    x->prev_trigger = 0.0;
    
    // Initialize connection status (assume no signals connected initially)
    x->trigger_has_signal = 0;
    x->loop_has_signal = 0;
    x->rise_has_signal = 0;
    x->fall_has_signal = 0;
    x->linearity_has_signal = 0;
    
    // Initialize sample rate
    x->sample_rate = 44100.0;   // Default, will be updated in dsp64
    x->samples_per_ms = x->sample_rate / 1000.0;
    
    // Create signal outlet
    outlet_new(x, "signal");

    return x;
}

void slewenv_assist(t_slewenv* x, void* b, long m, long a, char* s)
{
    if (m == ASSIST_INLET) {
        switch (a) {
            case 0: sprintf(s, "(float) trigger input - triggers envelope, scales amplitude"); break;
            case 1: sprintf(s, "(float/signal) loop mode - non-zero enables looping"); break;
            case 2: sprintf(s, "(float/signal) rise time - normalized 0-1"); break;
            case 3: sprintf(s, "(float/signal) fall time - normalized 0-1"); break;
            case 4: sprintf(s, "(float/signal) linearity - -1=exponential, 0=linear, 1=logarithmic"); break;
        }
    }
    else {    // outlet
        sprintf(s, "(signal) envelope output - args: rise fall linearity");
    }
}

void slewenv_float(t_slewenv* x, double f)
{
    // Determine which inlet received the float message
    long inlet_number = proxy_getinlet((t_object*)x);
    
    switch (inlet_number) {
        case 0: // Inlet 1: Trigger - starts rise phase
            if (f > 0.0 && x->prev_trigger <= 0.0) {
                trigger_integrator(x, f);
            }
            x->prev_trigger = f;
            break;
            
        case 1: // Inlet 2: Loop mode
            x->loop_float = f;
            x->loop_enabled = (f != 0.0);
            object_attr_touch((t_object*)x, gensym("looping"));
            break;
            
        case 2: // Inlet 3: Rise time
            x->rise_float = CLAMP(f, 0.001, 1.0);
            x->rise_time = x->rise_float;
            break;
            
        case 3: // Inlet 4: Fall time
            x->fall_float = CLAMP(f, 0.001, 1.0);
            x->fall_time = x->fall_float;
            break;
            
        case 4: // Inlet 5: Linearity
            x->linearity_float = CLAMP(f, -1.0, 1.0);
            x->linearity = x->linearity_float;
            break;
    }
}

void slewenv_int(t_slewenv* x, long n)
{
    // Convert int to float and handle the same way
    slewenv_float(x, (double)n);
}

void slewenv_free(t_slewenv* x)
{
    // Standard DSP cleanup for MSP objects
    dsp_free((t_pxobject*)x);
}

// registers a function for the signal chain in Max
void slewenv_dsp64(t_slewenv* x, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
    x->sample_rate = samplerate;
    x->samples_per_ms = samplerate / 1000.0;
    
    // lores~ pattern: store signal connection status
    x->trigger_has_signal = count[0];
    x->loop_has_signal = count[1];
    x->rise_has_signal = count[2];
    x->fall_has_signal = count[3];
    x->linearity_has_signal = count[4];
    
    
    object_method(dsp64, gensym("dsp_add64"), x, slewenv_perform64, 0, NULL);
}

// this is the 64-bit perform method for audio vectors
void slewenv_perform64(t_slewenv* x, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
    // Input buffers for all 5 inlets
    double *trigger_in = ins[0];
    double *loop_in = ins[1];
    double *rise_in = ins[2];
    double *fall_in = ins[3];
    double *linearity_in = ins[4];
    
    // Output signal
    double *envelope_out = outs[0];
    
    // Process each sample
    for (int i = 0; i < sampleframes; i++) {
        // lores~ pattern: choose signal vs float for each inlet
        double trigger = x->trigger_has_signal ? trigger_in[i] : x->prev_trigger;
        double loop_mode = x->loop_has_signal ? loop_in[i] : x->loop_float;
        double rise_time = x->rise_has_signal ? rise_in[i] : x->rise_float;
        double fall_time = x->fall_has_signal ? fall_in[i] : x->fall_float;
        double linearity = x->linearity_has_signal ? linearity_in[i] : x->linearity_float;
        
        // Detect trigger edge (rising edge from <= 0 to > 0) - only for inlet 1
        if (trigger > 0.0 && x->prev_trigger <= 0.0) {
            trigger_integrator(x, trigger);
        }
        x->prev_trigger = trigger;
        
        // Update parameters with bounds checking
        double current_rise_time = CLAMP(rise_time, 0.001, 1.0);
        double current_fall_time = CLAMP(fall_time, 0.001, 1.0);
        double current_linearity = CLAMP(linearity, -1.0, 1.0);
        x->loop_enabled = (loop_mode != 0.0);
        
        // Store for next iteration
        x->rise_time = current_rise_time;
        x->fall_time = current_fall_time;
        x->linearity = current_linearity;
        
        // Update integrator using CURRENT parameter values
        update_integrator(x, current_rise_time, current_fall_time, current_linearity);
        
        // Output scaled envelope
        envelope_out[i] = x->current_output * x->trigger_amplitude;
    }
}

//***********************************************************************************************
// Make Noise Maths-style Integrator Implementation
//***********************************************************************************************

void trigger_integrator(t_slewenv* x, double amplitude)
{
    x->state = 1; // Start rising
    x->trigger_amplitude = CLAMP(amplitude, 0.0, 10.0);
}

void update_integrator(t_slewenv* x, double rise_time, double fall_time, double linearity)
{
    if (x->state == 0) return; // Idle, no integration
    
    // Convert rise/fall times to rates per sample - MUCH simpler approach
    // Let's make 0.1 = 1 second, 1.0 = 10 seconds for testing
    double rise_seconds = rise_time * 10.0; // 0.1 -> 1 sec, 1.0 -> 10 sec
    double fall_seconds = fall_time * 10.0; // 0.1 -> 1 sec, 1.0 -> 10 sec
    
    // Ensure minimum time
    if (rise_seconds < 0.01) rise_seconds = 0.01; // Minimum 10ms
    if (fall_seconds < 0.01) fall_seconds = 0.01; // Minimum 10ms
    
    double rise_rate = 1.0 / (rise_seconds * x->sample_rate); // Rate per sample to reach 1.0
    double fall_rate = 1.0 / (fall_seconds * x->sample_rate); // Rate per sample to reach 0.0
    
    
    if (x->state == 1) { // Rising
        double increment = rise_rate;
        
        // Apply curve shaping to the increment
        if (linearity < -0.001) {
            // Exponential: MUCH faster at start, MUCH slower near top
            double progress = x->current_output;
            // Use actual exponential function for dramatic curve
            double exp_amount = -linearity * 5.0; // -1.0 linearity → 5.0 exponent
            double curve_multiplier = exp(-exp_amount * progress); // Exponential decay
            increment *= curve_multiplier * (1.0 + (-linearity * 3.0)); // Extra boost
        } else if (linearity > 0.001) {
            // Logarithmic: slower at start, faster near top  
            double progress = x->current_output;
            increment *= (0.1 + progress * 0.9) * (1.0 + linearity * 2.0);
        }
        
        x->current_output += increment;
        
        // Check if we've reached the top
        if (x->current_output >= 1.0) {
            x->current_output = 1.0;
            x->state = 2; // Always start falling when we reach the top
        }
        
    } else if (x->state == 2) { // Falling
        double decrement = fall_rate;
        
        // Apply curve shaping to the decrement
        if (linearity < -0.001) {
            // Exponential: MUCH faster at start, MUCH slower near bottom
            double progress = 1.0 - x->current_output; // Distance from top (0.0 = top, 1.0 = bottom)
            // Use actual exponential function for dramatic curve
            double exp_amount = -linearity * 5.0; // -1.0 linearity → 5.0 exponent
            double curve_multiplier = exp(-exp_amount * progress); // Exponential decay
            decrement *= curve_multiplier * (1.0 + (-linearity * 3.0)); // Extra boost
        } else if (linearity > 0.001) {
            // Logarithmic: slower at start, faster near bottom
            double progress = x->current_output; // How far from bottom
            decrement *= (0.1 + (1.0 - progress) * 0.9) * (1.0 + linearity * 2.0);
        }
        
        x->current_output -= decrement;
        
        // Check if we've reached the bottom
        if (x->current_output <= 0.0) {
            x->current_output = 0.0;
            if (x->loop_enabled) {
                x->state = 1; // Start rising again (loop mode)
            } else {
                x->state = 0; // Stop at bottom (one-shot mode)
            }
        }
    }
}

//***********************************************************************************************
// Attribute Setters
//***********************************************************************************************

t_max_err slewenv_attr_setlooping(t_slewenv* x, void* attr, long argc, t_atom* argv)
{
    if (argc && argv) {
        long loop_value = atom_getlong(argv);
        long old_loop_enabled = x->loop_enabled;
        
        x->loop_enabled = (loop_value != 0);
        x->loop_float = (double)x->loop_enabled;
        
        // Auto-trigger when enabling looping (0 → 1 transition)
        if (!old_loop_enabled && x->loop_enabled) {
            trigger_integrator(x, 1.0);  // Trigger with amplitude 1.0
        }
    }
    return 0;
}