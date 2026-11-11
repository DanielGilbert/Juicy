/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's pad: one chord at a time, three notes (the root, a minor or major third, the fifth). Each note is two
 * sawtooths a little apart in pitch, which is what makes it wide and slowly moving; all six go through one
 * low-pass filter. It fades in and out (ATTACK, RELEASE), and a new chord takes over from the one that sounds
 * without a gap. How far apart the two sawtooths are is SPREAD, how much the filter sings RESONANCE. Everything here runs in the audio interrupt: integers only, no division (docs/RULES.md). sound.c and
 * synth.c (for SYNTH_CUTOFF, SYNTH_DAMPING) come first. */
#define PAD_NOTE_COUNT 3u
#define PAD_MINOR_THIRD 3u                      /* semitones above the root */
#define PAD_MAJOR_THIRD 4u
#define PAD_FIFTH 7u
#define PAD_DETUNE_SHIFT 8u                     /* SPREAD 64: the two sawtooths of a note 1/256 below and above it (7 cents each way) */
#define PAD_SPREAD_UNIT_SHIFT 6u                /* SPREAD counts in 1/64 of that */
#define PAD_LEVEL_FULL (1 << 24)                /* the loudness is Q24 and moves in a straight line */
#define PAD_GAIN 20000                          /* Q15 */

/* How far the level moves a frame for ATTACK or RELEASE 0..100: silent to full (or back) in 5 ms to 4 s, in equal
 * ratios; 51 is 150 ms, 72 is 600 ms exactly */
static const uint32_t PAD_LEVEL_STEP[SYNTH_PARAMETER_MAX + 1u] = {
    76087, 71167, 66566, 62261, 58235, 54470, 50948, 47654, 44572, 41690, 38994, 36473,
    34115, 31909, 29845, 27916, 26111, 24422, 22843, 21366, 19985, 18692, 17484, 16353,
    15296, 14307, 13382, 12516, 11707, 10950, 10242, 9580, 8960, 8381, 7839, 7332,
    6858, 6415, 6000, 5612, 5249, 4910, 4592, 4295, 4017, 3758, 3515, 3287,
    3075, 2876, 2690, 2536, 2353, 2201, 2059, 1926, 1801, 1685, 1576, 1474,
    1379, 1290, 1206, 1128, 1055, 987, 923, 863, 808, 755, 707, 661,
    634, 578, 541, 506, 473, 443, 414, 387, 362, 339, 317, 296,
    277, 259, 242, 227, 212, 198, 186, 174, 162, 152, 142, 133,
    124, 116, 109, 102, 95};

static struct {
    uint32_t phase[PAD_NOTE_COUNT][2], phase_step[PAD_NOTE_COUNT][2], held;
    int32_t level, low, band;
} pad;

static void pad_chord_on(uint32_t root, int major, uint32_t spread)   /* root: MIDI; spread 0..SYNTH_PARAMETER_MAX */
{
    const uint32_t notes[PAD_NOTE_COUNT] = {root, root + (major ? PAD_MAJOR_THIRD : PAD_MINOR_THIRD), root + PAD_FIFTH};
    uint32_t note;
    for (note = 0; note < PAD_NOTE_COUNT; note++) {
        uint32_t phase_step = note_phase_step(notes[note]);
        uint32_t detune = ((phase_step >> PAD_DETUNE_SHIFT) * spread) >> PAD_SPREAD_UNIT_SHIFT;
        pad.phase_step[note][0] = phase_step - detune;
        pad.phase_step[note][1] = phase_step + detune;
    }
    pad.held = 1;
}

static void pad_chord_off(void) { pad.held = 0; }

static int pad_quiet(void) { return !pad.held && !pad.level; }

/* adds to out (L R L R ...), and send_gain / 512 of it to send (one value a frame); cutoff, resonance, attack and
 * release 0..SYNTH_PARAMETER_MAX */
static void pad_render(int32_t *out, int32_t *send, uint32_t frame_count, uint32_t cutoff, int32_t send_gain,
                       uint32_t resonance, uint32_t attack, uint32_t release)
{
    int32_t filter_frequency = SYNTH_CUTOFF[cutoff], damping = SYNTH_DAMPING[resonance];
    int32_t attack_step = (int32_t)PAD_LEVEL_STEP[attack], release_step = (int32_t)PAD_LEVEL_STEP[release];
    uint32_t i, note;
    if (pad_quiet()) return;
    for (i = 0; i < frame_count; i++) {
        int32_t wave = 0, high, sample;
        if (pad.held) {
            pad.level += attack_step;
            if (pad.level > PAD_LEVEL_FULL) pad.level = PAD_LEVEL_FULL;
        } else {
            pad.level -= release_step;
            if (pad.level < 0) pad.level = 0;
        }
        for (note = 0; note < PAD_NOTE_COUNT; note++) {
            pad.phase[note][0] += pad.phase_step[note][0];
            pad.phase[note][1] += pad.phase_step[note][1];
            wave += ((int32_t)pad.phase[note][0] >> 20) + ((int32_t)pad.phase[note][1] >> 20);   /* six of these: -12288..12282 */
        }
        pad.low += (filter_frequency * pad.band) >> 15;
        high = wave - pad.low - ((damping * pad.band) >> 15);
        pad.band += (filter_frequency * high) >> 15;
        sample = (pad.low * (pad.level >> 9)) >> 15;
        sample = (sample * PAD_GAIN) >> 15;
        out[2u * i] += sample;
        out[2u * i + 1u] += sample;
        send[i] += (sample * send_gain) >> 9;
    }
}
