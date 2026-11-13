/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's sequencer and mixer: they run in the audio ISR (app_audio), so a step starts on its exact frame.
 * The main loop and the ISR share the variables below, each written by one side only (docs/RULES.md).
 * juicy.c includes it after song.c. */

static void melodic_shape(uint32_t melodic, synth_shape_t *shape)   /* (the audio ISR's) the synth as the sound says */
{
    const volatile uint8_t *knobs = song.sounds[melodic];
    shape->wave = knobs[0];
    shape->gain = MELODIC[melodic].gains[knobs[0]];
    shape->damping = SYNTH_DAMPING[knobs[1]];
    shape->attack_rate = SYNTH_ATTACK_RATE[knobs[2]];
    shape->envelope_amount = knobs[3];
    shape->release_rate = MELODIC[melodic].release_rate;
}

static void melodic_note_on(uint32_t melodic, uint32_t value, int accented)   /* value: as in song.notes; (the pad: no accents) */
{
    if (melodic == MELODIC_PAD) pad_chord_on(value & NOTE_MASK, value >> CHORD_SHIFT, song.sounds[MELODIC_PAD][0]);
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

/* the mixer: each track (mono) at its LEVEL and PAN into out, and from there into the echo and the reverb; the echo
 * comes back into both. Gains are Q12 (4096: as it is), so the mix as it was made adds up to the same numbers. */
static void render(int32_t *out, uint32_t frame_count)   /* at most BLOCK_FRAMES_MAX */
{
    static int32_t track_out[BLOCK_FRAMES_MAX], echo_send[BLOCK_FRAMES_MAX], reverb_send[BLOCK_FRAMES_MAX];
    static int32_t echo_return[2u * BLOCK_FRAMES_MAX];   /* (L R L R ...) */
    uint32_t track, i;
    memset(echo_send, 0, frame_count * sizeof echo_send[0]);
    memset(reverb_send, 0, frame_count * sizeof reverb_send[0]);
    memset(echo_return, 0, 2u * frame_count * sizeof echo_return[0]);
    for (track = 0; track < 1u + MELODIC_COUNT; track++) {   /* the drums, then the melodic tracks */
        const uint32_t melodic = track - 1u, pan = song.pans[track];
        const int32_t level = track ? (int32_t)(song.levels[melodic] * 4096u / MIX_MAX) : 4096;   /* (the drums': each drum's) */
        const int32_t left = pan <= MIX_MAX / 2u ? 4096 : (int32_t)((MIX_MAX - pan) * 4096u / (MIX_MAX / 2u));
        const int32_t right = pan >= MIX_MAX / 2u ? 4096 : (int32_t)(pan * 4096u / (MIX_MAX / 2u));
        const int32_t echo_gain = (int32_t)((track ? song.echo[melodic] : song.drum_echo) * 5u);   /* of 512: echo / 100, near enough */
        const int32_t reverb_gain = (int32_t)(song.reverb_sends[track] * 4096u / MIX_MAX);
        memset(track_out, 0, frame_count * sizeof track_out[0]);
        if (!track) {
            int32_t drum_levels[DRUM_COUNT];
            uint32_t drum;
            for (drum = 0; drum < DRUM_COUNT; drum++) drum_levels[drum] = (int32_t)(song.drum_levels[drum] * 4096u / MIX_MAX);
            drums_render(track_out, frame_count, drum_levels);
        } else if (melodic == MELODIC_PAD)
            pad_render(track_out, frame_count, song.cutoff[melodic], song.sounds[melodic][1], song.sounds[melodic][2], song.sounds[melodic][3]);
        else {
            synth_shape_t shape;
            melodic_shape(melodic, &shape);
            synth_render(&melodic_voices[melodic], &shape, track_out, frame_count, song.cutoff[melodic], song.decay[melodic]);
        }
        for (i = 0; i < frame_count; i++) {
            const int32_t sample = (track_out[i] * level) >> 12;
            out[2u * i] += (sample * left) >> 12;
            out[2u * i + 1u] += (sample * right) >> 12;
            echo_send[i] += (sample * echo_gain) >> 9;
            reverb_send[i] += (sample * reverb_gain) >> 12;
        }
    }
    echo_render(echo_return, echo_send, frame_count, ECHO_STEPS * frames_per_step_q8 >> 8);
    for (i = 0; i < frame_count; i++) {         /* the echo: heard, and into the reverb */
        out[2u * i] += echo_return[2u * i];
        out[2u * i + 1u] += echo_return[2u * i + 1u];
        reverb_send[i] += echo_return[2u * i];
    }
    reverb_render(out, reverb_send, frame_count, song.reverb);
    limiter_render(out, frame_count);
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
                if (melodic == MELODIC_PAD && pad_pattern_value) melodic_note_on(melodic, pad_pattern_value, 0);   /* back to the pattern's chord */
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
            frames_to_step_q8 += frames_per_step_q8;   /* (the fraction carries over: the tempo does not drift) */
        }
        if (step_waits && !swing_frames_left) { /* the step sounds: on its start, a swung one later (always before the next starts) */
            const volatile pattern_t *pattern = &song.patterns[pattern_now];
            const uint32_t bar = waiting_bar, step = waiting_step;
            step_waits = 0;
            for (drum = 0; drum < DRUM_COUNT; drum++)
                if (!(muted_tracks & 1u) && pattern->drum_hits[drum][bar % pattern->bar_totals[0]] >> step & 1u)
                    drums_trigger(drum, pattern->accents[0][bar % pattern->bar_totals[0]] >> step & 1u);
            {                                   /* a chord on the pad's step: ARP starts over on it (the pad comes after ARP below) */
                const uint32_t pad_bar = bar % pattern->bar_totals[1u + MELODIC_PAD];
                const uint32_t chord = step_value(pattern->notes[MELODIC_PAD][pad_bar], pattern->gates[MELODIC_PAD][pad_bar], MELODIC_PAD, step);
                if (chord) {
                    arp_chord = chord;
                    arp_position = 0;
                }
            }
            for (melodic = 0; melodic < MELODIC_COUNT; melodic++) {   /* a note lasts its step and its ties: an empty step lets it go */
                const uint32_t track_bar = bar % pattern->bar_totals[1u + melodic];
                const uint32_t gate = melodic == MELODIC_PAD ? 0u : note_gate(pattern->gates[melodic][track_bar], step);   /* (the pad's holds its chord) */
                uint32_t note = step_value(pattern->notes[melodic][track_bar], pattern->gates[melodic][track_bar], melodic, step);
                if (muted_tracks >> (1u + melodic) & 1u) note = 0;   /* muted: as if the steps were empty */
                if (melodic == MELODIC_PAD) {   /* but the pad holds its chord until the next one */
                    if (!note && muted_tracks >> (1u + melodic) & 1u && pad_pattern_value) {   /* (muted: not that one either) */
                        pad_pattern_value = 0;
                        melodic_note_off(melodic);
                    }
                    if (!note) continue;
                    pad_pattern_value = note;
                } else if (note & NOTE_TIE)
                    continue;                   /* tied: the note before goes on */
                if (melodic == MELODIC_ARP && note) {   /* a hit: the chord's next note (the one the pad's keys play, if they do; none: silent) */
                    const uint32_t chord = audition_frames_left[MELODIC_PAD] ? audition_notes[MELODIC_PAD] : arp_chord;
                    if (chord != arp_chord_used) {
                        arp_chord_used = chord;
                        arp_position = 0;
                    }
                    note = chord ? arp_note(chord, pattern->arp_mode, arp_position++) : 0u;
                }
                if (note) melodic_note_on(melodic, note, pattern->accents[1u + melodic][track_bar] >> step & 1u);
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
