/* SPDX-License-Identifier: GPL-3.0-only */
/* What the panel shows from the main loop's state (ui) and its pass (pass_t): the keys' and the buttons' lights (the
 * start screen's light first), the screen (only what changed, with screen.c's draw_ functions), the knobs' places.
 * juicy.c includes this after them. */

/* the pad's chord a track's notes go with: with EDIT (or stopped) the one at the cursor, playing the one that plays */
static uint32_t chord_at_hand(const pass_t *pass)
{
    if (playing && !ui.edit_on)
        return pad_chord_at(&song.patterns[playing_pattern], pass->step_now / STEPS_PER_BAR, pass->step_now % STEPS_PER_BAR);
    return pad_chord_at(pass->pattern, ui.shown_bar, ui.cursor_steps[pass->melodic]);
}

/* the keys: one that does something now glows, one that is on or selected is lit */
static void light_keys(pass_t *pass)
{
    uint32_t step, drum, track, key;
    pass->step_now = playing_step;              /* (read once: the ISR moves it) */
    pass->marked_bar = playing && playing_pattern == ui.shown_pattern ? pass->step_now / STEPS_PER_BAR % pass->bar_total : NO_BAR;   /* (a short track goes round) */
    pass->marked_step = pass->marked_bar == ui.shown_bar && pass->page != PAGE_GLOBAL ? pass->step_now % STEPS_PER_BAR : NO_STEP;
    pass->marked_slot = playing ? playing_slot : CHAIN_SLOTS;
    /* the keys: one that does something now glows, one that is on or selected is lit */
    if (ui.select_on) {                         /* the track keys, the shown track's lit */
        for (track = 0; track < TRACK_COUNT; track++) {   /* and the mute keys, lit where the track plays */
            pass->keys_glowing |= 1u << TRACK_KEYS[track] | 1u << MUTE_KEYS[track];
            if (!(muted_tracks >> track & 1u)) pass->keys_lit |= 1u << MUTE_KEYS[track];
        }
        if (!ui.global_shown) pass->keys_lit |= 1u << TRACK_KEYS[ui.shown_track];
    } else if (pass->page == PAGE_NAMING) {     /* the lower keys are the names' places, the song's then the pattern's: the cursor's lit */
        for (step = 0; step < 2u * NAME_LENGTH; step++) pass->keys_glowing |= 1u << STEP_KEYS[step];
        pass->keys_lit = 1u << STEP_KEYS[ui.naming_cursor];
    } else if (pass->page == PAGE_SONGS) {      /* no key does anything */
    } else if (pass->page == PAGE_LIBRARY) {    /* the lower keys are the places, lit where there is a pattern */
        for (step = 0; step < LIBRARY_PLACES; step++) {
            pass->keys_glowing |= 1u << STEP_KEYS[step];
            if (ui.places_with_pattern >> step & 1u) pass->keys_lit |= 1u << STEP_KEYS[step];
        }
    } else if (pass->keys_edit) {               /* the lower keys, lit where there is something; what runs (the step, the chain's place) flips its key, the cursor blinks */
        for (step = 0; step < STEPS_PER_BAR; step++) {
            if (pass->page < TRACK_COUNT && step % 4u == 0u) pass->keys_glowing_bright |= 1u << STEP_KEYS[step];   /* a beat */
            else pass->keys_glowing |= 1u << STEP_KEYS[step];
            if ((pass->step_leds >> step & 1u) ^ (step == (pass->page == PAGE_GLOBAL ? pass->marked_slot : pass->marked_step))
                ^ (pass->notes_page && step == ui.cursor_steps[pass->melodic] && (fm1_ms / CURSOR_BLINK_MS & 1u)))
                pass->keys_lit |= 1u << STEP_KEYS[step];
        }
    } else if (pass->page == PAGE_GLOBAL) {     /* playing: where the song is, the step on the lower keys, the bar on OP1 .. OP4 */
        if (playing) {
            const uint32_t bars_playing = pattern_bars(&song.patterns[playing_pattern]);
            for (track = 0; track < bars_playing; track++)
                pass->keys_glowing |= 1u << TRACK_KEYS[track];
            pass->keys_lit = 1u << STEP_KEYS[pass->step_now % STEPS_PER_BAR] | 1u << TRACK_KEYS[pass->step_now / STEPS_PER_BAR % bars_playing];
        }
    } else if (pass->page == PAGE_DRUMS) {      /* a key per drum, the selected one's lit */
        for (drum = 0; drum < DRUM_COUNT; drum++)
            pass->keys_glowing |= 1u << STEP_KEYS[drum];
        pass->keys_lit = 1u << STEP_KEYS[ui.selected_drum];
    } else if (pass->page != PAGE_GLOBAL) {     /* the keys with a note of the track, the shown note's key lit. With a key: */
        const uint32_t first_note = keyboard_first_note(pass->melodic, ui.keyboard_octaves[pass->melodic]), song_key_now = song_key();
        const uint32_t chord_now = song_key_now != KEY_OFF && pass->melodic != MELODIC_PAD ? chord_notes(chord_at_hand(pass)) : 0u;
        for (key = 0; key < KEY_COUNT; key++) { /* the chord's notes lit (not the pad's: its keys are roots), the key's brighter */
            const uint32_t note = first_note + key;
            if (note < MELODIC[pass->melodic].note_lowest || note > MELODIC[pass->melodic].note_highest) continue;
            if (chord_now >> note % 12u & 1u) pass->keys_lit |= 1u << key;
            else if (song_key_now != KEY_OFF && note_in_key(song_key_now, note)) pass->keys_glowing_bright |= 1u << key;
            else pass->keys_glowing |= 1u << key;
        }
        if (pass->shown_note >= first_note && pass->shown_note < first_note + KEY_COUNT) {
            const uint32_t shown = 1u << (pass->shown_note - first_note);
            if (song_key_now != KEY_OFF) {      /* (it blinks, so it stands out among the lit) */
                pass->keys_lit &= ~shown;
                pass->keys_glowing &= ~shown;
                pass->keys_glowing_bright &= ~shown;
            }
            if (song_key_now == KEY_OFF || fm1_ms / CURSOR_BLINK_MS & 1u) pass->keys_lit |= shown;
        }
    }
}

/* the start screen's light (1: it is still there, nothing else lights up) */
static int splash(pass_t *pass)
{
    uint32_t knob, step, key;
    if (ui.splash_shown) {                      /* the start screen: the light runs over the lower keys, the rest is dark
                                               (before the page's lights below: set and cleared again, they would flash) */
        int touched = pass->buttons_pressed || pass->keys_pressed || pass->presets_detents || pass->algorithm_detents;
        for (knob = 0; knob < KNOB_COUNT; knob++) touched |= pass->detents[knob] != 0;
        for (key = 0; key <= BTN_REC; key++) {  /* (REC: the highest button id) */
            fm1_led_key(key, 0);
            fm1_led_key_glow(key, 0);
        }
        if (!touched && fm1_ms - ui.splash_start_ms < SPLASH_MS) {
            const uint32_t light = (fm1_ms - ui.splash_start_ms) * (STEPS_PER_BAR + SPLASH_TAIL_KEYS) / SPLASH_MS;   /* the lower key it is on, 0 the leftmost */
            for (key = 0; key < KEY_COUNT; key++) {
                uint32_t behind = ~0u;          /* (keys ahead of it, and the upper keys: a huge number, dark) */
                for (step = 0; step < STEPS_PER_BAR; step++)
                    if (STEP_KEYS[step] == key) behind = light - step;
                fm1_led_key(KEY_LED(key), behind == 0u);
                fm1_led_key_glow_bright(KEY_LED(key), behind >= 1u && behind <= SPLASH_TAIL_KEYS / 2u);
                fm1_led_key_glow(KEY_LED(key), behind > SPLASH_TAIL_KEYS / 2u && behind <= SPLASH_TAIL_KEYS);
            }
            return 1;
        }
        ui.splash_shown = 0;                    /* over: the page from scratch, the buttons that always do something glow */
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.page_drawn = ~0u;
        fm1_led_key_glow(BTN_PLAY, 1);
        fm1_led_key_glow(BTN_HOME, 1);
        fm1_led_key_glow(BTN_SEL, 1);
        fm1_led_key_glow(BTN_EDIT, 1);
        fm1_led_key_glow(BTN_SAVE, 1);
    }
    return 0;
}

/* the keys' and the buttons' lights */
static void light_up(pass_t *pass)
{
    uint32_t key;
    for (key = 0; key < KEY_COUNT; key++) {
        fm1_led_key_glow(KEY_LED(key), pass->keys_glowing >> key & 1u);
        fm1_led_key_glow_bright(KEY_LED(key), pass->keys_glowing_bright >> key & 1u);
        fm1_led_key(KEY_LED(key), pass->keys_lit >> key & 1u);
    }
    fm1_led_key(BTN_PLAY, playing && pass->step_now % 4u == 0u);   /* playing: lit on each beat's first step, the beat */
    fm1_led_key(BTN_HOME, pass->page == PAGE_GLOBAL || pass->page == PAGE_NAMING);
    fm1_led_key(BTN_SAVE, pass->page == PAGE_SONGS || pass->page == PAGE_LIBRARY);
    fm1_led_key_glow(BTN_FX, pass->page < TRACK_COUNT || pass->page == PAGE_GLOBAL);   /* a track's (JUICY's) other pages are there */
    fm1_led_key(BTN_FX, pass->sound_page || pass->mix_page || pass->page == PAGE_NAMING);
    fm1_led_key(BTN_SEQ, chain_on != 0);
    fm1_led_key_glow(BTN_SEQ, pass->page == PAGE_SONGS ? ui.places_with_song >> ui.selected_place & 1u
                              : pass->page == PAGE_LIBRARY ? ui.places_with_pattern >> ui.selected_library_place & 1u : ui.edit_on != 0);
    fm1_led_key(BTN_SEL, ui.select_on != 0);
    fm1_led_key(BTN_EDIT, ui.edit_on != 0);
    fm1_led_key_glow(BTN_OCT_DOWN, ui.recording_on ? ui.keyboard_octaves[pass->melodic] > 0u : ui.shown_bar > 0u);   /* there is a bar (recording: an octave) that way */
    fm1_led_key_glow(BTN_OCT_UP, ui.recording_on ? ui.keyboard_octaves[pass->melodic] + 1u < KEYBOARD_OCTAVES
                                 : ui.shown_bar + 1u < pass->bar_total || (ui.edit_on && pass->page < TRACK_COUNT && pass->bar_total < BAR_COUNT));
    fm1_led_key(BTN_OCT_DOWN, ui.recording_on && ui.keyboard_octaves[pass->melodic] < 1u);   /* recording: the keys are an octave that way */
    fm1_led_key(BTN_OCT_UP, ui.recording_on && ui.keyboard_octaves[pass->melodic] > 1u);
    fm1_led_key(BTN_REC, ui.recording_on || ui.groove_shown != GROOVE_OWN);   /* (a groove that plays: REC keeps it) */
    fm1_led_key_glow(BTN_REC, pass->page == PAGE_SONGS ? ui.selected_place >= PLACE_FIRST_KEPT : pass->page == PAGE_LIBRARY || ((pass->page == PAGE_GLOBAL || (pass->page < TRACK_COUNT && !pass->mix_page)) && ui.edit_on));
}

/* the screen: only what changed */
static void draw_screen(pass_t *pass)
{
    uint32_t state;
    uint32_t knob, drum;
    /* the screen: only what changed. Each *_drawn holds what is on the screen (+ 1, so 0 means "draw it") */
    if (pass->page + (pass->sound_page ? 16u : 0u) + (pass->mix_page ? 32u : 0u) != ui.page_drawn) {   /* (a sound or mix page is a page of its own) */
        ui.page_drawn = pass->page + (pass->sound_page ? 16u : 0u) + (pass->mix_page ? 32u : 0u);
        for (knob = 0; knob < DRUM_COUNT; knob++) ui.mix_drawn[knob] = 0;
        lcd_fill(0, BODY_TOP, 240, BODY_HEIGHT, C_BLACK);
        ui.header_drawn = ui.roll_drawn = ui.roll_marker_drawn = ui.chain_drawn = ui.songs_drawn = ui.library_drawn = ui.sound_drawn = ui.naming_drawn = 0;
        for (drum = 0; drum < DRUMS_PER_PAGE; drum++) ui.drum_row_drawn[drum] = 0;
        ui.drum_pages_drawn = 0;
        for (knob = 0; knob < KNOB_COUNT; knob++) ui.knob_drawn[knob] = 0;
    }
    state = 1u + (ui.shown_bar | pass->bar_total << 4 | pass->marked_bar << 8 | ui.shown_pattern << 12 | ui.edit_on << 16 | muted_tracks << 17);
    if (state != ui.header_drawn) {
        ui.header_drawn = state;
        draw_header(pass->page, ui.shown_pattern, pass->bar_total, ui.shown_bar, pass->marked_bar, ui.edit_on != 0, pass->page < TRACK_COUNT && muted_tracks >> pass->page & 1u);
        ui.chord_name_drawn = 0;                /* (the header covered it) */
    }
    if (pass->notes_page && (pass->melodic == MELODIC_BASS || pass->melodic == MELODIC_LEAD) && song_key() != KEY_OFF) {
        const uint32_t chord = chord_at_hand(pass);   /* (with a key) the chord the notes go with, after the page's name */
        if (1u + chord != ui.chord_name_drawn) {
            ui.chord_name_drawn = 1u + chord;
            draw_chord_name(pass->page, chord);
        }
    }
    if (pass->page == PAGE_GLOBAL) {
        state = 1u + (ui.shown_pattern | pass->marked_slot << 4 | (uint32_t)(chain_on != 0) << 12);   /* (a changed place sets chain_drawn to 0) */
        if (state != ui.chain_drawn) {
            ui.chain_drawn = state;
            draw_chain(ui.shown_pattern, pass->marked_slot, chain_on != 0);
        }
    } else if (pass->page == PAGE_DRUMS && !pass->mix_page) {
        uint32_t row;                           /* the page of the selected drum */
        const uint32_t accents = pass->pattern->accents[0][ui.shown_bar];
        if (1u + accents != ui.drum_accents_drawn) {   /* (they do not fit in a row's state) */
            ui.drum_accents_drawn = 1u + accents;
            for (row = 0; row < DRUMS_PER_PAGE; row++) ui.drum_row_drawn[row] = 0;
        }
        for (row = 0; row < DRUMS_PER_PAGE; row++) {
            uint32_t row_drum = ui.selected_drum / DRUMS_PER_PAGE * DRUMS_PER_PAGE + row;
            uint32_t hits = pass->pattern->drum_hits[row_drum][ui.shown_bar];
            state = 1u + (hits | pass->marked_step << 16 | (uint32_t)(row_drum == ui.selected_drum) << 24 | row_drum << 25);
            if (state != ui.drum_row_drawn[row]) {
                ui.drum_row_drawn[row] = state;
                draw_drum_row(row_drum, row, hits, accents, row_drum == ui.selected_drum, pass->marked_step);
            }
        }
    } else if (pass->page == PAGE_NAMING) {
        uint32_t place;
        state = 1u + (ui.naming_cursor | ui.shown_pattern << 4);
        for (place = 0; place < NAME_LENGTH; place++) state = state * 31u + song.name[place] * 64u + pass->pattern->name[place];
        if (state != ui.naming_drawn) {
            ui.naming_drawn = state;
            draw_naming(pass->pattern->name, ui.shown_pattern, ui.naming_cursor);
        }
    } else if (pass->page == PAGE_SONGS) {
        state = 1u + (ui.selected_place | ui.songs_note << 4 | ui.places_with_song << 8 | ui.preview_on << 20);
        if (state != ui.songs_drawn) {
            ui.songs_drawn = state;
            draw_songs(ui.selected_place, ui.places_with_song, ui.place_names, ui.songs_note, ui.preview_on != 0);
        }
    } else if (pass->page == PAGE_LIBRARY) {
        state = 1u + (ui.selected_library_place | ui.library_note << 4 | ui.shown_pattern << 8 | ui.places_with_pattern << 12 | ui.preview_on << 28);
        if (state != ui.library_drawn) {
            ui.library_drawn = state;
            draw_library(ui.selected_library_place, ui.places_with_pattern, ui.shown_pattern, ui.library_note, ui.preview_on != 0);
        }
    } else if (pass->mix_page) {
        static const char *const MIX_NAMES[4] = {"LEVEL", "PAN", "REVERB", "ECHO"};
        const uint32_t rows = pass->page == PAGE_DRUMS ? DRUM_COUNT : 4u, row_melodic = pass->page - MELODIC_FIRST_PAGE;
        uint32_t row;
        for (row = 0; row < rows; row++) {
            const uint32_t value = pass->page == PAGE_DRUMS ? song.drum_levels[row]
                                 : row == 0u ? song.levels[row_melodic] : row == 1u ? song.pans[pass->page]
                                 : row == 2u ? song.reverb_sends[pass->page] : song.echo[row_melodic];
            const int selected = pass->page == PAGE_DRUMS && row == ui.selected_drum;
            state = 1u + (value | (uint32_t)selected << 8);
            if (state != ui.mix_drawn[row]) {
                ui.mix_drawn[row] = state;
                draw_mix_row(row, pass->page == PAGE_DRUMS ? DRUM_NAMES[row] : MIX_NAMES[row], value, pass->page != PAGE_DRUMS && row == 1u, selected);
            }
        }
    } else if (pass->sound_page) {
        state = 1u + (song.sound_choices[pass->melodic] | (uint32_t)sound_edited(pass->melodic) << 4 | ui.sound_note << 5);
        if (state != ui.sound_drawn) {
            ui.sound_drawn = state;
            draw_sound(pass->melodic, sound_edited(pass->melodic), ui.sound_note);
        }
    } else {
        state = 1u + (ui.shown_bar | pass->shown_note << 4 | ui.roll_lowest_notes[pass->melodic] << 12 | ui.shown_pattern << 20 | ui.cursor_steps[pass->melodic] << 23);   /* (a changed step sets roll_drawn to 0) */
        if (state != ui.roll_drawn) {
            ui.roll_drawn = state;
            draw_roll(pass->pattern, pass->melodic, ui.shown_bar, pass->shown_note, ui.roll_lowest_notes[pass->melodic], ui.cursor_steps[pass->melodic]);
        }
        if (1u + pass->marked_step != ui.roll_marker_drawn) {
            ui.roll_marker_drawn = 1u + pass->marked_step;
            draw_roll_marker(pass->marked_step);
        }
    }
}

typedef struct {                                /* what a knob's place shows on the page */
    const char *label, *text;                   /* (a label "": the place is empty) */
    uint32_t value;                             /* what it stands for: the place is drawn again when it changes */
    int big;                                    /* numbers and notes are big; a word (too wide for that) is small */
    char number[8];                             /* (the text, when it is made here) */
} knob_view_t;

static void knob_view(const pass_t *pass, uint32_t knob, knob_view_t *view)
{
    static const char *const MELODIC_LABELS[KNOB_COUNT] = {"NOTE", "CUTOFF", "DECAY", "LENGTH"};
    static const char *const PAD_LABELS[KNOB_COUNT] = {"NOTE", "CUTOFF", "CHORD", ""};
    static const char *const ARP_LABELS[KNOB_COUNT] = {"MODE", "CUTOFF", "DECAY", "LENGTH"};
    static const char *const MIX_LABELS[KNOB_COUNT] = {"LEVEL", "PAN", "REVERB", "ECHO"};
    static const char *const GLOBAL_LABELS[KNOB_COUNT] = {"TEMPO", "REVERB", "SWING", "KEY"};
    static const char *const SONGS_LABELS[KNOB_COUNT] = {"SONG", "", "", ""};
    static const char *const LIBRARY_LABELS[KNOB_COUNT] = {"PLACE", "", "", ""};
    static const char *const NAMING_LABELS[KNOB_COUNT] = {"LETTER", "", "", ""};
    static const char *const SYNTH_SOUND_LABELS[KNOB_COUNT] = {"WAVE", "RESO", "ATTACK", "ENV"};
    static const char *const PAD_SOUND_LABELS[KNOB_COUNT] = {"SPREAD", "RESO", "ATTACK", "RELEASE"};
    const uint32_t page = pass->page, melodic = pass->melodic;
    const int pad_shown = page == MELODIC_FIRST_PAGE + MELODIC_PAD, arp_shown = page == MELODIC_FIRST_PAGE + MELODIC_ARP;
    view->text = view->number;
    view->big = 1;
    if (pass->mix_page) {                       /* LEVEL (DRUMS: the selected drum's), PAN (L50 .. C .. R50), REVERB, ECHO */
        const uint32_t values[KNOB_COUNT] = {page == PAGE_DRUMS ? song.drum_levels[ui.selected_drum] : song.levels[melodic],
                                             song.pans[page], song.reverb_sends[page], page == PAGE_DRUMS ? song.drum_echo : song.echo[melodic]};
        view->label = MIX_LABELS[knob];
        view->value = values[knob];
        if (knob == 1u && view->value == MIX_MAX / 2u) view->text = "C";
        else if (knob == 1u) {
            view->number[0] = view->value < MIX_MAX / 2u ? 'L' : 'R';
            format_number(view->number + 1, view->value < MIX_MAX / 2u ? MIX_MAX / 2u - view->value : view->value - MIX_MAX / 2u);
        } else format_number(view->number, view->value);
    } else if (pass->sound_page) {              /* the sound's knobs; WAVE a word */
        view->label = pad_shown ? PAD_SOUND_LABELS[knob] : SYNTH_SOUND_LABELS[knob];
        view->value = song.sounds[melodic][knob];
        if (!pad_shown && knob == 0u) {
            view->text = WAVE_NAMES[view->value];
            view->big = 0;
        } else format_number(view->number, view->value);
    } else if (page == PAGE_SONGS) {            /* the place: a demo's name small, a kept one's number big */
        view->label = SONGS_LABELS[knob];
        view->value = knob ? 0u : ui.selected_place;
        view->text = PLACE_NAMES[view->value];
        view->big = view->value >= PLACE_FIRST_KEPT;
    } else if (page == PAGE_LIBRARY) {
        view->label = LIBRARY_LABELS[knob];
        view->value = knob ? 0u : ui.selected_library_place + 1u;
        format_number(view->number, view->value);
    } else if (page == PAGE_NAMING) {           /* the character at the cursor (a space: the word, small) */
        view->label = NAMING_LABELS[knob];
        view->value = knob ? 0u : ui.naming_cursor < NAME_LENGTH ? song.name[ui.naming_cursor] : pass->pattern->name[ui.naming_cursor - NAME_LENGTH];
        view->number[0] = NAME_CHARACTERS[view->value];
        view->number[1] = 0;
        if (!view->value) view->text = "SPACE";
        view->big = view->value != 0u;
    } else if (page == PAGE_GLOBAL) {           /* TEMPO (EXT: the clock's), REVERB, SWING in % (50 straight), KEY */
        const uint32_t values[KNOB_COUNT] = {ui.external_clock ? ui.external_bpm : song.tempo, song.reverb, 50u + song.patterns[ui.shown_pattern].swing, song_key()};
        view->label = knob == 0u && ui.external_clock ? "EXT" : GLOBAL_LABELS[knob];
        view->value = values[knob];
        if (knob == 3u) {                       /* KEY: a word, small */
            format_key(view->number, view->value);
            view->big = 0;
        } else format_number(view->number, view->value);
    } else if (page == PAGE_DRUMS) {            /* GROOVE (a word), its usual BPM or REC's question; the selected drum's row is orange: that says it */
        view->label = knob == 0u ? "GROOVE" : knob == 3u && ui.groove_asked ? "REC" : knob == 3u && GROOVES[ui.groove_shown].tempo ? "BPM" : "";
        view->value = knob == 0u ? ui.groove_shown : knob == 3u ? (ui.groove_asked ? 1000u : GROOVES[ui.groove_shown].tempo) : 0u;
        view->big = knob == 3u && !ui.groove_asked;
        if (knob == 0u) view->text = GROOVES[ui.groove_shown].name;
        else if (knob == 3u && ui.groove_asked) view->text = "AGAIN";
        else format_number(view->number, view->value);
    } else {                                    /* a track's notes: NOTE (ARP: MODE), CUTOFF, DECAY (PAD: CHORD), LENGTH */
        const uint32_t values[KNOB_COUNT] = {arp_shown ? pass->pattern->arp_mode : pass->shown_note, song.cutoff[melodic],
                                             pad_shown ? pass->shown_chord : song.decay[melodic], pass->shown_length};
        view->label = pad_shown ? PAD_LABELS[knob] : arp_shown ? ARP_LABELS[knob] : MELODIC_LABELS[knob];
        view->value = values[knob];
        if (arp_shown && knob == 0u) {
            view->text = ARP_MODE_NAMES[view->value];
            view->big = 0;
        } else if (knob == 0u) format_note(view->number, view->value);
        else if (pad_shown && knob == 2u) {
            view->text = CHORD_NAMES[view->value == PAD_CHORD_AUTO && song_key() == KEY_OFF ? PAD_CHORD_MINOR : view->value];   /* (AUTO without a key: MIN) */
            view->big = 0;
        } else if (knob == 3u && view->value < LENGTH_WHOLE_FIRST) {   /* LENGTH: 10 % .. 90 % of a step, then whole steps */
            view->number[0] = (char)('0' + view->value);
            view->number[1] = '0';
            view->number[2] = '%';
            view->number[3] = 0;
        } else if (knob == 3u) format_number(view->number, length_steps(view->value));
        else format_number(view->number, view->value);
    }
    if (!view->label[0]) view->text = "";
}

/* the four knobs' places, and the drums' page marks over them */
static void draw_knobs(pass_t *pass)
{
    uint32_t knob;
    for (knob = 0; knob < KNOB_COUNT; knob++) {
        knob_view_t view;
        knob_view(pass, knob, &view);
        if (1u + view.value != ui.knob_drawn[knob]) {
            ui.knob_drawn[knob] = 1u + view.value;
            draw_knob(knob, view.label, view.text, view.big);
        }
    }
    if (pass->page == PAGE_DRUMS && !pass->mix_page && 1u + ui.selected_drum / DRUMS_PER_PAGE != ui.drum_pages_drawn) {   /* (after the knobs: over their empty places) */
        ui.drum_pages_drawn = 1u + ui.selected_drum / DRUMS_PER_PAGE;
        draw_drum_pages(ui.selected_drum / DRUMS_PER_PAGE);
    }
}
