/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy: a groovebox for the M-VAVE FM-1, built on Dinghy (platform.c and everything below it: updates, the USB
 * rescue, the keys, knobs, LEDs, the screen, flash storage). So far it is the drum track:
 *
 *   lower keys   the 16 steps of the selected drum: a press sets or clears a hit
 *   OP1 OP2 OP3  (the first three upper keys) select KICK, HAT or CLAP, and play it once
 *   PLAY         start and stop
 *   KNOB1        the tempo
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. tests/host.c and tests/web.c run this
 * file on a computer. */
#define APP_NAME "JUICY"                        /* the rescue and crash screens (platform.c) */
#include "platform.c"
#include "drums.c"

/* ---- the sequencer (runs in the audio ISR, so a step starts on its exact frame) */
#define STEP_COUNT 16u
#define TEMPO_MIN 60u
#define TEMPO_MAX 200u
#define FRAMES_PER_STEP_Q8(tempo) (FS * 15u * 256u / (tempo))   /* a step is a sixteenth note: 60 / tempo / 4 seconds */

static volatile uint16_t pattern[DRUM_COUNT];   /* bit s: a hit on step s (the main loop writes) */
static volatile uint32_t playing;               /* (the main loop writes) */
static volatile uint32_t frames_per_step_q8;    /* (the main loop writes) */
static volatile uint32_t audition_requests[DRUM_COUNT];   /* counts up when the main loop wants to hear a drum */
static volatile uint32_t playing_step;          /* the step that sounds now (the ISR writes) */

static void app_audio(int32_t *out, uint32_t frame_count)
{
    static uint32_t was_playing, next_step, frames_to_step_q8, auditions_done[DRUM_COUNT];
    uint32_t frames_done = 0, drum;
    for (drum = 0; drum < DRUM_COUNT; drum++)
        if (auditions_done[drum] != audition_requests[drum]) {
            auditions_done[drum] = audition_requests[drum];
            drums_trigger(drum);
        }
    if (!playing) {
        was_playing = 0;
        drums_render(out, frame_count);
        return;
    }
    if (!was_playing) {                         /* PLAY: from the top, at once */
        was_playing = 1;
        next_step = 0;
        frames_to_step_q8 = 0;
    }
    while (frames_done < frame_count) {
        uint32_t run_frames;
        if (frames_to_step_q8 < 256u) {         /* less than a frame left: the step starts here */
            for (drum = 0; drum < DRUM_COUNT; drum++)
                if (pattern[drum] >> next_step & 1u)
                    drums_trigger(drum);
            playing_step = next_step;
            next_step = (next_step + 1u) % STEP_COUNT;
            frames_to_step_q8 += frames_per_step_q8;   /* (the fraction carries over: the tempo does not drift) */
        }
        run_frames = frames_to_step_q8 >> 8;
        if (run_frames > frame_count - frames_done) run_frames = frame_count - frames_done;
        drums_render(out + 2u * frames_done, run_frames);
        frames_done += run_frames;
        frames_to_step_q8 -= run_frames << 8;
    }
}

/* ---- the screen: the track's name, a row of 16 steps per drum, the knobs' values along the bottom */
#define C_ACCENT RGB(255, 140, 0)               /* orange: what is selected */
#define C_LABEL RGB(130, 130, 130)
#define C_HIT RGB(200, 200, 200)                /* a hit on a row that is not selected */
#define C_STEP_ODD_BEAT RGB(60, 60, 64)         /* empty steps: the beats alternate, so four steps read as one */
#define C_STEP_EVEN_BEAT RGB(40, 40, 44)
#define ROWS_TOP 50u
#define ROW_HEIGHT 46u
#define NO_STEP STEP_COUNT

static const char *const DRUM_NAMES[DRUM_COUNT] = {"KICK", "HAT", "CLAP"};
static const uint8_t STEP_KEYS[STEP_COUNT] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* the lower keys, left to right */
static const uint8_t DRUM_KEYS[DRUM_COUNT] = {1, 3, 5};   /* the upper keys printed OP1, OP2, OP3 */

static void draw_row(uint32_t drum, uint32_t hits, int selected, uint32_t marked_step)
{
    uint32_t step;
    cv_begin(240, ROW_HEIGHT, C_BLACK);
    cv_text(4, 0, &FONT_S, DRUM_NAMES[drum], selected ? C_ACCENT : C_LABEL);
    for (step = 0; step < STEP_COUNT; step++) {
        int32_t x = (int32_t)(4u + step * 14u + step / 4u * 2u);
        uint16_t colour = hits >> step & 1u ? (selected ? C_ACCENT : C_HIT)
                        : step / 4u % 2u ? C_STEP_EVEN_BEAT : C_STEP_ODD_BEAT;
        cv_rect(x, 20, 12, 20, colour);
        if (step == marked_step) cv_rect(x, 42, 12, 3, C_WHITE);
    }
    cv_blit(0, ROWS_TOP + drum * ROW_HEIGHT);
}

static void draw_knob_value(uint32_t knob, const char *label, uint32_t value)   /* knob 0..3: KNOB1..KNOB4, value 0..999 */
{
    char text[4];
    text[0] = (char)('0' + value / 100u);
    text[1] = (char)('0' + value / 10u % 10u);
    text[2] = (char)('0' + value % 10u);
    text[3] = 0;
    draw_text_box(knob * 60u, 190, 60, &FONT_S, label, C_LABEL, 1);
    draw_text_box(knob * 60u, 207, 60, &FONT_L, value < 100u ? text + 1 : text, C_WHITE, 1);
}

/* ---- the main loop: the panel, the LEDs, the screen, saving */
static struct { uint16_t pattern[DRUM_COUNT]; uint8_t tempo; } saved =   /* what survives a power-off */
    {{0x1111, 0x4444, 0x1010}, 138};            /* a first beat: the kick on every quarter, the hat between, the clap on 2 and 4 */

static void app_main(void)
{
    uint32_t previous_keys = 0, selected_drum = DRUM_KICK, drum, step, unsaved = 0, changed_ms = 0;
    uint32_t row_drawn[DRUM_COUNT], tempo_drawn = 0;

    if (settings_load(&saved, sizeof saved) || saved.tempo < TEMPO_MIN || saved.tempo > TEMPO_MAX)
        saved.pattern[DRUM_KICK] = 0x1111, saved.pattern[DRUM_HAT] = 0x4444, saved.pattern[DRUM_CLAP] = 0x1010, saved.tempo = 138;
    for (drum = 0; drum < DRUM_COUNT; drum++) {
        pattern[drum] = saved.pattern[drum];
        row_drawn[drum] = ~0u;
    }
    frames_per_step_q8 = FRAMES_PER_STEP_Q8(saved.tempo);

    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(4, 6, 232, &FONT_L, "DRUMS", C_ACCENT, 0);

    for (;;) {
        uint32_t keys, keys_pressed, buttons_pressed, buttons_released, marked_step;
        int32_t turned;
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        keys_pressed = keys & ~previous_keys;
        previous_keys = keys;
        for (drum = 0; drum < DRUM_COUNT; drum++)
            if (keys_pressed >> DRUM_KEYS[drum] & 1u) {
                selected_drum = drum;
                audition_requests[drum]++;
            }
        for (step = 0; step < STEP_COUNT; step++)
            if (keys_pressed >> STEP_KEYS[step] & 1u) {
                saved.pattern[selected_drum] ^= (uint16_t)(1u << step);
                pattern[selected_drum] = saved.pattern[selected_drum];
                unsaved = 1;
                changed_ms = fm1_ms;
            }

        buttons_pressed = fm1_input_edges(&buttons_released);
        if (buttons_pressed & (1u << BTN_PLAY)) playing = !playing;
        if ((turned = fm1_enc_take(ENC_KNOB1)) != 0) {
            int32_t tempo = (int32_t)saved.tempo + turned;
            saved.tempo = (uint8_t)(tempo < (int32_t)TEMPO_MIN ? TEMPO_MIN : tempo > (int32_t)TEMPO_MAX ? TEMPO_MAX : (uint32_t)tempo);
            frames_per_step_q8 = FRAMES_PER_STEP_Q8(saved.tempo);
            unsaved = 1;
            changed_ms = fm1_ms;
        }

        marked_step = playing ? playing_step : NO_STEP;
        for (step = 0; step < STEP_COUNT; step++)   /* a lower key is lit where the selected drum hits; the running step flips its key */
            fm1_led_key(KEY_LED(STEP_KEYS[step]), (saved.pattern[selected_drum] >> step & 1u) ^ (step == marked_step));
        for (drum = 0; drum < DRUM_COUNT; drum++)
            fm1_led_key(KEY_LED(DRUM_KEYS[drum]), drum == selected_drum);
        fm1_led_key(BTN_PLAY, playing != 0);

        for (drum = 0; drum < DRUM_COUNT; drum++) {   /* redraw only what changed */
            uint32_t state = saved.pattern[drum] | marked_step << 16 | (uint32_t)(drum == selected_drum) << 24;
            if (state != row_drawn[drum]) {
                row_drawn[drum] = state;
                draw_row(drum, saved.pattern[drum], drum == selected_drum, marked_step);
            }
        }
        if (saved.tempo != tempo_drawn) {
            tempo_drawn = saved.tempo;
            draw_knob_value(0, "TEMPO", saved.tempo);
        }

        if (unsaved && !playing && drums_quiet() && fm1_ms - changed_ms > 2000u) {   /* an erase stalls the sound: only when stopped and silent */
            settings_save(&saved, sizeof saved);
            unsaved = 0;
        }
    }
}
