/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's LFOs: a slow wave per melodic track that moves its CUTOFF, its PITCH or its LEVEL, in step with the song (a
 * cycle lasts a quarter of a bar to 16 bars). The shapes: a sine, a triangle, a sawtooth up or down, a square, a
 * random value held for a cycle, and a random drift that glides from one value to the next. The audio interrupt runs
 * it (the LFO page's picture too): integers only, no 64-bit division (docs/RULES.md). sound.c comes first. */
#define LFO_RATE_COUNT 7u                       /* RATE: a cycle is 4 << RATE steps: a quarter and half a bar, 1, 2, 4, 8, 16 bars */
#define LFO_RATE_DEFAULT 4u                     /* (4 bars) */
#define LFO_LAP_STEPS 256u                      /* the longest cycle (16 bars): each cycle fits into it a whole number of times */
#define LFO_DEPTH_MAX 100u
#define LFO_CUTOFF_STEPS 50u                    /* DEPTH 100 on the CUTOFF: up and down by 50 of its 100 steps (over 3 octaves) */
#define LFO_PITCH_SEMITONES 12u                 /* ... on the PITCH: an octave up and down (DEPTH counts squared: 10 is 12 cents) */
enum { LFO_TARGET_CUTOFF, LFO_TARGET_PITCH, LFO_TARGET_LEVEL, LFO_TARGET_COUNT };
enum { LFO_SHAPE_SINE, LFO_SHAPE_TRIANGLE, LFO_SHAPE_SAW_UP, LFO_SHAPE_SAW_DOWN, LFO_SHAPE_SQUARE, LFO_SHAPE_RANDOM,
       LFO_SHAPE_DRIFT, LFO_SHAPE_COUNT };

typedef struct {                                /* RANDOM and DRIFT: their own random numbers, the cycle the values are for, */
    uint32_t seed, cycle;                       /* the value at its start (DRIFT comes from it) and at its end */
    int32_t from, to;
} lfo_random_t;

/* the wave at phase (a cycle over 2^32) in cycle number cycle: -32767..32767 */
static int32_t lfo_wave(uint32_t shape, uint32_t phase, uint32_t cycle, lfo_random_t *random)
{
    int32_t value, place = (int32_t)(phase >> 16);   /* (0..65535 through the cycle) */
    if (shape >= LFO_SHAPE_RANDOM && cycle != random->cycle) {   /* a new cycle: a new value to go to */
        random->seed = random->seed * 1664525u + 1013904223u;
        random->cycle = cycle;
        random->from = random->to;
        random->to = (int32_t)(random->seed >> 17) * 2 - 32767;
    }
    switch (shape) {
    case LFO_SHAPE_TRIANGLE: value = place < 16384 ? 2 * place : place < 49152 ? 65536 - 2 * place : 2 * place - 131072; break;
    case LFO_SHAPE_SAW_UP: value = place - 32768; break;
    case LFO_SHAPE_SAW_DOWN: value = 32767 - place; break;
    case LFO_SHAPE_SQUARE: value = phase < 0x80000000u ? 32767 : -32767; break;
    case LFO_SHAPE_RANDOM: value = random->to; break;
    case LFO_SHAPE_DRIFT:                       /* from one value to the next along half a cosine: no corners */
        value = random->from + (((random->to - random->from) * ((32767 - sine((phase >> 1) + 0x40000000u)) >> 1)) >> 15);
        break;
    default: value = sine(phase);
    }
    return value > 32767 ? 32767 : value < -32767 ? -32767 : value;
}

/* the PITCH's factor for semitones_q8 (1/256 of a semitone, an octave down to one up), Q16 */
static uint32_t lfo_pitch_q16(int32_t semitones_q8)
{
    static const uint32_t SEMITONE_RATIOS_Q16[2u * LFO_PITCH_SEMITONES + 1u] = {   /* 2^(n / 12) for n = -12..12 */
        32768, 34716, 36781, 38968, 41285, 43740, 46341, 49097, 52016, 55109, 58386, 61858, 65536,
        69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715, 131072};
    const int32_t limit = (int32_t)LFO_PITCH_SEMITONES * 256;
    int32_t index, fraction;
    semitones_q8 = semitones_q8 > limit ? limit : semitones_q8 < -limit ? -limit : semitones_q8;
    index = (semitones_q8 >> 8) + (int32_t)LFO_PITCH_SEMITONES;   /* (the semitone below, and how far above it) */
    fraction = semitones_q8 & 255;
    if (index >= (int32_t)(2u * LFO_PITCH_SEMITONES)) return SEMITONE_RATIOS_Q16[2u * LFO_PITCH_SEMITONES];
    return SEMITONE_RATIOS_Q16[index] + (((SEMITONE_RATIOS_Q16[index + 1] - SEMITONE_RATIOS_Q16[index]) * (uint32_t)fraction) >> 8);
}
