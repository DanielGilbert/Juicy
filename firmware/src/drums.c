/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's drum voices: a kick, a hat and a clap, made from a sine and noise. Everything here runs in the audio
 * interrupt: integers only, no division (docs/RULES.md). sound.c comes first. */
enum { DRUM_KICK, DRUM_HAT, DRUM_CLAP, DRUM_COUNT };

/* Levels are Q23 (full scale 32767 << 8): a decay is "level -= level >> shift" each frame, and the eight extra
 * bits let it fall smoothly to silence. A shift of n is a time constant of 2^n frames (10: 23 ms, 12: 93 ms). */
#define DRUM_LEVEL_FULL (32767 << 8)
#define DRUM_LEVEL_SILENT (16 << 8)

#define KICK_BASE_STEP 4674794u                 /* 48 Hz: phase step per frame = f * 2^32 / 44100 */
#define KICK_SWEEP_STEP 24542671u               /* the pitch starts 252 Hz above the base and falls to it */
#define KICK_SWEEP_DECAY_SHIFT 10u
#define KICK_LEVEL_DECAY_SHIFT 12u
#define KICK_GAIN 16500                         /* gains are Q15; all tracks together leave room for two more */

#define HAT_LEVEL_DECAY_SHIFT 10u
#define HAT_GAIN 4500

#define CLAP_BURST_FRAMES 441u                  /* three short bursts 10 ms apart, then the tail */
#define CLAP_TAIL_START_FRAME (3u * CLAP_BURST_FRAMES)
#define CLAP_BURST_DECAY_SHIFT 8u
#define CLAP_TAIL_DECAY_SHIFT 11u
#define CLAP_FILTER_FREQUENCY 5603              /* band-pass at 1.2 kHz: 2 * pi * f / 44100, Q15 */
#define CLAP_FILTER_DAMPING 16384               /* Q15 */
#define CLAP_GAIN 10500

static struct { uint32_t phase, sweep_step; int32_t level; } kick;
static struct { int32_t level, previous_noise; } hat;
static struct { int32_t level, low, band; uint32_t age_frames; } clap;
static uint32_t drum_noise_state = 1u;

static int32_t drum_noise(void)                 /* white noise, -32768..32767 */
{
    drum_noise_state = drum_noise_state * 1664525u + 1013904223u;
    return (int32_t)drum_noise_state >> 16;
}

static void drums_trigger(uint32_t drum)
{
    if (drum == DRUM_KICK) {
        kick.phase = 0;
        kick.sweep_step = KICK_SWEEP_STEP;
        kick.level = DRUM_LEVEL_FULL;
    } else if (drum == DRUM_HAT) {
        hat.level = DRUM_LEVEL_FULL;
    } else if (drum == DRUM_CLAP) {
        clap.age_frames = 0;
        clap.level = DRUM_LEVEL_FULL;
    }
}

static int drums_quiet(void) { return !kick.level && !hat.level && !clap.level; }

static void drums_render(int32_t *out, uint32_t frame_count)   /* adds to out (L R L R ...) */
{
    uint32_t i;
    if (kick.level) {
        for (i = 0; i < frame_count; i++) {
            int32_t sample;
            kick.phase += KICK_BASE_STEP + kick.sweep_step;
            kick.sweep_step -= kick.sweep_step >> KICK_SWEEP_DECAY_SHIFT;
            sample = (sine(kick.phase) * (kick.level >> 8)) >> 15;
            sample = (sample * KICK_GAIN) >> 15;
            kick.level -= kick.level >> KICK_LEVEL_DECAY_SHIFT;
            out[2u * i] += sample;
            out[2u * i + 1u] += sample;
        }
        if (kick.level < DRUM_LEVEL_SILENT) kick.level = 0;
    }
    if (hat.level) {
        for (i = 0; i < frame_count; i++) {
            int32_t noise = drum_noise(), sample;
            sample = (((noise - hat.previous_noise) >> 1) * (hat.level >> 8)) >> 15;   /* the difference: only the highs */
            hat.previous_noise = noise;
            sample = (sample * HAT_GAIN) >> 15;
            hat.level -= hat.level >> HAT_LEVEL_DECAY_SHIFT;
            out[2u * i] += sample;
            out[2u * i + 1u] += sample;
        }
        if (hat.level < DRUM_LEVEL_SILENT) hat.level = 0;
    }
    if (clap.level) {
        for (i = 0; i < frame_count; i++) {
            int32_t high, sample;
            if (clap.age_frames == CLAP_BURST_FRAMES || clap.age_frames == 2u * CLAP_BURST_FRAMES)
                clap.level = DRUM_LEVEL_FULL;
            else if (clap.age_frames == CLAP_TAIL_START_FRAME)
                clap.level = DRUM_LEVEL_FULL / 4 * 3;
            clap.low += (CLAP_FILTER_FREQUENCY * clap.band) >> 15;
            high = (drum_noise() >> 2) - clap.low - ((CLAP_FILTER_DAMPING * clap.band) >> 15);
            clap.band += (CLAP_FILTER_FREQUENCY * high) >> 15;
            sample = (clap.band * (clap.level >> 8)) >> 15;
            sample = (sample * CLAP_GAIN) >> 15;
            clap.level -= clap.level >> (clap.age_frames < CLAP_TAIL_START_FRAME ? CLAP_BURST_DECAY_SHIFT : CLAP_TAIL_DECAY_SHIFT);
            clap.age_frames++;
            out[2u * i] += sample;
            out[2u * i + 1u] += sample;
        }
        if (clap.age_frames > CLAP_TAIL_START_FRAME && clap.level < DRUM_LEVEL_SILENT) clap.level = 0;
    }
}
