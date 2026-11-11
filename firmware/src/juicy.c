/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy: a groovebox for the M-VAVE FM-1, built on Dinghy (platform.c and everything below it: updates, the USB
 * rescue, the keys, knobs, LEDs, the screen, flash storage). Five tracks, DRUMS, BASS, SEQUENCE, PAD and LEAD, in
 * eight patterns (A..H) in which each track has up to four bars of 16 steps of its own (a pattern lasts as long as
 * its longest track, the shorter ones go round), a chain that plays patterns one after the other, an echo
 * three steps long that all but the drums can send into, a reverb for SEQUENCE, PAD, LEAD and the echo, and a
 * limiter over all of it.
 *
 *   the keys     play: on DRUMS the lower keys from the left are the drums, on the other tracks all 27 keys are
 *                notes (the pad's: the chord on that note). Nothing of it goes into the pattern, but what was
 *                played last is what EDIT puts there
 *   SEL          (lit) OP1 .. OP5, the first five upper keys, pick the track on the screen: DRUMS, BASS,
 *                SEQUENCE, PAD, LEAD. SEL again: the keys play. SEL and EDIT are never on together: the one
 *                pressed turns the other off
 *   EDIT         (lit, the page's name orange) the lower keys are the 16 steps of the bar on the screen: a press
 *                puts what was played or what the knobs show there (a hit of that drum, that note, that chord),
 *                or takes it away again
 *   KNOB1..4     (with EDIT on, as SEQ and REC) what the four places along the bottom of the screen say; on DRUMS KNOB1 moves the orange row
 *   OCT- OCT+    the bar on the screen: the one before, the next one. With EDIT on, on a track's page, OCT+ past
 *                the track's last bar gives it one more (up to four), and OCT- held takes the bar on the screen
 *                out of the track (its only bar: emptied)
 *   PRESETS      the pattern on the screen, A .. H, on every page
 *   HOME         the page for the whole song: TEMPO, the PATTERN on the screen, REVERB,
 *                and the chain, where with EDIT on the lower keys are its 16 places (HOME again: back to the track)
 *   REC          on HOME's page: a copy of the pattern on the screen, in the first empty pattern
 *   SAVE         the page of the songs: KNOB1 picks one (the demo song, an empty one, or one of eight kept in
 *                flash), SEQ twice loads it, REC saves the song there (twice where there is one already). This
 *                page needs no EDIT, and nothing of it works while the song plays. SAVE again: the library, 16
 *                places for patterns of any song (their notes and hits, not the sounds): KNOB1 or a lower key picks
 *                one, SEQ puts it into the pattern on the screen (twice where that holds something; this works
 *                while it plays), REC stores the pattern on the screen there (twice where there is one already)
 *   SEQ          what plays: the chain (lit), or the pattern on the screen, round and round
 *   PLAY         start and stop
 *
 * The lights: a key or button that does something here glows dimly (a bar's four beats a bit brighter); it is fully lit when it is on, selected, or
 * holds something (a step that is set, a chain place with a pattern). The others are dark.
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. tests/host.c and tests/web.c run this
 * file on a computer. */
#define APP_NAME "JUICY"                        /* the rescue and crash screens (platform.c) */
#define CV_MAX (240u * 132u)                    /* the biggest canvas: a piano roll */
#include "platform.c"
#include "sound.c"
#include "drums.c"
#include "synth.c"
#include "pad.c"
#include "echo.c"
#include "reverb.c"
#include "limiter.c"

/* ---- the melodic tracks: BASS, SEQUENCE and LEAD are each one synth voice (synth.c) with a shape of its own,
 * PAD is the pad (pad.c), where a note is the root of a chord */
enum { MELODIC_BASS, MELODIC_SEQUENCE, MELODIC_PAD, MELODIC_LEAD, MELODIC_COUNT };
#define NOTE_MASK 0x7Fu                         /* a step of a melodic track: the MIDI note, 0 = none ... */
#define NOTE_MAJOR 0x80u                        /* ... and on the pad's track: the chord is major, not minor */

static const struct {
    synth_shape_t shape;
    uint8_t note_lowest, note_highest;          /* MIDI */
} MELODIC[MELODIC_COUNT] = {
    {{0, 25, 12000, 0, SYNTH_RELEASE_RATE}, 24, 60},   /* BASS: a sawtooth, C1..C4 */
    {{0x55555555u, 24, 8250, 0, SYNTH_RELEASE_RATE}, 48, 84},   /* SEQUENCE: a pulse a third wide, the filter plucked twice as far, C3..C6 */
    {{0, 0, 0, 0, 0}, 36, 72},                  /* PAD: no synth voice; the chord's root C2..C5 */
    {{0, 25, 8000, 7, 0}, 48, 84},              /* LEAD: two sawtooths 13 cents apart, a note rings on past its step, C3..C6 */
};
static synth_voice_t melodic_voices[MELODIC_COUNT];   /* (the pad's is not used) */

static void melodic_note_on(uint32_t melodic, uint32_t value)   /* value: as in song.notes */
{
    if (melodic == MELODIC_PAD) pad_chord_on(value & NOTE_MASK, (value & NOTE_MAJOR) != 0u);
    else synth_note_on(&melodic_voices[melodic], value);
}

static void melodic_note_off(uint32_t melodic)
{
    if (melodic == MELODIC_PAD) pad_chord_off();
    else synth_note_off(&melodic_voices[melodic]);
}

/* ---- the song: what plays, what is saved. The main loop writes it, the audio ISR reads it. */
#define STEPS_PER_BAR 16u
#define BAR_COUNT 4u
#define PATTERN_COUNT 8u
#define CHAIN_SLOTS 16u                         /* the chain's places: one per lower key */
#define TEMPO_MIN 60u
#define TEMPO_MAX 200u
#define ECHO_SEND_MAX 100u
#define REVERB_MAX 100u
#define ECHO_STEPS 3u                           /* the echo comes back a dotted eighth later */

typedef struct {
    uint16_t drum_hits[DRUM_COUNT][BAR_COUNT];  /* bit s: a hit on step s of that bar */
    uint8_t notes[MELODIC_COUNT][BAR_COUNT][STEPS_PER_BAR];   /* what is on that step: NOTE_MASK, NOTE_MAJOR */
    uint8_t bar_totals[1u + MELODIC_COUNT];     /* how many bars a track has, 1..BAR_COUNT: the drums, then the melodic tracks */
} pattern_t;

static uint32_t pattern_bars(const volatile pattern_t *pattern)   /* how long it lasts: as its longest track */
{
    uint32_t track, bars = 0;
    for (track = 0; track < sizeof pattern->bar_totals; track++)
        if (pattern->bar_totals[track] > bars) bars = pattern->bar_totals[track];
    return bars;
}

typedef struct {
    pattern_t patterns[PATTERN_COUNT];
    uint8_t chain[CHAIN_SLOTS];                 /* the patterns in the order they play: pattern + 1, 0 = nothing there (skipped) */
    uint8_t cutoff[MELODIC_COUNT], decay[MELODIC_COUNT];      /* 0..SYNTH_PARAMETER_MAX (the pad has no decay) */
    uint8_t echo[MELODIC_COUNT];                /* how much of the track goes into the echo, 0..ECHO_SEND_MAX */
    uint8_t tempo;
    uint8_t reverb;                             /* how much of SEQUENCE, PAD, LEAD and the echo goes into the reverb, 0..REVERB_MAX */
} song_t;

static volatile song_t song;

/* a song with nothing in it but the sounds of the tracks, the tempo and the reverb */
static void empty_song(void)
{
    uint32_t index, track;
    memset((void *)&song, 0, sizeof song);
    for (index = 0; index < PATTERN_COUNT; index++)
        for (track = 0; track < sizeof song.patterns[index].bar_totals; track++)
            song.patterns[index].bar_totals[track] = 1;
    song.cutoff[MELODIC_BASS] = 45;
    song.decay[MELODIC_BASS] = 35;
    song.cutoff[MELODIC_SEQUENCE] = 50;
    song.decay[MELODIC_SEQUENCE] = 25;
    song.echo[MELODIC_SEQUENCE] = 40;
    song.cutoff[MELODIC_PAD] = 50;
    song.cutoff[MELODIC_LEAD] = 65;
    song.decay[MELODIC_LEAD] = 55;
    song.echo[MELODIC_LEAD] = 50;
    song.tempo = 138;
    song.reverb = 35;
}

/* The first song, in A minor: four bars over the chords Am F C G, in five patterns that differ in which tracks
 * play, and a chain that tells them as a song.
 *   A  everything              B  A without the lead         C  the intro: pad and sequence alone
 *   D  C with kick and hat     E  the break: pad and lead alone
 * The drums are one bar that goes round under the four of the others.
 * The chain: C D B A B A E D A A C. Patterns F, G and H are empty. */
static void first_song(void)
{
    enum { KICK = 1, HAT = 2, CLAP = 4, BASS = 8, SEQUENCE = 16, PAD = 32, LEAD = 64 };
    static const uint8_t PARTS[5] = {KICK | HAT | CLAP | BASS | SEQUENCE | PAD | LEAD, KICK | HAT | CLAP | BASS | SEQUENCE | PAD,
                                     SEQUENCE | PAD, KICK | HAT | SEQUENCE | PAD, PAD | LEAD};
    static const uint8_t CHAIN[11] = {3, 4, 2, 1, 2, 1, 5, 4, 1, 1, 3};   /* (as in song.chain: pattern + 1) */
    static const uint8_t BASS_NOTES[BAR_COUNT] = {33, 29, 36, 31};   /* A1 F1 C2 G1 */
    static const uint8_t PAD_CHORDS[BAR_COUNT] = {57, 53 | NOTE_MAJOR, 60 | NOTE_MAJOR, 55 | NOTE_MAJOR};   /* A3 minor, F3, C4, G3 major */
    static const uint8_t ARPEGGIOS[BAR_COUNT][8] = {   /* each twice a bar */
        {57, 64, 60, 64, 67, 64, 60, 64},       /* A3 E4 C4 E4 G4 E4 C4 E4 */
        {53, 60, 57, 60, 65, 60, 57, 60},       /* F3 C4 A3 C4 F4 C4 A3 C4 */
        {55, 64, 60, 64, 67, 64, 60, 64},       /* G3 E4 C4 E4 G4 E4 C4 E4 */
        {55, 62, 59, 62, 67, 62, 59, 62},       /* G3 D4 B3 D4 G4 D4 B3 D4 */
    };
    static const uint8_t LEAD_STEPS[4] = {0, 6, 10, 12};
    static const uint8_t LEAD_NOTES[BAR_COUNT][4] = {
        {69, 76, 74, 72},                       /* A4 E5 D5 C5 */
        {72, 77, 76, 72},                       /* C5 F5 E5 C5 */
        {76, 79, 76, 74},                       /* E5 G5 E5 D5 */
        {74, 79, 74, 71},                       /* D5 G5 D5 B4 */
    };
    uint32_t index, index2, bar, step;
    empty_song();
    for (index = 0; index < sizeof PARTS; index++) {
        volatile pattern_t *pattern = &song.patterns[index];
        uint32_t parts = PARTS[index];
        for (index2 = 0; index2 < MELODIC_COUNT; index2++)
            pattern->bar_totals[1u + index2] = BAR_COUNT;
        if (parts & KICK) pattern->drum_hits[DRUM_KICK][0] = 0x1111;   /* every quarter */
        if (parts & HAT) pattern->drum_hits[DRUM_HAT][0] = 0x4444;     /* between the kicks */
        if (parts & CLAP) pattern->drum_hits[DRUM_CLAP][0] = 0x1010;   /* on 2 and 4 */
        for (bar = 0; bar < BAR_COUNT; bar++) {
            if (parts & BASS)
                for (step = 2; step < STEPS_PER_BAR; step += 4u)             /* with the hat */
                    pattern->notes[MELODIC_BASS][bar][step] = BASS_NOTES[bar];
            if (parts & SEQUENCE)
                for (step = 0; step < STEPS_PER_BAR; step++)
                    pattern->notes[MELODIC_SEQUENCE][bar][step] = ARPEGGIOS[bar][step % 8u];
            if (parts & PAD) pattern->notes[MELODIC_PAD][bar][0] = PAD_CHORDS[bar];
            if (parts & LEAD)
                for (step = 0; step < sizeof LEAD_STEPS; step++)
                    pattern->notes[MELODIC_LEAD][bar][LEAD_STEPS[step]] = LEAD_NOTES[bar][step];
        }
    }
    for (index = 0; index < sizeof CHAIN; index++)
        song.chain[index] = CHAIN[index];
}

static int pattern_empty(const volatile pattern_t *pattern)
{
    uint32_t bar, drum, melodic, step;
    for (bar = 0; bar < BAR_COUNT; bar++) {
        for (drum = 0; drum < DRUM_COUNT; drum++)
            if (pattern->drum_hits[drum][bar]) return 0;
        for (melodic = 0; melodic < MELODIC_COUNT; melodic++)
            for (step = 0; step < STEPS_PER_BAR; step++)
                if (pattern->notes[melodic][bar][step]) return 0;
    }
    return 1;
}

static int song_valid(const volatile song_t *checked)
{
    uint32_t melodic, index, track;
    for (melodic = 0; melodic < MELODIC_COUNT; melodic++)
        if (checked->cutoff[melodic] > SYNTH_PARAMETER_MAX || checked->decay[melodic] > SYNTH_PARAMETER_MAX
            || checked->echo[melodic] > ECHO_SEND_MAX) return 0;
    for (index = 0; index < PATTERN_COUNT; index++)
        for (track = 0; track < sizeof checked->patterns[index].bar_totals; track++)
            if (checked->patterns[index].bar_totals[track] < 1u || checked->patterns[index].bar_totals[track] > BAR_COUNT) return 0;
    for (index = 0; index < CHAIN_SLOTS; index++)
        if (checked->chain[index] > PATTERN_COUNT) return 0;
    return checked->tempo >= TEMPO_MIN && checked->tempo <= TEMPO_MAX && checked->reverb <= REVERB_MAX;
}

/* ---- the songs' places: two that are always there, the demo song and an empty one, and eight kept in flash
 * (a blob each, docs/API.md). The song that plays is none of them: it is a copy, kept as the settings. */
#define PLACES_KEPT 8u
enum { PLACE_DEMO, PLACE_NEW, PLACE_FIRST_KEPT, PLACE_COUNT = PLACE_FIRST_KEPT + PLACES_KEPT };
_Static_assert(PLACES_KEPT <= NBLOB && sizeof(song_t) <= SECTOR_MAX, "a song a blob");

static song_t place_song;                       /* a kept song on its way in: checked before it is the song */

static int place_kept_valid(uint32_t place)     /* place_song holds that place's song then */
{
    return !blob_load(place - PLACE_FIRST_KEPT, &place_song, sizeof place_song) && song_valid(&place_song);
}

static int place_load(uint32_t place)           /* 0: that place's song is the song now */
{
    if (place == PLACE_DEMO) first_song();
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
    uint32_t place, track, valid = !slot_load(0, &library, sizeof library);
    for (place = 0; valid && place < LIBRARY_PLACES; place++)
        for (track = 0; track < sizeof library.patterns[place].bar_totals; track++)
            if (library.patterns[place].bar_totals[track] < 1u || library.patterns[place].bar_totals[track] > BAR_COUNT) valid = 0;
    if (valid) return;
    memset(&library, 0, sizeof library);
    for (place = 0; place < LIBRARY_PLACES; place++)
        for (track = 0; track < sizeof library.patterns[place].bar_totals; track++)
            library.patterns[place].bar_totals[track] = 1;
}

/* ---- the sequencer (runs in the audio ISR, so a step starts on its exact frame) */
#define FRAMES_PER_STEP_Q8(tempo) (FS * 15u * 256u / (tempo))   /* a step is a sixteenth note: 60 / tempo / 4 seconds */
#define AUDITION_FRAMES (FS / 5u)               /* a note played by hand sounds this long */
#define BLOCK_FRAMES_MAX 256u                   /* app_audio is never asked for more (docs/API.md) */

static volatile uint32_t playing;               /* (the main loop writes) */
static volatile uint32_t frames_per_step_q8;    /* (the main loop writes) */
static volatile uint32_t drum_auditions[DRUM_COUNT];   /* counts up when the main loop wants to hear a drum */
static volatile uint32_t note_auditions[MELODIC_COUNT], audition_notes[MELODIC_COUNT];   /* the same for a note (the note is written first) */
static volatile uint32_t note_held[MELODIC_COUNT];   /* the key that plays that note is down: it sounds until the key lets go (the main loop writes) */
static volatile uint32_t chain_on;              /* the chain plays, not the pattern on the screen (the main loop writes) */
static volatile uint32_t loop_pattern;          /* the pattern on the screen: it plays when the chain does not (the main loop writes) */
static volatile uint32_t playing_step;          /* the step that sounds now, counted from the pattern's bar 1 (the ISR writes) */
static volatile uint32_t playing_pattern;       /* the pattern that sounds now (the ISR writes) */
static volatile uint32_t playing_slot;          /* its place in the chain, CHAIN_SLOTS = it is not from the chain (the ISR writes) */

static void render(int32_t *out, uint32_t frame_count)   /* at most BLOCK_FRAMES_MAX */
{
    static int32_t echo_send[BLOCK_FRAMES_MAX];
    static int32_t room[2u * BLOCK_FRAMES_MAX];   /* what the reverb hears (L R L R ...): all but the drums and the bass */
    uint32_t melodic, i;
    memset(echo_send, 0, frame_count * sizeof echo_send[0]);
    memset(room, 0, 2u * frame_count * sizeof room[0]);
    drums_render(out, frame_count);
    for (melodic = 0; melodic < MELODIC_COUNT; melodic++) {
        int32_t send_gain = (int32_t)(song.echo[melodic] * 5u);   /* of 512: echo / 100, near enough */
        if (melodic == MELODIC_PAD)
            pad_render(room, echo_send, frame_count, song.cutoff[melodic], send_gain);
        else
            synth_render(&melodic_voices[melodic], &MELODIC[melodic].shape, melodic == MELODIC_BASS ? out : room, echo_send,
                         frame_count, song.cutoff[melodic], song.decay[melodic], send_gain);
    }
    echo_render(room, echo_send, frame_count, ECHO_STEPS * frames_per_step_q8 >> 8);
    reverb_render(out, room, frame_count, song.reverb);
    for (i = 0; i < 2u * frame_count; i++)
        out[i] += room[i];
    limiter_render(out, frame_count);
}

static void app_audio(int32_t *out, uint32_t frame_count)
{
    static uint32_t was_playing, next_step, frames_to_step_q8, drum_auditions_done[DRUM_COUNT];
    static uint32_t note_auditions_done[MELODIC_COUNT], audition_frames_left[MELODIC_COUNT];
    static uint32_t pad_pattern_value;          /* the chord the pattern holds on the pad now, 0 = none */
    static uint32_t pattern_now, chain_position;   /* what plays, and the last place of the chain that played */
    uint32_t frames_done = 0, drum, melodic;
    for (drum = 0; drum < DRUM_COUNT; drum++)
        if (drum_auditions_done[drum] != drum_auditions[drum]) {
            drum_auditions_done[drum] = drum_auditions[drum];
            drums_trigger(drum);
        }
    for (melodic = 0; melodic < MELODIC_COUNT; melodic++) {
        if (note_auditions_done[melodic] != note_auditions[melodic]) {
            note_auditions_done[melodic] = note_auditions[melodic];
            melodic_note_on(melodic, audition_notes[melodic]);
            audition_frames_left[melodic] = AUDITION_FRAMES;
        } else if (audition_frames_left[melodic]) {
            if (note_held[melodic])
                audition_frames_left[melodic] = 1;   /* it ends when the key lets go */
            else if (audition_frames_left[melodic] <= frame_count) {
                audition_frames_left[melodic] = 0;
                if (melodic == MELODIC_PAD && pad_pattern_value) melodic_note_on(melodic, pad_pattern_value);   /* back to the pattern's chord */
                else melodic_note_off(melodic);
            } else
                audition_frames_left[melodic] -= frame_count;
        }
    }
    if (!playing) {
        if (was_playing)                        /* STOP: the drums ring out, the notes let go */
            for (melodic = 0; melodic < MELODIC_COUNT; melodic++)
                melodic_note_off(melodic);
        was_playing = 0;
        pad_pattern_value = 0;
        render(out, frame_count);
        return;
    }
    if (!was_playing) {                         /* PLAY: from the top, at once */
        was_playing = 1;
        next_step = ~0u;                        /* (past any pattern's end: the first step picks the pattern) */
        chain_position = CHAIN_SLOTS - 1u;      /* (the chain starts at its first place) */
        frames_to_step_q8 = 0;
    }
    while (frames_done < frame_count) {
        uint32_t run_frames;
        if (frames_to_step_q8 < 256u) {         /* less than a frame left: the step starts here */
            const volatile pattern_t *pattern;
            uint32_t bar, step;
            if (next_step >= pattern_bars(&song.patterns[pattern_now]) * STEPS_PER_BAR) {   /* the pattern's end (or bars were taken away): which one now? */
                uint32_t tried;
                next_step = 0;
                pad_pattern_value = 0;          /* the pad's chord ends with its pattern (a chord on step 1 takes over without a gap) */
                melodic_note_off(MELODIC_PAD);
                pattern_now = loop_pattern;
                playing_slot = CHAIN_SLOTS;
                for (tried = 1; chain_on && tried <= CHAIN_SLOTS; tried++) {   /* the chain's next place that holds a pattern */
                    uint32_t slot = (chain_position + tried) % CHAIN_SLOTS;
                    if (song.chain[slot]) {
                        pattern_now = song.chain[slot] - 1u;
                        playing_slot = chain_position = slot;
                        break;
                    }
                }
                playing_pattern = pattern_now;
            }
            pattern = &song.patterns[pattern_now];
            bar = next_step / STEPS_PER_BAR;    /* (of the pattern: a track with fewer bars goes round) */
            step = next_step % STEPS_PER_BAR;
            for (drum = 0; drum < DRUM_COUNT; drum++)
                if (pattern->drum_hits[drum][bar % pattern->bar_totals[0]] >> step & 1u)
                    drums_trigger(drum);
            for (melodic = 0; melodic < MELODIC_COUNT; melodic++) {   /* a note lasts its step: an empty step lets it go */
                uint32_t note = pattern->notes[melodic][bar % pattern->bar_totals[1u + melodic]][step];
                if (melodic == MELODIC_PAD) {   /* but the pad holds its chord until the next one */
                    if (!note) continue;
                    pad_pattern_value = note;
                }
                if (note) melodic_note_on(melodic, note);
                else melodic_note_off(melodic);
                audition_frames_left[melodic] = 0;
            }
            playing_step = next_step;
            next_step++;
            frames_to_step_q8 += frames_per_step_q8;   /* (the fraction carries over: the tempo does not drift) */
        }
        run_frames = frames_to_step_q8 >> 8;
        if (run_frames > frame_count - frames_done) run_frames = frame_count - frames_done;
        render(out + 2u * frames_done, run_frames);
        frames_done += run_frames;
        frames_to_step_q8 -= run_frames << 8;
    }
}

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
#define ROLL_HEIGHT (ROLL_ROWS * ROLL_ROW_HEIGHT)
#define KNOB_COUNT 4u
#define CHAIN_HEIGHT 100u
#define NO_STEP STEPS_PER_BAR
#define NO_BAR BAR_COUNT

/* a page per track, and the one behind HOME. A melodic track's page is MELODIC_FIRST_PAGE + its number */
enum { PAGE_DRUMS, MELODIC_FIRST_PAGE, TRACK_COUNT = MELODIC_FIRST_PAGE + MELODIC_COUNT, PAGE_GLOBAL = TRACK_COUNT, PAGE_SONGS, PAGE_LIBRARY };
_Static_assert(sizeof ((pattern_t *)0)->bar_totals == TRACK_COUNT, "a track's page is its place in bar_totals");

static const char *const PAGE_NAMES[] = {"DRUMS", "BASS", "SEQUENCE", "PAD", "LEAD", "JUICY", "SONGS", "LIBRARY"};
static const char *const DRUM_NAMES[DRUM_COUNT] = {"KICK", "HAT", "CLAP"};
static const char *const CHORD_NAMES[2] = {"MIN", "MAJ"};
static const uint8_t STEP_KEYS[STEPS_PER_BAR] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* the lower keys, left to right */
static const uint8_t TRACK_KEYS[TRACK_COUNT] = {1, 3, 5, 8, 10};   /* the upper keys printed OP1 .. OP5 */
#define KEY_COUNT 27u
#define HOLD_MS 600u                            /* a button held this long: its other job */
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

/* the name, and a box per bar of the track on the screen (HOME's page: of the pattern): orange with the pattern's letter the one on the screen,
 * dark those that do not play, a mark under the one that sounds. The name is orange while EDIT is on */
static void draw_header(uint32_t page, uint32_t pattern_index, uint32_t bar_total, uint32_t shown_bar, uint32_t marked_bar, int editing)
{
    uint32_t bar;
    char letter[2];
    pattern_name(letter, pattern_index);
    cv_begin(240, HEADER_HEIGHT, C_BLACK);
    cv_text(4, 6, &FONT_L, PAGE_NAMES[page], editing ? C_ACCENT : C_HIT);   /* orange: the keys change this page (EDIT) */
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
    uint32_t slot;
    cv_begin(240, CHAIN_HEIGHT, C_BLACK);
    cv_text(4, 0, &FONT_S, on ? "CHAIN ON" : "CHAIN OFF", on ? C_ACCENT : C_LABEL);
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
    cv_blit(0, BODY_TOP);
}

/* what the songs' page says under its places */
enum { SONGS_NOTE_NONE, SONGS_NOTE_LOAD_ASKED, SONGS_NOTE_SAVE_ASKED, SONGS_NOTE_SAVING, SONGS_NOTE_SAVED, SONGS_NOTE_STOP_FIRST };

static const char *const PLACE_NAMES[PLACE_COUNT] = {"DEMO", "NEW", "1", "2", "3", "4", "5", "6", "7", "8"};

/* the songs' places: a box each (D the demo song, N an empty one, 1..8 those kept), light where there is a song,
 * orange the selected one; what SEQ and REC do with it, or what has just happened */
static void draw_songs(uint32_t selected_place, uint32_t places_with_song, uint32_t note)
{
    static const char *const NOTES[] = {"", "SEQ AGAIN: LOAD IT", "REC AGAIN: SAVE OVER IT", "SAVING", "SAVED", "PRESS STOP FIRST"};
    const int has_song = places_with_song >> selected_place & 1u;
    uint32_t place;
    cv_begin(240, CHAIN_HEIGHT, C_BLACK);
    cv_text(4, 0, &FONT_S, selected_place == PLACE_DEMO ? "THE DEMO SONG" : selected_place == PLACE_NEW ? "AN EMPTY SONG"
                           : has_song ? "A SONG OF YOURS" : "NOTHING HERE YET", C_LABEL);
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

/* the library's places: a box each, light where there is a pattern, orange the selected one; what SEQ and REC do
 * with it and the pattern on the screen, or what has just happened */
static void draw_library(uint32_t selected_place, uint32_t places_with_pattern, uint32_t shown_pattern, uint32_t note)
{
    static const char *const NOTES[] = {"", "SEQ AGAIN: IT REPLACES @", "REC AGAIN: STORE OVER IT", "STORING", "STORED",
                                        "IT IS PATTERN @ NOW", "PRESS STOP FIRST"};
    const int has_pattern = places_with_pattern >> selected_place & 1u;
    char line[32];
    uint32_t place;
    cv_begin(240, CHAIN_HEIGHT, C_BLACK);
    cv_text(4, 0, &FONT_S, has_pattern ? "A PATTERN" : "NOTHING HERE YET", C_LABEL);
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
    }
    cv_blit(0, BODY_TOP);
}

static void draw_drum_row(uint32_t drum, uint32_t hits, int selected, uint32_t marked_step)
{
    uint32_t step;
    cv_begin(240, DRUM_ROW_HEIGHT, C_BLACK);
    cv_text(4, 0, &FONT_S, DRUM_NAMES[drum], selected ? C_ACCENT : C_LABEL);
    for (step = 0; step < STEPS_PER_BAR; step++) {
        uint16_t colour = hits >> step & 1u ? (selected ? C_ACCENT : C_HIT)
                        : step / 4u % 2u ? C_STEP_EVEN_BEAT : C_STEP_ODD_BEAT;
        cv_rect(step_x(step), 20, 12, 20, colour);
        if (step == marked_step) cv_rect(step_x(step), 42, 12, 3, C_WHITE);
    }
    cv_blit(0, BODY_TOP + drum * DRUM_ROW_HEIGHT);
}

/* a melodic track's bar as a piano roll: a column per step, a row per note (lowest_note at the bottom), the selected
 * note's row lit. A note above or below what fits is a thin line at that edge. The pad's chords show their third
 * and fifth above the root, dimmer. */
static void draw_roll(const volatile pattern_t *pattern, uint32_t melodic, uint32_t bar, uint32_t selected_note, uint32_t lowest_note)
{
    uint32_t step;
    int32_t selected_y = (int32_t)((lowest_note + ROLL_ROWS - 1u - selected_note) * ROLL_ROW_HEIGHT);
    cv_begin(240, ROLL_HEIGHT, C_BLACK);
    for (step = 0; step < STEPS_PER_BAR; step++) {
        uint32_t value = pattern->notes[melodic][bar][step], note = value & NOTE_MASK, chord_note;
        cv_rect(step_x(step), 0, 12, (int32_t)ROLL_HEIGHT, step / 4u % 2u ? C_ROLL_EVEN_BEAT : C_ROLL_ODD_BEAT);
        cv_rect(step_x(step), selected_y, 12, (int32_t)ROLL_ROW_HEIGHT - 1, step / 4u % 2u ? C_STEP_EVEN_BEAT : C_STEP_ODD_BEAT);
        if (!note) continue;
        if (note < lowest_note)
            cv_rect(step_x(step), (int32_t)ROLL_HEIGHT - 3, 12, 3, C_LABEL);
        else if (note >= lowest_note + ROLL_ROWS)
            cv_rect(step_x(step), 0, 12, 3, C_LABEL);
        else
            cv_rect(step_x(step), (int32_t)((lowest_note + ROLL_ROWS - 1u - note) * ROLL_ROW_HEIGHT), 12,
                    (int32_t)ROLL_ROW_HEIGHT - 1, note == selected_note ? C_ACCENT : C_HIT);
        if (melodic != MELODIC_PAD) continue;
        for (chord_note = 0; chord_note < 2u; chord_note++) {
            uint32_t above = note + (chord_note ? PAD_FIFTH : value & NOTE_MAJOR ? PAD_MAJOR_THIRD : PAD_MINOR_THIRD);
            if (above >= lowest_note && above < lowest_note + ROLL_ROWS)
                cv_rect(step_x(step), (int32_t)((lowest_note + ROLL_ROWS - 1u - above) * ROLL_ROW_HEIGHT), 12,
                        (int32_t)ROLL_ROW_HEIGHT - 1, C_LABEL);
        }
    }
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

/* ---- the main loop: the panel, the LEDs, the screen, saving */
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

static int sound_quiet(void)
{
    uint32_t melodic;
    for (melodic = 0; melodic < MELODIC_COUNT; melodic++)
        if (!synth_quiet(&melodic_voices[melodic])) return 0;
    return drums_quiet() && pad_quiet() && echo_quiet() && reverb_quiet();
}

static void app_main(void)
{
    uint32_t previous_keys = 0, shown_track = PAGE_DRUMS, global_shown = 1, shown_pattern = 0, shown_bar = 0, selected_drum = DRUM_KICK;
    uint32_t selected_notes[MELODIC_COUNT] = {33, 60, 57, 69}, roll_lowest_notes[MELODIC_COUNT] = {28, 56, 53, 65};   /* A1, C4, A3, A4 */
    uint32_t selected_major = 0;                /* the chord KNOB3 shows on the pad's page: 0 minor, 1 major */
    uint32_t select_on = 0;                     /* SEL: the keys OP1 .. OP5 pick the page */
    uint32_t edit_on = 0;                       /* EDIT: the lower keys are the steps. Neither: the keys play */
    uint32_t held_key = 0;                      /* the key that played the last note by hand */
    uint32_t oct_down_waits = 0, oct_down_ms = 0;   /* OCT- is down and has not done anything yet; since when */
    uint32_t songs_shown = 0;                   /* SAVE: the songs' page */
    uint32_t library_shown = 0;                 /* SAVE again: the library's page */
    uint32_t selected_library_place = 0, library_note = LIBRARY_NOTE_NONE, places_with_pattern = 0;   /* bit p: library place p holds a pattern */
    uint32_t library_unsaved = 0;               /* the library goes to flash as soon as nothing sounds */
    uint32_t selected_place = PLACE_FIRST_KEPT, songs_note = SONGS_NOTE_NONE;
    uint32_t places_with_song = 1u << PLACE_DEMO | 1u << PLACE_NEW;   /* bit p: place p holds a song */
    uint32_t place_to_save = PLACE_COUNT;       /* the place the song goes to as soon as nothing sounds, PLACE_COUNT = none */
    uint32_t unsaved = 0, changed_ms = 0, knob, step, drum, track, key, place;
    uint32_t page_drawn = ~0u, header_drawn = 0, drum_row_drawn[DRUM_COUNT] = {0}, roll_drawn = 0, roll_marker_drawn = 0;
    uint32_t knob_drawn[KNOB_COUNT] = {0}, chain_drawn = 0, songs_drawn = 0, library_drawn = 0;

    if (settings_load((void *)&song, sizeof song) || !song_valid(&song))
        first_song();
    for (place = PLACE_FIRST_KEPT; place < PLACE_COUNT; place++)
        if (place_kept_valid(place)) places_with_song |= 1u << place;
    library_load();
    for (place = 0; place < LIBRARY_PLACES; place++)
        if (!pattern_empty(&library.patterns[place])) places_with_pattern |= 1u << place;
    frames_per_step_q8 = FRAMES_PER_STEP_Q8(song.tempo);
    for (step = 0; step < CHAIN_SLOTS; step++)   /* a song with a chain starts with the chain on */
        if (song.chain[step]) chain_on = 1;
    fm1_led_key_glow(BTN_PLAY, 1);              /* what always does something glows */
    fm1_led_key_glow(BTN_HOME, 1);
    fm1_led_key_glow(BTN_SEL, 1);
    fm1_led_key_glow(BTN_EDIT, 1);
    fm1_led_key_glow(BTN_SAVE, 1);

    for (;;) {
        uint32_t keys, keys_pressed, buttons_pressed, buttons_released, page, step_now, marked_step, marked_bar, marked_slot, state;
        uint32_t step_leds = 0, melodic = 0, keys_glowing = 0, keys_glowing_bright = 0, keys_lit = 0;
        uint32_t bar_total;                     /* of what the page shows: its track, or the whole pattern */
        volatile pattern_t *pattern;
        int32_t detents[KNOB_COUNT], presets_detents;
        int song_changed = 0, keys_edit, keys_play;
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        keys_pressed = keys & ~previous_keys;
        previous_keys = keys;
        buttons_pressed = fm1_input_edges(&buttons_released);
        for (knob = 0; knob < KNOB_COUNT; knob++)
            detents[knob] = fm1_enc_take(ENC_KNOB1 + knob);

        if (buttons_pressed & (1u << BTN_PLAY)) playing = !playing;
        if (buttons_pressed & (1u << BTN_SAVE)) {   /* SAVE: the songs, the library, back */
            library_shown = songs_shown;
            songs_shown = !songs_shown && !library_shown;
        }
        if (buttons_pressed & (1u << BTN_HOME)) {   /* (from the songs' or the library's page: home) */
            global_shown = songs_shown || library_shown || !global_shown;
            songs_shown = library_shown = 0;
        }
        if (buttons_pressed & (1u << BTN_SEL)) {   /* SEL and EDIT: one at a time, the one pressed takes over */
            select_on = !select_on;
            if (select_on) edit_on = 0;
        }
        if (buttons_pressed & (1u << BTN_EDIT)) {
            edit_on = !edit_on;
            if (edit_on) select_on = 0;
        }
        keys_edit = !select_on && edit_on;      /* what the keys do: SEL comes first, then EDIT, else they play */
        keys_play = !select_on && !edit_on;
        for (track = 0; track < TRACK_COUNT; track++)
            if (select_on && keys_pressed >> TRACK_KEYS[track] & 1u) {
                shown_track = track;
                global_shown = songs_shown = library_shown = 0;
            }
        page = songs_shown ? PAGE_SONGS : library_shown ? PAGE_LIBRARY : global_shown ? PAGE_GLOBAL : shown_track;
        presets_detents = fm1_enc_take(ENC_PRESETS);   /* the pattern on the screen, on every page, as OCT is */
        shown_pattern = turned_value(shown_pattern, presets_detents, 0, PATTERN_COUNT - 1u);
        if (page != PAGE_SONGS) songs_note = SONGS_NOTE_NONE;
        if (page != PAGE_LIBRARY || presets_detents) library_note = LIBRARY_NOTE_NONE;
        if (page == PAGE_LIBRARY) {             /* the library's page asks for no EDIT either */
            for (step = 0; step < LIBRARY_PLACES; step++)
                if (keys_pressed >> STEP_KEYS[step] & 1u) detents[0] = (int32_t)step - (int32_t)selected_library_place;
            if (detents[0]) {
                selected_library_place = turned_value(selected_library_place, detents[0], 0, LIBRARY_PLACES - 1u);
                library_note = LIBRARY_NOTE_NONE;
            }
            if (buttons_pressed & (1u << BTN_SEQ) && places_with_pattern >> selected_library_place & 1u) {
                if (!pattern_empty(&song.patterns[shown_pattern]) && library_note != LIBRARY_NOTE_FETCH_ASKED)
                    library_note = LIBRARY_NOTE_FETCH_ASKED;
                else {
                    memcpy((void *)&song.patterns[shown_pattern], &library.patterns[selected_library_place], sizeof(pattern_t));
                    library_note = LIBRARY_NOTE_FETCHED;
                    song_changed = 1;
                }
            }
            if (buttons_pressed & (1u << BTN_REC)) {
                if (playing) library_note = LIBRARY_NOTE_STOP_FIRST;
                else if (places_with_pattern >> selected_library_place & 1u && library_note != LIBRARY_NOTE_STORE_ASKED)
                    library_note = LIBRARY_NOTE_STORE_ASKED;
                else {                          /* (an empty pattern stored: the place is empty again) */
                    memcpy(&library.patterns[selected_library_place], (const void *)&song.patterns[shown_pattern], sizeof(pattern_t));
                    if (pattern_empty(&library.patterns[selected_library_place])) places_with_pattern &= ~(1u << selected_library_place);
                    else places_with_pattern |= 1u << selected_library_place;
                    library_unsaved = 1;
                    library_note = LIBRARY_NOTE_SAVING;
                }
            }
            for (knob = 0; knob < KNOB_COUNT; knob++) detents[knob] = 0;
        } else if (page != PAGE_SONGS) {
            if (!edit_on)                       /* nothing changes unless EDIT is on: the knobs, SEQ and REC wait for it */
                for (knob = 0; knob < KNOB_COUNT; knob++) detents[knob] = 0;
            if (edit_on && buttons_pressed & (1u << BTN_SEQ)) chain_on = !chain_on;
        } else {                                /* the songs' page asks for no EDIT: loading and saving over a song ask twice */
            if (detents[0]) {
                selected_place = turned_value(selected_place, detents[0], 0, PLACE_COUNT - 1u);
                songs_note = SONGS_NOTE_NONE;
            }
            if (buttons_pressed & (1u << BTN_SEQ) && places_with_song >> selected_place & 1u) {
                if (playing) songs_note = SONGS_NOTE_STOP_FIRST;
                else if (songs_note != SONGS_NOTE_LOAD_ASKED) songs_note = SONGS_NOTE_LOAD_ASKED;
                else if (!place_load(selected_place)) {   /* the song is another one: home, where its chain shows */
                    chain_on = 0;
                    for (step = 0; step < CHAIN_SLOTS; step++)
                        if (song.chain[step]) chain_on = 1;
                    shown_pattern = shown_bar = 0;
                    songs_shown = 0;
                    global_shown = 1;
                    page = PAGE_GLOBAL;
                    song_changed = 1;
                }
            }
            if (buttons_pressed & (1u << BTN_REC) && selected_place >= PLACE_FIRST_KEPT) {
                if (playing) songs_note = SONGS_NOTE_STOP_FIRST;
                else if (places_with_song >> selected_place & 1u && songs_note != SONGS_NOTE_SAVE_ASKED) songs_note = SONGS_NOTE_SAVE_ASKED;
                else {
                    place_to_save = selected_place;
                    songs_note = SONGS_NOTE_SAVING;
                }
            }
            for (knob = 0; knob < KNOB_COUNT; knob++) detents[knob] = 0;
        }
        if (page == PAGE_GLOBAL) shown_pattern = turned_value(shown_pattern, detents[2], 0, PATTERN_COUNT - 1u);
        if (page == PAGE_GLOBAL && edit_on && buttons_pressed & (1u << BTN_REC) && !pattern_empty(&song.patterns[shown_pattern])) {
            uint32_t index;                     /* a copy goes to the first empty pattern, which is then the one on the screen */
            for (index = 0; index < PATTERN_COUNT; index++)
                if (pattern_empty(&song.patterns[index])) {
                    memcpy((void *)&song.patterns[index], (const void *)&song.patterns[shown_pattern], sizeof(pattern_t));
                    shown_pattern = index;
                    song_changed = 1;
                    break;
                }
        }
        loop_pattern = shown_pattern;
        pattern = &song.patterns[shown_pattern];
        if (buttons_pressed & (1u << BTN_OCT_DOWN)) {   /* OCT- moves when it is let go: held, it takes the bar away */
            oct_down_waits = 1;
            oct_down_ms = fm1_ms;
        }
        if (oct_down_waits && buttons_released & (1u << BTN_OCT_DOWN)) {
            oct_down_waits = 0;
            if (shown_bar > 0u) shown_bar--;
        }
        if (oct_down_waits && edit_on && page < TRACK_COUNT && fm1_ms - oct_down_ms >= HOLD_MS) {
            uint32_t bar;                       /* the track's bars after it move up, its last one is empty; a track keeps one bar */
            oct_down_waits = 0;
            for (bar = shown_bar; bar < BAR_COUNT; bar++) {
                for (drum = 0; page == PAGE_DRUMS && drum < DRUM_COUNT; drum++)
                    pattern->drum_hits[drum][bar] = bar + 1u < BAR_COUNT ? pattern->drum_hits[drum][bar + 1u] : 0u;
                for (step = 0; page != PAGE_DRUMS && step < STEPS_PER_BAR; step++)
                    pattern->notes[page - MELODIC_FIRST_PAGE][bar][step] = bar + 1u < BAR_COUNT ? pattern->notes[page - MELODIC_FIRST_PAGE][bar + 1u][step] : 0u;
            }
            if (pattern->bar_totals[page] > 1u) pattern->bar_totals[page]--;
            roll_drawn = 0;
            song_changed = 1;
        }
        if (buttons_pressed & (1u << BTN_OCT_UP)) {
            if (edit_on && page < TRACK_COUNT && shown_bar + 1u == pattern->bar_totals[page] && pattern->bar_totals[page] < BAR_COUNT) {
                pattern->bar_totals[page]++;    /* EDIT on a track's page: past its last bar is a new one */
                song_changed = 1;
            }
            shown_bar++;
        }
        bar_total = page < TRACK_COUNT ? pattern->bar_totals[page] : pattern_bars(pattern);
        if (shown_bar >= bar_total) shown_bar = bar_total - 1u;   /* the screen never shows a bar that does not play */

        if (page == PAGE_GLOBAL) {
            song_changed |= turn_setting(&song.tempo, detents[0], TEMPO_MIN, TEMPO_MAX);
            frames_per_step_q8 = FRAMES_PER_STEP_Q8(song.tempo);
            song_changed |= turn_setting(&song.reverb, detents[3], 0, REVERB_MAX);
            for (step = 0; step < CHAIN_SLOTS; step++) {
                if (keys_edit && keys_pressed >> STEP_KEYS[step] & 1u) {   /* the pattern on the screen goes there; it is taken away if it was there */
                    song.chain[step] = (uint8_t)(song.chain[step] == shown_pattern + 1u ? 0u : shown_pattern + 1u);
                    chain_drawn = 0;
                    song_changed = 1;
                }
                if (song.chain[step]) step_leds |= 1u << step;
            }
        } else if (page == PAGE_DRUMS) {
            selected_drum = turned_value(selected_drum, detents[0], 0, DRUM_COUNT - 1u);
            for (drum = 0; drum < DRUM_COUNT; drum++)   /* played: the lower keys from the left are the drums, and the one hit is the one EDIT changes */
                if (keys_play && keys_pressed >> STEP_KEYS[drum] & 1u) {
                    selected_drum = drum;
                    drum_auditions[drum]++;
                }
            for (step = 0; step < STEPS_PER_BAR; step++)
                if (keys_edit && keys_pressed >> STEP_KEYS[step] & 1u) {
                    pattern->drum_hits[selected_drum][shown_bar] ^= (uint16_t)(1u << step);
                    song_changed = 1;
                }
            step_leds = pattern->drum_hits[selected_drum][shown_bar];
        } else if (page < TRACK_COUNT) {
            uint32_t note_lowest, note_highest, selected_note, roll_lowest_note, placed_value, chord_rows, rows_above;
            int audition = detents[0] != 0;
            melodic = page - MELODIC_FIRST_PAGE;
            note_lowest = MELODIC[melodic].note_lowest;
            note_highest = MELODIC[melodic].note_highest;
            selected_note = selected_notes[melodic];
            roll_lowest_note = roll_lowest_notes[melodic];
            selected_note = turned_value(selected_note, detents[0], note_lowest, note_highest);
            for (key = 0; key < KEY_COUNT; key++)   /* played: a key's note sounds while it is down, and is the one EDIT places */
                if (keys_play && keys_pressed >> key & 1u) {
                    selected_note = note_lowest + KEYS_FIRST_NOTE_ABOVE_LOWEST + key;
                    held_key = key;
                    audition = 1;
                }
            song_changed |= turn_setting(&song.cutoff[melodic], detents[1], 0, SYNTH_PARAMETER_MAX);
            if (melodic == MELODIC_PAD) {
                if (detents[2]) {
                    selected_major = turned_value(selected_major, detents[2], 0, 1);
                    audition = 1;
                }
            } else
                song_changed |= turn_setting(&song.decay[melodic], detents[2], 0, SYNTH_PARAMETER_MAX);
            placed_value = selected_note | (melodic == MELODIC_PAD && selected_major ? NOTE_MAJOR : 0u);
            if (audition) {
                audition_notes[melodic] = placed_value;
                note_auditions[melodic]++;
            }
            /* the roll follows the selected note, two rows ahead of it while there are notes that way; the pad's
             * roll keeps the whole chord above it in view */
            chord_rows = melodic == MELODIC_PAD ? PAD_FIFTH : 0u;
            rows_above = chord_rows > ROLL_MARGIN_ROWS ? chord_rows : ROLL_MARGIN_ROWS;
            if (selected_note < roll_lowest_note + ROLL_MARGIN_ROWS)
                roll_lowest_note = selected_note < note_lowest + ROLL_MARGIN_ROWS ? note_lowest : selected_note - ROLL_MARGIN_ROWS;
            if (selected_note + rows_above >= roll_lowest_note + ROLL_ROWS)
                roll_lowest_note = (selected_note + rows_above > note_highest + chord_rows ? note_highest + chord_rows : selected_note + rows_above) - ROLL_ROWS + 1u;
            selected_notes[melodic] = selected_note;
            roll_lowest_notes[melodic] = roll_lowest_note;
            song_changed |= turn_setting(&song.echo[melodic], detents[3], 0, ECHO_SEND_MAX);
            for (step = 0; step < STEPS_PER_BAR; step++) {
                if (keys_edit && keys_pressed >> STEP_KEYS[step] & 1u) {   /* what the knobs show goes there; it is taken away if it was there */
                    pattern->notes[melodic][shown_bar][step] = (uint8_t)(pattern->notes[melodic][shown_bar][step] == placed_value ? 0u : placed_value);
                    roll_drawn = 0;
                    song_changed = 1;
                }
                if (pattern->notes[melodic][shown_bar][step]) step_leds |= 1u << step;
            }
        }
        for (track = 0; track < MELODIC_COUNT; track++)
            note_held[track] = keys_play && page == MELODIC_FIRST_PAGE + track && (keys >> held_key & 1u);
        if (song_changed) {
            unsaved = 1;
            changed_ms = fm1_ms;
        }

        step_now = playing_step;                /* (read once: the ISR moves it) */
        marked_bar = playing && playing_pattern == shown_pattern ? step_now / STEPS_PER_BAR % bar_total : NO_BAR;   /* (a short track goes round) */
        marked_step = marked_bar == shown_bar && page != PAGE_GLOBAL ? step_now % STEPS_PER_BAR : NO_STEP;
        marked_slot = playing ? playing_slot : CHAIN_SLOTS;
        /* the keys: one that does something now glows, one that is on or selected is lit */
        if (select_on) {                        /* the track keys, the shown track's lit */
            for (track = 0; track < TRACK_COUNT; track++)
                keys_glowing |= 1u << TRACK_KEYS[track];
            if (!global_shown) keys_lit = 1u << TRACK_KEYS[shown_track];
        } else if (page == PAGE_SONGS) {        /* no key does anything */
        } else if (page == PAGE_LIBRARY) {      /* the lower keys are the places, lit where there is a pattern */
            for (step = 0; step < LIBRARY_PLACES; step++) {
                keys_glowing |= 1u << STEP_KEYS[step];
                if (places_with_pattern >> step & 1u) keys_lit |= 1u << STEP_KEYS[step];
            }
        } else if (edit_on) {                   /* the lower keys, lit where there is something; what runs (the step, the chain's place) flips its key */
            for (step = 0; step < STEPS_PER_BAR; step++) {
                if (page < TRACK_COUNT && step % 4u == 0u) keys_glowing_bright |= 1u << STEP_KEYS[step];   /* a beat */
                else keys_glowing |= 1u << STEP_KEYS[step];
                if ((step_leds >> step & 1u) ^ (step == (page == PAGE_GLOBAL ? marked_slot : marked_step)))
                    keys_lit |= 1u << STEP_KEYS[step];
            }
        } else if (page == PAGE_DRUMS) {        /* a key per drum, the selected one's lit */
            for (drum = 0; drum < DRUM_COUNT; drum++)
                keys_glowing |= 1u << STEP_KEYS[drum];
            keys_lit = 1u << STEP_KEYS[selected_drum];
        } else if (page != PAGE_GLOBAL) {       /* the whole keyboard, the selected note's key lit */
            uint32_t first_note = MELODIC[melodic].note_lowest + KEYS_FIRST_NOTE_ABOVE_LOWEST;
            keys_glowing = (1u << KEY_COUNT) - 1u;
            if (selected_notes[melodic] >= first_note && selected_notes[melodic] < first_note + KEY_COUNT)
                keys_lit = 1u << (selected_notes[melodic] - first_note);
        }
        for (key = 0; key < KEY_COUNT; key++) {
            fm1_led_key_glow(KEY_LED(key), keys_glowing >> key & 1u);
            fm1_led_key_glow_bright(KEY_LED(key), keys_glowing_bright >> key & 1u);
            fm1_led_key(KEY_LED(key), keys_lit >> key & 1u);
        }
        fm1_led_key(BTN_PLAY, playing && step_now % 4u == 0u);   /* playing: lit on each beat's first step, the beat */
        fm1_led_key(BTN_HOME, page == PAGE_GLOBAL);
        fm1_led_key(BTN_SAVE, page == PAGE_SONGS || page == PAGE_LIBRARY);
        fm1_led_key(BTN_SEQ, chain_on != 0);
        fm1_led_key_glow(BTN_SEQ, page == PAGE_SONGS ? places_with_song >> selected_place & 1u
                                  : page == PAGE_LIBRARY ? places_with_pattern >> selected_library_place & 1u : edit_on != 0);
        fm1_led_key(BTN_SEL, select_on != 0);
        fm1_led_key(BTN_EDIT, edit_on != 0);
        fm1_led_key_glow(BTN_OCT_DOWN, shown_bar > 0u);   /* there is a bar that way */
        fm1_led_key_glow(BTN_OCT_UP, shown_bar + 1u < bar_total || (edit_on && page < TRACK_COUNT && bar_total < BAR_COUNT));
        fm1_led_key_glow(BTN_REC, page == PAGE_SONGS ? selected_place >= PLACE_FIRST_KEPT : page == PAGE_LIBRARY || (page == PAGE_GLOBAL && edit_on));

        /* the screen: only what changed. Each *_drawn holds what is on the screen (+ 1, so 0 means "draw it") */
        if (page != page_drawn) {
            page_drawn = page;
            lcd_fill(0, BODY_TOP, 240, BODY_HEIGHT, C_BLACK);
            header_drawn = roll_drawn = roll_marker_drawn = chain_drawn = songs_drawn = library_drawn = 0;
            for (drum = 0; drum < DRUM_COUNT; drum++) drum_row_drawn[drum] = 0;
            for (knob = 0; knob < KNOB_COUNT; knob++) knob_drawn[knob] = 0;
        }
        state = 1u + (shown_bar | bar_total << 4 | marked_bar << 8 | shown_pattern << 12 | edit_on << 16);
        if (state != header_drawn) {
            header_drawn = state;
            draw_header(page, shown_pattern, bar_total, shown_bar, marked_bar, edit_on != 0);
        }
        if (page == PAGE_GLOBAL) {
            state = 1u + (shown_pattern | marked_slot << 4 | (uint32_t)(chain_on != 0) << 12);   /* (a changed place sets chain_drawn to 0) */
            if (state != chain_drawn) {
                chain_drawn = state;
                draw_chain(shown_pattern, marked_slot, chain_on != 0);
            }
        } else if (page == PAGE_DRUMS) {
            for (drum = 0; drum < DRUM_COUNT; drum++) {
                uint32_t hits = pattern->drum_hits[drum][shown_bar];
                state = 1u + (hits | marked_step << 16 | (uint32_t)(drum == selected_drum) << 24);
                if (state != drum_row_drawn[drum]) {
                    drum_row_drawn[drum] = state;
                    draw_drum_row(drum, hits, drum == selected_drum, marked_step);
                }
            }
        } else if (page == PAGE_SONGS) {
            state = 1u + (selected_place | songs_note << 4 | places_with_song << 8);
            if (state != songs_drawn) {
                songs_drawn = state;
                draw_songs(selected_place, places_with_song, songs_note);
            }
        } else if (page == PAGE_LIBRARY) {
            state = 1u + (selected_library_place | library_note << 4 | shown_pattern << 8 | places_with_pattern << 12);
            if (state != library_drawn) {
                library_drawn = state;
                draw_library(selected_library_place, places_with_pattern, shown_pattern, library_note);
            }
        } else {
            state = 1u + (shown_bar | selected_notes[melodic] << 4 | roll_lowest_notes[melodic] << 12 | shown_pattern << 20);   /* (a changed step sets roll_drawn to 0) */
            if (state != roll_drawn) {
                roll_drawn = state;
                draw_roll(pattern, melodic, shown_bar, selected_notes[melodic], roll_lowest_notes[melodic]);
            }
            if (1u + marked_step != roll_marker_drawn) {
                roll_marker_drawn = 1u + marked_step;
                draw_roll_marker(marked_step);
            }
        }
        for (knob = 0; knob < KNOB_COUNT; knob++) {
            static const char *const MELODIC_LABELS[KNOB_COUNT] = {"NOTE", "CUTOFF", "DECAY", "ECHO"};
            static const char *const PAD_LABELS[KNOB_COUNT] = {"NOTE", "CUTOFF", "CHORD", "ECHO"};
            static const char *const GLOBAL_LABELS[KNOB_COUNT] = {"TEMPO", "", "PATTERN", "REVERB"};
            static const char *const SONGS_LABELS[KNOB_COUNT] = {"SONG", "", "", ""};
            static const char *const LIBRARY_LABELS[KNOB_COUNT] = {"PLACE", "", "", ""};
            const int pad_shown = page == MELODIC_FIRST_PAGE + MELODIC_PAD;
            const uint32_t melodic_values[KNOB_COUNT] = {selected_notes[melodic], song.cutoff[melodic],
                                                         pad_shown ? selected_major : song.decay[melodic], song.echo[melodic]};
            const uint32_t global_values[KNOB_COUNT] = {song.tempo, 0u, shown_pattern, song.reverb};
            const char *label = page == PAGE_SONGS ? SONGS_LABELS[knob] : page == PAGE_LIBRARY ? LIBRARY_LABELS[knob] : page == PAGE_GLOBAL ? GLOBAL_LABELS[knob] : page == PAGE_DRUMS ? ""   /* (the selected drum's row is orange: that says it) */
                              : pad_shown ? PAD_LABELS[knob] : MELODIC_LABELS[knob];
            uint32_t value = page == PAGE_SONGS ? (knob ? 0u : selected_place) : page == PAGE_LIBRARY ? (knob ? 0u : selected_library_place + 1u) : page == PAGE_GLOBAL ? global_values[knob]
                           : page == PAGE_DRUMS ? 0u : melodic_values[knob];
            if (1u + value != knob_drawn[knob]) {
                char number[8];
                const char *text = number;
                if (!label[0]) text = "";
                else if (page == PAGE_SONGS) text = PLACE_NAMES[value];
                else if (page == PAGE_LIBRARY) format_number(number, value);
                else if (page != PAGE_GLOBAL && knob == 0u) format_note(number, value);
                else if (pad_shown && knob == 2u) text = CHORD_NAMES[value];
                else if (page == PAGE_GLOBAL && knob == 2u) pattern_name(number, value);
                else format_number(number, value);
                knob_drawn[knob] = 1u + value;
                draw_knob(knob, label, text, page == PAGE_SONGS ? value >= PLACE_FIRST_KEPT : page != PAGE_DRUMS);
            }
        }

        if (place_to_save < PLACE_COUNT && !playing && sound_quiet()) {   /* (as below: only when stopped and silent) */
            if (!blob_save(place_to_save - PLACE_FIRST_KEPT, (const void *)&song, sizeof song)) {
                places_with_song |= 1u << place_to_save;
                if (songs_note == SONGS_NOTE_SAVING) songs_note = SONGS_NOTE_SAVED;
            }
            place_to_save = PLACE_COUNT;
        }
        if (library_unsaved && !playing && sound_quiet()) {   /* (a slot: four sectors erased, ~200 ms) */
            library_unsaved = 0;
            if (!slot_save(0, &library, sizeof library) && library_note == LIBRARY_NOTE_SAVING) library_note = LIBRARY_NOTE_STORED;
        }
        if (unsaved && !playing && sound_quiet() && fm1_ms - changed_ms > 2000u) {   /* an erase stalls the sound: only when stopped and silent */
            settings_save((const void *)&song, sizeof song);
            unsaved = 0;
        }
    }
}
