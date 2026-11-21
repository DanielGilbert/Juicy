/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's echo: two delay lines that all tracks send into, each feeding the other: what goes in comes back after
 * delay_frames on the left, then on the right, then on the left again (a ping-pong), each time quieter by FEEDBACK
 * and darker by TONE (a low-pass in the loop). WIDTH spreads the two lines from the middle (both in both ears: a mono
 * echo, as it always was) to one each side. Everything here runs in the audio interrupt: integers only
 * (docs/RULES.md). */
#define ECHO_FRAMES_MAX 44100u                  /* the longest delay: 1 s (TIME's longest at a slower tempo: cut to this) */
#define ECHO_PARAMETER_MAX 100u                 /* FEEDBACK, WIDTH and TONE go 0..100 */
#define ECHO_FEEDBACK_MAX 230u                  /* of 256 at FEEDBACK 100: nearly endless, never louder */
#define ECHO_QUIET_LEVEL 16                     /* below this (of 32767) the lines count as silent */

/* how far the loop's low-pass moves a frame for TONE 0..100, Q15: 0 not at all (as it was), then from 16 kHz down to
 * 600 Hz in equal ratios (1 - e^(-2 pi f / 44100)) */
static const uint16_t ECHO_TONE_RATE[ECHO_PARAMETER_MAX + 1u] = {
    32767, 29159, 28892, 28616, 28329, 28033, 27728, 27414, 27091, 26760, 26422, 26076,
    25724, 25365, 25001, 24631, 24257, 23878, 23495, 23109, 22721, 22330, 21937, 21542,
    21147, 20751, 20356, 19960, 19566, 19172, 18780, 18390, 18003, 17618, 17235, 16856,
    16481, 16109, 15741, 15377, 15017, 14662, 14312, 13966, 13626, 13291, 12961, 12636,
    12317, 12003, 11695, 11392, 11095, 10804, 10518, 10238, 9964, 9695, 9432, 9175,
    8923, 8677, 8437, 8202, 7972, 7748, 7529, 7315, 7106, 6903, 6705, 6511,
    6323, 6139, 5960, 5785, 5616, 5450, 5289, 5133, 4980, 4832, 4687, 4547,
    4410, 4278, 4149, 4023, 3901, 3783, 3668, 3556, 3448, 3342, 3240, 3140,
    3044, 2950, 2859, 2771, 2685};

static int16_t echo_lines[2][ECHO_FRAMES_MAX] __attribute__((section(".pool")));   /* the left one first */
static uint32_t echo_position;
static int32_t echo_lows[2];                    /* the loop's low-pass, a line each */
static uint32_t echo_quiet_frames;              /* how long nothing above ECHO_QUIET_LEVEL has gone into the lines */

static int echo_quiet(void) { return echo_quiet_frames >= ECHO_FRAMES_MAX; }

static int16_t echo_kept(int32_t value)         /* (into a line: as an int16) */
{
    if (value > 32767) value = 32767;
    if (value < -32767) value = -32767;
    if (value > ECHO_QUIET_LEVEL || value < -ECHO_QUIET_LEVEL) echo_quiet_frames = 0;
    return (int16_t)value;
}

/* send: one value a frame, what goes into the left line. Adds the echoes to out (L R L R ...). feedback, width and
 * tone 0..ECHO_PARAMETER_MAX */
static void echo_render(int32_t *out, const int32_t *send, uint32_t frame_count, uint32_t delay_frames,
                        uint32_t feedback, uint32_t width, uint32_t tone)
{
    const int32_t feedback_q8 = (int32_t)(feedback * ECHO_FEEDBACK_MAX / ECHO_PARAMETER_MAX);
    const int32_t other_side_q8 = (int32_t)((ECHO_PARAMETER_MAX - width) * 256u / ECHO_PARAMETER_MAX);   /* how much of each line the other side gets */
    const int32_t tone_rate = ECHO_TONE_RATE[tone];
    uint32_t i, line;
    if (delay_frames > ECHO_FRAMES_MAX) delay_frames = ECHO_FRAMES_MAX;
    for (i = 0; i < frame_count; i++) {
        int32_t echoed[2], into[2];
        if (echo_position >= delay_frames) echo_position = 0;   /* the end, or the delay got shorter */
        echoed[0] = echo_lines[0][echo_position];
        echoed[1] = echo_lines[1][echo_position];
        into[0] = send[i] + echoed[1] * feedback_q8 / 256;   /* the right one comes back on the left, */
        into[1] = echoed[0] * feedback_q8 / 256;  /* the left one on the right */
        for (line = 0; line < 2u; line++) {
            echo_lows[line] = tone_rate == 32767 ? into[line] : echo_lows[line] + (((into[line] - echo_lows[line]) * tone_rate) >> 15);
            echo_lines[line][echo_position] = echo_kept(echo_lows[line]);
        }
        if (echo_quiet_frames < ECHO_FRAMES_MAX) echo_quiet_frames++;
        echo_position++;
        out[2u * i] += echoed[0] + (echoed[1] * other_side_q8 >> 8);
        out[2u * i + 1u] += echoed[1] + (echoed[0] * other_side_q8 >> 8);
    }
}
