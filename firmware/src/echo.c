/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's echo: one delay line that all tracks can send into. What went in comes back after delay_frames, again
 * and again, a bit over half as loud each time. Everything here runs in the audio interrupt: integers only
 * (docs/RULES.md). */
#define ECHO_FRAMES_MAX 33075u                  /* the longest delay: 0.75 s (three steps at 60 BPM) */
#define ECHO_FEEDBACK 140                       /* of 256: how much of an echo comes back once more */
#define ECHO_QUIET_LEVEL 16                     /* below this (of 32767) the line counts as silent */

static int16_t echo_line[ECHO_FRAMES_MAX] __attribute__((section(".pool")));
static uint32_t echo_position;
static uint32_t echo_quiet_frames;              /* how long nothing above ECHO_QUIET_LEVEL has gone into the line */

static int echo_quiet(void) { return echo_quiet_frames >= ECHO_FRAMES_MAX; }

/* send: one value a frame, what goes into the line. Adds the echoes to out (L R L R ...). */
static void echo_render(int32_t *out, const int32_t *send, uint32_t frame_count, uint32_t delay_frames)
{
    uint32_t i;
    if (delay_frames > ECHO_FRAMES_MAX) delay_frames = ECHO_FRAMES_MAX;
    for (i = 0; i < frame_count; i++) {
        int32_t echoed, kept;
        if (echo_position >= delay_frames) echo_position = 0;   /* the end, or the delay got shorter */
        echoed = echo_line[echo_position];
        kept = send[i] + echoed * ECHO_FEEDBACK / 256;
        if (kept > 32767) kept = 32767;
        if (kept < -32767) kept = -32767;
        echo_line[echo_position++] = (int16_t)kept;
        if (kept > ECHO_QUIET_LEVEL || kept < -ECHO_QUIET_LEVEL) echo_quiet_frames = 0;
        else if (echo_quiet_frames < ECHO_FRAMES_MAX) echo_quiet_frames++;
        out[2u * i] += echoed;
        out[2u * i + 1u] += echoed;
    }
}
