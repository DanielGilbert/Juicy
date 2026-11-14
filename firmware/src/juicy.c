/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy: a groovebox for the M-VAVE FM-1, built on Dinghy (platform.c and everything below it: updates, the USB
 * rescue, the keys, knobs, LEDs, the screen, flash storage). What it does, key by key and page by page, is in
 * README.md (the panel, the screens, notes, accents, ARP, the lights, MIDI); what changed, in CHANGELOG.md.
 *
 * The files, built as one (this one includes the rest):
 *   song.c       what a song holds: tracks, patterns, notes and chords, names, sounds; checking and growing a kept one
 *   demos.c      the demo songs (DEMO1 is also what a new FM-1 starts with)
 *   sequencer.c  the sequencer and the mixer, in the audio interrupt
 *   screen.c     drawing: colours, the header, each page's body, the knobs' places
 *   pages.c      what each page does with the knobs, the keys, SEQ and REC
 *   view.c       the lights and the screen, from the main loop's state
 *   juicy.c      the songs' places and the library in flash, the main loop: its state (ui) and its passes
 *   drums.c, synth.c, pad.c, echo.c, reverb.c, limiter.c: the sound
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division; the audio
 * interrupt under half of its 5.8 ms (it peaks near a quarter). What platform.c offers: docs/API.md. The chip's rules:
 * docs/RULES.md. tests/host.c and tests/web.c run this file on a computer; tests/regress.sh tells whether a change
 * changed what it plays and shows. */
#define APP_NAME "JUICY"                        /* the rescue and crash screens (platform.c) */
#define CV_MAX (240u * 132u)                    /* the biggest canvas: a piano roll */
#include <stddef.h>                             /* offsetof (the compiler's own, freestanding) */
#include "platform.c"
#include "sound.c"
#include "drums.c"
#include "synth.c"
#include "pad.c"
#include "echo.c"
#include "reverb.c"
#include "limiter.c"

#include "song.c"
#include "demos.c"
#if defined(DINGHY_HOST) && !defined(HOST_WEB)
#include "../../tests/test_song.c"              /* (on a computer: the host scripts' song) */
#endif

/* ---- the songs' places: four that are always there, the three demo songs and an empty one, and eight kept in flash
 * (a blob each, docs/API.md). The song that plays is none of them: it is a copy, kept as the settings. */
#define PLACES_KEPT 8u
enum { PLACE_DEMO, PLACE_DEMO2, PLACE_DEMO3, PLACE_NEW, PLACE_FIRST_KEPT, PLACE_COUNT = PLACE_FIRST_KEPT + PLACES_KEPT };
_Static_assert(PLACES_KEPT <= NBLOB && sizeof(song_t) <= SECTOR_MAX, "a song a blob");

static song_t place_song;                       /* a kept song on its way in: checked before it is the song */
static song_t preview_backup;                   /* the song, put aside while a place's song or pattern plays (a preview) */

static int place_kept_valid(uint32_t place)     /* place_song holds that place's song then */
{
    uint32_t kept = 0;                          /* (as it is now, or kept in an older size) */
    while (kept < PATTERN_SIZES_COUNT && blob_load(place - PLACE_FIRST_KEPT, &place_song, SONG_SIZE_KEPT(PATTERN_SIZES[kept]))) kept++;
    if (kept == PATTERN_SIZES_COUNT) return 0;
    if (kept) song_grow(&place_song, PATTERN_SIZES[kept]);
    return song_valid(&place_song);
}

static int place_load(uint32_t place)           /* 0: that place's song is the song now */
{
    if (place == PLACE_DEMO) first_song();
    else if (place == PLACE_DEMO2) demo_uplifting();
    else if (place == PLACE_DEMO3) demo_lofi();
    else if (place == PLACE_NEW) empty_song();
    else if (!place_kept_valid(place)) return -1;
    else memcpy((void *)&song, &place_song, sizeof song);
    return 0;
}

/* ---- the library: patterns kept apart from the songs, all 16 together in slot 0 (docs/API.md) */
#define LIBRARY_PLACES 16u
static struct { pattern_t patterns[LIBRARY_PLACES]; } library;
_Static_assert(sizeof library <= SLOT_MAX, "the library a slot");

static void library_load(void)                  /* nothing kept, or something broken: an empty library */
{
    uint32_t place, track, kept = 0, valid;     /* (as it is now, or kept in an older size) */
    while (kept < PATTERN_SIZES_COUNT && slot_load(0, &library, LIBRARY_PLACES * PATTERN_SIZES[kept])) kept++;
    valid = kept < PATTERN_SIZES_COUNT;
    if (valid && kept) patterns_grow(library.patterns, LIBRARY_PLACES, PATTERN_SIZES[kept]);
    for (place = 0; valid && place < LIBRARY_PLACES; place++)
        if (!pattern_valid(&library.patterns[place])) valid = 0;
    if (valid) return;
    memset(&library, 0, sizeof library);
    for (place = 0; place < LIBRARY_PLACES; place++)
        for (track = 0; track < sizeof library.patterns[place].bar_totals; track++)
            library.patterns[place].bar_totals[track] = 1;
}

#include "sequencer.c"
#include "screen.c"

/* ---- the main loop. Each pass: the panel and the MIDI clock, PLAY, the page; the page works on the knobs, the keys,
 * SEQ, REC and OCT (pages.c); the lights and the screen show it (view.c); what waits is saved once nothing sounds.
 * What lasts from one pass to the next is in ui, what a pass finds and works out in its pass_t. */
static uint32_t turned_value(uint32_t value, int32_t detents, uint32_t lowest, uint32_t highest)
{
    int32_t turned = (int32_t)value + detents;
    return turned < (int32_t)lowest ? lowest : turned > (int32_t)highest ? highest : (uint32_t)turned;
}

static int turn_setting(volatile uint8_t *setting, int32_t detents, uint32_t lowest, uint32_t highest)   /* 1: it changed */
{
    uint32_t turned = turned_value(*setting, detents, lowest, highest);
    if (turned == *setting) return 0;
    *setting = (uint8_t)turned;
    return 1;
}

static uint32_t keyboard_first_note(uint32_t melodic, uint32_t octave)   /* the leftmost key's note at that octave */
{
    return MELODIC[melodic].note_lowest + KEYS_FIRST_NOTE_ABOVE_LOWEST + octave * 12u - 12u;
}

/* the main loop's state, from one pass to the next (the panel, the pages, what waits to be saved, what is drawn) */
static struct {
    uint32_t previous_keys;                     /* the keys down in the pass before */
    uint32_t shown_track, global_shown, songs_shown, library_shown, names_shown;   /* which page: a track's, JUICY, SAVE's two, JUICY's NAMES */
    uint32_t fx_page;                           /* FX: which page of a track (DRUMS has no sound page) */
    uint32_t shown_pattern, shown_bar;          /* the pattern and the bar on the screen */
    uint32_t selected_drum;                     /* DRUMS: the drum the keys edit */
    uint32_t selected_notes[MELODIC_COUNT];     /* the next note a track places */
    uint32_t selected_chord;                    /* the chord KNOB3 shows on the pad's page: PAD_CHORD_* */
    uint32_t selected_lengths[MELODIC_COUNT];   /* KNOB4: how long a placed note is (not the pad's) */
    uint32_t roll_lowest_notes[MELODIC_COUNT];  /* the lowest note a track's roll shows */
    uint32_t cursor_steps[MELODIC_COUNT];       /* SELECT: the cursor's step in the bar on the screen */
    uint32_t keyboard_octaves[MELODIC_COUNT];   /* where the keys are, 0 .. KEYBOARD_OCTAVES - 1 (1: as they start) */
    uint32_t recording_on;                      /* EDIT + REC on a track's notes: a key puts its note at the cursor */
    uint32_t select_on;                         /* SEL: the keys OP1 .. OP5 pick the page */
    uint32_t edit_on;                           /* EDIT: the lower keys are the steps. Neither: the keys play */
    uint32_t held_key;                          /* the key that played the last note by hand */
    uint32_t oct_down_waits, oct_down_ms;       /* OCT- is down and has not done anything yet; since when */
    uint32_t rec_waits, rec_ms;                 /* the same for REC (copy or clear) */
    uint32_t step_key_waiting, step_key_ms;     /* EDIT: a step key is down and has not done anything yet (DRUMS, a track); since when */
    uint32_t selected_library_place, library_note, places_with_pattern;   /* bit p: library place p holds a pattern */
    uint32_t library_unsaved;                   /* the library goes to flash as soon as nothing sounds */
    uint32_t splash_shown;                      /* the start screen is there (until SPLASH_MS, or a touch) */
    uint32_t splash_start_ms;                   /* when it came (loading takes a while after power-on) */
    uint32_t clock_times_ms[CLOCK_RING], clock_count;   /* MIDI clock in: when the last ones came */
    uint32_t external_clock, external_bpm;      /* a clock comes in: Juicy follows it, at this tempo */
    uint32_t clock_start;                       /* START / CONTINUE came: Juicy plays from the next clock */
    uint32_t clock_locked, clocks_since_start;  /* playing from a START / CONTINUE: in step with the clock */
    int32_t clock_correction_q8;                /* how much shorter a step is, to catch up with the clock (frames, Q8) */
    uint32_t clock_ticks_sent;                  /* Juicy the master: the clocks sent since PLAY */
    uint32_t sound_note, user_sounds_unsaved;   /* the sound page's note; the user sounds go to flash as soon as nothing sounds */
    uint32_t selected_place, songs_note;        /* the songs' page */
    uint32_t places_with_song;                  /* bit p: place p holds a song */
    uint8_t place_names[PLACES_KEPT][NAME_LENGTH];   /* the kept songs' names (the songs' page shows them) */
    uint32_t naming_cursor;                     /* the NAMES page's cursor: 0 .. 7 the song's name, 8 .. 15 the pattern's */
    uint32_t groove_shown, groove_asked, groove_pattern;   /* DRUMS: the groove that plays in place of the drums (GROOVE_OWN: none), REC asked, in which pattern */
    drums_kept_t groove_kept;                   /* (meanwhile: the drums as they were) */
    uint32_t progression_shown, progression_asked, progression_pattern;   /* PAD: the progression that plays in place of the chords (PROGRESSION_OWN: none), the same */
    pad_kept_t progression_kept;                /* (meanwhile: the pad as it was) */
    uint32_t undo_ready, undo_change_ms;        /* GLO has a song to bring back; when the song last changed */
    uint32_t place_to_save;                     /* the place the song goes to as soon as nothing sounds, PLACE_COUNT = none */
    uint32_t preview_on, preview_page, preview_place, preview_wanted;   /* PLAY on a place: its song (pattern) plays; on which page, which place; PLAY asked for one */
    uint32_t kept_chain_on, kept_shown_pattern, kept_shown_bar;   /* (meanwhile: as they were with the song) */
    uint32_t unsaved, changed_ms;               /* the song changed (since when): it goes to flash once nothing sounds */
    /* what is on the screen (+ 1, so 0 means "draw it"): the page, its header, each part of its body, the knobs */
    uint32_t page_drawn, header_drawn, chord_name_drawn, chain_drawn, songs_drawn, library_drawn, naming_drawn, sound_drawn;
    uint32_t drum_row_drawn[DRUMS_PER_PAGE], drum_accents_drawn, drum_pages_drawn, mix_drawn[DRUM_COUNT];
    uint32_t roll_drawn, roll_marker_drawn, knob_drawn[KNOB_COUNT];
} ui = {
    .shown_track = PAGE_DRUMS, .global_shown = 1, .fx_page = FX_PAGE_NOTES, .selected_drum = DRUM_KICK,
    .selected_notes = {33, 60, 57, 69}, .roll_lowest_notes = {28, 56, 53, 65},   /* A1, C4, A3, A4 */
    .selected_chord = PAD_CHORD_AUTO,
    .selected_lengths = {LENGTH_WHOLE_FIRST, LENGTH_WHOLE_FIRST, LENGTH_WHOLE_FIRST, LENGTH_WHOLE_FIRST},
    .keyboard_octaves = {1, 1, 1, 1}, .step_key_waiting = NO_STEP, .library_note = LIBRARY_NOTE_NONE, .splash_shown = 1,
    .clock_start = CLOCK_START_NONE, .sound_note = SOUND_NOTE_NONE, .selected_place = PLACE_FIRST_KEPT, .songs_note = SONGS_NOTE_NONE,
    .places_with_song = 1u << PLACE_DEMO | 1u << PLACE_DEMO2 | 1u << PLACE_DEMO3 | 1u << PLACE_NEW, .place_to_save = PLACE_COUNT,
    .page_drawn = ~0u,
};

static int suggestion_plays(void)               /* a groove or a progression plays, not the song's own yet */
{
    return ui.groove_shown != GROOVE_OWN || ui.progression_shown != PROGRESSION_OWN;
}

/* what one pass of the main loop finds and works out: the panel's moves, the page, what the knobs and keys show */
typedef struct {
    uint32_t keys, keys_pressed, buttons_pressed, buttons_released;   /* bit k: key k (0 = F3 .. 26 = G5); buttons by id */
    int32_t detents[KNOB_COUNT], presets_detents, algorithm_detents, select_detents;
    uint32_t page;                              /* PAGE_* */
    int sound_page, mix_page, notes_page;       /* a track's other pages (FX), or its notes */
    int keys_edit, keys_play;                   /* what the keys do: SEL comes first, then EDIT, else they play */
    volatile pattern_t *pattern;                /* the pattern on the screen */
    uint32_t bar_total;                         /* of what the page shows: its track, or the whole pattern */
    uint32_t melodic;                           /* a track's page: its melodic track */
    uint32_t tapped_steps, held_step;           /* EDIT: the step keys let go after a tap (bit s), the one held long enough */
    uint32_t shown_note, shown_chord, shown_length;   /* a track's notes: what the knobs show (the cursor's note, or the next one) */
    uint32_t step_leds;                         /* bit s: the lower key of step s is lit (EDIT) */
    uint32_t step_now, marked_step, marked_bar, marked_slot;   /* what plays: its step, and where the screen marks it */
    uint32_t keys_glowing, keys_glowing_bright, keys_lit;   /* bit k: key k's light */
    int song_changed;
} pass_t;

#include "pages.c"
#include "view.c"

/* the keys, the buttons and the four knobs as they moved since the pass before */
static void read_panel(pass_t *pass)
{
    uint32_t knob;
    pass->keys = fm1_in.notes;                  /* bit k: key k (0 = F3 .. 26 = G5) */
    pass->keys_pressed = pass->keys & ~ui.previous_keys;
    ui.previous_keys = pass->keys;
    pass->buttons_pressed = fm1_input_edges(&pass->buttons_released);
    for (knob = 0; knob < KNOB_COUNT; knob++)
        pass->detents[knob] = fm1_enc_take(ENC_KNOB1 + knob);
}

/* MIDI clock and transport in (USB, TRS): Juicy follows a clock that comes in, its tempo and its START, CONTINUE, STOP */
static void follow_clock(void)
{
    uint8_t midi_status, midi_data1, midi_data2;
    int midi_source;
    while ((midi_source = midi_get(&midi_status, &midi_data1, &midi_data2)) != 0) {   /* clock and transport (USB, TRS) */
        if (midi_status == 0xF8u) {             /* a clock: 24 a quarter note */
            ui.clock_times_ms[ui.clock_count % CLOCK_RING] = fm1_ms;
            ui.clock_count++;
            if (ui.clock_start != CLOCK_START_NONE) {   /* the first clock after START / CONTINUE: the downbeat */
                resume_playing = ui.clock_start == CLOCK_START_CONTINUE;
                playing = 1;
                ui.clock_locked = 1;
                ui.clocks_since_start = 0;
                ui.clock_start = CLOCK_START_NONE;
            } else if (ui.clock_locked && playing)
                ui.clocks_since_start++;
            if (ui.clock_count > CLOCK_TEMPO_CLOCKS) {   /* the tempo: the last bar's clocks (as many as there are at first) */
                const uint32_t span = ui.clock_count > CLOCK_RING ? CLOCK_RING - 1u : ui.clock_count - 1u;
                const uint32_t span_ms = fm1_ms - ui.clock_times_ms[(ui.clock_count - 1u - span) % CLOCK_RING];
                const uint32_t step_q8 = span_ms * 67738u / span;   /* ms over 24 clocks * 44.1 / 4 * 256 */
                ui.external_bpm = span_ms ? 60000u * span / (span_ms * 24u) : 0u;
                if (!ui.clock_locked || !playing) ui.clock_correction_q8 = 0;
                else if (ui.clocks_since_start % 6u == 0u && steps_since_play) {
                    /* a step by the clock: where the ISR's step is against it, a quarter of that put right */
                    const int32_t step_ms_q8 = (int32_t)(step_q8 * 1000u / FS);
                    const int32_t steps_apart = (int32_t)(ui.clocks_since_start / 6u) - (int32_t)(steps_since_play - 1u);
                    const int32_t late_ms_q8 = (int32_t)(step_started_ms - fm1_ms) * 256 + steps_apart * step_ms_q8;
                    ui.clock_correction_q8 = late_ms_q8 * 441 / 40;   /* ms -> frames (44.1), a quarter of it */
                    if (ui.clock_correction_q8 > (int32_t)step_q8 / 4) ui.clock_correction_q8 = (int32_t)step_q8 / 4;
                    if (ui.clock_correction_q8 < -(int32_t)step_q8 / 4) ui.clock_correction_q8 = -(int32_t)step_q8 / 4;
                }
                frames_per_step_q8 = (uint32_t)((int32_t)step_q8 - ui.clock_correction_q8);   /* (until the next step's clock) */
            }
        } else if (midi_status == 0xFAu) ui.clock_start = CLOCK_START_START;
        else if (midi_status == 0xFBu) ui.clock_start = CLOCK_START_CONTINUE;
        else if (midi_status == 0xFCu) {
            playing = 0;
            ui.clock_start = CLOCK_START_NONE;
            ui.clock_locked = 0;
        }
    }
    ui.external_clock = ui.clock_count && fm1_ms - ui.clock_times_ms[(ui.clock_count - 1u) % CLOCK_RING] < CLOCK_GONE_MS;
    if (!ui.external_clock) {                   /* our own tempo */
        frames_per_step_q8 = FRAMES_PER_STEP_Q8(song.tempo);
        ui.clock_locked = 0;
    }
}

/* PLAY: start and stop (on the songs' or the library's page: a preview, below); the master's clock out */
static void play_button(pass_t *pass)
{
    if (pass->buttons_pressed & (1u << BTN_PLAY) && !playing
        && (ui.songs_shown ? ui.places_with_song >> ui.selected_place & 1u : ui.library_shown && ui.places_with_pattern >> ui.selected_library_place & 1u))
        ui.preview_wanted = 1;                  /* stopped, on the songs' or the library's page, a place with something: it plays (below) */
    else if (pass->buttons_pressed & (1u << BTN_PLAY)) {
        playing = !playing;
        resume_playing = 0;
        ui.clock_locked = 0;                    /* (started by hand: the clock's tempo, not its beat) */
        ui.clock_ticks_sent = 0;
        if (!ui.external_clock) midi_send(playing ? 0xFAu : 0xFCu, 0, 0);   /* the master: START / STOP */
    }
    if (ui.external_clock) ui.clock_ticks_sent = steps_since_play * 6u;   /* (should the clock stop: on as the master from here, no burst) */
    else if (playing && steps_since_play) { /* the master's clock: six a step, each in its time */
        const uint32_t step_ms_q8 = frames_per_step_q8 * 1000u / FS, since_ms = fm1_ms - step_started_ms;
        uint32_t ticks_due = (steps_since_play - 1u) * 6u;
        ticks_due += (int32_t)since_ms < 0 ? 0u : since_ms * 256u * 6u / step_ms_q8 + 1u > 6u ? 6u : since_ms * 256u * 6u / step_ms_q8 + 1u;
        for (; ui.clock_ticks_sent < ticks_due; ui.clock_ticks_sent++) midi_send(0xF8u, 0, 0);
    }
}

/* SAVE, HOME, SEL, EDIT, the mute and track keys, FX: the page; ALGORITHM, SELECT, PRESETS */
static void choose_page(pass_t *pass)
{
    uint32_t track;
    if (pass->buttons_pressed & (1u << BTN_SAVE)) {   /* SAVE: the songs, the library, back */
        ui.library_shown = ui.songs_shown;
        ui.songs_shown = !ui.songs_shown && !ui.library_shown;
    }
    if (pass->buttons_pressed & (1u << BTN_HOME)) {   /* (from the songs', the library's or the NAMES page: home) */
        ui.global_shown = ui.songs_shown || ui.library_shown || ui.names_shown || !ui.global_shown;
        ui.songs_shown = ui.library_shown = ui.names_shown = 0;
    }
    if (pass->buttons_pressed & (1u << BTN_SEL)) {   /* SEL and EDIT: one at a time, the one pressed takes over */
        ui.select_on = !ui.select_on;
        if (ui.select_on) ui.edit_on = 0;
    }
    if (pass->buttons_pressed & (1u << BTN_EDIT)) {
        ui.edit_on = !ui.edit_on;
        if (ui.edit_on) ui.select_on = 0;
    }
    for (track = 0; track < TRACK_COUNT; track++)
        if (ui.select_on && pass->keys_pressed >> MUTE_KEYS[track] & 1u)
            muted_tracks ^= 1u << track;
    for (track = 0; track < TRACK_COUNT; track++)
        if (ui.select_on && pass->keys_pressed >> TRACK_KEYS[track] & 1u) {
            ui.shown_track = track;
            ui.global_shown = ui.songs_shown = ui.library_shown = 0;
        }
    pass->page = ui.songs_shown ? PAGE_SONGS : ui.library_shown ? PAGE_LIBRARY : ui.global_shown ? PAGE_GLOBAL : ui.shown_track;
    if (pass->buttons_pressed & (1u << BTN_FX) && pass->page == PAGE_GLOBAL) ui.names_shown = !ui.names_shown;   /* JUICY's other page: the names */
    if (pass->page != PAGE_GLOBAL) ui.names_shown = 0;
    if (ui.names_shown) pass->page = PAGE_NAMING;
    if (pass->buttons_pressed & (1u << BTN_FX) && pass->page < TRACK_COUNT)   /* the notes, the sound (not DRUMS), the mix, round */
        ui.fx_page = ui.fx_page == FX_PAGE_MIX ? FX_PAGE_NOTES : pass->page == PAGE_DRUMS ? FX_PAGE_MIX : ui.fx_page + 1u;
    pass->sound_page = ui.fx_page == FX_PAGE_SOUND && pass->page > PAGE_DRUMS && pass->page < TRACK_COUNT;
    pass->mix_page = ui.fx_page == FX_PAGE_MIX && pass->page < TRACK_COUNT;
    pass->notes_page = pass->page >= MELODIC_FIRST_PAGE && pass->page < TRACK_COUNT && !pass->sound_page && !pass->mix_page;
    if (!ui.edit_on || !pass->notes_page) ui.recording_on = 0;
    pass->keys_edit = !ui.select_on && ui.edit_on && !pass->sound_page && !pass->mix_page && !ui.recording_on;   /* what the keys do: SEL comes first, then EDIT, else they play */
    pass->keys_play = !ui.select_on && (!ui.edit_on || pass->sound_page || pass->mix_page);   /* (on a sound or mix page they always play) */
    pass->algorithm_detents = fm1_enc_take(ENC_ALGORITHM);
    pass->select_detents = fm1_enc_take(ENC_SELECT);   /* (taken on every page: turns elsewhere do not pile up for DRUMS) */
    if (!pass->sound_page) ui.sound_note = SOUND_NOTE_NONE;
    pass->presets_detents = fm1_enc_take(ENC_PRESETS);   /* the pattern on the screen, on every page, as OCT is */
    ui.shown_pattern = turned_value(ui.shown_pattern, pass->presets_detents, 0, PATTERN_COUNT - 1u);
}

static void preview_put_back(void)              /* the song as it was before the preview */
{
    ui.preview_on = 0;
    memcpy((void *)&song, &preview_backup, sizeof song);
    chain_on = ui.kept_chain_on;
    ui.shown_pattern = ui.kept_shown_pattern;
    ui.shown_bar = ui.kept_shown_bar;
}

/* a preview ends (stopped, another page, PRESETS, SEQ or REC): the song is back as it was */
static void preview_end(pass_t *pass)
{
    if (ui.preview_on && (!playing || pass->page != ui.preview_page || pass->presets_detents || pass->buttons_pressed & (1u << BTN_SEQ | 1u << BTN_REC))) {
        if (playing && !ui.external_clock) midi_send(0xFCu, 0, 0);
        playing = 0;
        preview_put_back();
        ui.shown_pattern = turned_value(ui.shown_pattern, pass->presets_detents, 0, PATTERN_COUNT - 1u);
    }
}

static void groove_drop(volatile pattern_t *pattern)   /* the groove goes, the drums are back as they were */
{
    if (ui.groove_shown == GROOVE_OWN) return;
    drums_put_back(pattern, &ui.groove_kept);
    ui.groove_shown = GROOVE_OWN;
    ui.groove_asked = 0;
}

/* a groove that plays in place of the drums goes (another page, PRESETS, EDIT off) */
static void groove_end(pass_t *pass)
{
    if (ui.groove_shown != GROOVE_OWN && (pass->page != PAGE_DRUMS || pass->mix_page || pass->presets_detents || !ui.edit_on))
        groove_drop(&song.patterns[ui.groove_pattern]);
}

/* a progression that plays in place of the pad's chords goes, the chords are back (another page, PRESETS, EDIT off) */
static void progression_end(pass_t *pass)
{
    if (ui.progression_shown != PROGRESSION_OWN && (pass->page != MELODIC_FIRST_PAGE + MELODIC_PAD || pass->sound_page || pass->mix_page || pass->presets_detents || !ui.edit_on)) {
        pad_put_back(&song.patterns[ui.progression_pattern], &ui.progression_kept);
        ui.progression_shown = PROGRESSION_OWN;
        ui.progression_asked = 0;
        ui.roll_drawn = 0;
    }
}

/* a preview goes on to another place, or starts (PLAY, above) */
static void preview_follow(pass_t *pass)
{
    if (ui.preview_on && (pass->page == PAGE_SONGS ? ui.selected_place : ui.selected_library_place) != ui.preview_place) {
        preview_put_back();                     /* another place while it plays: the song back, and that place plays, if it holds something */
        ui.preview_wanted = pass->page == PAGE_SONGS ? ui.places_with_song >> ui.selected_place & 1u : ui.places_with_pattern >> ui.selected_library_place & 1u;
        if (!ui.preview_wanted) {
            if (!ui.external_clock) midi_send(0xFCu, 0, 0);
            playing = 0;
        }
    }
    if (ui.preview_wanted && (pass->page == PAGE_SONGS || pass->page == PAGE_LIBRARY)) {   /* the song put aside, the place's song plays (a pattern: in the pattern on the screen, round and round) */
        const int going_on = playing;           /* (from one place to the next: no new START) */
        playing = 0;                            /* (while the song changes under it) */
        memcpy(&preview_backup, (const void *)&song, sizeof song);
        ui.kept_chain_on = chain_on;
        ui.kept_shown_pattern = ui.shown_pattern;
        ui.kept_shown_bar = ui.shown_bar;
        ui.preview_on = pass->page != PAGE_SONGS || !place_load(ui.selected_place);
        if (!ui.preview_on) memcpy((void *)&song, &preview_backup, sizeof song);   /* (a kept song that will not load: nothing plays) */
        else if (pass->page == PAGE_SONGS) {
            chain_on = song_has_chain();
            ui.shown_pattern = 0;
        } else {
            memcpy((void *)&song.patterns[ui.shown_pattern], &library.patterns[ui.selected_library_place], sizeof(pattern_t));
            chain_on = 0;
        }
        if (ui.preview_on) {
            ui.preview_page = pass->page;
            ui.preview_place = pass->page == PAGE_SONGS ? ui.selected_place : ui.selected_library_place;
            ui.shown_bar = 0;
            resume_playing = 0;
            ui.clock_locked = 0;
            steps_since_play = ui.clock_ticks_sent = 0;
            if (!going_on && !ui.external_clock) midi_send(0xFAu, 0, 0);
            play_starts++;
            playing = 1;
        } else if (going_on && !ui.external_clock) midi_send(0xFCu, 0, 0);
    }
    ui.preview_wanted = 0;
}

/* REC on JUICY or a track (EDIT): tapped, a copy or recording; held, clearing */
static void rec_button(pass_t *pass)
{
    if (!ui.edit_on || !(pass->page == PAGE_GLOBAL || (pass->page < TRACK_COUNT && !pass->sound_page && !pass->mix_page)))
        ui.rec_waits = 0;                       /* (another page, or EDIT off, while REC is down: it does nothing) */
    else if (pass->buttons_pressed & (1u << BTN_REC)) {   /* REC moves when it is let go: held, it clears */
        ui.rec_waits = 1;
        ui.rec_ms = fm1_ms;
    }
    if (ui.rec_waits && pass->buttons_released & (1u << BTN_REC)) {
        ui.rec_waits = 0;
        if (ui.groove_shown != GROOVE_OWN) {    /* DRUMS: the groove that plays is kept (asked again over drums that held something) */
            if (!drums_kept_empty(&ui.groove_kept) && !ui.groove_asked) ui.groove_asked = 1;
            else {
                ui.groove_shown = GROOVE_OWN;
                ui.groove_asked = 0;
                pass->song_changed = 1;
            }
        } else if (ui.progression_shown != PROGRESSION_OWN) {   /* PAD: the same for the progression that plays */
            if (!pad_kept_empty(&ui.progression_kept) && !ui.progression_asked) ui.progression_asked = 1;
            else {
                ui.progression_shown = PROGRESSION_OWN;
                ui.progression_asked = 0;
                pass->song_changed = 1;
            }
        } else if (pass->notes_page) ui.recording_on = !ui.recording_on;
        else if (pass->page == PAGE_GLOBAL && !pattern_empty(&song.patterns[ui.shown_pattern])) {
            uint32_t index;                     /* a copy goes to the first empty pattern, which is then the one on the screen */
            for (index = 0; index < PATTERN_COUNT; index++)
                if (pattern_empty(&song.patterns[index])) {
                    memcpy((void *)&song.patterns[index], (const void *)&song.patterns[ui.shown_pattern], sizeof(pattern_t));
                    ui.shown_pattern = index;
                    pass->song_changed = 1;
                    break;
                }
        }
    }
    if (ui.rec_waits && fm1_ms - ui.rec_ms >= HOLD_MS && !suggestion_plays()) {
        ui.rec_waits = 0;                       /* held: JUICY empties the pattern, DRUMS the selected drum, a track itself; the bars stay */
        pattern_clear(&song.patterns[ui.shown_pattern], pass->page == PAGE_GLOBAL ? ALL_TRACKS : pass->page, ui.selected_drum);
        ui.roll_drawn = 0;
        pass->song_changed = 1;
    }
}

/* OCT- / OCT+: the bar on the screen (EDIT: one more, or one taken out); recording: the keys' octave */
static void oct_buttons(pass_t *pass)
{
    if (ui.recording_on) {                      /* recording: OCT- / OCT+ move the keys (the cursor goes through the bars) */
        const uint32_t octave_melodic = pass->page - MELODIC_FIRST_PAGE;
        if (pass->buttons_pressed & (1u << BTN_OCT_DOWN) && ui.keyboard_octaves[octave_melodic] > 0u) ui.keyboard_octaves[octave_melodic]--;
        if (pass->buttons_pressed & (1u << BTN_OCT_UP) && ui.keyboard_octaves[octave_melodic] + 1u < KEYBOARD_OCTAVES) ui.keyboard_octaves[octave_melodic]++;
    }
    if (!ui.recording_on && pass->buttons_pressed & (1u << BTN_OCT_DOWN)) {   /* OCT- moves when it is let go: held, it takes the bar away */
        ui.oct_down_waits = 1;
        ui.oct_down_ms = fm1_ms;
    }
    if (ui.oct_down_waits && pass->buttons_released & (1u << BTN_OCT_DOWN)) {
        ui.oct_down_waits = 0;
        if (ui.shown_bar > 0u) ui.shown_bar--;
    }
    if (ui.oct_down_waits && ui.edit_on && pass->page < TRACK_COUNT && fm1_ms - ui.oct_down_ms >= HOLD_MS) {
        ui.oct_down_waits = 0;
        pattern_remove_bar(pass->pattern, pass->page, ui.shown_bar);
        ui.roll_drawn = 0;
        pass->song_changed = 1;
    }
    if (!ui.recording_on && pass->buttons_pressed & (1u << BTN_OCT_UP)) {
        if (ui.edit_on && pass->page < TRACK_COUNT && ui.shown_bar + 1u == pass->pattern->bar_totals[pass->page] && pass->pattern->bar_totals[pass->page] < BAR_COUNT) {
            pass->pattern->bar_totals[pass->page]++;   /* EDIT on a track's page: past its last bar is a new one */
            pass->song_changed = 1;
        }
        ui.shown_bar++;
    }
    pass->bar_total = pass->page < TRACK_COUNT ? pass->pattern->bar_totals[pass->page] : pattern_bars(pass->pattern);
    if (ui.shown_bar >= pass->bar_total) ui.shown_bar = pass->bar_total - 1u;   /* the screen never shows a bar that does not play */
}

/* EDIT on DRUMS or a track's notes: which step keys were tapped, which one is held */
static void step_keys(pass_t *pass)
{
    uint32_t step;
    if (!pass->keys_edit || !(pass->page == PAGE_DRUMS || pass->notes_page))
        ui.step_key_waiting = NO_STEP;          /* (EDIT went off, another page: it does nothing) */
    for (step = 0; pass->keys_edit && (pass->page == PAGE_DRUMS || pass->notes_page) && step < STEPS_PER_BAR; step++)
        if (pass->keys_pressed >> STEP_KEYS[step] & 1u) {   /* a step key does its job when it is let go: held, it accents its step */
            if (ui.step_key_waiting != NO_STEP) pass->tapped_steps |= 1u << ui.step_key_waiting;   /* (another one went down: that one was a tap) */
            ui.step_key_waiting = step;
            ui.step_key_ms = fm1_ms;
        }
    if (ui.step_key_waiting != NO_STEP && !(pass->keys >> STEP_KEYS[ui.step_key_waiting] & 1u)) {
        pass->tapped_steps |= 1u << ui.step_key_waiting;
        ui.step_key_waiting = NO_STEP;
    } else if (ui.step_key_waiting != NO_STEP && fm1_ms - ui.step_key_ms >= HOLD_MS) {
        pass->held_step = ui.step_key_waiting;
        ui.step_key_waiting = NO_STEP;
    }
}

/* GLO: the song as it was before the last change comes back; GLO again: the change is back (the two swap). A change is
 * what the song went through in one go: changes less than UNDO_GAP_MS apart are one. Not while a preview, a groove or
 * a progression plays (the song on the screen is not the song's own then) */
#define UNDO_GAP_MS 1000u
static song_t undo_song;                        /* the song before the last change (after GLO: the change) */
static song_t undo_before;                      /* the song as the last change left it */

static void undo_button(pass_t *pass)
{
    if (pass->song_changed && !suggestion_plays()) {
        if (!ui.undo_ready || fm1_ms - ui.undo_change_ms > UNDO_GAP_MS) {   /* a new change: what was before it comes back */
            memcpy(&undo_song, &undo_before, sizeof undo_song);
            ui.undo_ready = 1;
        }
        ui.undo_change_ms = fm1_ms;
        memcpy(&undo_before, (const void *)&song, sizeof undo_before);
    } else if (pass->buttons_pressed & (1u << BTN_GLO) && ui.undo_ready && !ui.preview_on && !suggestion_plays()) {
        volatile uint8_t *const now = (volatile uint8_t *)&song;   /* (byte by byte: what plays is always one song or the other's) */
        uint8_t *const undone = (uint8_t *)&undo_song;
        uint32_t index;
        for (index = 0; index < sizeof undo_song; index++) {
            const uint8_t byte = now[index];
            now[index] = undone[index];
            undone[index] = byte;
        }
        memcpy(&undo_before, (const void *)&song, sizeof undo_before);
        ui.undo_change_ms = fm1_ms - UNDO_GAP_MS - 1u;   /* (the next change is a new one) */
        ui.unsaved = 1;
        ui.changed_ms = fm1_ms;
        ui.page_drawn = ~0u;                    /* (everything on the screen may have changed) */
    }
}

/* what waits to be saved goes to flash, only when stopped and silent (an erase stalls the sound) */
static void save_when_quiet(void)
{
    if (ui.place_to_save < PLACE_COUNT && !playing && sound_quiet()) {   /* (as below: only when stopped and silent) */
        if (!blob_save(ui.place_to_save - PLACE_FIRST_KEPT, (const void *)&song, sizeof song)) {
            ui.places_with_song |= 1u << ui.place_to_save;
            memcpy(ui.place_names[ui.place_to_save - PLACE_FIRST_KEPT], (const void *)song.name, NAME_LENGTH);
            if (ui.songs_note == SONGS_NOTE_SAVING) ui.songs_note = SONGS_NOTE_SAVED;
        }
        ui.place_to_save = PLACE_COUNT;
    }
    if (ui.user_sounds_unsaved && !playing && sound_quiet()) {   /* (a slot: four sectors erased, ~200 ms) */
        ui.user_sounds_unsaved = 0;
        slot_save(USER_SOUNDS_SLOT, user_sounds, sizeof user_sounds);
    }
    if (ui.library_unsaved && !playing && sound_quiet()) {   /* (a slot: four sectors erased, ~200 ms) */
        ui.library_unsaved = 0;
        if (!slot_save(0, &library, sizeof library) && ui.library_note == LIBRARY_NOTE_SAVING) ui.library_note = LIBRARY_NOTE_STORED;
    }
    if (ui.unsaved && !playing && sound_quiet() && fm1_ms - ui.changed_ms > 2000u && !suggestion_plays()) {   /* an erase stalls the sound: only when stopped and silent (and not a groove or progression that only plays) */
        settings_save((const void *)&song, sizeof song);
        ui.unsaved = 0;
    }
}

static void app_main(void)                      /* what was kept comes back, then the main loop, a pass at a time */
{
    uint32_t place;
    {                                           /* the song as it was kept: as it is now, or in an older size */
        uint32_t kept = 0;
        while (kept < PATTERN_SIZES_COUNT && settings_load((void *)&song, SONG_SIZE_KEPT(PATTERN_SIZES[kept]))) kept++;
        if (kept == PATTERN_SIZES_COUNT) first_song();
        else if (kept) song_grow((song_t *)&song, PATTERN_SIZES[kept]);
    }
    if (!song_valid(&song))
        first_song();
    for (place = PLACE_FIRST_KEPT; place < PLACE_COUNT; place++)
        if (place_kept_valid(place)) {
            ui.places_with_song |= 1u << place;
            memcpy(ui.place_names[place - PLACE_FIRST_KEPT], place_song.name, NAME_LENGTH);
        }
    library_load();
    user_sounds_load();
    for (place = 0; place < LIBRARY_PLACES; place++)
        if (!pattern_empty(&library.patterns[place])) ui.places_with_pattern |= 1u << place;
    frames_per_step_q8 = FRAMES_PER_STEP_Q8(song.tempo);
    chain_on = song_has_chain();                /* a song with a chain starts with the chain on */
    draw_splash();
    ui.splash_start_ms = fm1_ms;
    memcpy(&undo_before, (const void *)&song, sizeof undo_before);

    for (;;) {
        pass_t pass_now, *pass = &pass_now;
        memset(pass, 0, sizeof *pass);
        pass->held_step = NO_STEP;
        pass->shown_length = LENGTH_WHOLE_FIRST;
        platform_poll();
        read_panel(pass);
        follow_clock();
        play_button(pass);
        choose_page(pass);
        preview_end(pass);
        groove_end(pass);
        progression_end(pass);
        knob_pages(pass);
        preview_follow(pass);
        rec_button(pass);
        loop_pattern = ui.shown_pattern;        /* (it plays when the chain does not) */
        pass->pattern = &song.patterns[ui.shown_pattern];
        oct_buttons(pass);
        step_keys(pass);
        page_input(pass);
        undo_button(pass);
        light_keys(pass);
        if (splash(pass)) continue;
        light_up(pass);
        draw_screen(pass);
        draw_knobs(pass);
        save_when_quiet();
    }
}
