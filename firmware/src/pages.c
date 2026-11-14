/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's pages, what each does with the knobs, the keys, SEQ and REC: the library's, the songs', a track's sound and
 * mix pages (knob_pages), then JUICY, NAMES, DRUMS and a track's notes (page_input). They work on the main loop's
 * state (ui) and its pass (pass_t); juicy.c includes this after them. */

/* the library's page (it asks for no EDIT either): a place (KNOB1 or a lower key), SEQ fetches its pattern, REC stores one */
static void library_input(pass_t *pass)
{
    uint32_t step;
    for (step = 0; step < LIBRARY_PLACES; step++)
        if (pass->keys_pressed >> STEP_KEYS[step] & 1u) pass->detents[0] = (int32_t)step - (int32_t)ui.selected_library_place;
    if (pass->detents[0]) {
        ui.selected_library_place = turned_value(ui.selected_library_place, pass->detents[0], 0, LIBRARY_PLACES - 1u);
        ui.library_note = LIBRARY_NOTE_NONE;
    }
    if (pass->buttons_pressed & (1u << BTN_SEQ) && ui.places_with_pattern >> ui.selected_library_place & 1u) {
        if (!pattern_empty(&song.patterns[ui.shown_pattern]) && ui.library_note != LIBRARY_NOTE_FETCH_ASKED)
            ui.library_note = LIBRARY_NOTE_FETCH_ASKED;
        else {
            memcpy((void *)&song.patterns[ui.shown_pattern], &library.patterns[ui.selected_library_place], sizeof(pattern_t));
            ui.library_note = LIBRARY_NOTE_FETCHED;
            pass->song_changed = 1;
        }
    }
    if (pass->buttons_pressed & (1u << BTN_REC)) {
        if (playing) ui.library_note = LIBRARY_NOTE_STOP_FIRST;
        else if (ui.places_with_pattern >> ui.selected_library_place & 1u && ui.library_note != LIBRARY_NOTE_STORE_ASKED)
            ui.library_note = LIBRARY_NOTE_STORE_ASKED;
        else {                                  /* (an empty pattern stored: the place is empty again) */
            memcpy(&library.patterns[ui.selected_library_place], (const void *)&song.patterns[ui.shown_pattern], sizeof(pattern_t));
            if (pattern_empty(&library.patterns[ui.selected_library_place])) ui.places_with_pattern &= ~(1u << ui.selected_library_place);
            else ui.places_with_pattern |= 1u << ui.selected_library_place;
            ui.library_unsaved = 1;
            ui.library_note = LIBRARY_NOTE_SAVING;
        }
    }
}

/* the songs' page (no EDIT): a place (KNOB1), on NEW the style it starts from (KNOB2); SEQ loads its song, REC saves
 * the song there (each asks twice where that replaces something) */
static void songs_input(pass_t *pass)
{
    if (pass->detents[0]) {
        ui.selected_place = turned_value(ui.selected_place, pass->detents[0], 0, PLACE_COUNT - 1u);
        ui.songs_note = SONGS_NOTE_NONE;
    }
    if (pass->detents[1] && ui.selected_place == PLACE_NEW) {
        ui.new_style = turned_value(ui.new_style, pass->detents[1], 0, STYLE_COUNT - 1u);
        ui.songs_note = SONGS_NOTE_NONE;
        if (ui.preview_on) ui.preview_place = PLACE_COUNT;   /* (listening: the new style plays instead) */
    }
    if (pass->buttons_pressed & (1u << BTN_SEQ) && ui.places_with_song >> ui.selected_place & 1u) {
        if (playing) ui.songs_note = SONGS_NOTE_STOP_FIRST;
        else if (ui.songs_note != SONGS_NOTE_LOAD_ASKED) ui.songs_note = SONGS_NOTE_LOAD_ASKED;
        else if (!place_load(ui.selected_place, ui.new_style)) {   /* the song is another one: home, where its chain shows */
            chain_on = song_has_chain();
            ui.shown_pattern = ui.shown_bar = 0;
            ui.songs_shown = 0;
            ui.global_shown = 1;
            pass->page = PAGE_GLOBAL;
            pass->song_changed = 1;
        }
    }
    if (pass->buttons_pressed & (1u << BTN_REC) && ui.selected_place >= PLACE_FIRST_KEPT) {
        if (playing) ui.songs_note = SONGS_NOTE_STOP_FIRST;
        else if (ui.places_with_song >> ui.selected_place & 1u && ui.songs_note != SONGS_NOTE_SAVE_ASKED) ui.songs_note = SONGS_NOTE_SAVE_ASKED;
        else {
            ui.place_to_save = ui.selected_place;
            ui.songs_note = SONGS_NOTE_SAVING;
        }
    }
}

/* a track's sound page (EDIT): the knobs shape the sound, ALGORITHM picks one, SEQ / REC keep it as USER1 / USER2 */
static void sound_input(pass_t *pass)
{
    const uint32_t sound_melodic = pass->page - MELODIC_FIRST_PAGE;
    uint32_t knob, user_place;
    if (ui.edit_on && pass->algorithm_detents) {
        sound_apply(sound_melodic, turned_value(song.sound_choices[sound_melodic], pass->algorithm_detents, 0, sound_count(sound_melodic) - 1u));
        ui.sound_note = SOUND_NOTE_NONE;
        pass->song_changed = 1;
    }
    for (knob = 0; knob < KNOB_COUNT; knob++)
        if (pass->detents[knob]) {
            pass->song_changed |= turn_setting(&song.sounds[sound_melodic][knob], pass->detents[knob], 0, sound_knob_max(sound_melodic, knob));
            ui.sound_note = SOUND_NOTE_NONE;
            pass->detents[knob] = 0;
        }
    for (user_place = 0; user_place < USER_SOUNDS; user_place++)
        if (ui.edit_on && pass->buttons_pressed & (1u << (user_place ? BTN_REC : BTN_SEQ))) {
            if (ui.sound_note != SOUND_NOTE_KEEP_ASKED + user_place) ui.sound_note = SOUND_NOTE_KEEP_ASKED + user_place;
            else {
                sound_of_track(sound_melodic, &user_sounds[sound_melodic][user_place]);
                song.sound_choices[sound_melodic] = (uint8_t)(PRESETS[sound_melodic].count + user_place);
                ui.user_sounds_unsaved = 1;
                pass->song_changed = 1;
                ui.sound_note = SOUND_NOTE_KEPT + user_place;
            }
        }
}

/* a track's mix page (EDIT): LEVEL (the drums': the selected drum's), PAN, REVERB, ECHO */
static void mix_input(pass_t *pass)
{
    const uint32_t mix_melodic = pass->page - MELODIC_FIRST_PAGE;
    pass->song_changed |= turn_setting(pass->page == PAGE_DRUMS ? &song.drum_levels[ui.selected_drum] : &song.levels[mix_melodic], pass->detents[0], 0, MIX_MAX);
    pass->song_changed |= turn_setting(&song.pans[pass->page], pass->detents[1], 0, MIX_MAX);
    pass->song_changed |= turn_setting(&song.reverb_sends[pass->page], pass->detents[2], 0, MIX_MAX);
    pass->song_changed |= turn_setting(pass->page == PAGE_DRUMS ? &song.drum_echo : &song.echo[mix_melodic], pass->detents[3], 0, ECHO_SEND_MAX);
}

/* the songs', the library's, a sound and a mix page take the knobs (no other page sees them then); elsewhere nothing
 * changes without EDIT: the knobs, SEQ and REC wait for it. SEQ (EDIT) elsewhere: the chain on or off */
static void knobs_taken(pass_t *pass)           /* (the page's own handler had them: no other part of the pass sees them) */
{
    uint32_t knob;
    for (knob = 0; knob < KNOB_COUNT; knob++) pass->detents[knob] = 0;
}

static void knob_pages(pass_t *pass)
{
    if (pass->page != PAGE_SONGS) ui.songs_note = SONGS_NOTE_NONE;
    if (pass->page != PAGE_LIBRARY || pass->presets_detents) ui.library_note = LIBRARY_NOTE_NONE;
    if (pass->page == PAGE_LIBRARY) {
        library_input(pass);
        knobs_taken(pass);
    } else if (pass->page == PAGE_SONGS) {
        songs_input(pass);
        knobs_taken(pass);
    } else {
        if (!ui.edit_on) knobs_taken(pass);
        if (pass->sound_page) sound_input(pass);
        else {
            if (pass->mix_page) {
                mix_input(pass);
                knobs_taken(pass);
            }
            if (ui.edit_on && pass->buttons_pressed & (1u << BTN_SEQ)) chain_on = !chain_on;
        }
    }
}

/* JUICY (EDIT): TEMPO, REVERB, the SWING of the pattern on the screen, the song's KEY; the lower keys are the chain's places */
static void global_input(pass_t *pass)
{
    uint32_t step;
    if (!ui.external_clock) pass->song_changed |= turn_setting(&song.tempo, pass->detents[0], TEMPO_MIN, TEMPO_MAX);
    pass->song_changed |= turn_setting(&song.reverb, pass->detents[1], 0, REVERB_MAX);
    pass->song_changed |= turn_setting(&song.patterns[ui.shown_pattern].swing, pass->detents[2], 0, SWING_MAX);
    pass->song_changed |= turn_setting(&song.key, pass->detents[3], KEY_OFF, SONG_KEY_MAX);
    for (step = 0; step < CHAIN_SLOTS; step++) {
        if (pass->keys_edit && pass->keys_pressed >> STEP_KEYS[step] & 1u) {   /* the pattern on the screen goes there; it is taken away if it was there */
            song.chain[step] = (uint8_t)(song.chain[step] == ui.shown_pattern + 1u ? 0u : ui.shown_pattern + 1u);
            ui.chain_drawn = 0;
            pass->song_changed = 1;
        }
        if (song.chain[step]) pass->step_leds |= 1u << step;
    }
}

/* NAMES: SELECT or a lower key moves the cursor; KNOB1 (EDIT): the character there */
static void naming_input(pass_t *pass)
{
    uint32_t step;
    ui.naming_cursor = turned_value(ui.naming_cursor, pass->select_detents, 0, 2u * NAME_LENGTH - 1u);
    for (step = 0; step < 2u * NAME_LENGTH; step++)
        if (!ui.select_on && pass->keys_pressed >> STEP_KEYS[step] & 1u) ui.naming_cursor = step;
    pass->song_changed |= turn_setting(ui.naming_cursor < NAME_LENGTH ? &song.name[ui.naming_cursor] : &pass->pattern->name[ui.naming_cursor - NAME_LENGTH],
                                       pass->detents[0], 0, NAME_CHARACTER_COUNT - 1u);
}

/* DRUMS: SELECT or a played key picks the drum. EDIT: a step key tapped puts the drum's hit there or takes it away;
 * held, the step's accent (every drum on it) goes on or off, and on an empty step the drum goes there, accented.
 * KNOB1 (EDIT): a groove plays in place of the drums (REC keeps it; meanwhile the step keys wait) */
static void drums_input(pass_t *pass)
{
    volatile uint16_t *hits;
    uint32_t step, drum, groove = turned_value(ui.groove_shown, pass->detents[0], GROOVE_OWN, GROOVE_COUNT - 1u);
    if (groove != ui.groove_shown) {
        if (ui.groove_shown == GROOVE_OWN) {
            drums_keep(pass->pattern, &ui.groove_kept);
            ui.groove_pattern = ui.shown_pattern;
        } else drums_put_back(pass->pattern, &ui.groove_kept);
        if (groove != GROOVE_OWN) groove_put(pass->pattern, groove, ui.shown_bar);
        ui.groove_shown = groove;
        ui.groove_asked = 0;
    }
    ui.selected_drum = turned_value(ui.selected_drum, pass->select_detents, 0, DRUM_COUNT - 1u);
    for (drum = 0; drum < DRUM_COUNT; drum++)   /* played: the lower keys from the left are the drums, and the one hit is the one EDIT changes */
        if (pass->keys_play && pass->keys_pressed >> STEP_KEYS[drum] & 1u) {
            ui.selected_drum = drum;
            drum_auditions[drum]++;
        }
    hits = &pass->pattern->drum_hits[ui.selected_drum][ui.shown_bar];
    if (ui.groove_shown != GROOVE_OWN) {     /* (a groove that only plays: nothing to edit yet) */
        pass->tapped_steps = 0;
        pass->held_step = NO_STEP;
    }
    for (step = 0; step < STEPS_PER_BAR; step++)
        if (pass->tapped_steps >> step & 1u) {
            *hits ^= (uint16_t)(1u << step);
            pass->song_changed = 1;
        }
    if (pass->held_step != NO_STEP) {
        if (!(*hits >> pass->held_step & 1u)) {
            *hits |= (uint16_t)(1u << pass->held_step);
            pass->pattern->accents[0][ui.shown_bar] |= (uint16_t)(1u << pass->held_step);
        } else
            pass->pattern->accents[0][ui.shown_bar] ^= (uint16_t)(1u << pass->held_step);
        pass->song_changed = 1;
    }
    pass->step_leds = *hits;
}

typedef struct {                                /* a track's page, while a pass works on it */
    uint32_t note_lowest, note_highest;         /* the track's notes (MELODIC) */
    uint32_t cursor_start;                      /* EDIT: where the note under the cursor starts, STEPS_PER_BAR: none */
    uint32_t audition_value;                    /* a note to hear (as in song.notes), 0: none */
    volatile uint8_t *bar_notes, *bar_gates;    /* the bar on the screen's */
} track_edit_t;

static void track_cursor(pass_t *pass, track_edit_t *edit)   /* SELECT: the cursor, on through the track's bars (the screen goes along) */
{
    const uint32_t melodic = pass->melodic;
    uint32_t position, start;
    if (!pass->notes_page || !pass->select_detents) return;
    position = turned_value(ui.shown_bar * STEPS_PER_BAR + ui.cursor_steps[melodic], pass->select_detents, 0, pass->bar_total * STEPS_PER_BAR - 1u);
    ui.shown_bar = position / STEPS_PER_BAR;
    ui.cursor_steps[melodic] = position % STEPS_PER_BAR;
    start = note_start(pass->pattern->notes[melodic][ui.shown_bar], melodic, ui.cursor_steps[melodic]);
    if (!playing && start < STEPS_PER_BAR && melodic != MELODIC_ARP)   /* stopped: the note it comes to sounds (an ARP hit has none of its own) */
        edit->audition_value = step_value(pass->pattern->notes[melodic][ui.shown_bar], pass->pattern->gates[melodic][ui.shown_bar], melodic, start)
                               & (melodic == MELODIC_PAD ? ~0u : NOTE_MASK);
}

static uint32_t chord_choice_turned(uint32_t chord, int32_t detents)   /* the next chord's type turned: AUTO (with a key) before MIN */
{
    const uint32_t lowest = song_key() != KEY_OFF ? 0u : 1u;   /* (choices: 0 AUTO, 1 + type) */
    uint32_t choice = chord == PAD_CHORD_AUTO ? 0u : chord + 1u;
    if (!detents) return chord;
    if (choice < lowest) choice = lowest;       /* (AUTO without a key: as MIN) */
    choice = turned_value(choice, detents, lowest, PAD_CHORD_COUNT);
    return choice ? choice - 1u : PAD_CHORD_AUTO;
}

/* the knobs: CUTOFF, DECAY; ARP's MODE. EDIT, the cursor on a note: they show it and change it (and the next note is
 * like it); else they set the next note */
static void track_knobs(pass_t *pass, track_edit_t *edit)
{
    const uint32_t melodic = pass->melodic;
    pass->song_changed |= turn_setting(&song.cutoff[melodic], pass->detents[1], 0, SYNTH_PARAMETER_MAX);
    if (melodic != MELODIC_PAD) pass->song_changed |= turn_setting(&song.decay[melodic], pass->detents[2], 0, SYNTH_PARAMETER_MAX);
    if (melodic == MELODIC_ARP) {               /* KNOB1: the pattern's MODE (ARP's steps have no notes to turn) */
        pass->song_changed |= turn_setting(&pass->pattern->arp_mode, pass->detents[0], 0, ARP_MODE_MAX);
        pass->detents[0] = 0;
    }
    edit->cursor_start = pass->notes_page && ui.edit_on && !ui.recording_on ? note_start(edit->bar_notes, melodic, ui.cursor_steps[melodic]) : STEPS_PER_BAR;
    if (edit->cursor_start < STEPS_PER_BAR) {
        const uint32_t start = edit->cursor_start, value = step_value(edit->bar_notes, edit->bar_gates, melodic, start);
        pass->shown_note = melodic == MELODIC_ARP ? ui.selected_notes[melodic] : turned_value(value & NOTE_MASK, pass->detents[0], edit->note_lowest, edit->note_highest);
        pass->shown_chord = melodic == MELODIC_PAD ? turned_value(value >> CHORD_SHIFT, pass->detents[2], 0, PAD_CHORD_COUNT - 1u) : 0u;
        if (melodic == MELODIC_PAD && pass->detents[0] && !pass->detents[2] && ui.selected_chord == PAD_CHORD_AUTO && song_key() != KEY_OFF)
            pass->shown_chord = pad_chord_for(pass->shown_note, PAD_CHORD_AUTO);   /* AUTO: a new root, the type that fits the key */
        pass->shown_length = note_gate(edit->bar_gates, start) ? note_gate(edit->bar_gates, start) : GATE_TENTHS_MAX + note_steps(edit->bar_notes, melodic, start);
        pass->shown_length = melodic == MELODIC_PAD ? LENGTH_WHOLE_FIRST : turned_value(pass->shown_length, pass->detents[3], 1, GATE_TENTHS_MAX + STEPS_PER_BAR - start);
        if (pass->detents[0] || (melodic == MELODIC_PAD ? pass->detents[2] : pass->detents[3])) {
            put_note(edit->bar_notes, edit->bar_gates, melodic, start, melodic == MELODIC_ARP ? ARP_HIT : CHORD(pass->shown_note, pass->shown_chord), pass->shown_length);
            ui.selected_notes[melodic] = pass->shown_note;
            if (melodic == MELODIC_PAD && (pass->detents[2] || ui.selected_chord != PAD_CHORD_AUTO || song_key() == KEY_OFF))
                ui.selected_chord = pass->shown_chord;   /* (AUTO stays AUTO while only the root moves) */
            else ui.selected_lengths[melodic] = pass->shown_length;
            if (pass->detents[0] || melodic == MELODIC_PAD) edit->audition_value = CHORD(pass->shown_note, pass->shown_chord);
            ui.roll_drawn = 0;
            pass->song_changed = 1;
        }
    } else {
        ui.selected_notes[melodic] = turned_value(ui.selected_notes[melodic], pass->detents[0], edit->note_lowest, edit->note_highest);
        if (melodic == MELODIC_PAD) ui.selected_chord = chord_choice_turned(ui.selected_chord, pass->detents[2]);
        else ui.selected_lengths[melodic] = turned_value(ui.selected_lengths[melodic], pass->detents[3], 1, LENGTH_MAX);
        if (pass->detents[0] || (melodic == MELODIC_PAD && pass->detents[2]))
            edit->audition_value = ui.selected_notes[melodic] | (melodic == MELODIC_PAD ? pad_chord_for(ui.selected_notes[melodic], ui.selected_chord) << CHORD_SHIFT : 0u);
    }
}

/* recording while playing (only into the pattern on the screen, while it is the one that plays): a key's note sounds at
 * once and lands on the step nearest to when it was played (a swung step: to when it sounds), as long as the key is
 * held (under a step: in tenths of one; the pad's chord: until the next). While the key is down the note grows a step
 * ahead of the song, so the steps it covers do not cut it; the bar as it was keeps what it grows over */
static void live_note_put(uint32_t length)      /* the note's bar as it was before it, the note over it */
{
    volatile pattern_t *const pattern = &song.patterns[ui.live_pattern];
    const uint32_t melodic = ui.live_melodic, bar = ui.live_start / STEPS_PER_BAR, step = ui.live_start % STEPS_PER_BAR;
    memcpy((void *)pattern->notes[melodic][bar], ui.live_bar_notes, sizeof ui.live_bar_notes);
    memcpy((void *)pattern->gates[melodic][bar], ui.live_bar_gates, sizeof ui.live_bar_gates);
    pattern->accents[MELODIC_FIRST_PAGE + melodic][bar] = (uint16_t)(ui.live_bar_accents & ~(1u << step));   /* (not accented) */
    put_note(pattern->notes[melodic][bar], pattern->gates[melodic][bar], melodic, step, ui.live_value, length);
    ui.live_length = length;
    ui.roll_drawn = 0;
}

static uint32_t live_held_q8(void)              /* how long the key has been down, from its step's sound on, in 1/256 of a step */
{
    const uint32_t held_ms = fm1_ms - ui.live_ms > 10000u ? 10000u : fm1_ms - ui.live_ms;   /* (longer than any bar: as long) */
    const int32_t held_q8 = (int32_t)(held_ms * 65536u / (frames_per_step_q8 * 1000u / FS)) + ui.live_offset_q8;
    return held_q8 < 0 ? 0u : (uint32_t)held_q8;
}

static void live_note_start(pass_t *pass, uint32_t key, uint32_t value)
{
    const uint32_t melodic = pass->melodic, late_q8 = pass->pattern->swing * 256u / 50u;   /* (a swung step sounds that late) */
    uint32_t step, started_ms, since_ms, since_q8, bar;
    do {                                        /* (both of the same step: the ISR moves them) */
        step = playing_step;
        started_ms = step_started_ms;
    } while (step != playing_step);
    since_ms = (int32_t)(fm1_ms - started_ms) < 0 ? 0u : fm1_ms - started_ms > 1000u ? 1000u : fm1_ms - started_ms;
    since_q8 = since_ms * 65536u / (frames_per_step_q8 * 1000u / FS);
    if (since_q8 > 255u) since_q8 = 255u;
    ui.live_offset_q8 = (int32_t)since_q8 - (int32_t)(step & 1u ? late_q8 : 0u);
    if ((int32_t)(256u + (step & 1u ? 0u : late_q8)) - (int32_t)since_q8 < (ui.live_offset_q8 < 0 ? -ui.live_offset_q8 : ui.live_offset_q8)) {
        ui.live_offset_q8 = (int32_t)since_q8 - (int32_t)(256u + (step & 1u ? 0u : late_q8));   /* nearer the next step's sound: played a bit early */
        step++;
    }
    ui.live_start = step % (pass->bar_total * STEPS_PER_BAR);
    bar = ui.live_start / STEPS_PER_BAR;
    ui.live_key = key;
    ui.live_melodic = melodic;
    ui.live_pattern = ui.shown_pattern;
    ui.live_value = value;
    ui.live_ms = fm1_ms;
    memcpy(ui.live_bar_notes, (const void *)pass->pattern->notes[melodic][bar], sizeof ui.live_bar_notes);
    memcpy(ui.live_bar_gates, (const void *)pass->pattern->gates[melodic][bar], sizeof ui.live_bar_gates);
    ui.live_bar_accents = pass->pattern->accents[MELODIC_FIRST_PAGE + melodic][bar];
    live_starts[melodic] = 1u + ui.live_start;
    live_note_put(melodic == MELODIC_PAD ? LENGTH_WHOLE_FIRST : GATE_TENTHS_MAX + 2u);   /* (its step and the next) */
    pass->song_changed = 1;
}

/* each pass: the note recorded while playing grows while its key is down; let go (another key, the recording or the
 * song stopped, another page or pattern), it is as long as it was held */
static void live_note_follow(pass_t *pass)
{
    const uint32_t melodic = ui.live_melodic;
    if (ui.live_key == KEY_COUNT) return;
    if (pass->keys >> ui.live_key & 1u && !pass->keys_pressed && ui.recording_on && playing
        && pass->page == MELODIC_FIRST_PAGE + melodic && ui.shown_pattern == ui.live_pattern) {
        const uint32_t grown = GATE_TENTHS_MAX + live_held_q8() / 256u + 2u;   /* (through the step after the one that plays) */
        if (melodic != MELODIC_PAD && grown > ui.live_length && grown <= GATE_TENTHS_MAX + STEPS_PER_BAR - ui.live_start % STEPS_PER_BAR) {
            live_note_put(grown);
            pass->song_changed = 1;
        }
        return;
    }
    if (melodic != MELODIC_PAD) {
        const uint32_t held_q8 = live_held_q8(), tenths = (held_q8 * 10u + 128u) / 256u;
        const uint32_t length = tenths < 10u ? (tenths ? tenths : 1u) : GATE_TENTHS_MAX + (held_q8 + 128u) / 256u;
        live_note_put(length < ui.live_length ? length : ui.live_length);
        pass->song_changed = 1;
    }
    live_starts[melodic] = 0;
    ui.live_key = KEY_COUNT;
}

/* played: a key's note sounds while it is down, and is the one EDIT places. Recording: the note goes to the cursor,
 * which moves on past it (the pad's: a bar on), round the track; while playing: where the song is (above) */
static void track_keys(pass_t *pass, track_edit_t *edit)
{
    const uint32_t melodic = pass->melodic, first_key_note = keyboard_first_note(melodic, ui.keyboard_octaves[melodic]);
    uint32_t key;
    for (key = 0; key < KEY_COUNT; key++) {
        const uint32_t key_note = first_key_note + key;
        uint32_t placed_value, length, position;
        if (!((pass->keys_play || ui.recording_on) && pass->keys_pressed >> key & 1u) || key_note < edit->note_lowest || key_note > edit->note_highest) continue;
        ui.selected_notes[melodic] = key_note;
        ui.held_key = key;
        edit->audition_value = key_note | (melodic == MELODIC_PAD ? pad_chord_for(key_note, ui.selected_chord) << CHORD_SHIFT : 0u);
        placed_value = melodic == MELODIC_ARP ? ARP_HIT : edit->audition_value;   /* (ARP: the key sounds, a hit is placed) */
        if (!ui.recording_on) continue;
        if (playing) {
            if (playing_pattern == ui.shown_pattern) live_note_start(pass, key, placed_value);
            continue;
        }
        length =melodic == MELODIC_PAD ? LENGTH_WHOLE_FIRST : ui.selected_lengths[melodic];
        put_note(edit->bar_notes, edit->bar_gates, melodic, ui.cursor_steps[melodic], placed_value, length);
        pass->pattern->accents[pass->page][ui.shown_bar] &= (uint16_t)~(1u << ui.cursor_steps[melodic]);   /* (not accented) */
        position = (ui.shown_bar * STEPS_PER_BAR + ui.cursor_steps[melodic] + (melodic == MELODIC_PAD ? STEPS_PER_BAR : length_steps(length))) % (pass->bar_total * STEPS_PER_BAR);
        ui.shown_bar = position / STEPS_PER_BAR;
        ui.cursor_steps[melodic] = position % STEPS_PER_BAR;
        edit->bar_notes = pass->pattern->notes[melodic][ui.shown_bar];
        edit->bar_gates = pass->pattern->gates[melodic][ui.shown_bar];
        ui.roll_drawn = 0;
        pass->song_changed = 1;
    }
}

/* EDIT, the step keys: tapped, a note on the step (or over it) goes; held, its accent goes on or off (the pad has none:
 * held is a tap). On an empty step the next note goes there (held: accented) */
static void track_steps(pass_t *pass, track_edit_t *edit)
{
    const uint32_t melodic = pass->melodic;
    const uint32_t placed_value = melodic == MELODIC_ARP ? ARP_HIT : ui.selected_notes[melodic] | (melodic == MELODIC_PAD ? pad_chord_for(ui.selected_notes[melodic], ui.selected_chord) << CHORD_SHIFT : 0u);
    volatile uint16_t *bar_accents = &pass->pattern->accents[pass->page][ui.shown_bar];
    uint32_t step;
    if (pass->held_step != NO_STEP && melodic == MELODIC_PAD) {
        pass->tapped_steps |= 1u << pass->held_step;
        pass->held_step = NO_STEP;
    }
    for (step = 0; step < STEPS_PER_BAR; step++) {
        if (pass->tapped_steps >> step & 1u || step == pass->held_step) {
            const uint32_t start = note_start(edit->bar_notes, melodic, step);
            if (start < STEPS_PER_BAR && step == pass->held_step) *bar_accents ^= (uint16_t)(1u << start);
            else if (start < STEPS_PER_BAR) {
                remove_note(edit->bar_notes, edit->bar_gates, melodic, start);
                *bar_accents &= (uint16_t)~(1u << start);
            } else {
                put_note(edit->bar_notes, edit->bar_gates, melodic, step, placed_value, melodic == MELODIC_PAD ? LENGTH_WHOLE_FIRST : ui.selected_lengths[melodic]);
                *bar_accents = (uint16_t)(step == pass->held_step ? *bar_accents | 1u << step : *bar_accents & ~(1u << step));
                if (melodic != MELODIC_ARP) edit->audition_value = placed_value;   /* (an ARP hit, as a drum's: silent) */
            }
            ui.cursor_steps[melodic] = step;
            ui.roll_drawn = 0;
            pass->song_changed = 1;
        }
        if (edit->bar_notes[step]) pass->step_leds |= 1u << step;
    }
}

/* the roll follows the note shown, two rows ahead of it while there are notes that way; the pad's roll keeps the
 * whole chord above it in view */
static void roll_follow(pass_t *pass, const track_edit_t *edit)
{
    const uint32_t melodic = pass->melodic, shown_note = pass->shown_note;
    const uint32_t shown_type = pad_chord_for(shown_note, pass->shown_chord);   /* (AUTO: the type it is for that note) */
    const uint32_t chord_rows = melodic == MELODIC_PAD ? PAD_CHORDS[shown_type].intervals[PAD_CHORDS[shown_type].note_count - 1u] : 0u;
    const uint32_t rows_above = chord_rows > ROLL_MARGIN_ROWS ? chord_rows : ROLL_MARGIN_ROWS;
    uint32_t lowest = ui.roll_lowest_notes[melodic];
    if (shown_note < lowest + ROLL_MARGIN_ROWS)
        lowest = shown_note < edit->note_lowest + ROLL_MARGIN_ROWS ? edit->note_lowest : shown_note - ROLL_MARGIN_ROWS;
    if (shown_note + rows_above >= lowest + ROLL_ROWS)
        lowest = (shown_note + rows_above > edit->note_highest + chord_rows ? edit->note_highest + chord_rows : shown_note + rows_above) - ROLL_ROWS + 1u;
    ui.roll_lowest_notes[melodic] = lowest;
}

/* EDIT: a suggestion plays in place of the track's notes: PAD's KNOB4 a progression (stopped: its first chord sounds),
 * BASS's ALGORITHM a bass line, ARP's a rhythm (its name over the roll a moment). REC keeps it; meanwhile NOTE (ARP:
 * MODE), CHORD or LENGTH, the step keys and recording wait */
static void suggestion_input(pass_t *pass, track_edit_t *edit)
{
    const uint32_t melodic = pass->melodic;
    const int32_t detents = melodic == MELODIC_PAD ? pass->detents[3] : pass->algorithm_detents;
    const uint32_t shown = ui.suggestion_shown != SUGGESTION_OWN && ui.suggestion_melodic == melodic ? ui.suggestion_shown : SUGGESTION_OWN;
    const uint32_t suggestion = turned_value(shown, detents, SUGGESTION_OWN, suggestion_count(melodic) - 1u);
    if (!ui.edit_on || suggestion_count(melodic) < 2u) return;
    if (suggestion != shown) {
        if (shown == SUGGESTION_OWN) {
            track_keep(pass->pattern, melodic, &ui.suggestion_kept);
            ui.suggestion_pattern = ui.shown_pattern;
            ui.suggestion_melodic = melodic;
        } else track_put_back(pass->pattern, melodic, &ui.suggestion_kept);
        if (suggestion != SUGGESTION_OWN) suggestion_put(pass->pattern, melodic, suggestion);
        if (suggestion != SUGGESTION_OWN && !playing && melodic == MELODIC_PAD)
            edit->audition_value = step_value(pass->pattern->notes[MELODIC_PAD][0], pass->pattern->gates[MELODIC_PAD][0], MELODIC_PAD, 0);
        ui.suggestion_shown = suggestion;
        ui.suggestion_asked = 0;
        ui.roll_drawn = 0;
    }
    if (detents && melodic != MELODIC_PAD) overlay_show(melodic == MELODIC_BASS ? "BASS LINE" : "RHYTHM", suggestion_name(melodic, suggestion));
    if (ui.suggestion_shown != SUGGESTION_OWN) {
        pass->detents[0] = 0;
        pass->detents[melodic == MELODIC_PAD ? 2 : 3] = 0;
        pass->tapped_steps = 0;
        pass->held_step = NO_STEP;
        ui.recording_on = 0;
    }
}

/* BASS, ARP, PAD, LEAD (their notes page, and on a sound or mix page what still works there: the keys played): the
 * cursor, the knobs, the keys played, the step keys, the roll */
static void track_input(pass_t *pass)
{
    track_edit_t edit;
    pass->melodic = pass->page - MELODIC_FIRST_PAGE;
    edit.note_lowest = MELODIC[pass->melodic].note_lowest;
    edit.note_highest = MELODIC[pass->melodic].note_highest;
    edit.audition_value = 0;
    track_cursor(pass, &edit);
    if (pass->notes_page) suggestion_input(pass, &edit);
    if (ui.recording_on && playing && playing_pattern == ui.shown_pattern)   /* recording while playing: the screen shows the bar that plays */
        ui.shown_bar = playing_step / STEPS_PER_BAR % pass->bar_total;
    edit.bar_notes = pass->pattern->notes[pass->melodic][ui.shown_bar];
    edit.bar_gates = pass->pattern->gates[pass->melodic][ui.shown_bar];
    track_knobs(pass, &edit);
    track_keys(pass, &edit);
    track_steps(pass, &edit);
    if (edit.cursor_start >= STEPS_PER_BAR) {   /* (no note under the cursor: the knobs show the next note) */
        pass->shown_note = ui.selected_notes[pass->melodic];
        pass->shown_chord = pass->melodic == MELODIC_PAD ? ui.selected_chord : 0u;
        pass->shown_length = ui.selected_lengths[pass->melodic];
    }
    if (edit.audition_value) {
        audition_notes[pass->melodic] = edit.audition_value;
        note_auditions[pass->melodic]++;
    }
    roll_follow(pass, &edit);
}

/* the page's own knobs and keys; then a note played by hand sounds while its key is down, and a changed song is kept
 * (once nothing sounds) */
static void page_input(pass_t *pass)
{
    uint32_t track;
    live_note_follow(pass);
    if (pass->page == PAGE_GLOBAL) global_input(pass);
    else if (pass->page == PAGE_NAMING) naming_input(pass);
    else if (pass->page == PAGE_DRUMS) drums_input(pass);
    else if (pass->page < TRACK_COUNT) track_input(pass);
    for (track = 0; track < MELODIC_COUNT; track++)
        note_held[track] = (pass->keys_play || ui.recording_on) && pass->page == MELODIC_FIRST_PAGE + track && (pass->keys >> ui.held_key & 1u);
    if (pass->song_changed) {
        ui.unsaved = 1;
        ui.changed_ms = fm1_ms;
    }
}
