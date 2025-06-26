/**
 * physicslfo~ - Physics-based LFO with bounce and elastic curves for Max/MSP
 * 
 * This external generates LFOs using physics simulations like bouncing balls
 * and spring systems, creating organic and musical modulation curves.
 * 
 * Features:
 * - 6 physics simulation types (bounce, elastic, spin, overshoot, multi-bounce, wobble)
 * - lores~ pattern: 4 signal inlets accept both signals and floats
 * - Looping mode (default) and envelope mode for different musical applications
 * - Bang trigger support for physics reset and envelope triggering
 * - Real-time parameter control with proper state management
 * - Universal binary support (Intel + Apple Silicon)
 * 
 * Inlets:
 *   1. Frequency/Trigger (signal/float/bang) - Hz frequency or bang to reset/trigger
 *   2. LFO type (signal/float/int, 0-5) - physics simulation type
 *   3. Physics parameter (signal/float, 0.0-1.0) - bounce height/spring tension
 *   4. Damping (signal/float, 0.0-1.0) - energy loss rate
 * 
 * Messages:
 *   looping 1 - Enable looping mode (default) - continuous physics simulation
 *   looping 0 - Enable envelope mode - one-shot physics triggered by bang
 *   phase <float> - Set phase position (0.0-1.0) in looping mode
 * 
 * Outlets:
 *   1. LFO output (signal, 0.0 to 1.0) - natural physics range
 * 
 * LFO Types:
 *   0: Bounce - param: bounce curve (0=droopy/slow, 1=sharp/fast)
 *   1: Damped decay - param: vibration frequency inside decay envelope
 *   2: Bounce with enhanced spin - param: complex spin rate and intensity  
 *   3: Elastic overshoot (settles to equilibrium) - param: overshoot intensity
 *   4: Multi-bounce (comes to complete rest) - param: number of bounces per cycle
 *   5: Wobble (settles to equilibrium) - param: frequency spread
 */

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include <math.h>

#define PI 3.14159265358979323846
#define MAX_BOUNCES 8

typedef struct _physicslfo {
    t_pxobject ob;              // MSP object header
    
    // Core oscillator state
    double phase;               // Current phase (0.0 to 1.0)
    double sr;                  // Sample rate
    double sr_inv;              // 1.0 / sample rate
    
    // Parameter storage (lores~ pattern)
    double freq_float;          // Frequency when no signal connected
    double type_float;          // LFO type when no signal connected
    double physics_float;       // Physics parameter when no signal connected
    double damping_float;       // Damping when no signal connected
    
    // Signal connection status (lores~ pattern)
    short freq_has_signal;      // 1 if frequency inlet has signal connection
    short type_has_signal;      // 1 if type inlet has signal connection
    short physics_has_signal;   // 1 if physics inlet has signal connection
    short damping_has_signal;   // 1 if damping inlet has signal connection
    
    // Physics simulation state
    double velocity;            // Current velocity
    double acceleration;        // Current acceleration
    double energy;              // Current energy level (0.0-1.0)
    double bounce_count;        // Number of bounces occurred
    double last_value;          // Previous output value
    double spin_phase;          // Phase for spin calculations
    
    // Mode control
    long looping_mode;          // 1 = looping (default), 0 = envelope mode
    long envelope_active;       // 1 when envelope is running, 0 when finished
    
    // Attribute parameters
    long looping;               // 0=envelope, 1=looping (default 1)
    long physics_type;          // 0-5 physics simulation type (default 0)
    double gravity;             // 0.0-2.0 gravity/physics strength (default 1.0)
    double air_resistance;      // 0.0-1.0 air resistance/damping (default 0.1)
    
} t_physicslfo;

// Function prototypes
void *physicslfo_new(t_symbol *s, long argc, t_atom *argv);
void physicslfo_free(t_physicslfo *x);
void physicslfo_dsp64(t_physicslfo *x, t_object *dsp64, short *count, double samplerate, 
                      long maxvectorsize, long flags);
void physicslfo_perform64(t_physicslfo *x, t_object *dsp64, double **ins, long numins, 
                          double **outs, long numouts, long sampleframes, long flags, void *userparam);
void physicslfo_float(t_physicslfo *x, double f);
void physicslfo_int(t_physicslfo *x, long n);
void physicslfo_bang(t_physicslfo *x);
void physicslfo_looping(t_physicslfo *x, long n);
void physicslfo_phase(t_physicslfo *x, double f);
void physicslfo_assist(t_physicslfo *x, void *b, long m, long a, char *s);

// Physics simulation functions
double simulate_bounce(t_physicslfo *x, double t, double param, double damping);
double simulate_elastic(t_physicslfo *x, double t, double tension, double damping);
double simulate_bounce_spin(t_physicslfo *x, double t, double param, double damping);
double simulate_elastic_overshoot(t_physicslfo *x, double t, double tension, double damping);
double simulate_multibounce(t_physicslfo *x, double t, double param, double damping);
double simulate_wobble(t_physicslfo *x, double t, double tension, double damping);

// Helper functions
void reset_physics_state(t_physicslfo *x);
void print_physics_info(t_physicslfo *x, long type);

// Attribute setter prototypes
t_max_err physicslfo_attr_setlooping(t_physicslfo *x, void *attr, long argc, t_atom *argv);
t_max_err physicslfo_attr_setphysicstype(t_physicslfo *x, void *attr, long argc, t_atom *argv);
t_max_err physicslfo_attr_setgravity(t_physicslfo *x, void *attr, long argc, t_atom *argv);
t_max_err physicslfo_attr_setairresistance(t_physicslfo *x, void *attr, long argc, t_atom *argv);

// Class pointer
static t_class *physicslfo_class = NULL;

//----------------------------------------------------------------------------------------------

void ext_main(void *r) {
    t_class *c;
    
    c = class_new("physicslfo~", (method)physicslfo_new, (method)physicslfo_free,
                  sizeof(t_physicslfo), 0L, A_GIMME, 0);
    
    class_addmethod(c, (method)physicslfo_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)physicslfo_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)physicslfo_float, "float", A_FLOAT, 0);
    class_addmethod(c, (method)physicslfo_int, "int", A_LONG, 0);
    class_addmethod(c, (method)physicslfo_bang, "bang", A_CANT, 0);
    class_addmethod(c, (method)physicslfo_looping, "looping", A_LONG, 0);
    class_addmethod(c, (method)physicslfo_phase, "phase", A_FLOAT, 0);
    
    // Attributes with Max 9 autocomplete support
    CLASS_ATTR_LONG(c, "looping", 0, t_physicslfo, looping);
    CLASS_ATTR_BASIC(c, "looping", 0);
    CLASS_ATTR_LABEL(c, "looping", 0, "Looping Mode");
    CLASS_ATTR_ACCESSORS(c, "looping", 0, physicslfo_attr_setlooping);
    CLASS_ATTR_ORDER(c, "looping", 0, "1");
    CLASS_ATTR_FILTER_CLIP(c, "looping", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "looping", 0, "1");
    CLASS_ATTR_STYLE(c, "looping", 0, "onoff");
    
    CLASS_ATTR_LONG(c, "physics_type", 0, t_physicslfo, physics_type);
    CLASS_ATTR_BASIC(c, "physics_type", 0);
    CLASS_ATTR_LABEL(c, "physics_type", 0, "Physics Type");
    CLASS_ATTR_ACCESSORS(c, "physics_type", 0, physicslfo_attr_setphysicstype);
    CLASS_ATTR_ORDER(c, "physics_type", 0, "2");
    CLASS_ATTR_FILTER_CLIP(c, "physics_type", 0, 5);
    CLASS_ATTR_DEFAULT_SAVE(c, "physics_type", 0, "0");
    CLASS_ATTR_STYLE(c, "physics_type", 0, "enumindex");
    
    CLASS_ATTR_DOUBLE(c, "gravity", 0, t_physicslfo, gravity);
    CLASS_ATTR_BASIC(c, "gravity", 0);
    CLASS_ATTR_LABEL(c, "gravity", 0, "Gravity/Physics Strength");
    CLASS_ATTR_ACCESSORS(c, "gravity", 0, physicslfo_attr_setgravity);
    CLASS_ATTR_ORDER(c, "gravity", 0, "3");
    CLASS_ATTR_FILTER_CLIP(c, "gravity", 0.0, 2.0);
    CLASS_ATTR_DEFAULT_SAVE(c, "gravity", 0, "1.0");
    CLASS_ATTR_STYLE(c, "gravity", 0, "text");
    
    CLASS_ATTR_DOUBLE(c, "air_resistance", 0, t_physicslfo, air_resistance);
    CLASS_ATTR_BASIC(c, "air_resistance", 0);
    CLASS_ATTR_LABEL(c, "air_resistance", 0, "Air Resistance/Damping");
    CLASS_ATTR_ACCESSORS(c, "air_resistance", 0, physicslfo_attr_setairresistance);
    CLASS_ATTR_ORDER(c, "air_resistance", 0, "4");
    CLASS_ATTR_FILTER_CLIP(c, "air_resistance", 0.0, 1.0);
    CLASS_ATTR_DEFAULT_SAVE(c, "air_resistance", 0, "0.1");
    CLASS_ATTR_STYLE(c, "air_resistance", 0, "text");
    
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    physicslfo_class = c;
}

//----------------------------------------------------------------------------------------------

void *physicslfo_new(t_symbol *s, long argc, t_atom *argv) {
    t_physicslfo *x = (t_physicslfo *)object_alloc(physicslfo_class);
    
    if (x) {
        // lores~ pattern: 4 signal inlets (freq, type, physics, damping)
        dsp_setup((t_pxobject *)x, 4);
        outlet_new(x, "signal");
        
        // Initialize core state
        x->phase = 0.0;
        x->sr = sys_getsr();
        x->sr_inv = 1.0 / x->sr;
        
        // Initialize parameter defaults
        x->freq_float = 1.0;        // 1 Hz default
        x->type_float = 0.0;        // Bounce type default
        x->physics_float = 0.5;     // Medium physics parameter
        x->damping_float = 0.1;     // Light damping
        
        // Initialize connection status (assume no signals connected initially)
        x->freq_has_signal = 0;
        x->type_has_signal = 0;
        x->physics_has_signal = 0;
        x->damping_has_signal = 0;
        
        // Initialize physics state
        reset_physics_state(x);
        
        // Initialize attributes with defaults
        x->looping = 1;             // Looping mode by default
        x->physics_type = 0;        // Bounce physics by default
        x->gravity = 1.0;           // Normal gravity
        x->air_resistance = 0.1;    // Light air resistance
        
        // Initialize mode control (looping mode is default)
        x->looping_mode = 1;        // 1 = looping, 0 = envelope
        x->envelope_active = 0;     // Envelope not active initially
        
        // Process creation arguments: [type] [physics] [damping]
        if (argc >= 1 && atom_gettype(argv) == A_LONG) {
            x->type_float = CLAMP(atom_getlong(argv), 0, 5);
        }
        if (argc >= 2 && (atom_gettype(argv + 1) == A_FLOAT || atom_gettype(argv + 1) == A_LONG)) {
            x->physics_float = CLAMP(atom_getfloat(argv + 1), 0.0, 1.0);
        }
        if (argc >= 3 && (atom_gettype(argv + 2) == A_FLOAT || atom_gettype(argv + 2) == A_LONG)) {
            x->damping_float = CLAMP(atom_getfloat(argv + 2), 0.0, 1.0);
        }
        
        // Print initial physics type info
        print_physics_info(x, (long)x->type_float);
        
        // Process attributes early for proper initialization
        long offset = attr_args_offset((short)argc, argv);
        attr_args_process(x, argc, argv);
        
        // Sync attributes with internal state
        x->looping_mode = x->looping;
        x->type_float = x->physics_type;
        x->physics_float = x->gravity;
        x->damping_float = x->air_resistance;
    }
    
    return x;
}

//----------------------------------------------------------------------------------------------

void physicslfo_free(t_physicslfo *x) {
    dsp_free((t_pxobject *)x);
}

//----------------------------------------------------------------------------------------------

void physicslfo_dsp64(t_physicslfo *x, t_object *dsp64, short *count, double samplerate, 
                      long maxvectorsize, long flags) {
    x->sr = samplerate;
    x->sr_inv = 1.0 / samplerate;
    
    // lores~ pattern: store signal connection status
    x->freq_has_signal = count[0];
    x->type_has_signal = count[1];
    x->physics_has_signal = count[2];
    x->damping_has_signal = count[3];
    
    object_method(dsp64, gensym("dsp_add64"), x, physicslfo_perform64, 0, NULL);
}

//----------------------------------------------------------------------------------------------

void physicslfo_perform64(t_physicslfo *x, t_object *dsp64, double **ins, long numins, 
                          double **outs, long numouts, long sampleframes, long flags, void *userparam) {
    // Input buffers for all 4 inlets
    double *freq_in = ins[0];
    double *type_in = ins[1];
    double *physics_in = ins[2];
    double *damping_in = ins[3];
    
    // Output buffer
    double *out = outs[0];
    
    long n = sampleframes;
    double phase = x->phase;
    double sr_inv = x->sr_inv;
    
    while (n--) {
        // lores~ pattern: choose signal vs float for each inlet
        double freq = x->freq_has_signal ? *freq_in++ : x->freq_float;
        double type_val = x->type_has_signal ? *type_in++ : x->physics_type;  // Use attribute
        double physics_param = x->physics_has_signal ? *physics_in++ : x->gravity;  // Use attribute
        double damping = x->damping_has_signal ? *damping_in++ : x->air_resistance;  // Use attribute
        
        // Clamp parameters
        freq = CLAMP(freq, 0.0, 1000.0);  // 0-1000 Hz
        long type = (long)CLAMP(type_val, 0.0, 5.0);  // 0-5 physics types
        physics_param = CLAMP(physics_param, 0.0, 2.0);  // Extended range for gravity
        damping = CLAMP(damping, 0.0, 1.0);
        
        // Handle mode-specific phase updates
        double value = 0.0;
        
        if (x->looping) {
            // Looping mode: continuous phase accumulation
            phase += freq * sr_inv;
            
            // Wrap phase and reset physics state when cycle completes
            if (phase >= 1.0) {
                phase -= 1.0;
                reset_physics_state(x);
            }
            while (phase < 0.0) phase += 1.0;
            
        } else {
            // Envelope mode: let physics play out naturally (no artificial cutoff)
            if (x->envelope_active) {
                phase += freq * sr_inv;
                
                // Mark first cycle as complete but continue physics simulation
                if (phase >= 1.0) {
                    x->envelope_active = 0;  // Mark first cycle complete but keep going
                }
            } else {
                // Continue physics simulation beyond first cycle - let natural decay occur
                phase += freq * sr_inv;
            }
        }
        
        // Apply physics simulation based on type
        switch (type) {
            case 0:  // Bounce
                value = simulate_bounce(x, phase, physics_param, damping);
                break;
            case 1:  // Elastic
                value = simulate_elastic(x, phase, physics_param, damping);
                break;
            case 2:  // Bounce with spin
                value = simulate_bounce_spin(x, phase, physics_param, damping);
                break;
            case 3:  // Elastic overshoot
                value = simulate_elastic_overshoot(x, phase, physics_param, damping);
                break;
            case 4:  // Multi-bounce
                value = simulate_multibounce(x, phase, physics_param, damping);
                break;
            case 5:  // Wobble
                value = simulate_wobble(x, phase, physics_param, damping);
                break;
        }
        
        // Store for next sample
        x->last_value = value;
        
        // Output unipolar range (0 to 1) - natural for physics simulations
        *out++ = value;
    }
    
    x->phase = phase;
}

//----------------------------------------------------------------------------------------------

void physicslfo_float(t_physicslfo *x, double f) {
    // lores~ pattern: proxy_getinlet works on signal inlets for float routing
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 0: // Frequency inlet
            x->freq_float = CLAMP(f, 0.0, 1000.0);
            break;
        case 1: // LFO type inlet
            {
                long new_type = (long)CLAMP(f, 0.0, 5.0);
                x->type_float = (double)new_type;
                print_physics_info(x, new_type);
            }
            break;
        case 2: // Physics parameter inlet
            x->physics_float = CLAMP(f, 0.0, 1.0);
            break;
        case 3: // Damping inlet
            x->damping_float = CLAMP(f, 0.0, 1.0);
            break;
    }
}

//----------------------------------------------------------------------------------------------

void physicslfo_int(t_physicslfo *x, long n) {
    // lores~ pattern: proxy_getinlet works on signal inlets for int routing
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 0: // Frequency inlet - convert int to float
            x->freq_float = CLAMP((double)n, 0.0, 1000.0);
            break;
        case 1: // LFO type inlet - direct int handling
            {
                long new_type = CLAMP(n, 0, 5);
                x->type_float = (double)new_type;
                print_physics_info(x, new_type);
            }
            break;
        case 2: // Physics parameter inlet - convert int to float
            x->physics_float = CLAMP((double)n, 0.0, 1.0);
            break;
        case 3: // Damping inlet - convert int to float
            x->damping_float = CLAMP((double)n, 0.0, 1.0);
            break;
    }
}

//----------------------------------------------------------------------------------------------

void physicslfo_bang(t_physicslfo *x) {
    // lores~ pattern: proxy_getinlet works on signal inlets for message routing
    long inlet = proxy_getinlet((t_object *)x);
    
    if (inlet == 0) {  // First inlet - mode-specific behavior
        if (x->looping) {
            // Looping mode: reset phase and physics state
            x->phase = 0.0;
            reset_physics_state(x);
        } else {
            // Envelope mode: trigger new envelope cycle
            x->phase = 0.0;
            reset_physics_state(x);
            x->envelope_active = 1;  // Start envelope
        }
    }
}

//----------------------------------------------------------------------------------------------

void physicslfo_looping(t_physicslfo *x, long n) {
    x->looping = n ? 1 : 0;  // Convert to boolean
    x->looping_mode = x->looping;  // Sync with old variable
    
    if (!x->looping) {
        // Switched to envelope mode - stop any current envelope
        x->envelope_active = 0;
    }
}

//----------------------------------------------------------------------------------------------

void physicslfo_phase(t_physicslfo *x, double f) {
    if (x->looping) {
        // Only works in looping mode
        x->phase = CLAMP(f, 0.0, 1.0);
        reset_physics_state(x);  // Reset physics state for new phase position
    } else {
        post("physicslfo~: phase message only works in looping mode (send 'looping 1' first)");
    }
}

//----------------------------------------------------------------------------------------------

void physicslfo_assist(t_physicslfo *x, void *b, long m, long a, char *s) {
    if (m == ASSIST_INLET) {
        switch (a) {
            case 0:
                sprintf(s, "(signal/float/bang) Frequency in Hz, bang to reset/trigger");
                break;
            case 1:
                sprintf(s, "(signal/float/int) Physics type (0-5)");
                break;
            case 2:
                sprintf(s, "(signal/float) Physics parameter (0-1)");
                break;
            case 3:
                sprintf(s, "(signal/float) Damping (0-1)");
                break;
        }
    } else {  // ASSIST_OUTLET
        sprintf(s, "(signal) Physics LFO output (0 to 1) - natural physics range");
    }
}

//----------------------------------------------------------------------------------------------
// Helper Functions
//----------------------------------------------------------------------------------------------

void reset_physics_state(t_physicslfo *x) {
    x->velocity = 0.0;
    x->acceleration = 0.0;
    x->energy = 1.0;           // Full energy at start
    x->bounce_count = 0.0;
    x->spin_phase = 0.0;
}

void print_physics_info(t_physicslfo *x, long type) {
    switch (type) {
        case 0:
            post("physicslfo~: Type 0 - BOUNCE | Param: bounce curve (0=droopy/slow, 1=sharp/fast) | Damping: energy loss rate");
            break;
        case 1:
            post("physicslfo~: Type 1 - DAMPED DECAY | Param: vibration frequency (0=slow, 1=fast) | Damping: decay rate (0=long ring, 1=quick stop)");
            break;
        case 2:
            post("physicslfo~: Type 2 - ENHANCED BOUNCE+SPIN | Param: complex spin rate & intensity (0=simple, 1=complex) | Damping: energy loss rate");
            break;
        case 3:
            post("physicslfo~: Type 3 - OVERSHOOT (settles) | Param: overshoot intensity (0=gentle, 1=dramatic) | Damping: settling speed");
            break;
        case 4:
            post("physicslfo~: Type 4 - MULTI-BOUNCE (stops) | Param: bounces per cycle (0=few, 1=many) | Damping: how quickly it stops");
            break;
        case 5:
            post("physicslfo~: Type 5 - WOBBLE (settles) | Param: frequency spread (0=simple, 1=complex) | Damping: settling speed");
            break;
        default:
            post("physicslfo~: Unknown physics type %ld", type);
            break;
    }
}

//----------------------------------------------------------------------------------------------
// Physics Simulation Functions
//----------------------------------------------------------------------------------------------

double simulate_bounce(t_physicslfo *x, double t, double param, double damping) {
    // Simple bounce: param controls how "bouncy" vs "droopy" the curve is
    // param = 0.0: very droopy (slow fall, sharp bounce)
    // param = 1.0: very bouncy (fast fall, high bounce)
    
    double curve_power = 0.5 + param * 3.0;  // 0.5 to 3.5 power curve
    
    // Apply continuous damping throughout the bounce cycle
    // Higher damping = lower overall energy and faster decay
    double decay_factor = 1.0 - (damping * t * 1.5);  // More dramatic continuous energy loss
    double bounce_height = x->energy * fmax(0.1, decay_factor);  // Don't go below 10%
    
    // Simple parabolic trajectory with variable curve sharpness
    // Higher param = sharper, more "bouncy" curve
    double height = bounce_height * (1.0 - pow(t, curve_power));
    
    // Ground collision and additional energy loss
    if (height <= 0.0) {
        height = 0.0;
        x->energy *= (1.0 - damping * 0.8);  // Even more dramatic energy loss on bounce
        x->bounce_count += 1.0;
    }
    
    return CLAMP(height, 0.0, 1.0);
}

double simulate_elastic(t_physicslfo *x, double t, double tension, double damping) {
    // Damped oscillation - decay envelope with vibrations inside (like struck bell or plucked string)
    
    // Oscillation frequency controlled by tension parameter
    double osc_frequency = 3.0 + tension * 12.0;  // 3-15 Hz vibration frequency
    
    // Exponential decay envelope - starts at 1.0 and decays toward 0
    double decay_rate = 1.0 + damping * 4.0;  // 1-5 decay rate (damping controls how fast it dies out)
    double decay_envelope = exp(-decay_rate * t);
    
    // Oscillation inside the decay envelope
    double oscillation = sin(2.0 * PI * osc_frequency * t);
    
    // Optional: Add slight frequency drift as energy dissipates (like real physical systems)
    double freq_drift = 1.0 - (t * 0.1 * tension);  // Slight frequency drop over time
    freq_drift = fmax(0.8, freq_drift);  // Don't let it drift too much
    oscillation = sin(2.0 * PI * osc_frequency * freq_drift * t);
    
    // Combine: oscillation amplitude modulated by decay envelope
    double result = decay_envelope * oscillation;
    
    // Scale and offset to 0-1 range (decay starts high, oscillates toward zero)
    result = (result + 1.0) * 0.5;  // Convert from ±1 to 0-1
    result *= decay_envelope;       // Apply envelope again for proper decay to zero
    
    return result;
}

double simulate_bounce_spin(t_physicslfo *x, double t, double param, double damping) {
    // Enhanced bounce with dynamic spin - more movement and character
    double curve_power = 0.3 + param * 2.5;  // More responsive curve variation
    
    // Apply continuous damping with spin-dependent energy retention
    double decay_factor = 1.0 - (damping * t * 1.0);  // Slower decay for more movement
    double bounce_height = x->energy * fmax(0.15, decay_factor);
    
    // Basic bounce trajectory  
    double base_bounce = bounce_height * (1.0 - pow(t, curve_power));
    
    // Enhanced multi-frequency spin system
    double primary_spin_freq = 3.0 + param * 12.0;   // Higher frequency range (3-15 Hz)
    double secondary_spin_freq = 1.5 + param * 6.0;   // Secondary harmonic (1.5-7.5 Hz)
    
    // Create complex spin pattern with multiple frequencies
    double primary_spin = sin(2.0 * PI * primary_spin_freq * t);
    double secondary_spin = sin(2.0 * PI * secondary_spin_freq * t + PI/3);  // Phase offset
    double complex_spin = (primary_spin * 0.7) + (secondary_spin * 0.3);     // Mix harmonics
    
    // Dynamic spin influence that increases with parameter and bounce energy
    double spin_base_influence = 0.3 + param * 0.4;  // 0.3 to 0.7 influence range
    double energy_boost = 1.0 + (x->energy * 0.5);   // More spin when more energy
    double spin_influence = spin_base_influence * base_bounce * energy_boost;
    
    // Add wobble effect - slower frequency modulation
    double wobble_freq = 0.5 + param * 1.5;  // 0.5-2 Hz wobble
    double wobble = sin(2.0 * PI * wobble_freq * t) * 0.15 * param;  // Subtle wobble
    
    // Combine all effects
    double final_height = base_bounce + (complex_spin * spin_influence) + (wobble * base_bounce);
    
    // Ground collision and energy loss (less aggressive to maintain movement)
    if (final_height <= 0.0) {
        final_height = 0.0;
        x->energy *= (1.0 - damping * 0.5);  // Reduced energy loss for more bounces
        x->bounce_count += 1.0;
    }
    
    return fmax(0.0, final_height);
}

double simulate_elastic_overshoot(t_physicslfo *x, double t, double tension, double damping) {
    // Step response with overshoot that settles to equilibrium
    double freq = 1.0 + tension * 4.0;    // Oscillation frequency
    double overshoot_amount = 0.3 + tension * 0.4; // How much overshoot (0.3-0.7)
    
    // Target equilibrium (where it eventually settles)
    double equilibrium = 0.6;
    
    // Exponential approach to equilibrium with proper settling
    double approach_rate = 2.0 + damping * 3.0;  // How fast it approaches equilibrium
    double base_approach = equilibrium * (1.0 - exp(-approach_rate * t));
    
    // Overshoot oscillation that properly decays to zero
    double overshoot_decay = exp(-damping * t * 2.5);  // Stronger decay
    double overshoot_osc = sin(2.0 * PI * freq * t) * overshoot_amount * overshoot_decay;
    
    // Cut off oscillations when they become negligible
    if (overshoot_decay < 0.01) {
        overshoot_osc = 0.0;
    }
    
    return base_approach + overshoot_osc;
}

double simulate_multibounce(t_physicslfo *x, double t, double param, double damping) {
    // Multiple bounces that eventually come to complete rest
    double bounces_per_cycle = 2.0 + param * 6.0;  // 2-8 bounces per cycle
    
    // Which bounce segment are we in?
    double segment_phase = fmod(t * bounces_per_cycle, 1.0);
    long current_bounce = (long)(t * bounces_per_cycle);
    
    // Parabolic trajectory for each bounce
    double height = 4.0 * segment_phase * (1.0 - segment_phase);
    
    // More aggressive exponential decay per bounce
    double energy_loss_per_bounce = 0.5 + damping * 0.4;  // 50-90% energy retained per bounce
    double bounce_amplitude = pow(energy_loss_per_bounce, current_bounce);
    
    // Complete stop when amplitude becomes negligible
    double stop_threshold = 0.02;  // Stop when below 2%
    if (bounce_amplitude < stop_threshold) {
        return 0.0;  // Complete rest
    }
    
    return height * bounce_amplitude;
}

double simulate_wobble(t_physicslfo *x, double t, double tension, double damping) {
    // Wobble that eventually settles to equilibrium position
    double base_freq = 1.5 + tension * 2.5;          // Base frequency
    double freq_spread = tension * 0.8;              // Frequency spread for beating
    
    double freq1 = base_freq;
    double freq2 = base_freq + freq_spread;
    
    // Equilibrium position where wobble settles
    double equilibrium = 0.5;
    
    // Approach equilibrium over time
    double approach_rate = 1.5 + damping * 2.0;
    double base_level = equilibrium * (1.0 - exp(-approach_rate * t));
    
    // Wobble oscillations that properly decay to zero
    double wobble_decay = exp(-damping * t * 1.2);  // Stronger decay than before
    
    // Create smooth beating pattern
    double osc1 = sin(2.0 * PI * freq1 * t);
    double osc2 = sin(2.0 * PI * freq2 * t);
    double beating = (osc1 + osc2 * 0.8) / 1.8;     // Weighted average
    
    // Cut off wobble when it becomes negligible
    double wobble_amplitude = 0.3 * wobble_decay;
    if (wobble_decay < 0.01) {
        wobble_amplitude = 0.0;
    }
    
    // Combine equilibrium approach with decaying wobble
    double result = base_level + (beating * wobble_amplitude);
    
    return result;
}

//----------------------------------------------------------------------------------------------
// Attribute Setters
//----------------------------------------------------------------------------------------------

t_max_err physicslfo_attr_setlooping(t_physicslfo *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->looping = atom_getlong(argv) ? 1 : 0;
        x->looping_mode = x->looping;  // Sync with old variable
        
        if (!x->looping) {
            // Switched to envelope mode - stop any current envelope
            x->envelope_active = 0;
        }
    }
    return 0;
}

t_max_err physicslfo_attr_setphysicstype(t_physicslfo *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->physics_type = CLAMP(atom_getlong(argv), 0, 5);
        x->type_float = x->physics_type;  // Sync with float parameter
        print_physics_info(x, x->physics_type);
    }
    return 0;
}

t_max_err physicslfo_attr_setgravity(t_physicslfo *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->gravity = CLAMP(atom_getfloat(argv), 0.0, 2.0);
        x->physics_float = x->gravity;  // Sync with float parameter
    }
    return 0;
}

t_max_err physicslfo_attr_setairresistance(t_physicslfo *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->air_resistance = CLAMP(atom_getfloat(argv), 0.0, 1.0);
        x->damping_float = x->air_resistance;  // Sync with float parameter
    }
    return 0;
}