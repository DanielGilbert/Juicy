/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's synth voice, one note at a time: a sawtooth or a pulse through a resonant low-pass filter. One envelope
 * falls through the note and drives both the loudness and the filter, so a short DECAY plucks and a long one
 * sustains. The bass, the sequence and the lead track are each one of these, told apart by their synth_shape_t.
 * Everything here runs in the audio interrupt: integers only, no division (docs/RULES.md). sound.c comes first. */
#define SYNTH_PARAMETER_MAX 100u                /* CUTOFF and DECAY go 0..100 */

#define SYNTH_CONTROL_FRAMES 32u                 /* the envelope and the filter move every 32 frames (0.7 ms), not every frame */
#define SYNTH_ENVELOPE_FULL (1 << 30)            /* the envelope is Q30, so even a 2 s decay has bits to fall through */
#define SYNTH_RELEASE_RATE 3170                  /* a note let go: 15 ms (rates as in SYNTH_DECAY_RATE) */
#define SYNTH_AMPLITUDE_SLEW 200                 /* the loudness follows the envelope by this much a frame (Q15): no clicks */
#define SYNTH_FILTER_DAMPING 13107               /* Q15: 0.4, a resonance that sings without ringing on */

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

typedef struct {                                /* what makes a track's sound its own */
    uint32_t pulse_width;                       /* 0: a sawtooth; else a pulse, high for this much of 2^32 */
    uint32_t envelope_to_cutoff_shift;          /* the envelope (Q30) opens the filter by envelope >> this steps of SYNTH_CUTOFF */
    int32_t gain;                               /* Q15 */
    uint32_t detune_shift;                      /* 0: one oscillator; else a second sawtooth, higher by 1 / 2^this of the pitch */
    uint32_t release_rate;                      /* a note let go falls at this rate (as SYNTH_DECAY_RATE); 0: it rings on as DECAY says */
} synth_shape_t;

typedef struct {
    uint32_t phase, phase_detuned, phase_step, held, control_frames_left;
    int32_t envelope, amplitude_target, amplitude, filter_frequency, low, band;
} synth_voice_t;

static void synth_note_on(synth_voice_t *voice, uint32_t note)
{
    voice->phase_step = note_phase_step(note);
    voice->phase_detuned = voice->phase + 0x40000000u;   /* a quarter turn apart: every note starts equally loud */
    voice->envelope = SYNTH_ENVELOPE_FULL;
    voice->held = 1;
    voice->control_frames_left = 0;               /* heard from the very next frame */
}

static void synth_note_off(synth_voice_t *voice) { voice->held = 0; }

static int synth_quiet(const synth_voice_t *voice) { return !voice->envelope && !voice->amplitude; }

/* adds to out (L R L R ...), and send_gain / 512 of it to send (one value a frame);
 * cutoff and decay 0..SYNTH_PARAMETER_MAX */
static void synth_render(synth_voice_t *voice, const synth_shape_t *shape, int32_t *out, int32_t *send, uint32_t frame_count,
                         uint32_t cutoff, uint32_t decay, int32_t send_gain)
{
    if (synth_quiet(voice)) return;
    while (frame_count) {
        uint32_t run_frames, i;
        if (!voice->control_frames_left) {
            uint32_t cutoff_now;
            voice->envelope -= (voice->envelope >> 16) * (voice->held || !shape->release_rate ? SYNTH_DECAY_RATE[decay] : shape->release_rate);
            if (voice->envelope < 65536) voice->envelope = 0;
            cutoff_now = cutoff + ((uint32_t)voice->envelope >> shape->envelope_to_cutoff_shift);
            voice->filter_frequency = SYNTH_CUTOFF[cutoff_now > SYNTH_PARAMETER_MAX ? SYNTH_PARAMETER_MAX : cutoff_now];
            voice->amplitude_target = voice->envelope >> 15;
            voice->control_frames_left = SYNTH_CONTROL_FRAMES;
        }
        run_frames = frame_count < voice->control_frames_left ? frame_count : voice->control_frames_left;
        for (i = 0; i < run_frames; i++) {
            int32_t wave, high, sample;
            if (voice->amplitude < voice->amplitude_target) {
                voice->amplitude += SYNTH_AMPLITUDE_SLEW;
                if (voice->amplitude > voice->amplitude_target) voice->amplitude = voice->amplitude_target;
            } else if (voice->amplitude > voice->amplitude_target) {
                voice->amplitude -= SYNTH_AMPLITUDE_SLEW;
                if (voice->amplitude < voice->amplitude_target) voice->amplitude = voice->amplitude_target;
            }
            voice->phase += voice->phase_step;
            voice->low += (voice->filter_frequency * voice->band) >> 15;
            wave = !shape->pulse_width ? (int32_t)voice->phase >> 18    /* the sawtooth: the phase itself */
                 : voice->phase < shape->pulse_width ? 8191 : -8192;
            if (shape->detune_shift) {
                voice->phase_detuned += voice->phase_step + (voice->phase_step >> shape->detune_shift);
                wave = (wave + ((int32_t)voice->phase_detuned >> 18)) >> 1;
            }
            high = wave - voice->low - ((SYNTH_FILTER_DAMPING * voice->band) >> 15);
            voice->band += (voice->filter_frequency * high) >> 15;
            sample = (voice->low * voice->amplitude) >> 15;
            sample = (sample * shape->gain) >> 15;
            out[2u * i] += sample;
            out[2u * i + 1u] += sample;
            send[i] += (sample * send_gain) >> 9;
        }
        out += 2u * run_frames;
        send += run_frames;
        frame_count -= run_frames;
        voice->control_frames_left -= run_frames;
    }
}
