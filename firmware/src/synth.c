/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's synth voice, one note at a time: a wave (a sawtooth, a square, a pulse, two sawtooths a little apart, or
 * a soft triangle) through a resonant low-pass filter. One envelope rises (ATTACK), then falls through the note and
 * drives both the loudness and the filter, so a short DECAY plucks and a long one sustains; an accented note is
 * louder and its envelope opens the filter further. The bass, the sequence
 * and the lead track are each one of these; their sounds (juicy.c) make the synth_shape_t.
 * Everything here runs in the audio interrupt: integers only, no division (docs/RULES.md). sound.c comes first. */
#define SYNTH_PARAMETER_MAX 100u                /* CUTOFF and DECAY go 0..100 */

#define SYNTH_CONTROL_FRAMES 32u                 /* the envelope and the filter move every 32 frames (0.7 ms), not every frame */
#define SYNTH_ENVELOPE_FULL (1 << 30)            /* the envelope is Q30, so even a 2 s decay has bits to fall through */
#define SYNTH_RELEASE_RATE 3170                  /* a note let go: 15 ms (rates as in SYNTH_DECAY_RATE) */
#define SYNTH_AMPLITUDE_SLEW 200                 /* the loudness follows the envelope by this much a frame (Q15): no clicks */
#define SYNTH_SQUARE_WIDTH 0x80000000u           /* the pulses: high for this much of 2^32 */
#define SYNTH_PULSE_WIDTH 0x55555555u            /* a third */
#define SYNTH_SAWS_DETUNE_SHIFT 7u               /* the second sawtooth: higher by 1 / 2^this of the pitch */
#define SYNTH_ACCENT_LOUDNESS_Q8 384             /* an accented note: half again as loud ... */
#define SYNTH_ACCENT_ENVELOPE 25u                /* ... and its envelope opens the filter this many steps of SYNTH_CUTOFF further */

enum { SYNTH_WAVE_SAW, SYNTH_WAVE_SQUARE, SYNTH_WAVE_PULSE, SYNTH_WAVE_SAWS, SYNTH_WAVE_SOFT, SYNTH_WAVE_COUNT };
/* OSC2, the second oscillator (the first one's wave; 2 SAWS: one sawtooth): a little higher, an octave down, a fifth up,
 * an octave up */
enum { SYNTH_OSC2_DETUNE, SYNTH_OSC2_OCTAVE_DOWN, SYNTH_OSC2_FIFTH, SYNTH_OSC2_OCTAVE_UP, SYNTH_OSC2_COUNT };
#define SYNTH_OSC2_DETUNE_SHIFT 7u              /* DETUNE: higher by 1 / 2^this of the pitch (13 cents) */

/* The filter's damping for RESONANCE 0..100, Q15: 0.8 (none to speak of) to 0.1 (it sings), in equal ratios each
 * half; 50 is 0.4. The pad's filter uses it too. */
static const uint16_t SYNTH_DAMPING[SYNTH_PARAMETER_MAX + 1u] = {
    26214, 25853, 25497, 25146, 24800, 24459, 24122, 23790, 23462, 23139, 22821, 22506,
    22197, 21891, 21590, 21292, 20999, 20710, 20425, 20144, 19866, 19593, 19323, 19057,
    18795, 18536, 18281, 18029, 17781, 17536, 17295, 17057, 16822, 16590, 16362, 16137,
    15914, 15695, 15479, 15266, 15056, 14849, 14644, 14443, 14244, 14048, 13854, 13664,
    13475, 13290, 13107, 12749, 12400, 12061, 11731, 11410, 11098, 10795, 10500, 10213,
    9933, 9662, 9397, 9140, 8891, 8647, 8411, 8181, 7957, 7740, 7528, 7322,
    7122, 6927, 6738, 6554, 6374, 6200, 6030, 5866, 5705, 5549, 5397, 5250,
    5106, 4967, 4831, 4699, 4570, 4445, 4324, 4205, 4090, 3979, 3870, 3764,
    3661, 3561, 3464, 3369, 3277};

/* How much the envelope (Q30) rises each 32 frames for ATTACK 0..100: at once, then 2 ms to 2 s in equal ratios */
static const uint32_t SYNTH_ATTACK_RATE[SYNTH_PARAMETER_MAX + 1u] = {
    0, 389566195, 363310737, 338824809, 315989151, 294692540, 274831249, 256308543,
    239034205, 222924099, 207899761, 193888013, 180820610, 168633906, 157268545, 146669172,
    136784160, 127565366, 118967887, 110949850, 103472201, 96498521, 89994844, 83929492,
    78272925, 72997592, 68077799, 63489583, 59210598, 55220002, 51498358, 48027541,
    44790646, 41771906, 38956619, 36331074, 33882481, 31598915, 29469254, 27483125,
    25630854, 23903420, 22292410, 20789976, 19388801, 18082061, 16863391, 15726855,
    14666917, 13678416, 12756537, 11896789, 11094985, 10347220, 9649852, 8999484,
    8392949, 7827293, 7299759, 6807780, 6348958, 5921060, 5522000, 5149836,
    4802754, 4479065, 4177191, 3895662, 3633107, 3388248, 3159892, 2946925,
    2748312, 2563085, 2390342, 2229241, 2078998, 1938880, 1808206, 1686339,
    1572685, 1466692, 1367842, 1275654, 1189679, 1109498, 1034722, 964985,
    899948, 839295, 782729, 729976, 680778, 634896, 592106, 552200,
    514984, 480275, 447906, 417719, 389566};

/* The filter's frequency for CUTOFF 0..100: 50 Hz to 5 kHz in equal ratios, as 2 * sin(pi * f / 44100) in Q15. */
static const uint16_t SYNTH_CUTOFF[SYNTH_PARAMETER_MAX + 1u] = {
    233, 244, 256, 268, 281, 294, 308, 322, 337, 353, 370, 387,
    406, 425, 445, 466, 488, 511, 535, 560, 586, 614, 643, 673,
    705, 738, 773, 809, 848, 887, 929, 973, 1019, 1067, 1117, 1170,
    1225, 1283, 1343, 1406, 1473, 1542, 1615, 1691, 1771, 1854, 1941, 2033,
    2129, 2229, 2334, 2444, 2559, 2679, 2806, 2938, 3076, 3221, 3373, 3531,
    3698, 3872, 4054, 4245, 4445, 4654, 4873, 5102, 5342, 5593, 5856, 6131,
    6419, 6720, 7036, 7366, 7712, 8073, 8452, 8848, 9262, 9695, 10149, 10623,
    11119, 11637, 12179, 12746, 13339, 13958, 14605, 15281, 15987, 16724, 17493, 18296,
    19133, 20007, 20917, 21865, 22853};

/* How far a gliding note goes each 32 frames for GLIDE 0..100, of what is left, Q16: 0 no glide, then a time
 * constant of 5 ms to 1.5 s in equal ratios */
static const uint16_t SYNTH_GLIDE_RATE[SYNTH_PARAMETER_MAX + 1u] = {
    0, 8853, 8391, 7951, 7532, 7134, 6756, 6397, 6056, 5732, 5425, 5134,
    4857, 4595, 4346, 4111, 3888, 3676, 3476, 3286, 3107, 2937, 2776, 2624,
    2480, 2343, 2214, 2092, 1977, 1868, 1765, 1667, 1575, 1488, 1405, 1328,
    1254, 1184, 1119, 1057, 998, 942, 890, 840, 794, 750, 708, 668,
    631, 596, 563, 531, 502, 474, 447, 422, 399, 377, 355, 336,
    317, 299, 282, 267, 252, 238, 224, 212, 200, 189, 178, 168,
    159, 150, 142, 134, 126, 119, 113, 106, 100, 95, 89, 84,
    80, 75, 71, 67, 63, 60, 56, 53, 50, 47, 45, 42,
    40, 38, 36, 34, 32};

/* How much of the envelope goes each 32 frames for DECAY 0..100: a time constant of 30 ms to 2 s in equal ratios,
 * as 32 * 65536 / (seconds * 44100). */
static const uint16_t SYNTH_DECAY_RATE[SYNTH_PARAMETER_MAX + 1u] = {
    1585, 1520, 1457, 1398, 1340, 1285, 1232, 1181, 1133, 1086, 1042, 999,
    958, 918, 880, 844, 810, 776, 744, 714, 684, 656, 629, 603,
    579, 555, 532, 510, 489, 469, 450, 431, 413, 396, 380, 365,
    350, 335, 321, 308, 295, 283, 272, 260, 250, 240, 230, 220,
    211, 202, 194, 186, 178, 171, 164, 157, 151, 145, 139, 133,
    128, 122, 117, 112, 108, 103, 99, 95, 91, 87, 84, 80,
    77, 74, 71, 68, 65, 62, 60, 57, 55, 53, 51, 49,
    47, 45, 43, 41, 39, 38, 36, 35, 33, 32, 31, 29,
    28, 27, 26, 25, 24};

typedef struct {                                /* what makes a sound its own */
    uint32_t wave;                              /* SYNTH_WAVE_* */
    int32_t gain;                               /* Q15 */
    int32_t damping;                            /* Q15, SYNTH_DAMPING */
    uint32_t attack_rate;                       /* SYNTH_ATTACK_RATE: 0 the note is there at once */
    uint32_t envelope_amount;                   /* the envelope at full opens the filter by this many steps of SYNTH_CUTOFF */
    uint32_t release_rate;                      /* a note let go falls at this rate (as SYNTH_DECAY_RATE); 0: it rings on as DECAY says */
    int32_t osc1_weight, sub_weight, osc2_weight;   /* Q15, how much of each goes in: with SUB and OSC2 off, only the first (sub_weight and osc2_weight 0) */
    uint32_t osc2_interval;                     /* SYNTH_OSC2_* */
    uint32_t glide_rate;                        /* SYNTH_GLIDE_RATE: 0 a note is at its pitch at once */
} synth_shape_t;

typedef struct {
    uint32_t phase, phase_detuned, phase_sub, phase_osc2, phase_step, phase_step_target, held, attacking, accented, control_frames_left;
    int32_t envelope, amplitude_target, amplitude, filter_frequency, low, band;
} synth_voice_t;

static int synth_quiet(const synth_voice_t *voice) { return !voice->attacking && !voice->envelope && !voice->amplitude; }

static void synth_note_on(synth_voice_t *voice, const synth_shape_t *shape, uint32_t note, int accented)
{
    voice->phase_step_target = note_phase_step(note);
    if (!shape->glide_rate || synth_quiet(voice)) voice->phase_step = voice->phase_step_target;   /* (gliding: from where the note before is) */
    voice->accented = accented != 0;
    voice->phase_detuned = voice->phase + 0x40000000u;   /* a quarter turn apart: every note starts equally loud */
    voice->phase_osc2 = voice->phase + 0x40000000u;
    voice->attacking = shape->attack_rate != 0u;  /* (an attack rises from where the envelope is: no click) */
    if (!voice->attacking) voice->envelope = SYNTH_ENVELOPE_FULL;
    voice->held = 1;
    voice->control_frames_left = 0;               /* heard from the very next frame */
}

static void synth_note_off(synth_voice_t *voice) { voice->held = 0; }

#define SYNTH_PITCH_UNCHANGED (1u << 16)        /* a pitch factor (Q16) that leaves the note as it is */

static uint32_t synth_pitched(uint32_t phase_step, uint32_t pitch_q16)   /* a phase step times the LFO's pitch factor */
{
    return pitch_q16 == SYNTH_PITCH_UNCHANGED ? phase_step : (uint32_t)(((uint64_t)phase_step * pitch_q16) >> 16);
}

static uint32_t synth_glided(uint32_t phase_step, uint32_t target, uint32_t glide_rate)   /* a control block's glide towards target */
{
    const int32_t left = (int32_t)(target - phase_step), moved = (int32_t)(((int64_t)left * glide_rate) >> 16);
    return moved ? phase_step + (uint32_t)moved : target;   /* (too close to move: there) */
}

static int32_t synth_wave(uint32_t wave, uint32_t phase)   /* one oscillator's wave at phase: -8192..8191 (2 SAWS: one sawtooth) */
{
    switch (wave) {
    case SYNTH_WAVE_SQUARE: return phase < SYNTH_SQUARE_WIDTH ? 8191 : -8192;
    case SYNTH_WAVE_PULSE: return phase < SYNTH_PULSE_WIDTH ? 8191 : -8192;
    case SYNTH_WAVE_SOFT: {                     /* the triangle: the phase folded */
        int32_t folded = (int32_t)phase >> 17;
        return (folded < 0 ? -folded : folded) - 8192;
    }
    default: return (int32_t)phase >> 18;       /* the sawtooth: the phase itself */
    }
}

static int32_t synth_cutoff_frequency(uint32_t cutoff_q8)   /* SYNTH_CUTOFF, also between its steps: cutoff_q8 0..SYNTH_PARAMETER_MAX << 8 */
{
    const uint32_t step = cutoff_q8 >> 8;
    const int32_t fraction = (int32_t)(cutoff_q8 & 255u);
    if (step >= SYNTH_PARAMETER_MAX) return SYNTH_CUTOFF[SYNTH_PARAMETER_MAX];
    return SYNTH_CUTOFF[step] + (((SYNTH_CUTOFF[step + 1u] - SYNTH_CUTOFF[step]) * fraction) >> 8);
}

/* adds to out (mono, a value a frame); cutoff_q8 0..SYNTH_PARAMETER_MAX << 8 (in 1/256 of a step), decay
 * 0..SYNTH_PARAMETER_MAX; the note's pitch times pitch_q16 (the LFO's). SUB (a square an octave down) and OSC2 come
 * in where the shape says so */
static void synth_render(synth_voice_t *voice, const synth_shape_t *shape, int32_t *out, uint32_t frame_count,
                         uint32_t cutoff_q8, uint32_t decay, uint32_t pitch_q16)
{
    const int layered = shape->sub_weight || shape->osc2_weight;
    uint32_t phase_step = synth_pitched(voice->phase_step, pitch_q16), osc2_step = 0;
    if (synth_quiet(voice)) return;
    while (frame_count) {
        uint32_t run_frames, i;
        if (!voice->control_frames_left) {
            uint32_t cutoff_now;
            if (voice->phase_step != voice->phase_step_target) {   /* GLIDE: on towards the note */
                voice->phase_step = synth_glided(voice->phase_step, voice->phase_step_target, shape->glide_rate);
                phase_step = synth_pitched(voice->phase_step, pitch_q16);
            }
            if (voice->attacking && voice->held) {
                voice->envelope += (int32_t)shape->attack_rate;
                if (voice->envelope >= SYNTH_ENVELOPE_FULL) {
                    voice->envelope = SYNTH_ENVELOPE_FULL;
                    voice->attacking = 0;
                }
            } else {
                voice->attacking = 0;
                voice->envelope -= (voice->envelope >> 16) * (voice->held || !shape->release_rate ? SYNTH_DECAY_RATE[decay] : shape->release_rate);
                if (voice->envelope < 65536) voice->envelope = 0;
            }
            cutoff_now = cutoff_q8 + (((((uint32_t)voice->envelope >> 16) * (shape->envelope_amount + (voice->accented ? SYNTH_ACCENT_ENVELOPE : 0u))) >> 14) << 8);
            voice->filter_frequency = synth_cutoff_frequency(cutoff_now);
            voice->amplitude_target = voice->accented ? ((voice->envelope >> 15) * SYNTH_ACCENT_LOUDNESS_Q8) >> 8 : voice->envelope >> 15;
            voice->control_frames_left = SYNTH_CONTROL_FRAMES;
        }
        run_frames = frame_count < voice->control_frames_left ? frame_count : voice->control_frames_left;
        if (layered)
            osc2_step = shape->osc2_interval == SYNTH_OSC2_DETUNE ? phase_step + (phase_step >> SYNTH_OSC2_DETUNE_SHIFT)
                      : shape->osc2_interval == SYNTH_OSC2_OCTAVE_DOWN ? phase_step >> 1
                      : shape->osc2_interval == SYNTH_OSC2_FIFTH ? phase_step + (phase_step >> 1) : phase_step << 1;
        for (i = 0; i < run_frames; i++) {
            int32_t wave, high, sample;
            if (voice->amplitude < voice->amplitude_target) {
                voice->amplitude += SYNTH_AMPLITUDE_SLEW;
                if (voice->amplitude > voice->amplitude_target) voice->amplitude = voice->amplitude_target;
            } else if (voice->amplitude > voice->amplitude_target) {
                voice->amplitude -= SYNTH_AMPLITUDE_SLEW;
                if (voice->amplitude < voice->amplitude_target) voice->amplitude = voice->amplitude_target;
            }
            voice->phase += phase_step;
            voice->low += (voice->filter_frequency * voice->band) >> 15;
            wave = synth_wave(shape->wave, voice->phase);
            if (shape->wave == SYNTH_WAVE_SAWS) {
                voice->phase_detuned += phase_step + (phase_step >> SYNTH_SAWS_DETUNE_SHIFT);
                wave = (wave + ((int32_t)voice->phase_detuned >> 18)) >> 1;
            }
            if (layered) {                      /* SUB and OSC2 with it, together as loud as the first alone */
                voice->phase_sub += phase_step >> 1;
                voice->phase_osc2 += osc2_step;
                wave = (wave * shape->osc1_weight + (voice->phase_sub < 0x80000000u ? 8191 : -8192) * shape->sub_weight
                        + synth_wave(shape->wave, voice->phase_osc2) * shape->osc2_weight) >> 15;
            }
            high = wave - voice->low - ((shape->damping * voice->band) >> 15);
            voice->band += (voice->filter_frequency * high) >> 15;
            sample = (voice->low * voice->amplitude) >> 15;
            sample = (sample * shape->gain) >> 15;
            out[i] += sample;
        }
        out += run_frames;
        frame_count -= run_frames;
        voice->control_frames_left -= run_frames;
    }
}
