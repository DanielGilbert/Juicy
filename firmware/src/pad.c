/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's pad: one chord at a time, one to four notes (PAD_CHORDS). Each note is two
 * sawtooths a little apart in pitch, which is what makes it wide and slowly moving; all of them go through one
 * low-pass filter. It fades in and out (ATTACK, RELEASE), and a new chord takes over from the one that sounds
 * without a gap. How far apart the two sawtooths are is SPREAD, how much the filter sings RESONANCE. Everything here runs in the audio interrupt: integers only, no division (docs/RULES.md). sound.c and
 * synth.c (for SYNTH_CUTOFF, SYNTH_DAMPING) come first. */
#define PAD_NOTES_MAX 4u
enum { PAD_CHORD_MINOR, PAD_CHORD_MAJOR, PAD_CHORD_MINOR7, PAD_CHORD_MAJOR7, PAD_CHORD_DOMINANT7, PAD_CHORD_SUS2,
       PAD_CHORD_SUS4, PAD_CHORD_DIMINISHED, PAD_CHORD_POWER, PAD_CHORD_ROOT, PAD_CHORD_MINOR_ADD9, PAD_CHORD_MAJOR_ADD9, PAD_CHORD_COUNT };
static const struct {
    uint8_t note_count;
    uint8_t intervals[PAD_NOTES_MAX];           /* semitones above the root, the root first */
} PAD_CHORDS[PAD_CHORD_COUNT] = {
    {3, {0, 3, 7}}, {3, {0, 4, 7}},             /* minor, major */
    {4, {0, 3, 7, 10}}, {4, {0, 4, 7, 11}}, {4, {0, 4, 7, 10}},   /* minor 7th, major 7th, dominant 7th */
    {3, {0, 2, 7}}, {3, {0, 5, 7}},             /* sus2, sus4: the third is a second or a fourth */
    {3, {0, 3, 6}},                             /* diminished */
    {2, {0, 7}}, {1, {0}},                      /* power chord (root and fifth), the root alone */
    {4, {0, 2, 3, 7}}, {4, {0, 2, 4, 7}},       /* minor and major with an added ninth, close: the ninth between the root and the third */
};
/* the notes of a chord about as loud together as three: Q8, by how many there are (the square root of 3 / count) */
static const int32_t PAD_NOTES_GAIN[PAD_NOTES_MAX + 1u] = {0, 443, 314, 256, 222};
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
    uint32_t phase[PAD_NOTES_MAX][2], phase_step[PAD_NOTES_MAX][2], held, note_count;
    int32_t level, low, band;
} pad;

static void pad_chord_on(uint32_t root, uint32_t chord, uint32_t spread)   /* root: MIDI; chord: PAD_CHORD_*; spread 0..SYNTH_PARAMETER_MAX */
{
    uint32_t note;
    pad.note_count = PAD_CHORDS[chord].note_count;
    for (note = 0; note < pad.note_count; note++) {
        uint32_t phase_step = note_phase_step(root + PAD_CHORDS[chord].intervals[note]);
        uint32_t detune = ((phase_step >> PAD_DETUNE_SHIFT) * spread) >> PAD_SPREAD_UNIT_SHIFT;
        pad.phase_step[note][0] = phase_step - detune;
        pad.phase_step[note][1] = phase_step + detune;
    }
    pad.held = 1;
}

static void pad_chord_off(void) { pad.held = 0; }

static int pad_quiet(void) { return !pad.held && !pad.level; }

/* adds to out (mono, a value a frame); cutoff, resonance, attack and release 0..SYNTH_PARAMETER_MAX */
static void pad_render(int32_t *out, uint32_t frame_count, uint32_t cutoff, uint32_t resonance, uint32_t attack, uint32_t release)
{
    int32_t filter_frequency = SYNTH_CUTOFF[cutoff], damping = SYNTH_DAMPING[resonance];
    int32_t attack_step = (int32_t)PAD_LEVEL_STEP[attack], release_step = (int32_t)PAD_LEVEL_STEP[release];
    const int32_t notes_gain = PAD_NOTES_GAIN[pad.note_count];
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
        for (note = 0; note < pad.note_count; note++) {
            pad.phase[note][0] += pad.phase_step[note][0];
            pad.phase[note][1] += pad.phase_step[note][1];
            wave += ((int32_t)pad.phase[note][0] >> 20) + ((int32_t)pad.phase[note][1] >> 20);   /* two a note: -4096..4094 */
        }
        wave = (wave * notes_gain) >> 8;        /* (four notes: up to -14189..14187) */
        pad.low += (filter_frequency * pad.band) >> 15;
        high = wave - pad.low - ((damping * pad.band) >> 15);
        pad.band += (filter_frequency * high) >> 15;
        sample = (pad.low * (pad.level >> 9)) >> 15;
        sample = (sample * PAD_GAIN) >> 15;
        out[i] += sample;
    }
}
