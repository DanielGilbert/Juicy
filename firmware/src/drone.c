/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's drone: one note held under everything while the song plays (the song key's root, or the root of the pad's
 * chord), two sawtooths beating slowly against each other and a sine an octave under them, through a soft low-pass
 * filter. It fades in when the song starts and out when it stops, and glides from one root to the next. The audio
 * interrupt runs it: integers only, no division (docs/RULES.md). sound.c, synth.c and pad.c come first. */
#define DRONE_DETUNE_SHIFT 9u                   /* the two sawtooths 1/512 below and above the note (3.4 cents each way) */
#define DRONE_FADE 90u                          /* in and out as PAD_LEVEL_STEP[this]: about 2 s */
#define DRONE_GLIDE 60u                         /* from one root to the next as SYNTH_GLIDE_RATE[this]: about 150 ms */
#define DRONE_LEVEL_FULL (1 << 24)              /* the loudness is Q24 and moves in a straight line, as the pad's */
#define DRONE_GAIN 45000                        /* Q15 (1.37: louder than its wave, which the filter keeps small) */

static struct {
    uint32_t phase[2], phase_sub, phase_step, phase_step_target, glide_frames;
    int32_t level, low, band;
} drone;

static int drone_quiet(void) { return !drone.level; }

/* adds to out (mono, a value a frame): held (the song plays), it fades in, else out; note: MIDI, it glides there;
 * cutoff_q8 0..SYNTH_PARAMETER_MAX << 8 (in 1/256 of a step) */
static void drone_render(int32_t *out, uint32_t frame_count, int held, uint32_t note, uint32_t cutoff_q8)
{
    const int32_t filter_frequency = synth_cutoff_frequency(cutoff_q8), damping = SYNTH_DAMPING[0];
    const int32_t fade_step = (int32_t)PAD_LEVEL_STEP[DRONE_FADE];
    uint32_t i;
    drone.phase_step_target = note_phase_step(note);
    if (!held && drone_quiet()) return;
    if (drone_quiet()) drone.phase_step = drone.phase_step_target;   /* (from silence: on the note at once) */
    for (i = 0; i < frame_count; i++) {
        const uint32_t detune = drone.phase_step >> DRONE_DETUNE_SHIFT;
        int32_t wave, high, sample;
        if (++drone.glide_frames >= SYNTH_CONTROL_FRAMES) {
            drone.glide_frames = 0;
            if (drone.phase_step != drone.phase_step_target)
                drone.phase_step = synth_glided(drone.phase_step, drone.phase_step_target, SYNTH_GLIDE_RATE[DRONE_GLIDE]);
        }
        drone.level += held ? fade_step : -fade_step;
        drone.level = drone.level > DRONE_LEVEL_FULL ? DRONE_LEVEL_FULL : drone.level < 0 ? 0 : drone.level;
        drone.phase[0] += drone.phase_step - detune;
        drone.phase[1] += drone.phase_step + detune;
        drone.phase_sub += drone.phase_step >> 1;
        wave = ((int32_t)drone.phase[0] >> 20) + ((int32_t)drone.phase[1] >> 20) + (sine(drone.phase_sub) >> 3);   /* (-8192..8190) */
        drone.low += (filter_frequency * drone.band) >> 15;
        high = wave - drone.low - ((damping * drone.band) >> 15);
        drone.band += (filter_frequency * high) >> 15;
        sample = (drone.low * (drone.level >> 9)) >> 15;
        out[i] += (sample * DRONE_GAIN) >> 15;
    }
}
