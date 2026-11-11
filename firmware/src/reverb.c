/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's reverb: a room of fixed size. What goes in runs round four delay lines of different lengths that feed
 * themselves (each round a bit quieter and duller), and their sum is smeared by two short all-pass lines, other
 * ones for the left than for the right, which is what makes it wide. Everything here runs in the audio interrupt:
 * integers only (docs/RULES.md). */
#define REVERB_COMB_COUNT 4u
#define REVERB_ALLPASS_COUNT 2u                 /* a channel */
#define REVERB_FEEDBACK 230                     /* of 256: a round of a comb line; the tail lasts about 2 s */
#define REVERB_DAMPING 77                       /* of 256: how much of the highs a round takes away */
#define REVERB_SEND_SHIFT 6u                    /* in * amount * 5 >> this goes in: the lines hold the sound 64 times finer than out, so a tail fades smoothly to nothing */
#define REVERB_RETURN_SHIFT 6u                  /* and this brings it back: together amount / 100, and room for the lines to add up */
#define REVERB_QUIET_LEVEL (16 << REVERB_RETURN_SHIFT)
#define REVERB_QUIET_FRAMES 2048u               /* longer than the longest line */

static const uint16_t REVERB_COMB_FRAMES[REVERB_COMB_COUNT] = {1116, 1277, 1422, 1617};
static const uint16_t REVERB_ALLPASS_FRAMES[2][REVERB_ALLPASS_COUNT] = {{556, 441}, {579, 464}};   /* left, right */

static int32_t reverb_comb_lines[REVERB_COMB_COUNT][1617] __attribute__((section(".pool")));
static int32_t reverb_allpass_lines[2][REVERB_ALLPASS_COUNT][579] __attribute__((section(".pool")));
static uint32_t reverb_comb_positions[REVERB_COMB_COUNT], reverb_allpass_positions[2][REVERB_ALLPASS_COUNT];
static int32_t reverb_comb_lows[REVERB_COMB_COUNT];   /* each comb's low-pass */
static uint32_t reverb_quiet_frames = REVERB_QUIET_FRAMES;   /* how long nothing above REVERB_QUIET_LEVEL has gone in or come round */

static int reverb_quiet(void) { return reverb_quiet_frames >= REVERB_QUIET_FRAMES; }

/* in and out are L R L R ...; adds the reverb of in (its left) to out. amount 0..100 */
static void reverb_render(int32_t *out, const int32_t *in, uint32_t frame_count, uint32_t amount)
{
    int32_t send_gain = (int32_t)(amount * 5u);
    uint32_t i, comb, channel, allpass;
    if (reverb_quiet()) {                       /* nothing in the room: is anything coming in? */
        for (i = 0; i < frame_count && !((in[2u * i] * send_gain) >> REVERB_SEND_SHIFT); i++) {}
        if (i == frame_count) return;
    }
    for (i = 0; i < frame_count; i++) {
        int32_t sent = (in[2u * i] * send_gain) >> REVERB_SEND_SHIFT, room = 0;
        for (comb = 0; comb < REVERB_COMB_COUNT; comb++) {
            int32_t *place = &reverb_comb_lines[comb][reverb_comb_positions[comb]];
            int32_t round = *place;
            room += round;
            reverb_comb_lows[comb] += (round - reverb_comb_lows[comb]) * (256 - REVERB_DAMPING) / 256;
            *place = sent + reverb_comb_lows[comb] * REVERB_FEEDBACK / 256;
            if (++reverb_comb_positions[comb] >= REVERB_COMB_FRAMES[comb]) reverb_comb_positions[comb] = 0;
        }
        if (room > REVERB_QUIET_LEVEL || room < -REVERB_QUIET_LEVEL || sent) reverb_quiet_frames = 0;
        else if (reverb_quiet_frames < REVERB_QUIET_FRAMES) reverb_quiet_frames++;
        for (channel = 0; channel < 2u; channel++) {
            int32_t smeared = room;
            for (allpass = 0; allpass < REVERB_ALLPASS_COUNT; allpass++) {
                int32_t *place = &reverb_allpass_lines[channel][allpass][reverb_allpass_positions[channel][allpass]];
                int32_t delayed = *place;
                *place = smeared + delayed / 2;
                smeared = delayed - smeared;
                if (++reverb_allpass_positions[channel][allpass] >= REVERB_ALLPASS_FRAMES[channel][allpass])
                    reverb_allpass_positions[channel][allpass] = 0;
            }
            out[2u * i + channel] += smeared >> REVERB_RETURN_SHIFT;
        }
    }
}
