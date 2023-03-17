// libAMY

// Brian Whitman
// brian@variogr.am

#include "amy.h"
#include "clipping_lookup_table.h"


#ifdef ESP_PLATFORM
// Defined in amy-example-esp32
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
extern SemaphoreHandle_t xQueueSemaphore;
extern TaskHandle_t amy_render_handle[AMY_CORES]; // one per core
#endif


// JOE
int amy_capture_flag = 0;      // boolean : do we capture?
int amy_capture_capacity = BLOCK_COUNT * BLOCK_SIZE + 1;  // in samples : maximum number of samples to capture
int amy_capture_max = BLOCK_SIZE;
int amy_capture_level = 0;     // in units of samples : current number of samples captured
int amy_capture_full = 0;      // boolean : when we fill the capture, this gets set
int16_t amy_capture_buffer[BLOCK_COUNT * BLOCK_SIZE];

void amy_enable_capture(int flag, int max) {
    // here be race conditions i fear
    if (flag) {
        if (max < 0) max = 0;
        if (max >= amy_capture_capacity) max = amy_capture_capacity;
        amy_capture_max = max;
        amy_capture_level = 0;
    }
    amy_capture_flag = flag;
}

int amy_full(void) {
    return amy_capture_flag;
}

int amy_trim(int length) {
    if (amy_full()) {
        return -1;
    }
    amy_capture_level = length;
    return 0;
}

int amy_frames(void) {
    return amy_capture_level;
}

void amy_set_frames(int length) {
    amy_capture_level = length;
}

int16_t *amy_captured(void) {
    return amy_capture_buffer;
}

// END

// Global state 
struct state global;
// set of deltas for the fifo to be played
struct delta * events;
// state per osc as multi-channel synthesizer that the scheduler renders into
struct event * synth;
// envelope-modified per-osc state
struct mod_event * msynth;

// Two float mixing blocks, one per core of rendering
float ** fbl;
float per_osc_fb[AMY_CORES][BLOCK_SIZE];

// block -- what gets sent to the DAC -- -32768...32767 (int16 LE)
output_sample_type * block;
int64_t total_samples;
uint32_t event_counter ;
uint32_t message_counter ;

char *message_start_pointer;
int16_t message_length;

int64_t computed_delta; // can be negative no prob, but usually host is larger # than client
uint8_t computed_delta_set; // have we set a delta yet?

int8_t check_init(amy_err_t (*fn)(), char *name) {
    fprintf(stderr,"Starting %s: ", name);
    const amy_err_t ret = (*fn)();
    if(ret != AMY_OK) {
#ifdef ESP_PLATFORM
        fprintf(stderr,"[ERROR:%i (%s)]\n", ret, esp_err_to_name((esp_err_t)ret));
#else
        fprintf(stderr,"[ERROR:%i]\n", ret);
#endif
        return -1;
    }
    fprintf(stderr,"[OK]\n");
    return 0;
}



int8_t global_init() {
    // function pointers
    global.next_event_write = 0;
    global.event_start = NULL;
    global.event_qsize = 0;
    global.volume = 1;
    global.latency_ms = 0;
    global.eq[0] = 0;
    global.eq[1] = 0;
    global.eq[2] = 0;
    global.hpf_state = 0;
    return 0;
}


float freq_for_midi_note(uint8_t midi_note) {
    return 440.0*pow(2,(midi_note-69.0)/12.0);
}


// Create a new default event -- mostly -1 or no change
struct event amy_default_event() {
    struct event e;
    e.status = EMPTY;
    e.time = 0;
    e.osc = 0;
    e.step = 0;
    e.substep = 0;
    e.sample = DOWN;
    e.patch = -1;
    e.wave = -1;
    e.phase = -1;
    e.duty = -1;
    e.feedback = -1;
    e.velocity = -1;
    e.midi_note = -1;
    e.amp = -1; 
    e.freq = -1;
    e.volume = -1;
    e.latency_ms = -1;
    e.ratio = -1;
    e.filter_freq = -1;
    e.resonance = -1;
    e.filter_type = -1;
    e.mod_source = -1;
    e.mod_target = -1;
    e.note_on_clock = -1;
    e.note_off_clock = -1;
    e.eq_l = -1;
    e.eq_m = -1;
    e.eq_h = -1;
    e.algorithm = -1;
    for(uint8_t i=0;i<MAX_ALGO_OPS;i++) e.algo_source[i] = -2;
    for(uint8_t i=0;i<MAX_BREAKPOINT_SETS;i++) { for(uint8_t j=0;j<MAX_BREAKPOINTS;j++) { e.breakpoint_times[i][j] = -1; e.breakpoint_values[i][j] = -1; } e.breakpoint_target[i] = -1; }
    return e;
}




void add_delta_to_queue(struct delta d) {
#ifdef ESP_PLATFORM
    //  Take the queue mutex before starting
    xSemaphoreTake(xQueueSemaphore, portMAX_DELAY);
#endif
    if(global.event_qsize < EVENT_FIFO_LEN) {
        // scan through the memory to find a free slot, starting at write pointer
        uint16_t write_location = global.next_event_write;
        int16_t found = -1;
        // guaranteed to find eventually if qsize stays accurate
        while(found<0) {
            if(events[write_location].time == UINT32_MAX) found = write_location;
            write_location = (write_location + 1) % EVENT_FIFO_LEN;
        }
        // Found a mem location. Copy the data in and update the write pointers.
        events[found].time = d.time;
        events[found].osc = d.osc;
        events[found].param = d.param;
        events[found].data = d.data;
        global.next_event_write = write_location;
        global.event_qsize++;

        // Now insert it into the sorted list for fast playback
        // First, see if it's eariler than the first item, special case
        if(d.time < global.event_start->time) {
            events[found].next = global.event_start;
            global.event_start = &events[found];
        } else {
            // or it's got to be found somewhere
            struct delta* ptr = global.event_start; 
            int8_t inserted = -1;
            while(inserted<0) {
                if(d.time < ptr->next->time) { 
                    // next should point to me, and my next should point to old next
                    events[found].next = ptr->next;
                    ptr->next = &events[found];
                    inserted = 1;
                }
                ptr = ptr->next;
            }
        }
        event_counter++;

    } else {
        // If there's no room in the queue, just skip the message
        // TODO -- report this somehow? 
    }
#ifdef ESP_PLATFORM
    xSemaphoreGive( xQueueSemaphore );
#endif
}


void amy_add_event(struct event e) {
    // make delta objects out of the UDP event and add them to the queue
    struct delta d;
    d.osc = e.osc;
    d.time = e.time;
    if(e.wave>-1) { d.param=WAVE; d.data = *(uint32_t *)&e.wave; add_delta_to_queue(d); }
    if(e.patch>-1) { d.param=PATCH; d.data = *(uint32_t *)&e.patch; add_delta_to_queue(d); }
    if(e.midi_note>-1) { d.param=MIDI_NOTE; d.data = *(uint32_t *)&e.midi_note; add_delta_to_queue(d); }
    if(e.amp>-1) {  d.param=AMP; d.data = *(uint32_t *)&e.amp; add_delta_to_queue(d); }
    if(e.duty>-1) { d.param=DUTY; d.data = *(uint32_t *)&e.duty; add_delta_to_queue(d); }
    if(e.feedback>-1) { d.param=FEEDBACK; d.data = *(uint32_t *)&e.feedback; add_delta_to_queue(d); }
    if(e.freq>-1) { d.param=FREQ; d.data = *(uint32_t *)&e.freq; add_delta_to_queue(d); }
    if(e.phase>-1) { d.param=PHASE; d.data = *(uint32_t *)&e.phase; add_delta_to_queue(d); }
    if(e.volume>-1) { d.param=VOLUME; d.data = *(uint32_t *)&e.volume; add_delta_to_queue(d); }
    if(e.latency_ms>-1) { d.param=LATENCY; d.data = *(uint32_t *)&e.latency_ms; add_delta_to_queue(d); }
    if(e.ratio>-1) { d.param=RATIO; d.data = *(uint32_t *)&e.ratio; add_delta_to_queue(d); }
    if(e.filter_freq>-1) { d.param=FILTER_FREQ; d.data = *(uint32_t *)&e.filter_freq; add_delta_to_queue(d); }
    if(e.resonance>-1) { d.param=RESONANCE; d.data = *(uint32_t *)&e.resonance; add_delta_to_queue(d); }
    if(e.mod_source>-1) { d.param=MOD_SOURCE; d.data = *(uint32_t *)&e.mod_source; add_delta_to_queue(d); }
    if(e.mod_target>-1) { d.param=MOD_TARGET; d.data = *(uint32_t *)&e.mod_target; add_delta_to_queue(d); }
    if(e.breakpoint_target[0]>-1) { d.param=BP0_TARGET; d.data = *(uint32_t *)&e.breakpoint_target[0]; add_delta_to_queue(d); }
    if(e.breakpoint_target[1]>-1) { d.param=BP1_TARGET; d.data = *(uint32_t *)&e.breakpoint_target[1]; add_delta_to_queue(d); }
    if(e.breakpoint_target[2]>-1) { d.param=BP2_TARGET; d.data = *(uint32_t *)&e.breakpoint_target[2]; add_delta_to_queue(d); }
    if(e.filter_type>-1) { d.param=FILTER_TYPE; d.data = *(uint32_t *)&e.filter_type; add_delta_to_queue(d); }
    if(e.algorithm>-1) { d.param=ALGORITHM; d.data = *(uint32_t *)&e.algorithm; add_delta_to_queue(d); }
    for(uint8_t i=0;i<MAX_ALGO_OPS;i++) 
        if(e.algo_source[i]>-2) { d.param=ALGO_SOURCE_START+i; d.data = *(uint32_t *)&e.algo_source[i]; add_delta_to_queue(d); }
    for(uint8_t i=0;i<MAX_BREAKPOINTS;i++) {
        if(e.breakpoint_times[0][i]>-1) { d.param=BP_START+(i*2)+(0*MAX_BREAKPOINTS*2); d.data = *(uint32_t *)&e.breakpoint_times[0][i]; add_delta_to_queue(d); }
        if(e.breakpoint_times[1][i]>-1) { d.param=BP_START+(i*2)+(1*MAX_BREAKPOINTS*2); d.data = *(uint32_t *)&e.breakpoint_times[1][i]; add_delta_to_queue(d); }
        if(e.breakpoint_times[2][i]>-1) { d.param=BP_START+(i*2)+(2*MAX_BREAKPOINTS*2); d.data = *(uint32_t *)&e.breakpoint_times[2][i]; add_delta_to_queue(d); }
        if(e.breakpoint_values[0][i]>-1) { d.param=BP_START+(i*2 + 1)+(0*MAX_BREAKPOINTS*2); d.data = *(uint32_t *)&e.breakpoint_values[0][i]; add_delta_to_queue(d); }
        if(e.breakpoint_values[1][i]>-1) { d.param=BP_START+(i*2 + 1)+(1*MAX_BREAKPOINTS*2); d.data = *(uint32_t *)&e.breakpoint_values[1][i]; add_delta_to_queue(d); }
        if(e.breakpoint_values[2][i]>-1) { d.param=BP_START+(i*2 + 1)+(2*MAX_BREAKPOINTS*2); d.data = *(uint32_t *)&e.breakpoint_values[2][i]; add_delta_to_queue(d); }
    }

    if(e.eq_l>-1) { d.param=EQ_L; d.data = *(uint32_t *)&e.eq_l; add_delta_to_queue(d); }
    if(e.eq_m>-1) { d.param=EQ_M; d.data = *(uint32_t *)&e.eq_m; add_delta_to_queue(d); }
    if(e.eq_h>-1) { d.param=EQ_H; d.data = *(uint32_t *)&e.eq_h; add_delta_to_queue(d); }

    // Add this last -- this is a trigger, that if sent alongside osc setup parameters, you want to run after those
    if(e.velocity>-1) { d.param=VELOCITY; d.data = *(uint32_t *)&e.velocity; add_delta_to_queue(d); }
    message_counter++;
}

void reset_osc(uint8_t i ) {
    // set all the synth state to defaults
    synth[i].osc = i; // self-reference to make updating oscs easier
    synth[i].wave = SINE;
    synth[i].duty = 0.5;
    msynth[i].duty = 0.5;
    synth[i].patch = -1;
    synth[i].midi_note = 0;
    synth[i].freq = 0;
    msynth[i].freq = 0;
    synth[i].feedback = 0; //.996; TODO KS feedback is v different from FM feedback
    msynth[i].feedback = 0; //.996; TODO KS feedback is v different from FM feedback
    synth[i].amp = 1;
    msynth[i].amp = 1;
    synth[i].phase = 0;
    synth[i].latency_ms = 0;
    synth[i].volume = 0;
    synth[i].eq_l = 0;
    synth[i].eq_m = 0;
    synth[i].eq_h = 0;
    synth[i].ratio = -1;
    synth[i].filter_freq = 0;
    msynth[i].filter_freq = 0;
    synth[i].resonance = 0.7;
    msynth[i].resonance = 0.7;
    synth[i].velocity = 0;
    synth[i].step = 0;
    synth[i].sample = DOWN;
    synth[i].substep = 0;
    synth[i].status = OFF;
    synth[i].mod_source = -1;
    synth[i].mod_target = 0; 
    synth[i].note_on_clock = -1;
    synth[i].note_off_clock = -1;
    synth[i].filter_type = FILTER_NONE;
    synth[i].lpf_state = 0;
    synth[i].lpf_alpha = 0;
    synth[i].last_amp = 0;
    synth[i].dc_offset = 0;
    synth[i].algorithm = 0;
    for(uint8_t j=0;j<MAX_ALGO_OPS;j++) synth[i].algo_source[j] = -2;
    for(uint8_t j=0;j<MAX_BREAKPOINT_SETS;j++) { for(uint8_t k=0;k<MAX_BREAKPOINTS;k++) { synth[i].breakpoint_times[j][k] =-1; synth[i].breakpoint_values[j][k] = -1; } synth[i].breakpoint_target[j] = 0; }
    for(uint8_t j=0;j<MAX_BREAKPOINT_SETS;j++) { synth[i].last_scale[j] = 0; }
    synth[i].last_two[0] = 0;
    synth[i].last_two[1] = 0;
}

void amy_reset_oscs() {
    for(uint8_t i=0;i<OSCS;i++) reset_osc(i);
    // Also reset filters and volume
    global.volume = 1;
    global.eq[0] = 0;
    global.eq[1] = 0;
    global.eq[2] = 0;
}



// The synth object keeps held state, whereas events are only deltas/changes
int8_t oscs_init() {
    ks_init();
    filters_init();
    algo_init();
    pcm_init();
    events = (struct delta*)malloc(sizeof(struct delta) * EVENT_FIFO_LEN);
    synth = (struct event*) malloc(sizeof(struct event) * OSCS);
    msynth = (struct mod_event*) malloc(sizeof(struct mod_event) * OSCS);

    block = (output_sample_type *) malloc(sizeof(output_sample_type) * BLOCK_SIZE);//dbl_block[0];
    // Set all oscillators to their default values
    amy_reset_oscs();

    // Make a fencepost last event with no next, time of end-1, and call it start for now, all other events get inserted before it
    events[0].next = NULL;
    events[0].time = UINT32_MAX - 1;
    events[0].osc = 0;
    events[0].data = 0;
    events[0].param = NO_PARAM;
    global.next_event_write = 1;
    global.event_start = &events[0];
    global.event_qsize = 1; // queue will always have at least 1 thing in it 

    // Set all the other events to empty
    for(uint16_t i=1;i<EVENT_FIFO_LEN;i++) { 
        events[i].time = UINT32_MAX;
        events[i].next = NULL;
        events[i].osc = 0;
        events[i].data = 0;
        events[i].param = NO_PARAM;
    }
    fbl = (float**) malloc(sizeof(float*) * 2); // one per core, just core 0 used off esp32
    fbl[0]= (float*)malloc(sizeof(float) * BLOCK_SIZE);
    fbl[1]= (float*)malloc(sizeof(float) * BLOCK_SIZE);
    // Clear out both as local mode won't use fbl[1] 
    for(uint16_t i=0;i<BLOCK_SIZE;i++) { fbl[0][i] =0; fbl[1][i] = 0;}
    total_samples = 0;
    computed_delta = 0;
    computed_delta_set = 0;
    event_counter = 0;
    message_counter = 0;
    return 0;
}



void _show_debug(uint8_t type, FILE *out) {
    if(type>1) {
        struct delta * ptr = global.event_start;
        uint16_t q = global.event_qsize;
        if(q > 25) q = 25;
        fprintf(out, "{\"queue\":[");
        for(uint16_t i=0;i<q;i++) {
            char c = ',';
            if (i == q-1) c = ']';
            fprintf(out, "{\"time\":%u, \"osc\":%d, \"param\":%d, [%f, %d]}%c\n",
                ptr->time,
                ptr->osc,
                ptr->param,
                *(float *)&ptr->data,
                *(int *)&ptr->data,
                c);
            ptr = ptr->next;
        }
    }
    if(type>2) {
        // print out all the osc data
        char c = ',';
        fprintf(out,",\"volume\":%f, \"eq\":[%f, %f, %f],\n",
            global.volume,
            global.eq[0],
            global.eq[1],
            global.eq[2]);
        fprintf(out, "\"bank\":[\n");
        for(uint8_t i=0;i<OSCS;i++) {
            if (i == OSCS-1) c = ']';
            fprintf(out,"{\"osc\":%d, \"status\":%d, \"amp\":%f, \"wave\":%d, \"freq\":%f, \"duty\":%f, \"mod_target\":%d \"mod_source\":%d, \"velocity\":%f, \"filter_freq\":%f, \"ratio\":%f, \"feedback\":%f, \"resonance\":%f, \"step\":%f, \"algo\":%d, \"source\":[%d,%d,%d,%d,%d,%d]}%c\n",
                i,
                synth[i].status,
                synth[i].amp,
                synth[i].wave,
                synth[i].freq,
                synth[i].duty,

                synth[i].mod_target,
                synth[i].mod_source,
                synth[i].velocity,
                synth[i].filter_freq,
                synth[i].ratio,
                synth[i].feedback,
                synth[i].resonance,

                synth[i].step,
                synth[i].algorithm,
                synth[i].algo_source[0],
                synth[i].algo_source[1],
                synth[i].algo_source[2],
                synth[i].algo_source[3],
                synth[i].algo_source[4],
                synth[i].algo_source[5],
                c);
            if(type>3) { 
                fprintf(out, "[");
                for(uint8_t j=0;j<MAX_BREAKPOINT_SETS;j++) {
                    fprintf(out,"{\"bp%d\":%d, [", j, synth[i].breakpoint_target[j]);
                    for(uint8_t k=0;k<MAX_BREAKPOINTS;k++) {
                        fprintf(out,"[%d, %f],", synth[i].breakpoint_times[j][k], synth[i].breakpoint_values[j][k]);
                    }
                    fprintf(out,"]\n");
                }
                fprintf(out,"\"mod_osc\":%d, \"amp\":%f, \"freq\":%f, \"duty\":%f, \"filter_freq\":%f, \"resonance:\"%f, \"fb_bw\":%f\n",
                    i,
                    msynth[i].amp,
                    msynth[i].freq,
                    msynth[i].duty,
                    msynth[i].filter_freq,
                    msynth[i].resonance,
                    msynth[i].feedback);
            }
        }
        fprintf(out, "}\n");
    }
    if(type>1) {
        fprintf(out, "}\n");
    }
}

/*
D2
{"queue":[{"time":7549, "osc":1, "param":7, [1.000000, 1065353216]},
{"time":9549, "osc":2, "param":7, [1.000000, 1065353216]},
{"time":13549, "osc":3, "param":0, [0.000000, 1441799]},
{"time":13549, "osc":3, "param":1, [-nan, -65514]},
{"time":13549, "osc":3, "param":6, [440.000000, 1138491392]},
{"time":13549, "osc":3, "param":7, [1.000000, 1065353216]},
{"time":4294967294, "osc":0, "param":86, [0.000000, 0]}]
}
*/

void show_debug(uint8_t type) { 
    _show_debug(type, stderr);
}
   
void oscs_deinit() {
    free(block);
    free(fbl[0]);
    free(fbl[1]);
    free(fbl);
    free(synth);
    free(msynth);
    free(events);

    ks_deinit();
    filters_deinit();
}



// Play an event, now -- tell the audio loop to start making noise
void play_event(struct delta d) {
    uint8_t trig=0;
    // TODO: event-only side effect, remove
    if(d.param == MIDI_NOTE) { synth[d.osc].midi_note = *(uint16_t *)&d.data; synth[d.osc].freq = freq_for_midi_note(*(uint16_t *)&d.data); } 
    if(d.param == WAVE) synth[d.osc].wave = *(int16_t *)&d.data; 
    if(d.param == PHASE) synth[d.osc].phase = *(float *)&d.data;
    if(d.param == PATCH) synth[d.osc].patch = *(int16_t *)&d.data;
    if(d.param == DUTY) synth[d.osc].duty = *(float *)&d.data;
    if(d.param == FEEDBACK) synth[d.osc].feedback = *(float *)&d.data;
    if(d.param == AMP) synth[d.osc].amp = *(float *)&d.data; 
    if(d.param == FREQ) synth[d.osc].freq = *(float *)&d.data;
    
    if(d.param == BP0_TARGET) { synth[d.osc].breakpoint_target[0] = *(int16_t *)&d.data; trig=1; }
    if(d.param == BP1_TARGET) { synth[d.osc].breakpoint_target[1] = *(int16_t *)&d.data; trig=1; }
    if(d.param == BP2_TARGET) { synth[d.osc].breakpoint_target[2] = *(int16_t *)&d.data; trig=1; }
    // TODO, i really should clean this up
    if(d.param >= BP_START && d.param < BP_END) {
        uint8_t pos = d.param - BP_START;
        uint8_t bp_set = 0;
        if(pos > (MAX_BREAKPOINTS * 2)) { bp_set = 1; pos = pos - (MAX_BREAKPOINTS * 2); }
        if(pos > (MAX_BREAKPOINTS * 4)) { bp_set = 2; pos = pos - (MAX_BREAKPOINTS * 2); } 
        if(pos % 2 == 0) {
            synth[d.osc].breakpoint_times[bp_set][pos / 2] = *(int32_t *)&d.data;
        } else {
            synth[d.osc].breakpoint_values[bp_set][(pos-1) / 2] = *(float *)&d.data;
        }
        trig=1;
    }
    if(trig) synth[d.osc].note_on_clock = total_samples;

    // TODO: event-only side effect, remove
    if(d.param == MOD_SOURCE) { synth[d.osc].mod_source = *(int8_t *)&d.data; synth[*(int8_t *)&d.data].status = IS_MOD_SOURCE; }
    if(d.param == MOD_TARGET) synth[d.osc].mod_target = *(int8_t *)&d.data; 

    if(d.param == RATIO) synth[d.osc].ratio = *(float *)&d.data;

    if(d.param == FILTER_FREQ) synth[d.osc].filter_freq = *(float *)&d.data;
    if(d.param == FILTER_TYPE) synth[d.osc].filter_type = *(int8_t *)&d.data; 
    if(d.param == RESONANCE) synth[d.osc].resonance = *(float *)&d.data;

    if(d.param == ALGORITHM) synth[d.osc].algorithm = *(int8_t *)&d.data; 

    if(d.param >= ALGO_SOURCE_START && d.param < ALGO_SOURCE_END) {
        uint8_t which_source = d.param - ALGO_SOURCE_START;
        synth[d.osc].algo_source[which_source] = *(int8_t *)&d.data; 
        synth[*(int8_t*)&d.data].status=IS_ALGO_SOURCE;
    }

    // For global changes, just make the change, no need to update the per-osc synth
    if(d.param == VOLUME) global.volume = *(float *)&d.data;
    if(d.param == LATENCY) { global.latency_ms = *(int16_t *)&d.data; computed_delta_set = 0; }
    if(d.param == EQ_L) global.eq[0] = powf(10, *(float *)&d.data / 20.0);
    if(d.param == EQ_M) global.eq[1] = powf(10, *(float *)&d.data / 20.0);
    if(d.param == EQ_H) global.eq[2] = powf(10, *(float *)&d.data / 20.0);

    // Triggers / envelopes 
    // The only way a sound is made is if velocity (note on) is >0.
    if(d.param == VELOCITY && *(float *)&d.data > 0) { // New note on (even if something is already playing on this osc)
        synth[d.osc].amp = *(float *)&d.data; // these could be decoupled, later
        synth[d.osc].velocity = *(float *)&d.data;
        synth[d.osc].status = AUDIBLE;
        // Take care of FM & KS first -- no special treatment for bp/MOD
        if(synth[d.osc].wave==KS) { ks_note_on(d.osc); } 
        else {
            // an osc came in with a note on.
            // Start the bp clock
            synth[d.osc].note_on_clock = total_samples; //esp_timer_get_time() / 1000;

            // If there was a filter active for this voice, reset it
            if(synth[d.osc].filter_type != FILTER_NONE) update_filter(d.osc);
            // Restart the waveforms, adjusting for phase if given
            if(synth[d.osc].wave==SINE) sine_note_on(d.osc);
            if(synth[d.osc].wave==SAW_DOWN) saw_down_note_on(d.osc);
            if(synth[d.osc].wave==SAW_UP) saw_up_note_on(d.osc);
            if(synth[d.osc].wave==TRIANGLE) triangle_note_on(d.osc);
            if(synth[d.osc].wave==PULSE) pulse_note_on(d.osc);
            if(synth[d.osc].wave==PCM) pcm_note_on(d.osc);
            if(synth[d.osc].wave==ALGO) algo_note_on(d.osc);
            if(synth[d.osc].wave==PARTIAL) partial_note_on(d.osc);
            if(synth[d.osc].wave==PARTIALS) partials_note_on(d.osc);
            // Trigger the MOD source, if we have one
            if(synth[d.osc].mod_source >= 0) {
                if(synth[synth[d.osc].mod_source].wave==SINE) sine_mod_trigger(synth[d.osc].mod_source);
                if(synth[synth[d.osc].mod_source].wave==SAW_DOWN) saw_up_mod_trigger(synth[d.osc].mod_source);
                if(synth[synth[d.osc].mod_source].wave==SAW_UP) saw_down_mod_trigger(synth[d.osc].mod_source);
                if(synth[synth[d.osc].mod_source].wave==TRIANGLE) triangle_mod_trigger(synth[d.osc].mod_source);
                if(synth[synth[d.osc].mod_source].wave==PULSE) pulse_mod_trigger(synth[d.osc].mod_source);
                if(synth[synth[d.osc].mod_source].wave==PCM) pcm_mod_trigger(synth[d.osc].mod_source);
            }

        }
    } else if(synth[d.osc].velocity > 0 && d.param == VELOCITY && *(float *)&d.data == 0) { // new note off
        synth[d.osc].velocity = 0;
        if(synth[d.osc].wave==KS) { ks_note_off(d.osc); }
        else if(synth[d.osc].wave==ALGO) { algo_note_off(d.osc); } 
        else if(synth[d.osc].wave==PARTIAL) { partial_note_off(d.osc); }
        else if(synth[d.osc].wave==PARTIALS) { partials_note_off(d.osc); }
        else if(synth[d.osc].wave==PCM) { pcm_note_off(d.osc); }
        else {
            // osc note off, start release
            synth[d.osc].note_on_clock = -1;
            synth[d.osc].note_off_clock = total_samples;    
        }
    }

}

// Apply an mod & bp, if any, to the osc
void hold_and_modify(uint8_t osc) {
    // Copy all the modifier variables
    msynth[osc].amp = synth[osc].amp;
    msynth[osc].duty = synth[osc].duty;
    msynth[osc].freq = synth[osc].freq;
    msynth[osc].feedback = synth[osc].feedback;
    msynth[osc].filter_freq = synth[osc].filter_freq;
    msynth[osc].resonance = synth[osc].resonance;

    // Modify the synth params by scale -- bp scale is (original * scale)
    float all_set_scale = 0;
    for(uint8_t i=0;i<MAX_BREAKPOINT_SETS;i++) {
        float scale = compute_breakpoint_scale(osc, i);
        if(synth[osc].breakpoint_target[i] & TARGET_AMP) msynth[osc].amp = msynth[osc].amp * scale;
        if(synth[osc].breakpoint_target[i] & TARGET_DUTY) msynth[osc].duty = msynth[osc].duty * scale;
        if(synth[osc].breakpoint_target[i] & TARGET_FREQ) msynth[osc].freq = msynth[osc].freq * scale;
        if(synth[osc].breakpoint_target[i] & TARGET_FEEDBACK) msynth[osc].feedback = msynth[osc].feedback * scale;
        if(synth[osc].breakpoint_target[i] & TARGET_FILTER_FREQ) msynth[osc].filter_freq = msynth[osc].filter_freq * scale;
        if(synth[osc].breakpoint_target[i] & TARGET_RESONANCE) msynth[osc].resonance = msynth[osc].resonance * scale;
        all_set_scale = all_set_scale + scale;
    }
    if(all_set_scale == 0) { // all BP sets were 0, which means we are in a note off and nobody is active anymore. time to stop the note.
        synth[osc].status=OFF;
        synth[osc].note_off_clock = -1;
    }

    // And the mod -- mod scale is (original + (original * scale))
    float scale = compute_mod_scale(osc);
    if(synth[osc].mod_target & TARGET_AMP) msynth[osc].amp = msynth[osc].amp + (msynth[osc].amp * scale);
    if(synth[osc].mod_target & TARGET_DUTY) msynth[osc].duty = msynth[osc].duty + (msynth[osc].duty * scale);
    if(synth[osc].mod_target & TARGET_FREQ) msynth[osc].freq = msynth[osc].freq + (msynth[osc].freq * scale);
    if(synth[osc].mod_target & TARGET_FEEDBACK) msynth[osc].feedback = msynth[osc].feedback + (msynth[osc].feedback * scale);
    if(synth[osc].mod_target & TARGET_FILTER_FREQ) msynth[osc].filter_freq = msynth[osc].filter_freq + (msynth[osc].filter_freq * scale);
    if(synth[osc].mod_target & RESONANCE) msynth[osc].resonance = msynth[osc].resonance + (msynth[osc].resonance * scale);
}


void render_task(uint8_t start, uint8_t end, uint8_t core) {
    for(uint16_t i=0;i<BLOCK_SIZE;i++) { fbl[core][i] = 0; per_osc_fb[core][i] = 0; }
    for(uint8_t osc=start; osc<end; osc++) {
        if(synth[osc].status==AUDIBLE) { // skip oscs that are silent or mod sources from playback
            for(uint16_t i=0;i<BLOCK_SIZE;i++) { per_osc_fb[core][i] = 0; }
            hold_and_modify(osc); // apply bp / mod
            if(synth[osc].wave == NOISE) render_noise(per_osc_fb[core], osc);
            if(synth[osc].wave == SAW_DOWN) render_saw_down(per_osc_fb[core], osc);
            if(synth[osc].wave == SAW_UP) render_saw_up(per_osc_fb[core], osc);
            if(synth[osc].wave == PULSE) render_pulse(per_osc_fb[core], osc);
            if(synth[osc].wave == TRIANGLE) render_triangle(per_osc_fb[core], osc);
            if(synth[osc].wave == SINE) render_sine(per_osc_fb[core], osc);
            if(synth[osc].wave == KS) render_ks(per_osc_fb[core], osc);
            if(synth[osc].wave == PCM) render_pcm(per_osc_fb[core], osc);
            if(synth[osc].wave == ALGO) render_algo(per_osc_fb[core], osc);
            if(synth[osc].wave == PARTIAL) render_partial(per_osc_fb[core], osc);
            if(synth[osc].wave == PARTIALS) render_partials(per_osc_fb[core], osc);
            // Check it's not off, just in case. TODO, why do i care?
            if(synth[osc].wave != OFF) {
                // Apply filter to osc if set
                if(synth[osc].filter_type != FILTER_NONE) filter_process(per_osc_fb[core], osc);
                for(uint16_t i=0;i<BLOCK_SIZE;i++) { fbl[core][i] += per_osc_fb[core][i]; }
            }
        }
    }
    // apply the EQ filters if set
    if(global.eq[0] != 0 || global.eq[1] != 0 || global.eq[2] != 0) parametric_eq_process(fbl[core]);
}

int amy_sample_rate(int n) {
    return SAMPLE_RATE;
}


// On all platforms, sysclock is based on total samples played, using audio out (i2s or etc) as system clock
int64_t amy_sysclock() {
    return (int64_t)((total_samples / (float)SAMPLE_RATE) * 1000);
}


void amy_increase_volume() {
    global.volume += 0.5;
    if(global.volume > MAX_VOLUME) global.volume = MAX_VOLUME;    
}

void amy_decrease_volume() {
    global.volume -= 0.5;
    if(global.volume < 0) global.volume = 0;    
}

#ifndef ESP_PLATFORM
// Shim for webaudio callback
int web_audio_buffer(float *samples, int length) {
    int16_t *s = fill_audio_buffer_task();
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        samples[i] = (float)s[i]/32767.0f;
    }
    return BLOCK_SIZE;
}
#endif

// This takes scheduled events and plays them at the right time
int16_t * fill_audio_buffer_task() {
    // Check to see which sounds to play 
    int64_t sysclock = amy_sysclock(); 

#ifdef ESP_PLATFORM
    // put a mutex around this so that the event parser doesn't touch these while i'm running  
    xSemaphoreTake(xQueueSemaphore, portMAX_DELAY);
#endif

    // Find any events that need to be played from the (in-order) queue
    while(sysclock >= global.event_start->time) {
        play_event(*global.event_start);
        global.event_start->time = UINT32_MAX;
        global.event_qsize--;
        global.event_start = global.event_start->next;
    }
#ifdef ESP_PLATFORM
    // Give the mutex back
    xSemaphoreGive(xQueueSemaphore);

    // Tell the rendering threads to start rendering
    xTaskNotifyGive(amy_render_handle[0]);
    if(AMY_CORES == 2) xTaskNotifyGive(amy_render_handle[1]);

    // And wait for each of them to come back
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    if(AMY_CORES == 2) ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
#else
    // TODO -- there's no reason we can't multicore render on other platforms
    render_task(0, OSCS, 0);        
#endif

    // Global volume is supposed to max out at 10, so scale by 0.1.
    float volume_scale = 0.1f * global.volume;
    //uint8_t nonzero = 0;
    for(int16_t i=0; i < BLOCK_SIZE; ++i) {
        // Mix all the oscillator buffers into one
        float fsample = volume_scale * (fbl[0][i] + fbl[1][i]) * 32767.0f;
        // One-pole high-pass filter to remove large low-frequency excursions from
        // some FM patches. b = [1 -1]; a = [1 -0.995]
        float new_state = fsample + 0.995f * global.hpf_state;
        fsample = new_state - global.hpf_state;
        global.hpf_state = new_state;
    
        // Soft clipping.
        int positive = 1; 
        if (fsample < 0) positive = 0;
        // Using a uint gives us factor-of-2 headroom (up to 65535 not 32767).
        uint16_t uintval;
        if (positive) {  // avoid fabs()
            uintval = (int)fsample;
        } else {
            uintval = (int)(-fsample);
        }
        if (uintval >= FIRST_NONLIN) {
            if (uintval >= FIRST_HARDCLIP) {
                uintval = SAMPLE_MAX;
            } else {
                uintval = clipping_lookup_table[uintval - FIRST_NONLIN];
            }
        }
        int16_t sample;
        if (positive) {
            sample = uintval;
        } else {
            sample = -uintval;
        }
#ifdef ESP_PLATFORM
        // ESP32's i2s driver has this bug
        block[i ^ 0x01] = sample;
#else
        block[i] = sample;
#endif
    }
    total_samples += BLOCK_SIZE;
    // JOE
    if (amy_capture_flag) {
        // do the capture
        for(int16_t i=0; i < BLOCK_SIZE; ++i) {
            amy_capture_buffer[amy_capture_level++] = block[i];
        }
        if (amy_capture_level >= amy_capture_max) {
            amy_capture_flag = 0;
        }
    }
    // END
    return block;
}

int32_t ms_to_samples(int32_t ms) {
    return (int32_t)(((float)ms / 1000.0) * (float)SAMPLE_RATE);
} 

// Helper to parse the list of source voices for an algorithm
void parse_algorithm(struct event * e, char *message) {
    uint8_t idx = 0;
    uint16_t c = 0;
    uint16_t stop = MAX_MESSAGE_LEN;
    for(uint16_t i=0;i<MAX_MESSAGE_LEN;i++) {
        if(message[i] >= 'A' || message[i] == 0) { stop =i; i = MAX_MESSAGE_LEN; }
    }
    while(c < stop) {
        if(message[c]!=',') {
            e->algo_source[idx] = atoi(message+c);
        }
        while(message[c]!=',' && message[c]!=0 && c < MAX_MESSAGE_LEN) c++;
        c++; idx++;
    }

}

// Helper to parse the special bp string
void parse_breakpoint(struct event * e, char* message, uint8_t which_bpset) {
    uint8_t idx = 0;
    uint16_t c = 0;
    // Set the breakpoint to default (-1) first
    for(uint8_t i=0;i<MAX_BREAKPOINTS;i++) {
        e->breakpoint_times[which_bpset][i] = -1;
        e->breakpoint_values[which_bpset][i] = -1;
    }
    uint16_t stop = MAX_MESSAGE_LEN;
    for(uint16_t i=0;i<MAX_MESSAGE_LEN;i++) {
        if(message[i] >= 'A' || message[i] == 0) { stop =i; i = MAX_MESSAGE_LEN; }
    }
    while(c < stop) {
        if(message[c]!=',') {
            if(idx % 2 == 0) {
                e->breakpoint_times[which_bpset][idx/2] = ms_to_samples(atoi(message+c));
            } else {
                e->breakpoint_values[which_bpset][(idx-1) / 2] = atof(message+c);
            }
        }
        while(message[c]!=',' && message[c]!=0 && c < MAX_MESSAGE_LEN) c++;
        c++; idx++;
    }
}

// given a string return an event
struct event amy_parse_message(char * message) {
    uint8_t mode = 0;
    uint8_t osc = 0;
    uint16_t start = 0;
    uint16_t c = 0;
    int16_t length = strlen(message);
    struct event e = amy_default_event();
    int64_t sysclock = amy_sysclock();

    // Cut the OSC cruft Max etc add, they put a 0 and then more things after the 0
    int new_length = length; 
    for(int d=0;d<length;d++) {
        if(message[d] == 0) { new_length = d; d = length + 1;  } 
    }
    length = new_length;

    while(c < length+1) {
        uint8_t b = message[c];
        //if(b == '_' && c==0) sync_response = 1;
        if( ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z')) || b == 0) {  // new mode or end
            if(mode=='t') {
                e.time=atol(message + start);
                // if we haven't yet synced our times, do it now
                if(!computed_delta_set) {
                    computed_delta = e.time - sysclock;
                    printf("{\"computed_delta\":%ld, \"e_time\":%ld, \"sysclock\":%ld, \"max_drift_ms\":%d, \"latency\":%d} /****/\n",
                        computed_delta,
                        e.time,
                        sysclock,
                        MAX_DRIFT_MS,
                        global.latency_ms);
                    /*
                    fprintf(stderr,
                        "# ! setting computed delta to %lld (e.time is %lld sysclock %lld) max_drift_ms %d latency %d\n",
                        computed_delta,
                        e.time,
                        sysclock,
                        MAX_DRIFT_MS,
                        global.latency_ms);
                    */
                    computed_delta_set = 1;
                }
            } else {
                if(mode >= 'A' && mode <= 'z') {
                    switch(mode) {
                        case 'a': e.amp=atof(message+start); break;
                        case 'A': parse_breakpoint(&e, message+start, 0); break;
                        case 'B': parse_breakpoint(&e, message+start, 1); break;
                        case 'b': e.feedback=atof(message+start); break; 
                        case 'C': parse_breakpoint(&e, message+start, 2); break; 
                        case 'd': e.duty=atof(message + start); break; 
                        case 'D': show_debug(atoi(message + start)); break; 
                        // reminder: don't use "E" or "e", lol 
                        case 'f': e.freq=atof(message + start);  break; 
                        case 'F': e.filter_freq=atof(message + start); break; 
                        case 'G': e.filter_type=atoi(message + start); break; 
                        case 'g': e.mod_target = atoi(message + start);  break; 
                        case 'I': e.ratio = atof(message + start); break; 
                        case 'l': e.velocity=atof(message + start); break; 
                        case 'L': e.mod_source=atoi(message + start); break; 
                        case 'N': e.latency_ms = atoi(message + start);  break; 
                        case 'n': e.midi_note=atoi(message + start); break; 
                        case 'o': e.algorithm=atoi(message+start); break; 
                        case 'O': parse_algorithm(&e, message+start); break; 
                        case 'p': e.patch=atoi(message + start); break; 
                        case 'P': e.phase=atof(message + start); break; 
                        case 'R': e.resonance=atof(message + start); break; 
                        case 'S': osc = atoi(message + start); if(osc > OSCS-1) { amy_reset_oscs(); } else { reset_osc(osc); } break; 
                        case 'T': e.breakpoint_target[0] = atoi(message + start);  break; 
                        case 'W': e.breakpoint_target[1] = atoi(message + start);  break; 
                        case 'v': e.osc=(atoi(message + start) % OSCS);  break; // allow osc wraparound
                        case 'V': e.volume = atof(message + start); break; 
                        case 'X': e.breakpoint_target[2] = atoi(message + start); break; 
                        case 'w': e.wave=atoi(message + start); break; 
                        case 'x': e.eq_l = atof(message+start); break; 
                        case 'y': e.eq_m = atof(message+start); break; 
                        case 'z': e.eq_h = atof(message+start); break; 
                        default:
                            break;
                    }
                }
            }
            mode=b;
            start=c+1;
        }
        c++;
    }

    // Only do this if we got some data
    if(length >0) {
        // TODO -- should time adjustment happen during parsing or playback?

        // Now adjust time in some useful way:
        // if we have a delta & got a time in this message, use it schedule it properly
        if((computed_delta_set && e.time > 0)) {
            // OK, so check for potentially negative numbers here (or really big numbers-sysclock) 
            int64_t potential_time = (e.time - computed_delta) + global.latency_ms;
            if(potential_time < 0 || (potential_time > sysclock + global.latency_ms + MAX_DRIFT_MS)) {
                //fprintf(stderr,"recomputing time base: message came in with %lld, mine is %lld, computed delta was %lld\n", e.time, sysclock, computed_delta);
                computed_delta = e.time - sysclock;
                //fprintf(stderr,"computed delta now %lld\n", computed_delta);
            }
            e.time = (e.time - computed_delta) + global.latency_ms;
        } else { // else play it asap 
            e.time = sysclock + global.latency_ms;
        }
        e.status = SCHEDULED;
        return e;
    }
    return amy_default_event();
}

// given a string play / schedule the event directly 
void amy_play_message(char *message) {
    struct event e = amy_parse_message(message);
    if(e.status == SCHEDULED) {
        amy_add_event(e);
    }
}

void amy_stop() {
    oscs_deinit();
}

void amy_start_web(uint8_t t) {
    global_init();
    oscs_init();
    amy_reset_oscs();
}

void amy_start() {
    global_init();
    oscs_init();
    amy_reset_oscs();
}

