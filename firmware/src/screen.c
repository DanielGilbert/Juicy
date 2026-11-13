/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's screen: the colours, the header, the body of each page, the four knobs' places. Each draw_ function
 * draws its part whole; the main loop calls one when what it shows has changed. juicy.c includes it after the
 * sequencer. */

/* ---- the screen: the header (the page's name, the bars), the body (the steps), the four knobs' places */
#define C_ACCENT RGB(255, 140, 0)               /* orange: what is selected */
#define C_LABEL RGB(130, 130, 130)
#define C_HIT RGB(200, 200, 200)                /* a hit or a note that is not selected; a bar that plays */
#define C_STEP_ODD_BEAT RGB(60, 60, 64)         /* empty steps: the beats alternate, so four steps read as one */
#define C_STEP_EVEN_BEAT RGB(40, 40, 44)        /* also: a bar that does not play */
#define C_ROLL_ODD_BEAT RGB(26, 26, 28)         /* a piano roll's background, the same beats, darker */
#define C_ROLL_EVEN_BEAT RGB(14, 14, 16)
#define HEADER_HEIGHT 40u
#define BODY_TOP 50u
#define BODY_HEIGHT 138u
#define DRUM_ROW_HEIGHT 46u
#define ROLL_ROWS 12u                           /* a piano roll shows an octave of notes at a time */
#define ROLL_ROW_HEIGHT 11u
#define ROLL_MARGIN_ROWS 2u
#define ACCENT_BAND_HEIGHT 3                    /* an accented hit or note: its top this many px white */
#define ROLL_HEIGHT (ROLL_ROWS * ROLL_ROW_HEIGHT)
#define KNOB_COUNT 4u
#define CHAIN_HEIGHT 120u
#define NO_STEP STEPS_PER_BAR
#define NO_BAR BAR_COUNT

/* a page per track, and the one behind HOME. A melodic track's page is MELODIC_FIRST_PAGE + its number */
enum { PAGE_DRUMS, MELODIC_FIRST_PAGE, TRACK_COUNT = MELODIC_FIRST_PAGE + MELODIC_COUNT, PAGE_GLOBAL = TRACK_COUNT, PAGE_SONGS, PAGE_LIBRARY, PAGE_NAMING };
_Static_assert(sizeof ((pattern_t *)0)->bar_totals == TRACK_COUNT, "a track's page is its place in bar_totals");

static const char *const PAGE_NAMES[] = {"DRUMS", "BASS", "ARP", "PAD", "LEAD", "JUICY", "SONGS", "LIBRARY", "NAMES"};
static const char *const DRUM_NAMES[DRUM_COUNT] = {"KICK", "HAT", "CLAP", "OPEN HAT", "SNARE", "TOM"};
#define DRUMS_PER_PAGE 3u                       /* the drums' page shows three at a time */
#define DRUM_PAGES (DRUM_COUNT / DRUMS_PER_PAGE)
static const char *const CHORD_NAMES[PAD_CHORD_COUNT] = {"MIN", "MAJ", "MIN7", "MAJ7", "DOM7", "SUS2", "SUS4", "DIM", "POWER", "ROOT"};
static const uint8_t STEP_KEYS[STEPS_PER_BAR] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* the lower keys, left to right */
static const uint8_t TRACK_KEYS[TRACK_COUNT] = {1, 3, 5, 8, 10};   /* the upper keys printed OP1 .. OP5 */
static const uint8_t MUTE_KEYS[TRACK_COUNT] = {13, 15, 17, 20, 22};   /* (with SEL) the same five an octave up */
#define KEY_COUNT 27u
#define HOLD_MS 600u                            /* a button held this long: its other job */
#define KEYBOARD_OCTAVES 3u                     /* where the keys are on a track: an octave down, as they start, an octave up */
#define CURSOR_BLINK_MS 250u                    /* with EDIT the cursor's key blinks: this long on, this long off */
#define KEYS_FIRST_NOTE_ABOVE_LOWEST 5u         /* a track's lowest note is a C, the leftmost key an F: the keys play F .. G, 27 notes */

static int32_t step_x(uint32_t step) { return (int32_t)(4u + step * 14u + step / 4u * 2u); }   /* a step is 12 px wide */

static void format_number(char *text, uint32_t value)   /* 0..999 */
{
    if (value >= 100u) *text++ = (char)('0' + value / 100u);
    if (value >= 10u) *text++ = (char)('0' + value / 10u % 10u);
    *text++ = (char)('0' + value % 10u);
    *text = 0;
}

static void format_note(char *text, uint32_t note)      /* MIDI 12..127: "C1", "F#2" */
{
    static const char *const NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const char *name = NAMES[note % 12u];
    while (*name) *text++ = *name++;
    *text++ = (char)('0' + note / 12u - 1u);
    *text = 0;
}

static void pattern_name(char *text, uint32_t pattern_index)   /* "A" .. "H" */
{
    text[0] = (char)('A' + pattern_index);
    text[1] = 0;
}

/* a half orange seen at an angle, 32 x 32: the round of its peel (darker to the lower left), and in front of it the
 * cut face, an ellipse turned 34 degrees: a rim of peel, the pith, the flesh in six segments */
#define ORANGE_SIZE 32
#define ORANGE_BODY_X 15                        /* the peel's round: centre and radius */
#define ORANGE_BODY_Y 17
#define ORANGE_BODY_RADIUS 14
#define ORANGE_FACE_X 18                        /* the face: centre, half its long and short axis */
#define ORANGE_FACE_Y 12
#define ORANGE_FACE_LONG 13
#define ORANGE_FACE_SHORT 9
#define ORANGE_TURN_COS 213                     /* Q8: the face turned 34 degrees */
#define ORANGE_TURN_SIN 143
#define C_ORANGE_PEEL C_ACCENT
#define C_ORANGE_SHADE RGB(200, 90, 0)
#define C_ORANGE_PITH RGB(255, 236, 200)
#define C_ORANGE_FLESH RGB(255, 172, 40)

static int orange_face_inside(int32_t along, int32_t across, int32_t long_half, int32_t short_half)   /* along, across in 1/16 px */
{
    return along * along * short_half * short_half + across * across * long_half * long_half
           <= (long_half * short_half * 16) * (long_half * short_half * 16) + long_half * short_half * 16 * 8;
}

static void draw_orange(int32_t left, int32_t top, int32_t scale)   /* scale: px a pixel */
{
    int32_t x, y;
    for (y = 0; y < ORANGE_SIZE; y++)
        for (x = 0; x < ORANGE_SIZE; x++) {
            const int32_t body_x = x - ORANGE_BODY_X, body_y = y - ORANGE_BODY_Y;
            const int32_t face_x = x - ORANGE_FACE_X, face_y = y - ORANGE_FACE_Y;
            const int32_t along = (face_x * ORANGE_TURN_COS + face_y * ORANGE_TURN_SIN) >> 4;
            const int32_t across = (face_y * ORANGE_TURN_COS - face_x * ORANGE_TURN_SIN) >> 4;
            uint16_t colour;
            if (orange_face_inside(along, across, ORANGE_FACE_LONG, ORANGE_FACE_SHORT)) {
                const int32_t round_along = along * ORANGE_FACE_SHORT, round_across = across * ORANGE_FACE_LONG;   /* the face as a circle */
                const int32_t line_width = (ORANGE_FACE_LONG + ORANGE_FACE_SHORT) * 4;   /* a pixel or so */
                if (!orange_face_inside(along, across, ORANGE_FACE_LONG - 1, ORANGE_FACE_SHORT - 1)) colour = C_ORANGE_PEEL;
                else if (!orange_face_inside(along, across, ORANGE_FACE_LONG - 2, ORANGE_FACE_SHORT - 2)) colour = C_ORANGE_PITH;
                else if ((round_across < 0 ? -round_across : round_across) <= line_width   /* six segments: diameters at 0, 60, 120 degrees */
                         || (4 * round_across - 7 * round_along < 0 ? 7 * round_along - 4 * round_across : 4 * round_across - 7 * round_along) <= 8 * line_width
                         || (4 * round_across + 7 * round_along < 0 ? -(4 * round_across + 7 * round_along) : 4 * round_across + 7 * round_along) <= 8 * line_width)
                    colour = C_ORANGE_PITH;
                else colour = C_ORANGE_FLESH;
            } else if (body_x * body_x + body_y * body_y <= ORANGE_BODY_RADIUS * ORANGE_BODY_RADIUS + ORANGE_BODY_RADIUS)
                colour = body_x - body_y < -12 ? C_ORANGE_SHADE : C_ORANGE_PEEL;
            else continue;
            cv_rect(left + x * scale, top + y * scale, scale, scale, colour);
        }
}

/* the start screen: the orange three times its size, the name under it, the version at the foot */
#define SPLASH_MS 2000u                         /* and the light's run over the keys */
#define SPLASH_ORANGE_SCALE 3
#define SPLASH_TAIL_KEYS 4u                     /* behind the light: two keys glowing brighter, two dimmer */

static void draw_splash(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(ORANGE_SIZE * SPLASH_ORANGE_SCALE, ORANGE_SIZE * SPLASH_ORANGE_SCALE, C_BLACK);
    draw_orange(0, 0, SPLASH_ORANGE_SCALE);
    cv_blit((240u - ORANGE_SIZE * SPLASH_ORANGE_SCALE) / 2u, 36);
    draw_text_box(0, 150, 240, &FONT_L, "JUICY", C_ACCENT, 1);
    draw_text_box(0, 210, 240, &FONT_S, APP_VER, C_LABEL, 1);
}

/* the name, and a box per bar of the track on the screen (HOME's page: of the pattern): orange with the pattern's letter the one on the screen,
 * dark those that do not play, a mark under the one that sounds. The name is orange while EDIT is on, dark while
 * the track is muted */
static void draw_header(uint32_t page, uint32_t pattern_index, uint32_t bar_total, uint32_t shown_bar, uint32_t marked_bar, int editing, int muted)
{
    uint32_t bar;
    char letter[2];
    pattern_name(letter, pattern_index);
    cv_begin(240, HEADER_HEIGHT, C_BLACK);
    if (page == PAGE_GLOBAL) draw_orange(2, (HEADER_HEIGHT - ORANGE_SIZE) / 2, 1);   /* JUICY's own: the orange before its name */
    cv_text(page == PAGE_GLOBAL ? ORANGE_SIZE + 8 : 4, 6, &FONT_L, PAGE_NAMES[page],
            editing ? C_ACCENT : muted ? C_STEP_ODD_BEAT : C_HIT);   /* orange: the keys change this page (EDIT); dark: muted */
    for (bar = 0; bar < BAR_COUNT; bar++) {
        int32_t x = (int32_t)(148u + bar * 22u);
        cv_rect(x, 10, 18, 18, bar == shown_bar ? C_ACCENT : bar < bar_total ? C_HIT : C_STEP_EVEN_BEAT);
        if (bar == shown_bar) cv_text(x + (18 - text_w(&FONT_S, letter)) / 2, 11, &FONT_S, letter, C_BLACK);
        if (bar == marked_bar) cv_rect(x, 32, 18, 3, C_WHITE);
    }
    cv_blit(0, 0);
}

/* the chain: a box per place with its pattern's letter, orange where it is the pattern on the screen, a mark under
 * the place that sounds */
static void draw_chain(uint32_t shown_pattern, uint32_t marked_slot, int on)
{
    char song_name[NAME_LENGTH + 1u];
    uint32_t slot;
    cv_begin(240, CHAIN_HEIGHT, C_BLACK);
    cv_text(4, 0, &FONT_S, on ? "CHAIN ON" : "CHAIN OFF", on ? C_ACCENT : C_LABEL);
    name_text(song_name, song.name);            /* the song's name, at the right */
    cv_text(236 - text_w(&FONT_S, song_name), 0, &FONT_S, song_name, C_WHITE);
    for (slot = 0; slot < CHAIN_SLOTS; slot++) {
        uint32_t entry = song.chain[slot];
        cv_rect(step_x(slot), 20, 12, 20, entry ? (entry - 1u == shown_pattern ? C_ACCENT : C_HIT)
                                        : slot / 4u % 2u ? C_STEP_EVEN_BEAT : C_STEP_ODD_BEAT);
        if (entry) {
            char letter[2];
            pattern_name(letter, entry - 1u);
            cv_text(step_x(slot) + (12 - text_w(&FONT_S, letter)) / 2, 22, &FONT_S, letter, C_BLACK);
        }
        if (slot == marked_slot) cv_rect(step_x(slot), 42, 12, 3, C_WHITE);
    }
    cv_text(4, 60, &FONT_S, "SEQ: CHAIN ON / OFF", C_LABEL);
    cv_text(4, 80, &FONT_S, "REC: COPY PATTERN", C_LABEL);
    cv_text(4, 100, &FONT_S, "HOLD REC: CLEAR PATTERN", C_LABEL);
    cv_blit(0, BODY_TOP);
}

/* what the songs' page says under its places */
enum { SONGS_NOTE_NONE, SONGS_NOTE_LOAD_ASKED, SONGS_NOTE_SAVE_ASKED, SONGS_NOTE_SAVING, SONGS_NOTE_SAVED, SONGS_NOTE_STOP_FIRST };

static const char *const PLACE_NAMES[PLACE_COUNT] = {"DEMO1", "DEMO2", "DEMO3", "NEW", "1", "2", "3", "4", "5", "6", "7", "8"};

/* the songs' places: a box each (D a demo song, N an empty one, 1..8 those kept), light where there is a song,
 * orange the selected one; what SEQ, REC and PLAY do with it (previewing: it plays), or what has just happened */
static void draw_songs(uint32_t selected_place, uint32_t places_with_song, const uint8_t place_names[PLACES_KEPT][NAME_LENGTH], uint32_t note, int previewing)
{
    static const char *const NOTES[] = {"", "SEQ AGAIN: LOAD IT", "REC AGAIN: SAVE OVER IT", "SAVING", "SAVED", "PRESS STOP FIRST"};
    const int has_song = places_with_song >> selected_place & 1u;
    const int named = has_song && selected_place >= PLACE_FIRST_KEPT && !name_empty(place_names[selected_place - PLACE_FIRST_KEPT]);
    char song_name[NAME_LENGTH + 1u];
    uint32_t place;
    cv_begin(240, CHAIN_HEIGHT, C_BLACK);
    if (named) name_text(song_name, place_names[selected_place - PLACE_FIRST_KEPT]);
    cv_text(4, 0, &FONT_S, selected_place == PLACE_DEMO ? "DEMO 1: ACID TECHNO" : selected_place == PLACE_DEMO2 ? "DEMO 2: UPLIFTING"
                           : selected_place == PLACE_DEMO3 ? "DEMO 3: LO-FI" : selected_place == PLACE_NEW ? "AN EMPTY SONG"
                           : named ? song_name : has_song ? "A SONG OF YOURS" : "NOTHING HERE YET", named ? C_WHITE : C_LABEL);
    for (place = 0; place < PLACE_COUNT; place++) {
        char letter[2] = {PLACE_NAMES[place][0], 0};
        const int lit = place == selected_place || (places_with_song >> place & 1u);
        cv_rect(step_x(place), 20, 12, 20, place == selected_place ? C_ACCENT : lit ? C_HIT : C_STEP_EVEN_BEAT);
        cv_text(step_x(place) + (12 - text_w(&FONT_S, letter)) / 2, 22, &FONT_S, letter, lit ? C_BLACK : C_LABEL);
    }
    if (note)
        cv_text(4, 60, &FONT_S, NOTES[note], C_ACCENT);
    else {
        if (has_song) cv_text(4, 60, &FONT_S, "SEQ: LOAD THIS SONG", C_LABEL);
        if (selected_place >= PLACE_FIRST_KEPT) cv_text(4, 80, &FONT_S, "REC: SAVE YOUR SONG HERE", C_LABEL);
        if (has_song) cv_text(4, 100, &FONT_S, previewing ? "PLAY: STOP LISTENING" : "PLAY: LISTEN TO IT", previewing ? C_ACCENT : C_LABEL);
    }
    cv_blit(0, BODY_TOP);
}

/* what the library's page says under its places */
enum { LIBRARY_NOTE_NONE, LIBRARY_NOTE_FETCH_ASKED, LIBRARY_NOTE_STORE_ASKED, LIBRARY_NOTE_SAVING, LIBRARY_NOTE_STORED,
       LIBRARY_NOTE_FETCHED, LIBRARY_NOTE_STOP_FIRST };

static void with_letter(char *text, const char *template, uint32_t pattern_index)   /* '@' in template: the pattern's letter */
{
    for (; *template; template++) *text++ = *template == '@' ? (char)('A' + pattern_index) : *template;
    *text = 0;
}

/* the library's places: a box each, light where there is a pattern, orange the selected one; what SEQ, REC and
 * PLAY do with it and the pattern on the screen (previewing: it plays), or what has just happened */
static void draw_library(uint32_t selected_place, uint32_t places_with_pattern, uint32_t shown_pattern, uint32_t note, int previewing)
{
    static const char *const NOTES[] = {"", "SEQ AGAIN: IT REPLACES @", "REC AGAIN: STORE OVER IT", "STORING", "STORED",
                                        "IT IS PATTERN @ NOW", "PRESS STOP FIRST"};
    const int has_pattern = places_with_pattern >> selected_place & 1u;
    const int named = has_pattern && !name_empty(library.patterns[selected_place].name);
    char line[32];
    uint32_t place;
    cv_begin(240, CHAIN_HEIGHT, C_BLACK);
    if (named) name_text(line, library.patterns[selected_place].name);
    cv_text(4, 0, &FONT_S, named ? line : has_pattern ? "A PATTERN" : "NOTHING HERE YET", named ? C_WHITE : C_LABEL);
    for (place = 0; place < LIBRARY_PLACES; place++)
        cv_rect(step_x(place), 20, 12, 20, place == selected_place ? C_ACCENT : places_with_pattern >> place & 1u ? C_HIT
                                           : place / 4u % 2u ? C_STEP_EVEN_BEAT : C_STEP_ODD_BEAT);
    if (note) {
        with_letter(line, NOTES[note], shown_pattern);
        cv_text(4, 60, &FONT_S, line, C_ACCENT);
    } else {
        if (has_pattern) {
            with_letter(line, "SEQ: INTO PATTERN @", shown_pattern);
            cv_text(4, 60, &FONT_S, line, C_LABEL);
        }
        with_letter(line, "REC: STORE PATTERN @ HERE", shown_pattern);
        cv_text(4, 80, &FONT_S, line, C_LABEL);
        if (has_pattern) cv_text(4, 100, &FONT_S, previewing ? "PLAY: STOP LISTENING" : "PLAY: LISTEN TO IT", previewing ? C_ACCENT : C_LABEL);
    }
    cv_blit(0, BODY_TOP);
}

/* the NAMES page: the song's name and the name of the pattern on the screen, a big character each, the cursor's orange */
static void draw_naming(const volatile uint8_t *pattern_name, uint32_t pattern_index, uint32_t cursor)
{
    char label[16];
    uint32_t place;
    cv_begin(240, CHAIN_HEIGHT, C_BLACK);
    cv_text(4, 0, &FONT_S, "SONG", C_LABEL);
    with_letter(label, "PATTERN @", pattern_index);
    cv_text(4, 58, &FONT_S, label, C_LABEL);
    for (place = 0; place < 2u * NAME_LENGTH; place++) {
        const uint32_t character = place < NAME_LENGTH ? song.name[place] : pattern_name[place - NAME_LENGTH];
        const int32_t x = (int32_t)(8u + place % NAME_LENGTH * 28u), y = place < NAME_LENGTH ? 18 : 76;
        const char text[2] = {NAME_CHARACTERS[character], 0};
        cv_text(x + (26 - text_w(&FONT_L, text)) / 2, y, &FONT_L, text, place == cursor ? C_ACCENT : C_WHITE);
        cv_rect(x, y + 34, 26, 2, place == cursor ? C_ACCENT : C_STEP_ODD_BEAT);   /* a line under each place */
    }
    cv_blit(0, BODY_TOP);
}

/* what the sound page says under the sound's name */
enum { SOUND_NOTE_NONE, SOUND_NOTE_KEEP_ASKED, SOUND_NOTE_KEEP_ASKED_2, SOUND_NOTE_KEPT, SOUND_NOTE_KEPT_2 };
enum { FX_PAGE_NOTES, FX_PAGE_SOUND, FX_PAGE_MIX };   /* a track's pages, FX goes round them */
#define SOUND_HEIGHT 124u

static const char *const WAVE_NAMES[SYNTH_WAVE_COUNT] = {"SAW", "SQUARE", "PULSE", "2 SAWS", "SOFT"};

/* a melodic track's sound page: the name of the sound (a * when it has been changed since), what ALGORITHM, SEQ and
 * REC do, or what has just happened */
static void draw_sound(uint32_t melodic, int edited, uint32_t note)
{
    static const char *const NOTES[] = {"", "SEQ AGAIN: KEEP AS USER1", "REC AGAIN: KEEP AS USER2", "KEPT AS USER1", "KEPT AS USER2"};
    const char *source = sound_name(melodic, song.sound_choices[melodic]);
    char name[16];
    uint32_t length = 0;
    while (source[length] && length < sizeof name - 2u) { name[length] = source[length]; length++; }
    if (edited) name[length++] = '*';
    name[length] = 0;
    cv_begin(240, SOUND_HEIGHT, C_BLACK);
    cv_text(4, 0, &FONT_S, "SOUND", C_LABEL);
    cv_text(4, 20, &FONT_L, name, C_WHITE);
    if (note)
        cv_text(4, 64, &FONT_S, NOTES[note], C_ACCENT);
    else {
        cv_text(4, 64, &FONT_S, "ALGORITHM: OTHER SOUNDS", C_LABEL);
        cv_text(4, 84, &FONT_S, "SEQ: KEEP AS USER1", C_LABEL);
        cv_text(4, 104, &FONT_S, "REC: KEEP AS USER2", C_LABEL);
    }
    cv_blit(0, BODY_TOP);
}

/* a track's mix page: a row per value, its name and a bar (PAN: a mark, the middle marked); on DRUMS a row per drum's
 * LEVEL, the selected drum's orange */
#define MIX_ROW_HEIGHT 22u
#define MIX_BAR_X 96
#define MIX_BAR_WIDTH 140

static void draw_mix_row(uint32_t row, const char *name, uint32_t value, int centred, int selected)
{
    cv_begin(240, MIX_ROW_HEIGHT, C_BLACK);
    cv_text(4, 3, &FONT_S, name, selected ? C_ACCENT : C_LABEL);
    cv_rect(MIX_BAR_X, 6, MIX_BAR_WIDTH, 10, C_STEP_EVEN_BEAT);
    if (centred) {
        cv_rect(MIX_BAR_X + MIX_BAR_WIDTH / 2 - 1, 4, 2, 14, C_LABEL);
        cv_rect(MIX_BAR_X + (int32_t)(value * (MIX_BAR_WIDTH - 4) / MIX_MAX), 4, 4, 14, selected ? C_ACCENT : C_HIT);
    } else
        cv_rect(MIX_BAR_X, 6, (int32_t)(value * MIX_BAR_WIDTH / MIX_MAX), 10, selected ? C_ACCENT : C_HIT);
    cv_blit(0, BODY_TOP + row * MIX_ROW_HEIGHT);
}

static void draw_drum_row(uint32_t drum, uint32_t row, uint32_t hits, uint32_t accents, int selected, uint32_t marked_step)   /* row: of the page */
{
    uint32_t step;
    cv_begin(240, DRUM_ROW_HEIGHT, C_BLACK);
    cv_text(4, 0, &FONT_S, DRUM_NAMES[drum], selected ? C_ACCENT : C_LABEL);
    for (step = 0; step < STEPS_PER_BAR; step++) {
        uint16_t colour = hits >> step & 1u ? (selected ? C_ACCENT : C_HIT)
                        : step / 4u % 2u ? C_STEP_EVEN_BEAT : C_STEP_ODD_BEAT;
        cv_rect(step_x(step), 20, 12, 20, colour);
        if (hits >> step & accents >> step & 1u) cv_rect(step_x(step), 20, 12, ACCENT_BAND_HEIGHT, C_WHITE);   /* accented: a white top */
        if (step == marked_step) cv_rect(step_x(step), 42, 12, 3, C_WHITE);
    }
    cv_blit(0, BODY_TOP + row * DRUM_ROW_HEIGHT);
}

static void draw_drum_pages(uint32_t drum_page)   /* a mark per page of drums, under the knobs' labels; orange the one shown */
{
    uint32_t mark;
    cv_begin(240, 8, C_BLACK);
    for (mark = 0; mark < DRUM_PAGES; mark++)
        cv_rect((int32_t)(120u - DRUM_PAGES * 12u + mark * 24u + 4u), 0, 16, 8, mark == drum_page ? C_ACCENT : C_STEP_ODD_BEAT);
    cv_blit(0, 216);
}

/* a melodic track's bar as a piano roll: a column per step, a row per note (lowest_note at the bottom), the selected
 * note's row lit. A note above or below what fits is a thin line at that edge. The pad's chords show their other
 * notes above the root, dimmer. The cursor's step is framed. */
static void draw_roll(const volatile pattern_t *pattern, uint32_t melodic, uint32_t bar, uint32_t selected_note, uint32_t lowest_note, uint32_t cursor_step)
{
    uint32_t step;
    int32_t selected_y = (int32_t)((lowest_note + ROLL_ROWS - 1u - selected_note) * ROLL_ROW_HEIGHT);
    cv_begin(240, ROLL_HEIGHT, C_BLACK);
    for (step = 0; step < STEPS_PER_BAR; step++) {
        uint32_t value = step_value(pattern->notes[melodic][bar], pattern->gates[melodic][bar], melodic, step), note = value & NOTE_MASK, chord_note;
        const int tied = melodic != MELODIC_PAD && value & NOTE_TIE && step;
        const uint32_t gate = melodic == MELODIC_PAD ? 0u : note_gate(pattern->gates[melodic][bar], step);
        const int32_t note_x = tied ? step_x(step - 1u) + 12 : step_x(step);   /* tied: one bar with the step before; short: as much of the step */
        const int32_t note_width = gate ? (int32_t)(gate * 12u / 10u + 1u) : step_x(step) + 12 - note_x;   /* (10 %: 2 px, 90 %: 11) */
        const int accented = note && !tied && pattern->accents[1u + melodic][bar] >> step & 1u;   /* (on the note's first step) */
        cv_rect(step_x(step), 0, 12, (int32_t)ROLL_HEIGHT, step / 4u % 2u ? C_ROLL_EVEN_BEAT : C_ROLL_ODD_BEAT);
        if (melodic == MELODIC_ARP) {           /* ARP's hits have no notes: a lane through the middle */
            if (note) cv_rect(note_x, (int32_t)ROLL_HEIGHT / 3, note_width, (int32_t)ROLL_HEIGHT / 3, C_HIT);
            if (accented) cv_rect(note_x, (int32_t)ROLL_HEIGHT / 3, note_width, ACCENT_BAND_HEIGHT, C_WHITE);
            continue;
        }
        cv_rect(step_x(step), selected_y, 12, (int32_t)ROLL_ROW_HEIGHT - 1, step / 4u % 2u ? C_STEP_EVEN_BEAT : C_STEP_ODD_BEAT);
        if (!note) continue;
        if (note < lowest_note)
            cv_rect(note_x, (int32_t)ROLL_HEIGHT - 3, note_width, 3, C_LABEL);
        else if (note >= lowest_note + ROLL_ROWS)
            cv_rect(note_x, 0, note_width, 3, C_LABEL);
        else {
            cv_rect(note_x, (int32_t)((lowest_note + ROLL_ROWS - 1u - note) * ROLL_ROW_HEIGHT), note_width,
                    (int32_t)ROLL_ROW_HEIGHT - 1, note == selected_note ? C_ACCENT : C_HIT);
            if (accented) cv_rect(note_x, (int32_t)((lowest_note + ROLL_ROWS - 1u - note) * ROLL_ROW_HEIGHT), note_width, ACCENT_BAND_HEIGHT, C_WHITE);
        }
        if (melodic != MELODIC_PAD) continue;
        for (chord_note = 1; chord_note < PAD_CHORDS[value >> CHORD_SHIFT].note_count; chord_note++) {
            uint32_t above = note + PAD_CHORDS[value >> CHORD_SHIFT].intervals[chord_note];
            if (above >= lowest_note && above < lowest_note + ROLL_ROWS)
                cv_rect(step_x(step), (int32_t)((lowest_note + ROLL_ROWS - 1u - above) * ROLL_ROW_HEIGHT), 12,
                        (int32_t)ROLL_ROW_HEIGHT - 1, C_LABEL);
        }
    }
    cv_rect(step_x(cursor_step) - 1, 0, 1, (int32_t)ROLL_HEIGHT, C_WHITE);   /* the cursor: a line either side of its step */
    cv_rect(step_x(cursor_step) + 12, 0, 1, (int32_t)ROLL_HEIGHT, C_WHITE);
    cv_blit(0, BODY_TOP);
}

static void draw_roll_marker(uint32_t marked_step)      /* under the roll, so the running step does not redraw the notes */
{
    cv_begin(240, 3, C_BLACK);
    if (marked_step != NO_STEP) cv_rect(step_x(marked_step), 0, 12, 3, C_WHITE);
    cv_blit(0, BODY_TOP + ROLL_HEIGHT + 2u);
}

/* knob 0..3: the place of KNOB1..KNOB4. Numbers and notes are big; a word (too wide for that) is small */
static void draw_knob(uint32_t knob, const char *label, const char *text, int big)
{
    draw_text_box(knob * 60u, 190, 60, &FONT_S, label, C_LABEL, 1);
    lcd_fill(knob * 60u, 207, 60, FONT_L.h, C_BLACK);
    draw_text_box(knob * 60u, big ? 207u : 215u, 60, big ? &FONT_L : &FONT_S, text, C_WHITE, 1);
}
