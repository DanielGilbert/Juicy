/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's limiter, the last thing before the sound leaves: when the tracks together are louder than fits, it turns
 * all of them down at once, and lets them come back slowly. Without it the sum would be cut off flat (platform.c
 * clips at full scale), which crackles. Runs in the audio interrupt: integers only (docs/RULES.md). */
#ifndef LIMITER_CEILING
#define LIMITER_CEILING 30000                   /* of 32767: nothing leaves louder than this */
#endif
#define LIMITER_GAIN_FULL 65536u                /* the gain is Q16 */
#define LIMITER_RELEASE_STEP 4u                 /* the gain comes back by this much a frame: from half to full in 0.2 s */
#define LIMITER_INPUT_MAX 500000                /* (louder than all tracks at once: keeps the sums below in 32 bits) */

static uint32_t limiter_gain = LIMITER_GAIN_FULL;

static void limiter_render(int32_t *out, uint32_t frame_count)   /* changes out (L R L R ...) */
{
    uint32_t i;
    for (i = 0; i < frame_count; i++) {
        int32_t left = out[2u * i], right = out[2u * i + 1u];
        int32_t peak = left < 0 ? -left : left, peak_right = right < 0 ? -right : right;
        if (peak_right > peak) peak = peak_right;
        if (peak > LIMITER_INPUT_MAX) peak = LIMITER_INPUT_MAX;
        if (limiter_gain < LIMITER_GAIN_FULL) limiter_gain += LIMITER_RELEASE_STEP;
        if (peak * (int32_t)(limiter_gain >> 4) > LIMITER_CEILING << 12)   /* too loud: down at once, exactly as far as needed */
            limiter_gain = (uint32_t)(LIMITER_CEILING << 16) / (uint32_t)peak;
        out[2u * i] = (left * (int32_t)(limiter_gain >> 4)) >> 12;
        out[2u * i + 1u] = (right * (int32_t)(limiter_gain >> 4)) >> 12;
    }
}
