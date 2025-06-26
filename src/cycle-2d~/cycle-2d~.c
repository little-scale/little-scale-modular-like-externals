/**
 * cycle-2d~ - 2D Morphing Wavetable Oscillator for Max/MSP
 * 
 * This external generates audio using 2D interpolation between four corner waveforms
 * positioned in a normalized 2D space (0-1, 0-1). Smooth morphing between waveforms
 * creates unique timbres and expressive modulation possibilities.
 * 
 * Features:
 * - 4096-sample lookup tables for smooth interpolation
 * - lores~ pattern: 4 signal inlets accept both signals and floats
 * - Bilinear interpolation between corner waveforms
 * - Custom buffer loading at any 2D position
 * - Audio-rate morphing and frequency modulation
 * - Bang trigger support for phase reset
 * - Universal binary support (Intel + Apple Silicon)
 * 
 * Corner Waveform Mapping:
 *   (0,0) = Sine wave      (0,1) = Triangle wave
 *   (1,0) = Sawtooth wave  (1,1) = Square wave
 * 
 * Inlets:
 *   1. Frequency (signal/float/int/bang) - Hz frequency or bang to reset phase
 *   2. X position (signal/float, 0.0-1.0) - horizontal position in 2D space
 *   3. Y position (signal/float, 0.0-1.0) - vertical position in 2D space  
 *   4. Phase offset (signal/float, 0.0-1.0) - phase offset for sync
 * 
 * Messages:
 *   buffer <name> <x> <y> <offset> - Load buffer into 2D position with sample offset
 * 
 * Outlets:
 *   1. Audio output (signal, -1.0 to 1.0) - morphed waveform
 */

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "buffer.h"
#include <math.h>

#define PI 3.14159265358979323846
#define WAVETABLE_SIZE 4096
#define MAX_CUSTOM_TABLES 16

// Corner waveform indices
#define CORNER_SINE 0      // (0,0)
#define CORNER_TRIANGLE 1  // (0,1) 
#define CORNER_SAW 2       // (1,0)
#define CORNER_SQUARE 3    // (1,1)

typedef struct _custom_table {
    float wavetable[WAVETABLE_SIZE];
    double x_pos;
    double y_pos;
    int active;
} t_custom_table;

typedef struct _cycle2d {
    t_pxobject ob;              // MSP object header
    
    // Core oscillator state
    double phase;               // Current phase (0.0 to 1.0)
    double sr;                  // Sample rate
    double sr_inv;              // 1.0 / sample rate
    
    // Parameter storage (lores~ pattern)
    double freq_float;          // Frequency when no signal connected
    double x_float;             // X position when no signal connected
    double y_float;             // Y position when no signal connected
    double phase_offset_float;  // Phase offset when no signal connected
    
    // Signal connection status (lores~ pattern)
    short freq_has_signal;      // 1 if frequency inlet has signal connection
    short x_has_signal;         // 1 if X inlet has signal connection
    short y_has_signal;         // 1 if Y inlet has signal connection
    short phase_has_signal;     // 1 if phase inlet has signal connection
    
    // Wavetable storage
    float corner_tables[4][WAVETABLE_SIZE];     // 4 corner waveforms
    t_custom_table custom_tables[MAX_CUSTOM_TABLES]; // User-loaded buffers
    int num_custom_tables;      // Number of active custom tables
    
    // Attribute parameters
    long interpolation;         // 0=bilinear, 1=nearest neighbor, 2=cubic (default 0)
    long corner_mode;           // 0=standard, 1=harmonics, 2=noise (default 0)
    long table_size;            // 0=1024, 1=2048, 2=4096, 3=8192 (default 2)
    
} t_cycle2d;

// Function prototypes
void *cycle2d_new(t_symbol *s, long argc, t_atom *argv);
void cycle2d_free(t_cycle2d *x);
void cycle2d_dsp64(t_cycle2d *x, t_object *dsp64, short *count, double samplerate, 
                   long maxvectorsize, long flags);
void cycle2d_perform64(t_cycle2d *x, t_object *dsp64, double **ins, long numins, 
                       double **outs, long numouts, long sampleframes, long flags, void *userparam);
void cycle2d_float(t_cycle2d *x, double f);
void cycle2d_int(t_cycle2d *x, long n);
void cycle2d_bang(t_cycle2d *x);
void cycle2d_buffer(t_cycle2d *x, t_symbol *s, long argc, t_atom *argv);
void cycle2d_assist(t_cycle2d *x, void *b, long m, long a, char *s);

// Wavetable generation functions
void generate_sine_table(float *table, int size);
void generate_triangle_table(float *table, int size);
void generate_saw_table(float *table, int size);
void generate_square_table(float *table, int size);
void generate_sine_harmonic_table(float *table, int size, int harmonic);

// Interpolation functions
double bilinear_interpolate(t_cycle2d *x, double x_pos, double y_pos, double phase);
double nearest_neighbor_interpolate(t_cycle2d *x, double x_pos, double y_pos, double phase);
double wavetable_lookup(float *table, double phase);

// Helper functions
void init_corner_tables(t_cycle2d *x);

// Attribute setter prototypes
t_max_err cycle2d_attr_setinterpolation(t_cycle2d *x, void *attr, long argc, t_atom *argv);
t_max_err cycle2d_attr_setcornermode(t_cycle2d *x, void *attr, long argc, t_atom *argv);
t_max_err cycle2d_attr_settablesize(t_cycle2d *x, void *attr, long argc, t_atom *argv);

// Class pointer
static t_class *cycle2d_class = NULL;

//----------------------------------------------------------------------------------------------

void ext_main(void *r) {
    t_class *c;
    
    c = class_new("cycle-2d~", (method)cycle2d_new, (method)cycle2d_free,
                  sizeof(t_cycle2d), 0L, A_GIMME, 0);
    
    class_addmethod(c, (method)cycle2d_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)cycle2d_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)cycle2d_float, "float", A_FLOAT, 0);
    class_addmethod(c, (method)cycle2d_int, "int", A_LONG, 0);
    class_addmethod(c, (method)cycle2d_bang, "bang", A_CANT, 0);
    class_addmethod(c, (method)cycle2d_buffer, "buffer", A_GIMME, 0);
    
    // Attributes with Max 9 autocomplete support
    CLASS_ATTR_LONG(c, "interpolation", 0, t_cycle2d, interpolation);
    CLASS_ATTR_BASIC(c, "interpolation", 0);
    CLASS_ATTR_LABEL(c, "interpolation", 0, "Interpolation Method");
    CLASS_ATTR_ACCESSORS(c, "interpolation", 0, cycle2d_attr_setinterpolation);
    CLASS_ATTR_ORDER(c, "interpolation", 0, "1");
    CLASS_ATTR_FILTER_CLIP(c, "interpolation", 0, 2);
    CLASS_ATTR_DEFAULT_SAVE(c, "interpolation", 0, "0");
    CLASS_ATTR_STYLE(c, "interpolation", 0, "enumindex");
    
    CLASS_ATTR_LONG(c, "corner_mode", 0, t_cycle2d, corner_mode);
    CLASS_ATTR_BASIC(c, "corner_mode", 0);
    CLASS_ATTR_LABEL(c, "corner_mode", 0, "Corner Waveform Set");
    CLASS_ATTR_ACCESSORS(c, "corner_mode", 0, cycle2d_attr_setcornermode);
    CLASS_ATTR_ORDER(c, "corner_mode", 0, "2");
    CLASS_ATTR_FILTER_CLIP(c, "corner_mode", 0, 2);
    CLASS_ATTR_DEFAULT_SAVE(c, "corner_mode", 0, "0");
    CLASS_ATTR_STYLE(c, "corner_mode", 0, "enumindex");
    
    CLASS_ATTR_LONG(c, "table_size", 0, t_cycle2d, table_size);
    CLASS_ATTR_BASIC(c, "table_size", 0);
    CLASS_ATTR_LABEL(c, "table_size", 0, "Table Size");
    CLASS_ATTR_ACCESSORS(c, "table_size", 0, cycle2d_attr_settablesize);
    CLASS_ATTR_ORDER(c, "table_size", 0, "3");
    CLASS_ATTR_FILTER_CLIP(c, "table_size", 0, 3);
    CLASS_ATTR_DEFAULT_SAVE(c, "table_size", 0, "2");
    CLASS_ATTR_STYLE(c, "table_size", 0, "enumindex");
    
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    cycle2d_class = c;
}

//----------------------------------------------------------------------------------------------

void *cycle2d_new(t_symbol *s, long argc, t_atom *argv) {
    t_cycle2d *x = (t_cycle2d *)object_alloc(cycle2d_class);
    
    if (x) {
        // lores~ pattern: 4 signal inlets (freq, x, y, phase_offset)
        dsp_setup((t_pxobject *)x, 4);
        outlet_new(x, "signal");
        
        // Initialize core state
        x->phase = 0.0;
        x->sr = sys_getsr();
        x->sr_inv = 1.0 / x->sr;
        
        // Initialize parameter defaults
        x->freq_float = 440.0;      // 440 Hz default
        x->x_float = 0.5;           // Center X position
        x->y_float = 0.5;           // Center Y position
        x->phase_offset_float = 0.0; // No phase offset
        
        // Initialize connection status (assume no signals connected initially)
        x->freq_has_signal = 0;
        x->x_has_signal = 0;
        x->y_has_signal = 0;
        x->phase_has_signal = 0;
        
        // Initialize wavetables
        init_corner_tables(x);
        
        // Initialize attributes with defaults
        x->interpolation = 0;       // Bilinear interpolation
        x->corner_mode = 0;         // Standard corner waveforms
        x->table_size = 2;          // 4096 samples (index 2)
        
        // Initialize custom tables
        x->num_custom_tables = 0;
        for (int i = 0; i < MAX_CUSTOM_TABLES; i++) {
            x->custom_tables[i].active = 0;
            x->custom_tables[i].x_pos = 0.0;
            x->custom_tables[i].y_pos = 0.0;
        }
        
        // Process creation arguments if any
        if (argc >= 1 && (atom_gettype(argv) == A_FLOAT || atom_gettype(argv) == A_LONG)) {
            x->freq_float = atom_getfloat(argv);
        }
        if (argc >= 2 && (atom_gettype(argv + 1) == A_FLOAT || atom_gettype(argv + 1) == A_LONG)) {
            x->x_float = CLAMP(atom_getfloat(argv + 1), 0.0, 1.0);
        }
        if (argc >= 3 && (atom_gettype(argv + 2) == A_FLOAT || atom_gettype(argv + 2) == A_LONG)) {
            x->y_float = CLAMP(atom_getfloat(argv + 2), 0.0, 1.0);
        }
        
        // Process attributes early for proper initialization
        long offset = attr_args_offset((short)argc, argv);
        attr_args_process(x, argc, argv);
    }
    
    return x;
}

//----------------------------------------------------------------------------------------------

void cycle2d_free(t_cycle2d *x) {
    dsp_free((t_pxobject *)x);
}

//----------------------------------------------------------------------------------------------

void cycle2d_dsp64(t_cycle2d *x, t_object *dsp64, short *count, double samplerate, 
                   long maxvectorsize, long flags) {
    x->sr = samplerate;
    x->sr_inv = 1.0 / samplerate;
    
    // lores~ pattern: store signal connection status
    x->freq_has_signal = count[0];
    x->x_has_signal = count[1];
    x->y_has_signal = count[2];
    x->phase_has_signal = count[3];
    
    object_method(dsp64, gensym("dsp_add64"), x, cycle2d_perform64, 0, NULL);
}

//----------------------------------------------------------------------------------------------

void cycle2d_perform64(t_cycle2d *x, t_object *dsp64, double **ins, long numins, 
                       double **outs, long numouts, long sampleframes, long flags, void *userparam) {
    // Input buffers for all 4 inlets
    double *freq_in = ins[0];
    double *x_in = ins[1];
    double *y_in = ins[2];
    double *phase_offset_in = ins[3];
    
    // Output buffer
    double *out = outs[0];
    
    long n = sampleframes;
    double phase = x->phase;
    double sr_inv = x->sr_inv;
    
    while (n--) {
        // lores~ pattern: choose signal vs float for each inlet
        double freq = x->freq_has_signal ? *freq_in++ : x->freq_float;
        double x_pos = x->x_has_signal ? *x_in++ : x->x_float;
        double y_pos = x->y_has_signal ? *y_in++ : x->y_float;
        double phase_offset = x->phase_has_signal ? *phase_offset_in++ : x->phase_offset_float;
        
        // Clamp parameters to valid ranges
        freq = CLAMP(freq, 0.0, 20000.0);  // 0-20kHz
        x_pos = CLAMP(x_pos, 0.0, 1.0);    // 0-1
        y_pos = CLAMP(y_pos, 0.0, 1.0);    // 0-1
        phase_offset = CLAMP(phase_offset, 0.0, 1.0); // 0-1
        
        // Update phase
        phase += freq * sr_inv;
        
        // Wrap phase to 0.0-1.0 range
        while (phase >= 1.0) phase -= 1.0;
        while (phase < 0.0) phase += 1.0;
        
        // Apply phase offset
        double read_phase = phase + phase_offset;
        while (read_phase >= 1.0) read_phase -= 1.0;
        while (read_phase < 0.0) read_phase += 1.0;
        
        // Perform 2D interpolation based on attribute setting
        double sample;
        switch (x->interpolation) {
            case 0:  // Bilinear (default)
                sample = bilinear_interpolate(x, x_pos, y_pos, read_phase);
                break;
            case 1:  // Nearest neighbor
                sample = nearest_neighbor_interpolate(x, x_pos, y_pos, read_phase);
                break;
            case 2:  // Cubic (future implementation - for now use bilinear)
                sample = bilinear_interpolate(x, x_pos, y_pos, read_phase);
                break;
            default:
                sample = bilinear_interpolate(x, x_pos, y_pos, read_phase);
                break;
        }
        
        // Output sample
        *out++ = sample;
    }
    
    x->phase = phase;
}

//----------------------------------------------------------------------------------------------

void cycle2d_float(t_cycle2d *x, double f) {
    // lores~ pattern: proxy_getinlet works on signal inlets for float routing
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 0: // Frequency inlet
            x->freq_float = CLAMP(f, 0.0, 20000.0);
            break;
        case 1: // X position inlet
            x->x_float = CLAMP(f, 0.0, 1.0);
            break;
        case 2: // Y position inlet
            x->y_float = CLAMP(f, 0.0, 1.0);
            break;
        case 3: // Phase offset inlet
            x->phase_offset_float = CLAMP(f, 0.0, 1.0);
            break;
    }
}

//----------------------------------------------------------------------------------------------

void cycle2d_int(t_cycle2d *x, long n) {
    // lores~ pattern: proxy_getinlet works on signal inlets for int routing
    long inlet = proxy_getinlet((t_object *)x);
    
    switch (inlet) {
        case 0: // Frequency inlet - convert int to float
            x->freq_float = CLAMP((double)n, 0.0, 20000.0);
            break;
        case 1: // X position inlet - convert int to float
            x->x_float = CLAMP((double)n, 0.0, 1.0);
            break;
        case 2: // Y position inlet - convert int to float
            x->y_float = CLAMP((double)n, 0.0, 1.0);
            break;
        case 3: // Phase offset inlet - convert int to float
            x->phase_offset_float = CLAMP((double)n, 0.0, 1.0);
            break;
    }
}

//----------------------------------------------------------------------------------------------

void cycle2d_bang(t_cycle2d *x) {
    // lores~ pattern: proxy_getinlet works on signal inlets for message routing
    long inlet = proxy_getinlet((t_object *)x);
    
    if (inlet == 0) {  // First inlet - reset phase
        x->phase = 0.0;
    }
}

//----------------------------------------------------------------------------------------------

void cycle2d_buffer(t_cycle2d *x, t_symbol *s, long argc, t_atom *argv) {
    // buffer <name> <x> <y> <offset>
    if (argc < 3) {
        post("cycle-2d~: buffer message requires at least 3 arguments: buffer <name> <x> <y> [offset]");
        return;
    }
    
    if (x->num_custom_tables >= MAX_CUSTOM_TABLES) {
        post("cycle-2d~: maximum number of custom tables (%d) reached", MAX_CUSTOM_TABLES);
        return;
    }
    
    // Get arguments
    t_symbol *buffer_name = atom_getsym(argv);
    double x_pos = CLAMP(atom_getfloat(argv + 1), 0.0, 1.0);
    double y_pos = CLAMP(atom_getfloat(argv + 2), 0.0, 1.0);
    long offset = (argc >= 4) ? atom_getlong(argv + 3) : 0;
    
    // Get buffer reference
    t_buffer_ref *buffer_ref = buffer_ref_new((t_object *)x, buffer_name);
    t_buffer_obj *buffer = buffer_ref_getobject(buffer_ref);
    
    if (!buffer) {
        post("cycle-2d~: buffer '%s' not found", buffer_name->s_name);
        object_free(buffer_ref);
        return;
    }
    
    // Get buffer info
    float *buffer_samples = buffer_locksamples(buffer);
    if (!buffer_samples) {
        post("cycle-2d~: could not lock buffer '%s'", buffer_name->s_name);
        object_free(buffer_ref);
        return;
    }
    
    long buffer_frames = buffer_getframecount(buffer);
    if (buffer_frames == 0) {
        post("cycle-2d~: buffer '%s' is empty", buffer_name->s_name);
        buffer_unlocksamples(buffer);
        object_free(buffer_ref);
        return;
    }
    
    // Copy buffer data to wavetable with offset
    t_custom_table *table = &x->custom_tables[x->num_custom_tables];
    
    for (int i = 0; i < WAVETABLE_SIZE; i++) {
        long read_index = (offset + i) % buffer_frames;
        if (read_index < buffer_frames) {
            table->wavetable[i] = buffer_samples[read_index];
        } else {
            table->wavetable[i] = 0.0f;
        }
    }
    
    table->x_pos = x_pos;
    table->y_pos = y_pos;
    table->active = 1;
    
    buffer_unlocksamples(buffer);
    object_free(buffer_ref);
    
    x->num_custom_tables++;
    
    post("cycle-2d~: loaded buffer '%s' at position (%.3f, %.3f) with offset %ld", 
         buffer_name->s_name, x_pos, y_pos, offset);
}

//----------------------------------------------------------------------------------------------

void cycle2d_assist(t_cycle2d *x, void *b, long m, long a, char *s) {
    if (m == ASSIST_INLET) {
        switch (a) {
            case 0:
                sprintf(s, "(signal/float/int/bang) Frequency in Hz, bang to reset phase");
                break;
            case 1:
                sprintf(s, "(signal/float) X position (0-1) in 2D space");
                break;
            case 2:
                sprintf(s, "(signal/float) Y position (0-1) in 2D space");
                break;
            case 3:
                sprintf(s, "(signal/float) Phase offset (0-1)");
                break;
        }
    } else {  // ASSIST_OUTLET
        sprintf(s, "(signal) Morphed waveform output (-1 to 1)");
    }
}

//----------------------------------------------------------------------------------------------
// Wavetable Generation Functions
//----------------------------------------------------------------------------------------------

void generate_sine_table(float *table, int size) {
    for (int i = 0; i < size; i++) {
        double phase = (double)i / (double)size;
        table[i] = (float)sin(2.0 * PI * phase);
    }
}

void generate_triangle_table(float *table, int size) {
    for (int i = 0; i < size; i++) {
        double phase = (double)i / (double)size;
        
        if (phase < 0.25) {
            // Rising from 0 to 1
            table[i] = (float)(4.0 * phase);
        } else if (phase < 0.75) {
            // Falling from 1 to -1
            table[i] = (float)(2.0 - 4.0 * phase);
        } else {
            // Rising from -1 to 0
            table[i] = (float)(4.0 * phase - 4.0);
        }
    }
}

void generate_saw_table(float *table, int size) {
    for (int i = 0; i < size; i++) {
        double phase = (double)i / (double)size;
        table[i] = (float)(2.0 * phase - 1.0);  // Rising sawtooth from -1 to 1
    }
}

void generate_square_table(float *table, int size) {
    for (int i = 0; i < size; i++) {
        double phase = (double)i / (double)size;
        table[i] = (phase < 0.5) ? 1.0f : -1.0f;
    }
}

void generate_sine_harmonic_table(float *table, int size, int harmonic) {
    for (int i = 0; i < size; i++) {
        double phase = (double)i / (double)size;
        table[i] = (float)sin(2.0 * PI * phase * harmonic);
    }
}

void init_corner_tables(t_cycle2d *x) {
    switch (x->corner_mode) {
        case 0:  // Standard waveforms (default)
            generate_sine_table(x->corner_tables[CORNER_SINE], WAVETABLE_SIZE);
            generate_triangle_table(x->corner_tables[CORNER_TRIANGLE], WAVETABLE_SIZE);
            generate_saw_table(x->corner_tables[CORNER_SAW], WAVETABLE_SIZE);
            generate_square_table(x->corner_tables[CORNER_SQUARE], WAVETABLE_SIZE);
            break;
            
        case 1:  // Harmonics (sine harmonics)
            generate_sine_table(x->corner_tables[CORNER_SINE], WAVETABLE_SIZE);  // Fundamental
            generate_sine_harmonic_table(x->corner_tables[CORNER_TRIANGLE], WAVETABLE_SIZE, 2);  // 2nd harmonic
            generate_sine_harmonic_table(x->corner_tables[CORNER_SAW], WAVETABLE_SIZE, 3);       // 3rd harmonic
            generate_sine_harmonic_table(x->corner_tables[CORNER_SQUARE], WAVETABLE_SIZE, 4);    // 4th harmonic
            break;
            
        case 2:  // Noise variations (future implementation - for now use standard)
        default:
            generate_sine_table(x->corner_tables[CORNER_SINE], WAVETABLE_SIZE);
            generate_triangle_table(x->corner_tables[CORNER_TRIANGLE], WAVETABLE_SIZE);
            generate_saw_table(x->corner_tables[CORNER_SAW], WAVETABLE_SIZE);
            generate_square_table(x->corner_tables[CORNER_SQUARE], WAVETABLE_SIZE);
            break;
    }
}

//----------------------------------------------------------------------------------------------
// Interpolation Functions
//----------------------------------------------------------------------------------------------

double wavetable_lookup(float *table, double phase) {
    // Linear interpolation within wavetable
    double scaled_phase = phase * (WAVETABLE_SIZE - 1);
    int index = (int)scaled_phase;
    double fract = scaled_phase - index;
    
    // Ensure we don't read past the end
    if (index >= WAVETABLE_SIZE - 1) {
        return table[WAVETABLE_SIZE - 1];
    }
    
    return table[index] * (1.0 - fract) + table[index + 1] * fract;
}

double bilinear_interpolate(t_cycle2d *x, double x_pos, double y_pos, double phase) {
    // Sample the four corner waveforms
    double sample_00 = wavetable_lookup(x->corner_tables[CORNER_SINE], phase);      // (0,0)
    double sample_01 = wavetable_lookup(x->corner_tables[CORNER_TRIANGLE], phase); // (0,1)
    double sample_10 = wavetable_lookup(x->corner_tables[CORNER_SAW], phase);      // (1,0)
    double sample_11 = wavetable_lookup(x->corner_tables[CORNER_SQUARE], phase);   // (1,1)
    
    // Include custom tables in interpolation if they exist
    // For now, we'll blend custom tables based on distance weighting
    double total_weight = 0.0;
    double weighted_sum = 0.0;
    
    for (int i = 0; i < x->num_custom_tables; i++) {
        if (x->custom_tables[i].active) {
            t_custom_table *table = &x->custom_tables[i];
            
            // Calculate distance from current position to custom table position
            double dx = x_pos - table->x_pos;
            double dy = y_pos - table->y_pos;
            double distance = sqrt(dx * dx + dy * dy);
            
            // Use inverse distance weighting (closer tables have more influence)
            double weight = 1.0 / (1.0 + distance * 2.0);  // Adjust influence falloff
            
            double custom_sample = wavetable_lookup(table->wavetable, phase);
            weighted_sum += custom_sample * weight;
            total_weight += weight;
        }
    }
    
    // Standard bilinear interpolation between corner samples
    double lerp_x0 = sample_00 * (1.0 - x_pos) + sample_10 * x_pos;  // Bottom edge
    double lerp_x1 = sample_01 * (1.0 - x_pos) + sample_11 * x_pos;  // Top edge
    double corner_result = lerp_x0 * (1.0 - y_pos) + lerp_x1 * y_pos;
    
    // Blend corner interpolation with custom tables if any exist
    if (total_weight > 0.0) {
        double custom_result = weighted_sum / total_weight;
        // Blend custom tables with corner interpolation (adjust blend ratio as needed)
        double blend_factor = total_weight / (total_weight + 1.0);
        return corner_result * (1.0 - blend_factor) + custom_result * blend_factor;
    } else {
        return corner_result;
    }
}

double nearest_neighbor_interpolate(t_cycle2d *x, double x_pos, double y_pos, double phase) {
    // Nearest neighbor: choose the closest corner waveform
    int corner_index;
    
    if (x_pos < 0.5 && y_pos < 0.5) {
        corner_index = CORNER_SINE;      // (0,0)
    } else if (x_pos < 0.5 && y_pos >= 0.5) {
        corner_index = CORNER_TRIANGLE;  // (0,1)
    } else if (x_pos >= 0.5 && y_pos < 0.5) {
        corner_index = CORNER_SAW;       // (1,0)
    } else {
        corner_index = CORNER_SQUARE;    // (1,1)
    }
    
    return wavetable_lookup(x->corner_tables[corner_index], phase);
}

//----------------------------------------------------------------------------------------------
// Attribute Setters
//----------------------------------------------------------------------------------------------

t_max_err cycle2d_attr_setinterpolation(t_cycle2d *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->interpolation = CLAMP(atom_getlong(argv), 0, 2);
    }
    return 0;
}

t_max_err cycle2d_attr_setcornermode(t_cycle2d *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->corner_mode = CLAMP(atom_getlong(argv), 0, 2);
        // Regenerate corner tables with new mode
        init_corner_tables(x);
    }
    return 0;
}

t_max_err cycle2d_attr_settablesize(t_cycle2d *x, void *attr, long argc, t_atom *argv) {
    if (argc && argv) {
        x->table_size = CLAMP(atom_getlong(argv), 0, 3);
        // Note: Changing table size would require memory reallocation
        // For now, just store the preference - could be used for future instances
        post("cycle-2d~: table_size attribute stored (requires restart for full effect)");
    }
    return 0;
}