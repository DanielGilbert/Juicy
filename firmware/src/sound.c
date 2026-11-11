/* SPDX-License-Identifier: GPL-3.0-only */
/* What Juicy's voices share: a sine and the pitch of a note. Integers only, no division (docs/RULES.md). */

static int32_t sine(uint32_t phase)             /* a full cycle over 2^32 -> -32767..32767 (Dinghy's refined parabola, ~0.1 %) */
{
    int32_t x = (int32_t)phase >> 16, absolute_x = x < 0 ? -x : x, y, absolute_y;
    y = (x * (32768 - absolute_x)) >> 13;       /* 4x(1-|x|), Q15 */
    absolute_y = y < 0 ? -y : y;
    y += ((((y * absolute_y) >> 15) - y) * 7373) >> 15;   /* + 0.225 (y|y| - y) */
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

/* An oscillator is a 32-bit phase that wraps: each frame it moves on by f * 2^32 / 44100. The table holds that
 * step for MIDI notes 120..131 (C9..B9, from Dinghy); every octave below is half of it. */
static uint32_t note_phase_step(uint32_t note)  /* note: MIDI, 0..127 */
{
    static const uint32_t STEP_C9[12] = {
        815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
        1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
    return STEP_C9[note % 12u] >> (10u - note / 12u);
}
