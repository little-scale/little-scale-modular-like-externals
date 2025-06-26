/*
	harmosc~ - Computationally efficient harmonic oscillator for Max/MSP
	
	Features:
	- Variable harmonic count at instantiation
	- Dynamic falloff control for harmonic amplitude distribution  
	- Selective harmonic activation (all/odd/even)
	- Wavetable-based synthesis for performance
	
	Usage: [harmosc~ <freq> <harmonics> <falloff> <detune>]
	Defaults: 440 Hz, 8 harmonics, 0.0 falloff, 0.0 detune
	All arguments optional
*/

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include <math.h>

#define TABLE_SIZE 4096
#define PI 3.14159265358979323846

typedef struct _harmosc {
    t_pxobject x_obj;           // MSP object header
    
    // Oscillator state
    double phase;               // Master phase accumulator (0-1)
    double sr;                  // Sample rate
    double sr_recip;            // 1/sr for efficiency
    
    // lores~ pattern: signal connection status
    short freq_has_signal;      // 1 if frequency inlet has signal connection
    short falloff_has_signal;   // 1 if falloff inlet has signal connection
    
    // Float parameters (stored values for when no signal connected)
    double freq_float;          // Fundamental frequency
    double falloff_float;       // Falloff parameter (-1 to 1)
    
    // Harmonic control
    int num_harmonics;          // Total number of harmonics
    double falloff;             // Falloff parameter (-1 to 1)
    double detune;              // Detune parameter (0-1)
    double *amplitudes;         // Pre-calculated amplitude array
    char *harmonic_states;      // On/off state for each harmonic
    double *detune_offsets;     // Random detune offsets for each harmonic
    
    // Optimization
    double *phase_increments;   // Pre-calculated phase increments
    double *sine_table;         // Wavetable for efficiency
    int table_size;             // Size of sine table
    int table_mask;             // For fast modulo operations
    
    // State flags
    char all_on;                // All harmonics active
    char odd_only;              // Only odd harmonics
    char even_only;             // Only even harmonics
    char custom_amps;           // Using custom amplitude values
    
    // Attribute parameters for Max 9 autocomplete
    long harmonic_mode;         // 0=all, 1=odd, 2=even (default 0)
    double falloff_curve;       // -1.0 to 1.0 automatic amplitude distribution (default 0.0)
    double detune_amount;       // 0.0 to 1.0 harmonic detuning (default 0.0)
    long amplitude_control;     // 0=automatic, 1=custom (default 0)
} t_harmosc;

// Function prototypes
void *harmosc_new(t_symbol *s, long argc, t_atom *argv);
void harmosc_free(t_harmosc *x);
void harmosc_assist(t_harmosc *x, void *b, long m, long a, char *s);
void harmosc_float(t_harmosc *x, double f);
void harmosc_dsp64(t_harmosc *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void harmosc_perform64(t_harmosc *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam);

// Message handlers
void harmosc_falloff(t_harmosc *x, double f);
void harmosc_detune(t_harmosc *x, double d);
void harmosc_all(t_harmosc *x);
void harmosc_odd(t_harmosc *x);
void harmosc_even(t_harmosc *x);
void harmosc_amps(t_harmosc *x, t_symbol *s, long argc, t_atom *argv);

// Internal functions
void harmosc_build_sine_table(t_harmosc *x);
void harmosc_update_phase_increments(t_harmosc *x);
void harmosc_calculate_amplitudes(t_harmosc *x);
void harmosc_generate_detune_offsets(t_harmosc *x);

// Attribute setter prototypes
t_max_err harmosc_attr_setharmonicmode(t_harmosc *x, void *attr, long argc, t_atom *argv);
t_max_err harmosc_attr_setfalloffcurve(t_harmosc *x, void *attr, long argc, t_atom *argv);
t_max_err harmosc_attr_setdetuneamount(t_harmosc *x, void *attr, long argc, t_atom *argv);
t_max_err harmosc_attr_setamplitudecontrol(t_harmosc *x, void *attr, long argc, t_atom *argv);

// Global class pointer
static t_class *harmosc_class;

void ext_main(void *r) {
    t_class *c;
    
    c = class_new("harmosc~", (method)harmosc_new, (method)harmosc_free, 
                  sizeof(t_harmosc), NULL, A_GIMME, 0);
    
    class_addmethod(c, (method)harmosc_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)harmosc_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)harmosc_float, "float", A_FLOAT, 0);
    
    // Message handlers
    class_addmethod(c, (method)harmosc_falloff, "falloff", A_FLOAT, 0);
    class_addmethod(c, (method)harmosc_detune, "detune", A_FLOAT, 0);
    class_addmethod(c, (method)harmosc_all, "all", 0);
    class_addmethod(c, (method)harmosc_odd, "odd", 0);
    class_addmethod(c, (method)harmosc_even, "even", 0);
    class_addmethod(c, (method)harmosc_amps, "amps", A_GIMME, 0);
    
    // Enhanced attributes with Max 9 autocomplete support
    CLASS_ATTR_LONG(c, "harmonic_mode", 0, t_harmosc, harmonic_mode);
    CLASS_ATTR_BASIC(c, "harmonic_mode", 0);
    CLASS_ATTR_LABEL(c, "harmonic_mode", 0, "Harmonic Mode");
    CLASS_ATTR_ACCESSORS(c, "harmonic_mode", 0, harmosc_attr_setharmonicmode);
    CLASS_ATTR_ORDER(c, "harmonic_mode", 0, "1");
    CLASS_ATTR_FILTER_CLIP(c, "harmonic_mode", 0, 2);
    CLASS_ATTR_DEFAULT_SAVE(c, "harmonic_mode", 0, "0");
    CLASS_ATTR_STYLE(c, "harmonic_mode", 0, "enumindex");
    
    CLASS_ATTR_DOUBLE(c, "falloff_curve", 0, t_harmosc, falloff_curve);
    CLASS_ATTR_BASIC(c, "falloff_curve", 0);
    CLASS_ATTR_LABEL(c, "falloff_curve", 0, "Falloff Curve");
    CLASS_ATTR_ACCESSORS(c, "falloff_curve", 0, harmosc_attr_setfalloffcurve);
    CLASS_ATTR_ORDER(c, "falloff_curve", 0, "2");
    CLASS_ATTR_FILTER_CLIP(c, "falloff_curve", -1.0, 1.0);
    CLASS_ATTR_DEFAULT_SAVE(c, "falloff_curve", 0, "0.0");
    CLASS_ATTR_STYLE(c, "falloff_curve", 0, "text");
    
    CLASS_ATTR_DOUBLE(c, "detune_amount", 0, t_harmosc, detune_amount);
    CLASS_ATTR_BASIC(c, "detune_amount", 0);
    CLASS_ATTR_LABEL(c, "detune_amount", 0, "Detune Amount");
    CLASS_ATTR_ACCESSORS(c, "detune_amount", 0, harmosc_attr_setdetuneamount);
    CLASS_ATTR_ORDER(c, "detune_amount", 0, "3");
    CLASS_ATTR_FILTER_CLIP(c, "detune_amount", 0.0, 1.0);
    CLASS_ATTR_DEFAULT_SAVE(c, "detune_amount", 0, "0.0");
    CLASS_ATTR_STYLE(c, "detune_amount", 0, "text");
    
    CLASS_ATTR_LONG(c, "amplitude_control", 0, t_harmosc, amplitude_control);
    CLASS_ATTR_BASIC(c, "amplitude_control", 0);
    CLASS_ATTR_LABEL(c, "amplitude_control", 0, "Amplitude Control");
    CLASS_ATTR_ACCESSORS(c, "amplitude_control", 0, harmosc_attr_setamplitudecontrol);
    CLASS_ATTR_ORDER(c, "amplitude_control", 0, "4");
    CLASS_ATTR_FILTER_CLIP(c, "amplitude_control", 0, 1);
    CLASS_ATTR_DEFAULT_SAVE(c, "amplitude_control", 0, "0");
    CLASS_ATTR_STYLE(c, "amplitude_control", 0, "enumindex");
    
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    harmosc_class = c;
}

void *harmosc_new(t_symbol *s, long argc, t_atom *argv) {
    t_harmosc *x = (t_harmosc *)object_alloc(harmosc_class);
    
    if (x) {
        // lores~ pattern: 2 signal inlets (frequency, falloff)
        dsp_setup((t_pxobject *)x, 2);
        outlet_new(x, "signal");
        
        // Parse arguments: [fundamental freq] [n harmonics] [falloff] [detune]
        x->num_harmonics = 8;   // Default
        x->freq_float = 440.0;  // Default  
        x->falloff = 0.0;       // Default
        x->falloff_float = 0.0; // Default
        x->detune = 0.0;        // Default
        
        // Initialize connection status (assume no signals connected initially)
        x->freq_has_signal = 0;
        x->falloff_has_signal = 0;
        
        // Argument 1: Fundamental frequency
        if (argc >= 1 && (atom_gettype(argv) == A_FLOAT || atom_gettype(argv) == A_LONG)) {
            double freq_arg = atom_gettype(argv) == A_FLOAT ? atom_getfloat(argv) : (double)atom_getlong(argv);
            x->freq_float = CLAMP(freq_arg, 0.1, 20000.0);
        }
        
        // Argument 2: Number of harmonics
        if (argc >= 2 && atom_gettype(argv + 1) == A_LONG) {
            x->num_harmonics = CLAMP(atom_getlong(argv + 1), 1, 64);
        }
        
        // Argument 3: Falloff
        if (argc >= 3 && (atom_gettype(argv + 2) == A_FLOAT || atom_gettype(argv + 2) == A_LONG)) {
            double falloff_arg = atom_gettype(argv + 2) == A_FLOAT ? atom_getfloat(argv + 2) : (double)atom_getlong(argv + 2);
            x->falloff = x->falloff_float = CLAMP(falloff_arg, -1.0, 1.0);
        }
        
        // Argument 4: Detune
        if (argc >= 4 && (atom_gettype(argv + 3) == A_FLOAT || atom_gettype(argv + 3) == A_LONG)) {
            double detune_arg = atom_gettype(argv + 3) == A_FLOAT ? atom_getfloat(argv + 3) : (double)atom_getlong(argv + 3);
            x->detune = CLAMP(detune_arg, 0.0, 1.0);
        }
        
        // Initialize oscillator state
        x->phase = 0.0;
        x->sr = sys_getsr();
        x->sr_recip = 1.0 / x->sr;
        
        // Initialize attribute defaults
        x->harmonic_mode = 0;       // All harmonics
        x->falloff_curve = 0.0;     // Equal amplitudes
        x->detune_amount = 0.0;     // No detuning
        x->amplitude_control = 0;   // Automatic control
        
        // Initialize state flags
        x->all_on = 1;
        x->odd_only = 0;
        x->even_only = 0;
        x->custom_amps = 0;
        
        // Allocate arrays
        x->amplitudes = (double *)sysmem_newptr(x->num_harmonics * sizeof(double));
        x->harmonic_states = (char *)sysmem_newptr(x->num_harmonics * sizeof(char));
        x->phase_increments = (double *)sysmem_newptr(x->num_harmonics * sizeof(double));
        x->detune_offsets = (double *)sysmem_newptr(x->num_harmonics * sizeof(double));
        
        if (!x->amplitudes || !x->harmonic_states || !x->phase_increments || !x->detune_offsets) {
            object_error((t_object *)x, "Failed to allocate memory");
            return NULL;
        }
        
        // Initialize harmonic states (all on)
        for (int i = 0; i < x->num_harmonics; i++) {
            x->harmonic_states[i] = 1;
        }
        
        // Build sine table
        harmosc_build_sine_table(x);
        
        // Generate detune offsets and calculate initial values
        harmosc_generate_detune_offsets(x);
        harmosc_update_phase_increments(x);
        harmosc_calculate_amplitudes(x);
        
        // Process attributes early for proper initialization
        long offset = attr_args_offset((short)argc, argv);
        attr_args_process(x, argc, argv);
        
        // Sync attributes with internal state
        x->falloff = x->falloff_curve;
        x->detune = x->detune_amount;
        x->custom_amps = x->amplitude_control;
        
        post("harmosc~: Created with %.1f Hz, %d harmonics, falloff %.2f, detune %.2f", 
             x->freq_float, x->num_harmonics, x->falloff_float, x->detune);
    }
    
    return x;
}

void harmosc_free(t_harmosc *x) {
    if (x->sine_table) {
        sysmem_freeptr(x->sine_table);
    }
    if (x->amplitudes) {
        sysmem_freeptr(x->amplitudes);
    }
    if (x->harmonic_states) {
        sysmem_freeptr(x->harmonic_states);
    }
    if (x->phase_increments) {
        sysmem_freeptr(x->phase_increments);
    }
    if (x->detune_offsets) {
        sysmem_freeptr(x->detune_offsets);
    }
    dsp_free((t_pxobject *)x);
}

void harmosc_assist(t_harmosc *x, void *b, long m, long a, char *s) {
    if (m == ASSIST_INLET) {
        switch (a) {
            case 0:
                sprintf(s, "(signal/float) Frequency in Hz");
                break;
            case 1:
                sprintf(s, "(signal/float) Falloff (-1 to 1)");
                break;
        }
    } else {
        sprintf(s, "(signal) Harmonic oscillator output");
    }
}

void harmosc_float(t_harmosc *x, double f) {
    // lores~ pattern: proxy_getinlet works on signal inlets for float routing
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 0: // Frequency inlet
            x->freq_float = CLAMP(f, 0.1, 20000.0);
            harmosc_update_phase_increments(x);
            break;
        case 1: // Falloff inlet
            x->falloff_float = CLAMP(f, -1.0, 1.0);
            x->falloff = x->falloff_float;
            x->custom_amps = 0;  // Clear custom amplitudes when using falloff
            harmosc_calculate_amplitudes(x);
            break;
    }
}

void harmosc_build_sine_table(t_harmosc *x) {
    x->table_size = TABLE_SIZE;
    x->table_mask = TABLE_SIZE - 1;
    x->sine_table = (double *)sysmem_newptr(TABLE_SIZE * sizeof(double));
    
    if (!x->sine_table) {
        object_error((t_object *)x, "Failed to allocate sine table");
        return;
    }
    
    for (int i = 0; i < TABLE_SIZE; i++) {
        x->sine_table[i] = sin(2.0 * PI * i / TABLE_SIZE);
    }
}

void harmosc_update_phase_increments(t_harmosc *x) {
    double base_increment = x->freq_float * x->sr_recip;
    
    for (int i = 0; i < x->num_harmonics; i++) {
        double harmonic_frequency = i + 1;  // Base harmonic number (1, 2, 3, ...)
        
        // Apply detune: 0.0 = harmonic, 1.0 = ±50 cents maximum
        if (x->detune > 0.0) {
            // Convert cents to frequency ratio: 2^(cents/1200)
            double cents_offset = x->detune_offsets[i] * x->detune;
            double frequency_ratio = pow(2.0, cents_offset / 1200.0);
            double detuned_frequency = harmonic_frequency * frequency_ratio;
            x->phase_increments[i] = base_increment * detuned_frequency;
        } else {
            x->phase_increments[i] = base_increment * harmonic_frequency;
        }
    }
}

void harmosc_calculate_amplitudes(t_harmosc *x) {
    double total = 0.0;
    
    // Skip automatic calculation if using custom amplitudes
    if (x->custom_amps) {
        // Just apply harmonic states and normalize
        for (int i = 0; i < x->num_harmonics; i++) {
            x->amplitudes[i] *= x->harmonic_states[i];
            total += x->amplitudes[i];
        }
        
        // Normalize to maintain consistent output level
        if (total > 0.0) {
            for (int i = 0; i < x->num_harmonics; i++) {
                x->amplitudes[i] /= total;
            }
        }
        return;
    }
    
    // Calculate amplitudes with bipolar falloff behavior
    // -1.0 = only fundamental, 0.0 = equal harmonics, +1.0 = only highest harmonic
    for (int i = 0; i < x->num_harmonics; i++) {
        double harmonic_number = i + 1;  // 1-indexed harmonic number
        double highest_harmonic = x->num_harmonics;  // Highest harmonic number
        
        if (x->falloff == -1.0) {
            // Only fundamental
            x->amplitudes[i] = (i == 0) ? 1.0 : 0.0;
        } else if (x->falloff == 1.0) {
            // Only highest harmonic
            x->amplitudes[i] = (i == x->num_harmonics - 1) ? 1.0 : 0.0;
        } else if (x->falloff == 0.0) {
            // Equal amplitudes
            x->amplitudes[i] = 1.0;
        } else if (x->falloff < 0.0) {
            // Negative range: -1 to 0 (fundamental to equal)
            // Exponential decay from fundamental, getting weaker as we approach 0
            double decay_strength = -x->falloff;  // 0.0 to 1.0
            double decay_exponent = decay_strength * 3.0;  // Scale decay rate
            x->amplitudes[i] = pow(harmonic_number, -decay_exponent);
        } else {
            // Positive range: 0 to 1 (equal to highest harmonic)  
            // Exponential decay from highest harmonic, getting stronger as we approach 1
            double decay_strength = x->falloff;  // 0.0 to 1.0
            double decay_exponent = decay_strength * 3.0;  // Scale decay rate
            // Reverse the harmonic order for decay calculation
            double reverse_harmonic = highest_harmonic - harmonic_number + 1;
            x->amplitudes[i] = pow(reverse_harmonic, -decay_exponent);
        }
        
        // Apply harmonic states during calculation
        x->amplitudes[i] *= x->harmonic_states[i];
        total += x->amplitudes[i];
    }
    
    // Normalize to maintain consistent output level
    if (total > 0.0) {
        for (int i = 0; i < x->num_harmonics; i++) {
            x->amplitudes[i] /= total;
        }
    }
}

void harmosc_dsp64(t_harmosc *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags) {
    // Update sample rate if changed
    if (x->sr != samplerate) {
        x->sr = samplerate;
        x->sr_recip = 1.0 / samplerate;
        harmosc_update_phase_increments(x);
    }
    
    // lores~ pattern: store signal connection status
    x->freq_has_signal = count[0];
    x->falloff_has_signal = count[1];
    
    object_method(dsp64, gensym("dsp_add64"), x, harmosc_perform64, 0, NULL);
}

void harmosc_perform64(t_harmosc *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam) {
    // Input buffers for frequency and falloff inlets
    double *freq_in = ins[0];
    double *falloff_in = ins[1];
    double *out = outs[0];
    double phase = x->phase;
    double *sine_table = x->sine_table;
    int table_mask = x->table_mask;
    
    // Process audio block
    for (long i = 0; i < sampleframes; i++) {
        // lores~ pattern: choose signal vs float for each inlet
        double current_freq = x->freq_has_signal ? freq_in[i] : x->freq_float;
        double current_falloff = x->falloff_has_signal ? falloff_in[i] : x->falloff_float;
        
        // Clamp falloff parameter
        current_falloff = CLAMP(current_falloff, -1.0, 1.0);
        
        // Update falloff if it changed (for signal-rate modulation)
        if (current_falloff != x->falloff) {
            x->falloff = current_falloff;
            // Only recalculate if not using custom amplitudes
            if (!x->custom_amps) {
                harmosc_calculate_amplitudes(x);
            }
        }
        
        double base_increment = current_freq * x->sr_recip;
        
        double sample = 0.0;
        
        // Additive synthesis loop - uses individual phase increments for detuning
        for (int h = 0; h < x->num_harmonics; h++) {
            if (x->amplitudes[h] > 0.0) {
                // Calculate harmonic frequency with detuning
                double harmonic_frequency = h + 1;  // Base harmonic number (1, 2, 3, ...)
                
                if (x->detune > 0.0) {
                    // Apply detune: 0.0 = harmonic, 1.0 = ±50 cents maximum
                    double cents_offset = x->detune_offsets[h] * x->detune;
                    double frequency_ratio = pow(2.0, cents_offset / 1200.0);
                    harmonic_frequency *= frequency_ratio;
                }
                
                double harmonic_increment = base_increment * harmonic_frequency;
                double phase_ratio = harmonic_increment / base_increment;
                double harmonic_phase = phase * phase_ratio;
                
                // Wrap phase to 0-1 range
                harmonic_phase = harmonic_phase - floor(harmonic_phase);
                int table_index = (int)(harmonic_phase * TABLE_SIZE) & table_mask;
                sample += sine_table[table_index] * x->amplitudes[h];
            }
        }
        
        *out++ = sample;
        
        // Update phase
        phase += base_increment;
        if (phase >= 1.0) phase -= 1.0;
    }
    
    x->phase = phase;
}

// Message handlers
void harmosc_falloff(t_harmosc *x, double f) {
    x->falloff = CLAMP(f, -1.0, 1.0);
    x->custom_amps = 0;  // Clear custom amplitudes when using falloff
    harmosc_calculate_amplitudes(x);
    post("harmosc~: falloff set to %.3f", x->falloff);
}

void harmosc_detune(t_harmosc *x, double d) {
    x->detune = CLAMP(d, 0.0, 1.0);
    harmosc_update_phase_increments(x);
    post("harmosc~: detune set to %.3f", x->detune);
    
    // Debug: show actual cents detuning for first few harmonics
    for (int i = 1; i < MIN(4, x->num_harmonics); i++) {
        double cents = x->detune_offsets[i] * x->detune;
        post("  harmonic %d: %.1f cents (offset %.1f * detune %.3f)", 
             i+1, cents, x->detune_offsets[i], x->detune);
    }
}

void harmosc_all(t_harmosc *x) {
    x->all_on = 1;
    x->odd_only = 0;
    x->even_only = 0;
    
    for (int i = 0; i < x->num_harmonics; i++) {
        x->harmonic_states[i] = 1;
    }
    harmosc_calculate_amplitudes(x);
    post("harmosc~: all harmonics enabled");
}

void harmosc_odd(t_harmosc *x) {
    x->all_on = 0;
    x->odd_only = 1;
    x->even_only = 0;
    
    for (int i = 0; i < x->num_harmonics; i++) {
        // Always keep fundamental (i=0), then odd harmonics only
        if (i == 0) {
            x->harmonic_states[i] = 1;  // Fundamental always on
        } else {
            // Harmonic numbers are 1-indexed (fundamental = 1)
            x->harmonic_states[i] = ((i + 1) % 2 == 1) ? 1 : 0;
        }
    }
    harmosc_calculate_amplitudes(x);
    post("harmosc~: odd harmonics enabled (with fundamental)");
}

void harmosc_even(t_harmosc *x) {
    x->all_on = 0;
    x->odd_only = 0;
    x->even_only = 1;
    
    for (int i = 0; i < x->num_harmonics; i++) {
        // Always keep fundamental (i=0), then even harmonics only
        if (i == 0) {
            x->harmonic_states[i] = 1;  // Fundamental always on
        } else {
            // Harmonic numbers are 1-indexed (fundamental = 1)
            x->harmonic_states[i] = ((i + 1) % 2 == 0) ? 1 : 0;
        }
    }
    harmosc_calculate_amplitudes(x);
    post("harmosc~: even harmonics enabled (with fundamental)");
}

void harmosc_generate_detune_offsets(t_harmosc *x) {
    // Generate random detune offsets for each harmonic
    // Range: ±50 cents maximum (detune=1.0) for musical detuning
    for (int i = 0; i < x->num_harmonics; i++) {
        if (i == 0) {
            // Keep fundamental undetuned
            x->detune_offsets[i] = 0.0;
        } else {
            // Random offset between -50 and +50 cents
            double random_val = (double)rand() / RAND_MAX;  // 0.0 to 1.0
            x->detune_offsets[i] = (random_val * 100.0) - 50.0;  // -50 to +50 cents
            
            // Debug output to verify detuning amounts
            post("harmosc~: harmonic %d detune offset: %.1f cents", i+1, x->detune_offsets[i]);
        }
    }
}

void harmosc_amps(t_harmosc *x, t_symbol *s, long argc, t_atom *argv) {
    if (argc == 0) {
        object_error((t_object *)x, "amps: requires at least one amplitude value");
        return;
    }
    
    // Set custom amplitudes flag
    x->custom_amps = 1;
    
    // Clear state flags since we're using custom amplitudes
    x->all_on = 0;
    x->odd_only = 0;
    x->even_only = 0;
    
    // Process each amplitude value
    int num_values = MIN(argc, x->num_harmonics);
    for (int i = 0; i < num_values; i++) {
        double amp_value = 0.0;
        
        // Extract amplitude value from atom
        if (atom_gettype(argv + i) == A_FLOAT) {
            amp_value = atom_getfloat(argv + i);
        } else if (atom_gettype(argv + i) == A_LONG) {
            amp_value = (double)atom_getlong(argv + i);
        } else {
            object_error((t_object *)x, "amps: argument %d is not a number", i + 1);
            continue;
        }
        
        // Clamp amplitude to 0-1 range
        amp_value = CLAMP(amp_value, 0.0, 1.0);
        x->amplitudes[i] = amp_value;
        
        // Set harmonic state based on amplitude (0 = off, >0 = on)
        x->harmonic_states[i] = (amp_value > 0.0) ? 1 : 0;
    }
    
    // Set remaining harmonics to 0 if fewer values provided than harmonics
    for (int i = num_values; i < x->num_harmonics; i++) {
        x->amplitudes[i] = 0.0;
        x->harmonic_states[i] = 0;
    }
    
    // Apply normalization via calculate_amplitudes (but skip the automatic calculation)
    harmosc_calculate_amplitudes(x);
    
    post("harmosc~: custom amplitudes set for %d harmonics", num_values);
}

//----------------------------------------------------------------------------------------------
// Attribute Setters
//----------------------------------------------------------------------------------------------

t_max_err harmosc_attr_setharmonicmode(t_harmosc *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->harmonic_mode = CLAMP(atom_getlong(argv), 0, 2);
        
        // Apply harmonic mode
        switch (x->harmonic_mode) {
            case 0: // All harmonics
                harmosc_all(x);
                break;
            case 1: // Odd harmonics
                harmosc_odd(x);
                break;
            case 2: // Even harmonics
                harmosc_even(x);
                break;
        }
    }
    return 0;
}

t_max_err harmosc_attr_setfalloffcurve(t_harmosc *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->falloff_curve = CLAMP(atom_getfloat(argv), -1.0, 1.0);
        x->falloff = x->falloff_curve;
        x->falloff_float = x->falloff_curve;
        x->custom_amps = 0;  // Clear custom amplitudes when using falloff
        harmosc_calculate_amplitudes(x);
    }
    return 0;
}

t_max_err harmosc_attr_setdetuneamount(t_harmosc *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->detune_amount = CLAMP(atom_getfloat(argv), 0.0, 1.0);
        x->detune = x->detune_amount;
        harmosc_update_phase_increments(x);
    }
    return 0;
}

t_max_err harmosc_attr_setamplitudecontrol(t_harmosc *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->amplitude_control = CLAMP(atom_getlong(argv), 0, 1);
        x->custom_amps = x->amplitude_control;
        
        if (!x->custom_amps) {
            // Switch back to automatic control
            harmosc_calculate_amplitudes(x);
        }
    }
    return 0;
}

// Note: Message system maintained as backup - attributes now provide Max 9 autocomplete support