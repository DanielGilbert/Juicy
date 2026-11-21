/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's sequencer and mixer: they run in the audio ISR (app_audio), so a step starts on its exact frame.
 * The main loop and the ISR share the variables below, each written by one side only (docs/RULES.md).
 * juicy.c includes it after song.c. */

static uint32_t square_root(uint32_t value)     /* rounded down */
{
    uint32_t root = 0, bit = 1u << 30;
    while (bit > value) bit >>= 2;
    for (; bit; bit >>= 2) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else root >>= 1;
    }
    return root;
}

static void melodic_shape(uint32_t melodic, synth_shape_t *shape)   /* (the audio ISR's) the synth as the sound says */
{
    const volatile uint8_t *knobs = song.sounds[melodic];
    const uint32_t sub = knobs[SOUND_KNOB_SUB], osc2 = knobs[SOUND_KNOB_OSC2_LEVEL];
    shape->wave = knobs[SOUND_KNOB_WAVE];
    shape->gain = MELODIC[melodic].gains[knobs[SOUND_KNOB_WAVE]];
    shape->damping = SYNTH_DAMPING[knobs[SOUND_KNOB_RESONANCE]];
    shape->attack_rate = SYNTH_ATTACK_RATE[knobs[SOUND_KNOB_ATTACK]];
    shape->envelope_amount = knobs[SOUND_KNOB_ENVELOPE];
    shape->release_rate = MELODIC[melodic].release_rate;
    shape->osc2_interval = knobs[SOUND_KNOB_OSC2];
    shape->glide_rate = SYNTH_GLIDE_RATE[knobs[SOUND_KNOB_GLIDE]];
    shape->osc1_weight = 32768;
    shape->sub_weight = shape->osc2_weight = 0;
    if (sub || osc2) {                          /* the three together as loud (RMS) as the first alone: each over sqrt(1 + sub^2 + osc2^2) */
        shape->osc1_weight = (int32_t)(32768u * 1600u / square_root((10000u + sub * sub + osc2 * osc2) << 8));   /* (100 * 16: the root of << 8) */
        shape->sub_weight = shape->osc1_weight * (int32_t)sub / 100;
        shape->osc2_weight = shape->osc1_weight * (int32_t)osc2 / 100;
    }
}

static void melodic_note_on(uint32_t melodic, uint32_t value, int accented)   /* value: as in song.notes; (the pad: no accents) */
{
    if (melodic == MELODIC_PAD)
        pad_chord_on(value & NOTE_MASK, value >> CHORD_SHIFT, song.sounds[MELODIC_PAD][0], SYNTH_GLIDE_RATE[song.sounds[MELODIC_PAD][SOUND_KNOB_GLIDE]]);
    else {
        synth_shape_t shape;
        melodic_shape(melodic, &shape);
        synth_note_on(&melodic_voices[melodic], &shape, value, accented);
    }
}

static void melodic_note_off(uint32_t melodic)
{
    if (melodic == MELODIC_PAD) pad_chord_off();
    else synth_note_off(&melodic_voices[melodic]);
}

/* ---- the sequencer (runs in the audio ISR, so a step starts on its exact frame) */
#define FRAMES_PER_STEP_Q8(tempo) (FS * 15u * 256u / (tempo))   /* a step is a sixteenth note: 60 / tempo / 4 seconds */
#define AUDITION_FRAMES (FS / 5u)               /* a note played by hand sounds this long */
#define BLOCK_FRAMES_MAX 256u                   /* app_audio is never asked for more (docs/API.md) */
#define CLOCK_RING 97u                          /* MIDI clock in: a bar's clocks (96) and the one before */
#define CLOCK_TEMPO_CLOCKS 6u                   /* the tempo is measured once a step's clocks are there */
#define CLOCK_GONE_MS 500u                      /* no clock this long: Juicy's own tempo again */
enum { CLOCK_START_NONE, CLOCK_START_START, CLOCK_START_CONTINUE };

static volatile uint32_t playing;               /* (the main loop writes) */
static volatile uint32_t frames_per_step_q8;    /* (the main loop writes) */
static volatile uint32_t drum_auditions[DRUM_COUNT];   /* counts up when the main loop wants to hear a drum */
static volatile uint32_t note_auditions[MELODIC_COUNT], audition_notes[MELODIC_COUNT];   /* the same for a note (the note is written first) */
static volatile uint32_t note_held[MELODIC_COUNT];   /* the key that plays that note is down: it sounds until the key lets go (the main loop writes) */
static volatile uint32_t live_starts[MELODIC_COUNT];   /* recording while playing: 1 + the step (of the track) the held key's note starts on, 0: none;
                                                        * it sounds already, the step does not start it again (the main loop writes) */
static volatile uint32_t chain_on;              /* the chain plays, not the pattern on the screen (the main loop writes) */
static volatile uint32_t muted_tracks;          /* bit t: track t (a page: DRUMS, then the melodic tracks) does not play its pattern (the main loop writes) */
static volatile uint32_t loop_pattern;          /* the pattern on the screen: it plays when the chain does not (the main loop writes) */
static volatile uint32_t playing_step;          /* the step that sounds now, counted from the pattern's bar 1 (the ISR writes) */
static volatile uint32_t playing_pattern;       /* the pattern that sounds now (the ISR writes) */
static volatile uint32_t playing_slot;          /* its place in the chain, CHAIN_SLOTS = it is not from the chain (the ISR writes) */
static volatile uint32_t resume_playing;        /* PLAY goes on from where it stopped, not from the top: MIDI CONTINUE (the main loop writes) */
static volatile uint32_t play_starts;           /* one more: what plays starts over from the top, as after a STOP (the main loop writes) */
static volatile uint32_t steps_since_play;      /* the steps started since PLAY (the ISR writes) ... */
static volatile uint32_t step_started_ms;       /* ... and when the last of them did, in fm1_ms (the ISR writes) */

/* ---- the LFOs (lfo.c): where they are, in step with the song. Playing, each step puts them where it is (counted from
 * PLAY); in between, and while stopped, they move on at the tempo */
static uint32_t lfo_position;                   /* (the ISR's) Q24 steps into a lap of LFO_LAP_STEPS (it wraps: a lap) ... */
static uint32_t lfo_laps;                       /* ... and the laps */
static lfo_random_t lfo_randoms[MELODIC_COUNT] = {{0x9E3779B9u, ~0u, 0, 0}, {0x3C6EF372u, ~0u, 0, 0},
                                                  {0xDAA66D2Bu, ~0u, 0, 0}, {0x78DDE6E4u, ~0u, 0, 0}};

typedef struct {                                /* what a melodic track's LFO does to it now */
    uint32_t cutoff_q8;                         /* its CUTOFF (in 1/256 of a step) */
    uint32_t pitch_q16;                         /* its notes' pitch times this */
    int32_t gain;                               /* its loudness, Q12 */
} lfo_effect_t;

static void lfo_effect(uint32_t melodic, lfo_effect_t *effect)   /* (the audio ISR's) */
{
    const volatile lfo_t *lfo = lfo_of(&song.patterns[playing ? playing_pattern : loop_pattern], melodic);
    const uint32_t rate = lfo->rate, target = lfo->target;
    const int32_t depth = lfo->depth;
    int32_t wave, cutoff_q8;
    effect->cutoff_q8 = song.cutoff[melodic] << 8;
    effect->pitch_q16 = SYNTH_PITCH_UNCHANGED;
    effect->gain = 4096;
    if (!depth) return;
    wave = lfo_wave(lfo->shape, lfo_position << (6u - rate), lfo_laps * (64u >> rate) + (lfo_position >> 26 >> rate), &lfo_randoms[melodic]);
    if (target == LFO_TARGET_CUTOFF) {          /* (wave * DEPTH / 256: 50 steps at the most, in 1/256 of one) */
        cutoff_q8 = (int32_t)effect->cutoff_q8 + wave * depth / (int32_t)(32767u * LFO_DEPTH_MAX / (LFO_CUTOFF_STEPS * 256u));
        effect->cutoff_q8 = cutoff_q8 < 0 ? 0u : cutoff_q8 > (int32_t)(SYNTH_PARAMETER_MAX << 8) ? SYNTH_PARAMETER_MAX << 8 : (uint32_t)cutoff_q8;
    } else if (target == LFO_TARGET_PITCH)      /* (DEPTH squared: fine steps where it is small) */
        effect->pitch_q16 = lfo_pitch_q16(wave * depth * depth / (int32_t)(32767u * LFO_DEPTH_MAX * LFO_DEPTH_MAX / (LFO_PITCH_SEMITONES * 256u)));
    else {                                      /* LEVEL: full at the wave's top, down by DEPTH % at its bottom */
        effect->gain = 4096 - (32767 - wave) * depth / (int32_t)(2u * 32767u * LFO_DEPTH_MAX / 4096u);
        if (effect->gain < 0) effect->gain = 0;
    }
}

/* ---- the drone (drone.c): its note, TONE and SWELL (it breathes along a DRIFT wave of the LFOs' over 8 bars) */
#define DRONE_SWELL_RATE 5u                     /* (a cycle of 4 << this steps) */
#define DRONE_TONE_STEPS 90u                    /* TONE 100: the filter at CUTOFF 90 */
#define DRONE_SWELL_STEPS 15u                   /* SWELL 100: the filter up and down by 15 steps, the level down to nothing */
#define DRONE_REVERB_SEND 60u                   /* of MIX_MAX: how much of it goes into the reverb (none into the echo) */
static uint32_t drone_chord_root = 12u;         /* (the ISR's) the root of the pad's last chord, 0..11; 12: none yet */
static lfo_random_t drone_random = {0x2545F491u, ~0u, 0, 0};

static void drone_effect(lfo_effect_t *effect)  /* (the audio ISR's) its filter and the swell's level now */
{
    const int32_t swell = song.drone_swell;
    const int32_t wave = swell ? lfo_wave(LFO_SHAPE_DRIFT, lfo_position << (6u - DRONE_SWELL_RATE),
                                          lfo_laps * (64u >> DRONE_SWELL_RATE) + (lfo_position >> 26 >> DRONE_SWELL_RATE), &drone_random) : 0;
    const int32_t cutoff_q8 = (int32_t)(song.drone_tone * DRONE_TONE_STEPS * 256u / SYNTH_PARAMETER_MAX)
                              + wave * swell / (int32_t)(32767u * SYNTH_PARAMETER_MAX / (DRONE_SWELL_STEPS * 256u));
    effect->cutoff_q8 = cutoff_q8 < 0 ? 0u : (uint32_t)cutoff_q8;
    effect->gain = 4096 - (32767 - wave) * swell / (int32_t)(2u * 32767u * SYNTH_PARAMETER_MAX / 4096u);
    if (effect->gain < 0) effect->gain = 0;
}

/* the mixer: each track (mono) at its LEVEL and PAN into out, and from there into the echo and the reverb; the echo
 * comes back into both. Gains are Q12 (4096: as it is), so the mix as it was made adds up to the same numbers.
 * Playing, each track is also at its level in the playing pattern's own mix (stopped: as it is); from one pattern to
 * the next that level glides (PATTERN_GAIN_STEP a frame), so a note that rings on does not click */
#define PATTERN_GAIN_STEP 8                     /* Q12 a frame: from silent to full in 512 frames, about 12 ms */
static void render(int32_t *out, uint32_t frame_count)   /* at most BLOCK_FRAMES_MAX */
{
    static int32_t track_out[BLOCK_FRAMES_MAX], echo_send[BLOCK_FRAMES_MAX], reverb_send[BLOCK_FRAMES_MAX];
    static int32_t echo_return[2u * BLOCK_FRAMES_MAX];   /* (L R L R ...) */
    static int32_t pattern_gains[MIXER_TRACKS] = {4096, 4096, 4096, 4096, 4096, 4096};   /* each track's in the pattern's mix, as it glides */
    uint32_t track, i;
    memset(echo_send, 0, frame_count * sizeof echo_send[0]);
    memset(reverb_send, 0, frame_count * sizeof reverb_send[0]);
    memset(echo_return, 0, 2u * frame_count * sizeof echo_return[0]);
    for (track = 0; track < MIXER_TRACKS; track++) {   /* the drums, the melodic tracks, the drone (in the middle, no echo) */
        const int drone_track = track == DRONE_MIXER_TRACK;
        const uint32_t melodic = track - 1u, pan = drone_track ? MIX_MAX / 2u : song.pans[track];
        const int32_t level = drone_track ? (int32_t)(song.drone_level * 4096u / MIX_MAX)
                            : track ? (int32_t)(song.levels[melodic] * 4096u / MIX_MAX) : 4096;   /* (the drums': each drum's) */
        const int32_t left = pan <= MIX_MAX / 2u ? 4096 : (int32_t)((MIX_MAX - pan) * 4096u / (MIX_MAX / 2u));
        const int32_t right = pan >= MIX_MAX / 2u ? 4096 : (int32_t)(pan * 4096u / (MIX_MAX / 2u));
        const int32_t echo_gain = drone_track ? 0 : (int32_t)((track ? song.echo[melodic] : song.drum_echo) * 5u);   /* of 512: echo / 100, near enough */
        const int32_t reverb_gain = (int32_t)((drone_track ? DRONE_REVERB_SEND : song.reverb_sends[track]) * 4096u / MIX_MAX);
        const volatile pattern_t *const playing_one = &song.patterns[playing_pattern];
        lfo_effect_t lfo = {0, SYNTH_PITCH_UNCHANGED, 4096};   /* (the drums have none) */
        int32_t pattern_gain_wanted, pattern_gain = pattern_gains[track];
        if (drone_track && !level) continue;    /* (no drone: nothing to do) */
        if (drone_track) drone_effect(&lfo);
        else if (track) lfo_effect(melodic, &lfo);
        pattern_gain_wanted = (drone_track ? (playing_one->mixer_mutes >> track & 1u ? 0 : (int32_t)(playing_one->mixer_levels[track] * 4096u / MIX_MAX))
                               : !playing ? 4096 : (int32_t)(playing_one->mixer_levels[track] * 4096u / MIX_MAX))   /* (the drone fades out in the last
                                                   pattern's mix; the tracks' mutes are on their steps) */
                              * lfo.gain >> 12;   /* (the LFO's LEVEL glides with it) */
        memset(track_out, 0, frame_count * sizeof track_out[0]);
        if (drone_track) drone_render(track_out, frame_count, playing != 0, drone_note_for(drone_chord_root), lfo.cutoff_q8);
        else if (!track) {
            int32_t drum_levels[DRUM_COUNT];
            uint32_t drum;
            for (drum = 0; drum < DRUM_COUNT; drum++) drum_levels[drum] = (int32_t)(song.drum_levels[drum] * 4096u / MIX_MAX);
            drums_render(track_out, frame_count, drum_levels);
        } else if (melodic == MELODIC_PAD)
            pad_render(track_out, frame_count, lfo.cutoff_q8, song.sounds[melodic][1], song.sounds[melodic][2], song.sounds[melodic][3],
                       song.sounds[melodic][SOUND_KNOB_SUB], lfo.pitch_q16);
        else {
            synth_shape_t shape;
            melodic_shape(melodic, &shape);
            synth_render(&melodic_voices[melodic], &shape, track_out, frame_count, lfo.cutoff_q8, song.decay[melodic], lfo.pitch_q16);
        }
        for (i = 0; i < frame_count; i++) {
            int32_t sample;
            if (pattern_gain != pattern_gain_wanted)
                pattern_gain += pattern_gain_wanted - pattern_gain > PATTERN_GAIN_STEP ? PATTERN_GAIN_STEP
                              : pattern_gain - pattern_gain_wanted > PATTERN_GAIN_STEP ? -PATTERN_GAIN_STEP : pattern_gain_wanted - pattern_gain;
            sample = (((track_out[i] * level) >> 12) * pattern_gain) >> 12;
            out[2u * i] += (sample * left) >> 12;
            out[2u * i + 1u] += (sample * right) >> 12;
            echo_send[i] += (sample * echo_gain) >> 9;
            reverb_send[i] += (sample * reverb_gain) >> 12;
        }
        pattern_gains[track] = pattern_gain;
    }
    echo_render(echo_return, echo_send, frame_count, ECHO_TIME_STEPS[song.echo_time] * frames_per_step_q8 >> 8,
                song.echo_feedback, song.echo_width, song.echo_tone);
    for (i = 0; i < frame_count; i++) {         /* the echo: heard, and into the reverb (both sides: in the middle, as it was, they are the same) */
        out[2u * i] += echo_return[2u * i];
        out[2u * i + 1u] += echo_return[2u * i + 1u];
        reverb_send[i] += (echo_return[2u * i] + echo_return[2u * i + 1u]) >> 1;
    }
    reverb_render(out, reverb_send, frame_count, song.reverb);
    limiter_render(out, frame_count);
    {                                           /* the LFOs move on by these frames, at the tempo (2^32 / frames_per_step_q8: Q24 steps a frame) */
        const uint32_t position_before = lfo_position;
        const uint32_t frame_steps = frames_per_step_q8;   /* (0 before the main loop's first pass) */
        if (frame_steps) lfo_position += frame_count * (0xFFFFFFFFu / frame_steps);
        if (lfo_position < position_before) lfo_laps++;
    }
}

/* ARP's note for its hit number position over chord (as on the pad's track): the chord's notes from its root
 * (an octave up while it is below ARP's lowest note), over MODE's octaves, in MODE's order */
static uint32_t arp_note(uint32_t chord, uint32_t mode, uint32_t position)
{
    static uint32_t random_state = 1;
    const uint32_t chord_type = chord >> CHORD_SHIFT, note_count = PAD_CHORDS[chord_type].note_count;
    const uint32_t order = mode / ARP_OCTAVES_MAX, count = note_count * (mode % ARP_OCTAVES_MAX + 1u);
    uint32_t root = chord & NOTE_MASK, index = position % count;
    while (root < MELODIC[MELODIC_ARP].note_lowest) root += 12u;
    if (order == ARP_ORDER_DOWN) index = count - 1u - index;
    else if (order == ARP_ORDER_UP_DOWN && count > 1u) {   /* up and down again, the top and the bottom once each (one note: just that) */
        index = position % (2u * count - 2u);
        if (index >= count) index = 2u * count - 2u - index;
    } else if (order == ARP_ORDER_RANDOM) {
        random_state = random_state * 1664525u + 1013904223u;
        index = (random_state >> 16) % count;
    }
    return root + index / note_count * 12u + PAD_CHORDS[chord_type].intervals[index % note_count];
}

/* ---- GEN, ARP's MODE that plays itself: a loop of up to GEN_LENGTH_MAX random values, counted from PLAY (so a
 * short loop drifts against the bars). Each time a step comes round, CHANGE is how likely its value is a new one. A
 * value's low byte over DENSITY is a rest, its high byte the note among NOTES' (the chord's, from its root up, as MODE
 * UP; or the key's scale) */
static volatile uint16_t gen_values[GEN_LENGTH_MAX];   /* (the ISR writes; the GEN page shows them) */
static volatile uint32_t gen_place = GEN_LENGTH_MAX;   /* the step of the loop that played last, GEN_LENGTH_MAX: none yet (the ISR writes) */
static uint32_t gen_random_state = 0x6C078965u, gen_filled;

static uint32_t gen_random(void)                /* 0..65535 */
{
    gen_random_state = gen_random_state * 1664525u + 1013904223u;
    return gen_random_state >> 16;
}

static uint32_t gen_note(const volatile pattern_t *pattern, uint32_t chord, uint32_t step_count)   /* MIDI, 0: a rest */
{
    const uint32_t place = step_count % pattern->gen_length, octaves = pattern->gen_notes % GEN_OCTAVES + 1u;
    uint32_t value, index;
    if (!gen_filled) {                          /* (the first time: a loop of random values) */
        for (index = 0; index < GEN_LENGTH_MAX; index++) gen_values[index] = (uint16_t)gen_random();
        gen_filled = 1;
    }
    if (gen_random() % GEN_PARAMETER_MAX < pattern->gen_change) gen_values[place] = (uint16_t)gen_random();
    gen_place = place;
    value = gen_values[place];
    if ((value & 255u) * GEN_PARAMETER_MAX >= pattern->gen_density * 256u) return 0;
    if (pattern->gen_notes < GEN_NOTES_SCALE) {   /* the chord's notes over the octaves (none: silent, as ARP) */
        const uint32_t count = PAD_CHORDS[chord >> CHORD_SHIFT].note_count * octaves;
        return chord ? arp_note(chord, ARP_MODE(ARP_ORDER_UP, octaves), (value >> 8) * count >> 8) : 0u;
    } else {                                    /* the key's scale from its root (at ARP's lowest note or above) */
        const uint32_t key = song_key(), scale = KEY_SCALES[key == KEY_OFF ? 0u : (key - 1u) & 1u];
        const uint32_t step_in_scale = (value >> 8) * (7u * octaves) >> 8;
        uint32_t note = key == KEY_OFF ? 9u : (key - 1u) / 2u, interval = 0, found = 0;
        while (note < MELODIC[MELODIC_ARP].note_lowest) note += 12u;
        for (interval = 0; interval < 12u; interval++)   /* (the scale's step_in_scale % 7-th note) */
            if (scale >> interval & 1u && found++ == step_in_scale % 7u) break;
        return note + step_in_scale / 7u * 12u + interval;
    }
}

static void app_audio(int32_t *out, uint32_t frame_count)
{
    static uint32_t was_playing, next_step, frames_to_step_q8, drum_auditions_done[DRUM_COUNT];
    static uint32_t note_auditions_done[MELODIC_COUNT], audition_frames_left[MELODIC_COUNT];
    static uint32_t pad_pattern_value;          /* the chord the pattern holds on the pad now, 0 = none */
    static uint32_t arp_chord;                  /* the same, muted or not: what ARP plays over (unless the pad's keys play one) */
    static uint32_t arp_chord_used, arp_position;   /* the chord ARP's last hit came from, and the hits since it began */
    static uint32_t pattern_now, chain_position;   /* what plays, and the last place of the chain that played */
    static uint32_t step_waits, swing_frames_left, waiting_bar, waiting_step;   /* a step that has started and not sounded yet (swing) */
    static uint32_t gate_frames_left[MELODIC_COUNT];   /* a note shorter than its step: the frames until it lets go, 0: none */
    static uint32_t play_starts_done;
    uint32_t frames_done = 0, drum, melodic;
    for (drum = 0; drum < DRUM_COUNT; drum++)
        if (drum_auditions_done[drum] != drum_auditions[drum]) {
            drum_auditions_done[drum] = drum_auditions[drum];
            drums_trigger(drum, 0);
        }
    for (melodic = 0; melodic < MELODIC_COUNT; melodic++) {
        if (note_auditions_done[melodic] != note_auditions[melodic]) {
            note_auditions_done[melodic] = note_auditions[melodic];
            melodic_note_on(melodic, audition_notes[melodic], 0);
            audition_frames_left[melodic] = AUDITION_FRAMES;
            gate_frames_left[melodic] = 0;
        } else if (audition_frames_left[melodic]) {
            if (note_held[melodic])
                audition_frames_left[melodic] = 1;   /* it ends when the key lets go */
            else if (audition_frames_left[melodic] <= frame_count) {
                audition_frames_left[melodic] = 0;
                if (melodic == MELODIC_PAD && pad_pattern_value) {   /* back to the pattern's chord (the same one, recorded: it goes on) */
                    if (pad_pattern_value != audition_notes[melodic]) melodic_note_on(melodic, pad_pattern_value, 0);
                }
                else melodic_note_off(melodic);
            } else
                audition_frames_left[melodic] -= frame_count;
        }
    }
    if (!playing || play_starts_done != play_starts) {   /* STOP (or started over: a preview went on to another place) */
        play_starts_done = play_starts;
        if (was_playing)                        /* the drums ring out, the notes let go */
            for (melodic = 0; melodic < MELODIC_COUNT; melodic++)
                melodic_note_off(melodic);
        was_playing = 0;
        pad_pattern_value = arp_chord = arp_chord_used = 0;
        for (melodic = 0; melodic < MELODIC_COUNT; melodic++) gate_frames_left[melodic] = 0;
        if (!playing) {
            render(out, frame_count);
            return;
        }
    }
    if (!was_playing) {                         /* PLAY: from the top, at once (CONTINUE: on from the step after the last) */
        was_playing = 1;
        if (!resume_playing) {
            next_step = ~0u;                    /* (past any pattern's end: the first step picks the pattern) */
            chain_position = CHAIN_SLOTS - 1u;  /* (the chain starts at its first place) */
        }
        frames_to_step_q8 = 0;
        steps_since_play = 0;
        step_waits = 0;
    }
    while (frames_done < frame_count) {
        uint32_t run_frames;
        if (frames_to_step_q8 < 256u) {         /* less than a frame left: the step starts here */
            if (next_step >= pattern_bars(&song.patterns[pattern_now]) * STEPS_PER_BAR) {   /* the pattern's end (or bars were taken away): which one now? */
                uint32_t tried;
                next_step = 0;
                pad_pattern_value = arp_chord = 0;   /* the pad's chord ends with its pattern (a chord on step 1 takes over without a gap) */
                melodic_note_off(MELODIC_PAD);
                pattern_now = loop_pattern;
                playing_slot = CHAIN_SLOTS;
                for (tried = 1; chain_on && tried <= CHAIN_SLOTS; tried++) {   /* the chain's next place that holds a pattern */
                    uint32_t slot = (chain_position + tried) % CHAIN_SLOTS;
                    if (song.chain[slot]) {
                        pattern_now = song.chain[slot] - 1u;
                        playing_slot = chain_position = slot;
                        break;
                    }
                }
                playing_pattern = pattern_now;
            }
            waiting_bar = next_step / STEPS_PER_BAR;   /* (of the pattern: a track with fewer bars goes round) */
            waiting_step = next_step % STEPS_PER_BAR;
            step_waits = 1;                     /* every second step sounds later by the swing: up to 0.6 of a step (80 %) */
            swing_frames_left = waiting_step & 1u ? (song.patterns[pattern_now].swing * frames_per_step_q8 / 50u) >> 8 : 0u;
            playing_step = next_step;
            next_step++;
            step_started_ms = fm1_ms + frames_done * 1000u / FS;
            steps_since_play = steps_since_play + 1u;
            lfo_position = (steps_since_play - 1u) % LFO_LAP_STEPS << 24;   /* the LFOs where this step is */
            lfo_laps = (steps_since_play - 1u) / LFO_LAP_STEPS;
            frames_to_step_q8 += frames_per_step_q8;   /* (the fraction carries over: the tempo does not drift) */
        }
        if (step_waits && !swing_frames_left) { /* the step sounds: on its start, a swung one later (always before the next starts) */
            const volatile pattern_t *pattern = &song.patterns[pattern_now];
            const uint32_t bar = waiting_bar, step = waiting_step;
            const uint32_t muted = muted_tracks | pattern->mixer_mutes;   /* (muted everywhere, or in this pattern) */
            step_waits = 0;
            for (drum = 0; drum < DRUM_COUNT; drum++)
                if (!(muted & 1u) && pattern->drum_hits[drum][bar % pattern->bar_totals[0]] >> step & 1u)
                    drums_trigger(drum, pattern->accents[0][bar % pattern->bar_totals[0]] >> step & 1u);
            {                                   /* a chord on the pad's step: ARP starts over on it (the pad comes after ARP below) */
                const uint32_t pad_bar = bar % pattern->bar_totals[1u + MELODIC_PAD];
                const uint32_t chord = step_value(pattern->notes[MELODIC_PAD][pad_bar], pattern->gates[MELODIC_PAD][pad_bar], MELODIC_PAD, step);
                if (chord) {
                    arp_chord = chord;
                    drone_chord_root = (chord & NOTE_MASK) % 12u;
                    arp_position = 0;
                }
            }
            for (melodic = 0; melodic < MELODIC_COUNT; melodic++) {   /* a note lasts its step and its ties: an empty step lets it go */
                const uint32_t track_bar = bar % pattern->bar_totals[1u + melodic];
                const int generated = melodic == MELODIC_ARP && pattern->arp_mode == ARP_MODE_GEN;   /* (GEN: not its hits, a note of the loop's on each step) */
                const uint32_t gate = melodic == MELODIC_PAD || generated ? 0u : note_gate(pattern->gates[melodic][track_bar], step);   /* (the pad's holds its chord) */
                uint32_t note = generated ? gen_note(pattern, audition_frames_left[MELODIC_PAD] ? audition_notes[MELODIC_PAD] : arp_chord, steps_since_play - 1u)
                                          : step_value(pattern->notes[melodic][track_bar], pattern->gates[melodic][track_bar], melodic, step);
                if (muted >> (1u + melodic) & 1u) note = 0;   /* muted: as if the steps were empty */
                if (melodic == MELODIC_PAD) {   /* but the pad holds its chord until the next one */
                    if (!note && muted >> (1u + melodic) & 1u && pad_pattern_value) {   /* (muted: not that one either) */
                        pad_pattern_value = 0;
                        melodic_note_off(melodic);
                    }
                    if (!note) continue;
                    pad_pattern_value = note;
                } else if (note & NOTE_TIE)
                    continue;                   /* tied: the note before goes on */
                if (note && !generated && note_held[melodic] && live_starts[melodic] == 1u + track_bar * STEPS_PER_BAR + step && pattern_now == loop_pattern)
                    continue;                   /* recorded while playing, its key still down: it sounds already */
                if (melodic == MELODIC_ARP && note && !generated) {   /* a hit: the chord's next note (the one the pad's keys play, if they do; none: silent) */
                    const uint32_t chord = audition_frames_left[MELODIC_PAD] ? audition_notes[MELODIC_PAD] : arp_chord;
                    if (chord != arp_chord_used) {
                        arp_chord_used = chord;
                        arp_position = 0;
                    }
                    note = !chord ? 0u : pattern->arp_mode == ARP_MODE_TONES ? arp_tone_note(chord, note)   /* (TONES: the hit's own tone) */
                                                                             : arp_note(chord, pattern->arp_mode, arp_position++);
                }
                if (note) melodic_note_on(melodic, note, !generated && pattern->accents[1u + melodic][track_bar] >> step & 1u);
                else melodic_note_off(melodic);
                audition_frames_left[melodic] = 0;
                gate_frames_left[melodic] = note ? gate * frames_per_step_q8 / 10u >> 8 : 0u;
                if (note && gate && !gate_frames_left[melodic]) gate_frames_left[melodic] = 1;
            }
        }
        run_frames = frames_to_step_q8 >> 8;
        if (step_waits && run_frames > swing_frames_left) run_frames = swing_frames_left;
        for (melodic = 0; melodic < MELODIC_COUNT; melodic++)
            if (gate_frames_left[melodic] && run_frames > gate_frames_left[melodic]) run_frames = gate_frames_left[melodic];
        if (run_frames > frame_count - frames_done) run_frames = frame_count - frames_done;
        render(out + 2u * frames_done, run_frames);
        frames_done += run_frames;
        frames_to_step_q8 -= run_frames << 8;
        if (step_waits) swing_frames_left -= run_frames;
        for (melodic = 0; melodic < MELODIC_COUNT; melodic++)
            if (gate_frames_left[melodic] && !(gate_frames_left[melodic] -= run_frames)) melodic_note_off(melodic);
    }
}

static int sound_quiet(void)
{
    uint32_t melodic;
    for (melodic = 0; melodic < MELODIC_COUNT; melodic++)
        if (!synth_quiet(&melodic_voices[melodic])) return 0;
    return drums_quiet() && pad_quiet() && echo_quiet() && reverb_quiet();
}
