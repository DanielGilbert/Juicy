/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's pad: one chord at a time, three notes (the root, a minor or major third, the fifth). Each note is two
 * sawtooths a little apart in pitch, which is what makes it wide and slowly moving; all six go through one
 * low-pass filter. It fades in and out slowly, and a new chord takes over from the one that sounds without a
 * gap. Everything here runs in the audio interrupt: integers only, no division (docs/RULES.md). sound.c and
 * synth.c (for SYNTH_CUTOFF) come first. */
#define PAD_NOTE_COUNT 3u
#define PAD_MINOR_THIRD 3u                      /* semitones above the root */
#define PAD_MAJOR_THIRD 4u
#define PAD_FIFTH 7u
#define PAD_DETUNE_SHIFT 8u                     /* the two sawtooths of a note: 1/256 below and above it (7 cents each way) */
#define PAD_LEVEL_FULL (1 << 24)                /* the loudness is Q24 and moves in a straight line */
#define PAD_ATTACK_STEP 2536                    /* a frame: silent to full in 150 ms */
#define PAD_RELEASE_STEP 634                    /* full to silent in 600 ms */
#define PAD_FILTER_DAMPING 26214                /* Q15: 0.8, no resonance to speak of */
#define PAD_GAIN 20000                          /* Q15 */

static struct {
    uint32_t phase[PAD_NOTE_COUNT][2], phase_step[PAD_NOTE_COUNT][2], held;
    int32_t level, low, band;
} pad;

static void pad_chord_on(uint32_t root, int major)   /* root: MIDI */
{
    const uint32_t notes[PAD_NOTE_COUNT] = {root, root + (major ? PAD_MAJOR_THIRD : PAD_MINOR_THIRD), root + PAD_FIFTH};
    uint32_t note;
    for (note = 0; note < PAD_NOTE_COUNT; note++) {
        uint32_t phase_step = note_phase_step(notes[note]);
        pad.phase_step[note][0] = phase_step - (phase_step >> PAD_DETUNE_SHIFT);
        pad.phase_step[note][1] = phase_step + (phase_step >> PAD_DETUNE_SHIFT);
    }
    pad.held = 1;
}

static void pad_chord_off(void) { pad.held = 0; }

static int pad_quiet(void) { return !pad.held && !pad.level; }

/* adds to out (L R L R ...), and send_gain / 512 of it to send (one value a frame); cutoff 0..SYNTH_PARAMETER_MAX */
static void pad_render(int32_t *out, int32_t *send, uint32_t frame_count, uint32_t cutoff, int32_t send_gain)
{
    int32_t filter_frequency = SYNTH_CUTOFF[cutoff];
    uint32_t i, note;
    if (pad_quiet()) return;
    for (i = 0; i < frame_count; i++) {
        int32_t wave = 0, high, sample;
        if (pad.held) {
            pad.level += PAD_ATTACK_STEP;
            if (pad.level > PAD_LEVEL_FULL) pad.level = PAD_LEVEL_FULL;
        } else {
            pad.level -= PAD_RELEASE_STEP;
            if (pad.level < 0) pad.level = 0;
        }
        for (note = 0; note < PAD_NOTE_COUNT; note++) {
            pad.phase[note][0] += pad.phase_step[note][0];
            pad.phase[note][1] += pad.phase_step[note][1];
            wave += ((int32_t)pad.phase[note][0] >> 20) + ((int32_t)pad.phase[note][1] >> 20);   /* six of these: -12288..12282 */
        }
        pad.low += (filter_frequency * pad.band) >> 15;
        high = wave - pad.low - ((PAD_FILTER_DAMPING * pad.band) >> 15);
        pad.band += (filter_frequency * high) >> 15;
        sample = (pad.low * (pad.level >> 9)) >> 15;
        sample = (sample * PAD_GAIN) >> 15;
        out[2u * i] += sample;
        out[2u * i + 1u] += sample;
        send[i] += (sample * send_gain) >> 9;
    }
}
