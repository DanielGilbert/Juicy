/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's drum voices: a kick, a hat, a clap, an open hat (the hat cuts it off), a snare and a tom, made from sines
 * and noise. Everything here runs in the audio interrupt: integers only, no division (docs/RULES.md). sound.c comes
 * first. */
enum { DRUM_KICK, DRUM_HAT, DRUM_CLAP, DRUM_OPEN_HAT, DRUM_SNARE, DRUM_TOM, DRUM_COUNT };

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

#define OPEN_HAT_LEVEL_DECAY_SHIFT 13u          /* 186 ms */
#define OPEN_HAT_CHOKE_DECAY_SHIFT 6u           /* the hat cut it off: gone in 1.5 ms, no click */
#define OPEN_HAT_GAIN 3500

#define SNARE_TONE_STEP 18017437u                /* 185 Hz */
#define SNARE_TONE_DECAY_SHIFT 10u
#define SNARE_NOISE_DECAY_SHIFT 11u
#define SNARE_TONE_GAIN 7000
#define SNARE_NOISE_GAIN 4500

#define TOM_BASE_STEP 13634817u                  /* 140 Hz */
#define TOM_SWEEP_STEP 8765239u                  /* the pitch starts 90 Hz above the base and falls to it */
#define TOM_SWEEP_DECAY_SHIFT 11u
#define TOM_LEVEL_DECAY_SHIFT 12u
#define TOM_GAIN 13000

static struct { uint32_t phase, sweep_step; int32_t level; } kick;
static struct { int32_t level, previous_noise; } hat;
static struct { int32_t level, low, band; uint32_t age_frames; } clap;
static struct { int32_t level, previous_noise; uint32_t choked; } open_hat;
static struct { uint32_t phase; int32_t tone_level, noise_level; } snare;
static struct { uint32_t phase, sweep_step; int32_t level; } tom;
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
        open_hat.choked = 1;
    } else if (drum == DRUM_CLAP) {
        clap.age_frames = 0;
        clap.level = DRUM_LEVEL_FULL;
    } else if (drum == DRUM_OPEN_HAT) {
        open_hat.level = DRUM_LEVEL_FULL;
        open_hat.choked = 0;
    } else if (drum == DRUM_SNARE) {
        snare.phase = 0;
        snare.tone_level = snare.noise_level = DRUM_LEVEL_FULL;
    } else if (drum == DRUM_TOM) {
        tom.phase = 0;
        tom.sweep_step = TOM_SWEEP_STEP;
        tom.level = DRUM_LEVEL_FULL;
    }
}

static int drums_quiet(void)
{
    return !kick.level && !hat.level && !clap.level && !open_hat.level && !snare.tone_level && !snare.noise_level && !tom.level;
}

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
    if (open_hat.level) {                       /* the hat's highs, longer */
        for (i = 0; i < frame_count; i++) {
            int32_t noise = drum_noise(), sample;
            sample = (((noise - open_hat.previous_noise) >> 1) * (open_hat.level >> 8)) >> 15;
            open_hat.previous_noise = noise;
            sample = (sample * OPEN_HAT_GAIN) >> 15;
            open_hat.level -= open_hat.level >> (open_hat.choked ? OPEN_HAT_CHOKE_DECAY_SHIFT : OPEN_HAT_LEVEL_DECAY_SHIFT);
            out[2u * i] += sample;
            out[2u * i + 1u] += sample;
        }
        if (open_hat.level < DRUM_LEVEL_SILENT) open_hat.level = 0;
    }
    if (snare.tone_level || snare.noise_level) {   /* a short tone and a longer rattle of noise */
        for (i = 0; i < frame_count; i++) {
            int32_t sample;
            snare.phase += SNARE_TONE_STEP;
            sample = (((sine(snare.phase) * (snare.tone_level >> 8)) >> 15) * SNARE_TONE_GAIN) >> 15;
            sample += ((((drum_noise() >> 1) * (snare.noise_level >> 8)) >> 15) * SNARE_NOISE_GAIN) >> 15;
            snare.tone_level -= snare.tone_level >> SNARE_TONE_DECAY_SHIFT;
            snare.noise_level -= snare.noise_level >> SNARE_NOISE_DECAY_SHIFT;
            out[2u * i] += sample;
            out[2u * i + 1u] += sample;
        }
        if (snare.tone_level < DRUM_LEVEL_SILENT) snare.tone_level = 0;
        if (snare.noise_level < DRUM_LEVEL_SILENT) snare.noise_level = 0;
    }
    if (tom.level) {                            /* the kick's way, higher and shorter in its fall */
        for (i = 0; i < frame_count; i++) {
            int32_t sample;
            tom.phase += TOM_BASE_STEP + tom.sweep_step;
            tom.sweep_step -= tom.sweep_step >> TOM_SWEEP_DECAY_SHIFT;
            sample = (sine(tom.phase) * (tom.level >> 8)) >> 15;
            sample = (sample * TOM_GAIN) >> 15;
            tom.level -= tom.level >> TOM_LEVEL_DECAY_SHIFT;
            out[2u * i] += sample;
            out[2u * i + 1u] += sample;
        }
        if (tom.level < DRUM_LEVEL_SILENT) tom.level = 0;
    }
}
