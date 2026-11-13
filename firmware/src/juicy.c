/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy: a groovebox for the M-VAVE FM-1, built on Dinghy (platform.c and everything below it: updates, the USB
 * rescue, the keys, knobs, LEDs, the screen, flash storage). Five tracks, DRUMS (six of them: KICK, HAT, CLAP,
 * OPEN HAT, SNARE, TOM), BASS, ARP, PAD and LEAD, in eight patterns (A..H) in which each track has up to four
 * bars of 16 steps of its own (a pattern lasts as long as its longest track, the shorter ones go round), a chain that plays patterns one after the other, an echo
 * three steps long that all but the drums can send into, a reverb for ARP, PAD, LEAD and the echo, and a
 * limiter over all of it.
 *
 *   the keys     play: on DRUMS the lower keys from the left are the six drums, on the other tracks the keys are
 *                notes (the pad's: the chord on that note; keys outside the track's notes are dark). Nothing of it
 *                goes into the pattern, but what was played last is what EDIT puts there
 *   SEL          (lit) OP1 .. OP5, the first five upper keys, pick the track on the screen: DRUMS, BASS,
 *                ARP, PAD, LEAD. SEL again: the keys play. SEL and EDIT are never on together: the one
 *                pressed turns the other off. The five black keys an octave above OP1 .. OP5 mute and unmute
 *                those tracks (lit: it plays): what the pattern holds is not heard, the keys still are. Mutes are
 *                not saved
 *   EDIT         (lit, the page's name orange) the lower keys are the 16 steps of the bar on the screen. DRUMS: a
 *                press puts a hit of the selected drum there or takes it away. The other tracks: a press on a
 *                note (or on a long note anywhere) takes it away; on an empty step it puts the next note there (what
 *                was played or what the knobs show; the pad's: that chord), it sounds, and the cursor goes there.
 *                On a track's notes the knobs show the note under the cursor and change it (on an empty step: the
 *                next note). On BASS, ARP, LEAD a note lasts LENGTH (KNOB4): 10 .. 90 % of a step, or 1 .. 16
 *                steps (to its bar's end at most). ARP's steps are hits, not notes (placed silently, as a drum's):
 *                each plays the next note of the chord the pad sounds (none: silent) in the order of MODE (KNOB1,
 *                the pattern's: UP, DOWN, UP/DN or RAND over 1 .. 3 octaves); a chord on the pad starts it over
 *   FX           the track's pages in turn: its notes, its sound (not DRUMS), its mix. The sound page: The knobs shape the sound (WAVE, RESO,
 *                ATTACK, ENV; the pad's SPREAD, RESO, ATTACK, RELEASE), ALGORITHM picks a named one or USER1 /
 *                USER2 (the player's own, there for every song), SEQ keeps the sound as USER1, REC as USER2
 *                (each asks twice); all of it with EDIT on. The keys play here, EDIT or not. The mix page (EDIT on):
 *                LEVEL (DRUMS: the selected drum's), PAN, REVERB and ECHO, how much goes into those; the keys play
 *   KNOB1..4     (with EDIT on, as SEQ and REC) what the four places along the bottom of the screen say
 *   SELECT       on DRUMS, EDIT or not: the drum the keys edit, the orange row (DRUMS shows three drums at a time:
 *                the page of the selected one, two marks at the bottom say which). On a track's notes, EDIT or not:
 *                the cursor, a step framed in the roll (with EDIT its key blinks), on through the track's bars;
 *                stopped, the note it comes to sounds (not ARP's hits)
 *   OCT- OCT+    the bar on the screen: the one before, the next one (the cursor goes along). With EDIT on, on a
 *                track's page, OCT+ past the track's last bar gives it one more (up to four), and OCT- held takes
 *                the bar on the screen out of the track (its only bar: emptied). Recording: the keys an octave
 *                down or up (lit: they are)
 *   PRESETS      the pattern on the screen, A .. H, on every page
 *   HOME         the page for the whole song: TEMPO, REVERB, the SWING of the pattern on the screen (50 straight .. 80 %),
 *                and the chain, where with EDIT on the lower keys are its 16 places (HOME again: back to the track)
 *   REC          on HOME's page: a copy of the pattern on the screen, in the first empty pattern (when REC is let go).
 *                On a track's notes, with EDIT on: recording on or off (REC lit). Recording, the keys play, and a
 *                key puts its note (ARP: a hit) at the cursor, which moves on by its LENGTH (the pad's: a bar); SELECT moves it
 *                over a rest. Held, with EDIT on: empties what is on the screen in the pattern on the screen, its bars stay: on HOME's
 *                page the whole pattern, on DRUMS the selected drum, on a track's notes page that track
 *   SAVE         the page of the songs: KNOB1 picks one (three demo songs, an empty one, or one of eight kept in
 *                flash), SEQ twice loads it, REC saves the song there (twice where there is one already). This
 *                page needs no EDIT, and nothing of it works while the song plays. SAVE again: the library, 16
 *                places for patterns of any song (their notes, hits and swing, not the sounds): KNOB1 or a lower key picks
 *                one, SEQ puts it into the pattern on the screen (twice where that holds something; this works
 *                while it plays), REC stores the pattern on the screen there (twice where there is one already)
 *   SEQ          what plays: the chain (lit), or the pattern on the screen, round and round
 *   PLAY         start and stop
 *   MIDI (USB, the TRS jack)  a clock coming in: Juicy follows its tempo (HOME's page says EXT) and START, CONTINUE,
 *                STOP. Without one Juicy is the master: PLAY sends START / STOP, and the clock goes out while it plays
 *
 * At power-on: the start screen, the orange big and the name, while a light runs over the lower keys from left to right
 * (2 s, or until a key, a button or a knob is touched: that does what it always does).
 *
 * The lights: a key or button that does something here glows dimly (a bar's four beats a bit brighter); it is fully lit when it is on, selected, or
 * holds something (a step that is set, a chain place with a pattern). The others are dark. On HOME's page, neither
 * SEL nor EDIT on, the song playing: the lower key of the step that sounds is lit, and OP1 .. OP4 show the bars of
 * the pattern that plays (glowing), the one that sounds lit.
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. tests/host.c and tests/web.c run this
 * file on a computer. */
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

/* ---- the melodic tracks: BASS, ARP and LEAD are each one synth voice (synth.c) whose shape is the track's
 * sound (below), PAD is the pad (pad.c), where a note is the root of a chord. ARP's steps hold no notes: a hit
 * plays the next note of the chord the pad sounds, in the order of the pattern's MODE */
enum { MELODIC_BASS, MELODIC_ARP, MELODIC_PAD, MELODIC_LEAD, MELODIC_COUNT };
#define NOTE_MASK 0x7Fu                         /* a step of a melodic track: the MIDI note, 0 = none ... */
#define NOTE_MAJOR 0x80u                        /* ... and on the pad's track: the chord is major, not minor */
#define NOTE_TIE 0x80u                          /* ... and on BASS, ARP, LEAD: the note before goes on through this step (the same note) */
#define ARP_HIT 1u                              /* ARP's step: a hit starts there (its note comes from the chord) */
#define ARP_OCTAVES_MAX 3u                      /* MODE: an order (ARP_ORDER_*) over 1 .. 3 octaves, order * 3 + octaves - 1 */
enum { ARP_ORDER_UP, ARP_ORDER_DOWN, ARP_ORDER_UP_DOWN, ARP_ORDER_RANDOM, ARP_ORDER_COUNT };
#define ARP_MODE_MAX (ARP_ORDER_COUNT * ARP_OCTAVES_MAX - 1u)
#define ARP_MODE(order, octaves) ((order) * ARP_OCTAVES_MAX + (octaves) - 1u)
static const char *const ARP_MODE_NAMES[ARP_MODE_MAX + 1u] = {"UP 1", "UP 2", "UP 3", "DOWN 1", "DOWN 2", "DOWN 3",
                                                             "UP/DN 1", "UP/DN 2", "UP/DN 3", "RAND 1", "RAND 2", "RAND 3"};
#define GATE_TENTHS_MAX 9u                      /* a note shorter than its step: 1 .. 9 tenths of it; 0: the whole step */
#define LENGTH_WHOLE_FIRST (GATE_TENTHS_MAX + 1u)   /* LENGTH (KNOB4): 1 .. 9 are 10 .. 90 % of a step, from here on 1 .. 16 steps */
#define LENGTH_MAX (GATE_TENTHS_MAX + STEPS_PER_BAR)   /* (a note ends with its bar) */

static const struct {
    int32_t gains[SYNTH_WAVE_COUNT];            /* Q15, a wave each (SYNTH_WAVE_*): as loud as one another */
    uint32_t release_rate;                      /* a note let go (synth_shape_t) */
    uint8_t note_lowest, note_highest;          /* MIDI */
} MELODIC[MELODIC_COUNT] = {
    {{12000, 6924, 7344, 16968, 12000}, SYNTH_RELEASE_RATE, 24, 60},   /* BASS: C1..C4 */
    {{13480, 7778, 8250, 19061, 13480}, SYNTH_RELEASE_RATE, 48, 84},   /* ARP: C3..C6 */
    {{0}, 0, 36, 72},                           /* PAD: no synth voice; the chord's root C2..C5 */
    {{5658, 3264, 3463, 8000, 5658}, 0, 48, 84},   /* LEAD: a note rings on past its step, C3..C6 */
};
static synth_voice_t melodic_voices[MELODIC_COUNT];   /* (the pad's is not used) */

/* ---- the song: what plays, what is saved. The main loop writes it, the audio ISR reads it. */
#define STEPS_PER_BAR 16u
#define BAR_COUNT 4u
#define PATTERN_COUNT 8u
#define CHAIN_SLOTS 16u                         /* the chain's places: one per lower key */
#define TEMPO_MIN 60u
#define TEMPO_MAX 200u
#define ECHO_SEND_MAX 100u
#define MIX_MAX 100u                            /* LEVEL, PAN (50: the middle), REVERB 0..100 */
#define REVERB_MAX 100u
#define SWING_MAX 30u                           /* a pattern's swing over straight (50 %): 30 is 80 %, the second step 0.6 of a step late */
#define ECHO_STEPS 3u                           /* the echo comes back a dotted eighth later */
#define SOUND_KNOBS 4u                          /* a sound's page: BASS, ARP, LEAD: WAVE, RESO, ATTACK, ENV; PAD: SPREAD, RESO, ATTACK, RELEASE */

typedef struct {
    uint16_t drum_hits[DRUM_COUNT][BAR_COUNT];  /* bit s: a hit on step s of that bar */
    uint8_t notes[MELODIC_COUNT][BAR_COUNT][STEPS_PER_BAR];   /* what is on that step: NOTE_MASK, NOTE_MAJOR, NOTE_TIE */
    uint8_t bar_totals[1u + MELODIC_COUNT];     /* how many bars a track has, 1..BAR_COUNT: the drums, then the melodic tracks */
    uint8_t swing;                              /* how late every second step sounds, 0..SWING_MAX: SWING 50..80 % */
    uint8_t gates[MELODIC_COUNT][BAR_COUNT][STEPS_PER_BAR / 2u];   /* a step's note shorter than the step: 4 bits a step (the even step low), 0..GATE_TENTHS_MAX */
    uint8_t arp_mode;                           /* ARP's MODE, 0..ARP_MODE_MAX */
} pattern_t;
#define PATTERN_SIZE_BEFORE_GATES 310u          /* a pattern kept before there were gates: all of it up to them */
#define PATTERN_SIZE_BEFORE_ARP 438u            /* ... and before there was ARP (its track was SEQUENCE): all of it up to MODE */
_Static_assert(offsetof(pattern_t, gates) == PATTERN_SIZE_BEFORE_GATES && offsetof(pattern_t, arp_mode) == PATTERN_SIZE_BEFORE_ARP
               && sizeof(pattern_t) == 440u, "what is new comes after what was there");
#define PATTERN_SIZES_COUNT 3u
static const uint32_t PATTERN_SIZES[PATTERN_SIZES_COUNT] = {sizeof(pattern_t), PATTERN_SIZE_BEFORE_ARP, PATTERN_SIZE_BEFORE_GATES};   /* now, then as kept before */

static uint32_t note_gate(const volatile uint8_t *bar_gates, uint32_t step)   /* bar_gates: a track's bar in gates */
{
    return bar_gates[step / 2u] >> (step % 2u * 4u) & 0xFu;
}

static void set_note_gate(volatile uint8_t *bar_gates, uint32_t step, uint32_t gate)
{
    const uint32_t shift = step % 2u * 4u;
    bar_gates[step / 2u] = (uint8_t)((bar_gates[step / 2u] & ~(0xFu << shift)) | gate << shift);
}

static void bytes_move_up(void *to, const void *from, uint32_t count)   /* to at or after from: the last byte first (libc.c has no memmove) */
{
    uint8_t *destination = to;
    const uint8_t *source = from;
    while (count-- > 0u) destination[count] = source[count];
}

/* patterns kept in an older size (kept_size), read into the start of where count of them go: each moves to its
 * place (the last first, so none is overwritten before it moved), what is new after it zero (the notes whole steps,
 * MODE UP 1). ARP's track was SEQUENCE then, with notes of its own: they go */
static void patterns_grow(pattern_t *patterns, uint32_t count, uint32_t kept_size)
{
    while (count-- > 0u) {
        bytes_move_up(&patterns[count], (const uint8_t *)patterns + count * kept_size, kept_size);
        memset((uint8_t *)&patterns[count] + kept_size, 0, sizeof(pattern_t) - kept_size);
        memset(patterns[count].notes[MELODIC_ARP], 0, sizeof patterns[count].notes[MELODIC_ARP]);
        memset(patterns[count].gates[MELODIC_ARP], 0, sizeof patterns[count].gates[MELODIC_ARP]);
    }
}

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
    uint8_t drum_echo;                          /* the same for the drums */
    uint8_t levels[MELODIC_COUNT];              /* the melodic tracks' loudness, 0..MIX_MAX (100: as it was made) */
    uint8_t drum_levels[DRUM_COUNT];            /* each drum's */
    uint8_t pans[1u + MELODIC_COUNT];           /* the drums, then the melodic tracks: 0 left .. 50 the middle .. 100 right */
    uint8_t reverb_sends[1u + MELODIC_COUNT];   /* how much of each goes into the reverb, 0..MIX_MAX */
    uint8_t tempo;
    uint8_t reverb;                             /* how much of ARP, PAD, LEAD and the echo goes into the reverb, 0..REVERB_MAX */
    uint8_t sounds[MELODIC_COUNT][SOUND_KNOBS]; /* each track's sound page (with its CUTOFF and DECAY above: its sound) */
    uint8_t sound_choices[MELODIC_COUNT];       /* the sound it came from: a preset, then USER1, USER2 */
} song_t;
#define SONG_SIZE_KEPT(pattern_size) (sizeof(song_t) - PATTERN_COUNT * (sizeof(pattern_t) - (pattern_size)))   /* a song kept with patterns that size */
_Static_assert(offsetof(song_t, patterns) == 0u, "the patterns first: a song kept with smaller ones grows in place");

static void song_grow(song_t *grown, uint32_t kept_pattern_size)   /* a song kept with patterns of an older size, read into its start */
{
    bytes_move_up(&grown->chain, (const uint8_t *)grown + PATTERN_COUNT * kept_pattern_size, sizeof(song_t) - offsetof(song_t, chain));
    patterns_grow(grown->patterns, PATTERN_COUNT, kept_pattern_size);
}

static volatile song_t song;

/* ---- the sounds of the melodic tracks. A sound is the four knobs of a track's sound page and its CUTOFF and
 * DECAY. Each track has a few named ones and two of the player's own (USER1, USER2: in flash, slot 1, for every
 * song). The song keeps each track's sound as it is, and which one it came from. */
#define PRESETS_MAX 4u
#define USER_SOUNDS 2u
#define USER_SOUNDS_SLOT 1u                     /* (the library's is 0) */

typedef struct { uint8_t knobs[SOUND_KNOBS]; uint8_t cutoff, decay; } sound_t;

static const struct {
    uint32_t count;
    const char *names[PRESETS_MAX];
    sound_t sounds[PRESETS_MAX];
} PRESETS[MELODIC_COUNT] = {
    {4, {"PLUCK", "SUB", "ACID", "REESE"},       /* BASS (PLUCK: the first song's) */
     {{{SYNTH_WAVE_SAW, 50, 0, 32}, 45, 35}, {{SYNTH_WAVE_SOFT, 0, 0, 0}, 30, 70},
      {{SYNTH_WAVE_SAW, 92, 0, 70}, 15, 30}, {{SYNTH_WAVE_SAWS, 30, 0, 10}, 40, 80}}},
    {4, {"PLUCK", "SQUARE", "SAW", "SOFT"},      /* ARP */
     {{{SYNTH_WAVE_PULSE, 50, 0, 64}, 50, 25}, {{SYNTH_WAVE_SQUARE, 50, 0, 40}, 45, 30},
      {{SYNTH_WAVE_SAW, 60, 0, 50}, 40, 25}, {{SYNTH_WAVE_SOFT, 20, 30, 10}, 60, 45}}},
    {3, {"WARM", "BRIGHT", "SLOW"},              /* PAD: SPREAD, RESO, ATTACK, RELEASE; no DECAY */
     {{{64, 0, 51, 72}, 50, 0}, {{90, 45, 40, 72}, 70, 0}, {{64, 0, 85, 90}, 45, 0}}},
    {3, {"SUPERSAW", "SQUARE", "SOFT"},          /* LEAD */
     {{{SYNTH_WAVE_SAWS, 50, 0, 32}, 65, 55}, {{SYNTH_WAVE_SQUARE, 40, 0, 30}, 60, 55},
      {{SYNTH_WAVE_SOFT, 20, 45, 10}, 70, 70}}},
};
static const char *const USER_NAMES[USER_SOUNDS] = {"USER1", "USER2"};
static sound_t user_sounds[MELODIC_COUNT][USER_SOUNDS];   /* (the main loop's) */

static uint32_t sound_count(uint32_t melodic) { return PRESETS[melodic].count + USER_SOUNDS; }

static const char *sound_name(uint32_t melodic, uint32_t choice)
{
    return choice < PRESETS[melodic].count ? PRESETS[melodic].names[choice] : USER_NAMES[choice - PRESETS[melodic].count];
}

static const sound_t *sound_source(uint32_t melodic, uint32_t choice)
{
    return choice < PRESETS[melodic].count ? &PRESETS[melodic].sounds[choice] : &user_sounds[melodic][choice - PRESETS[melodic].count];
}

static uint32_t sound_knob_max(uint32_t melodic, uint32_t knob)   /* WAVE is one of the waves, the rest 0..100 */
{
    return melodic != MELODIC_PAD && knob == 0u ? SYNTH_WAVE_COUNT - 1u : SYNTH_PARAMETER_MAX;
}

static void sound_apply(uint32_t melodic, uint32_t choice)   /* the track's sound is that one now */
{
    const sound_t *source = sound_source(melodic, choice);
    uint32_t knob;
    for (knob = 0; knob < SOUND_KNOBS; knob++) song.sounds[melodic][knob] = source->knobs[knob];
    song.cutoff[melodic] = source->cutoff;
    song.decay[melodic] = source->decay;
    song.sound_choices[melodic] = (uint8_t)choice;
}

static void sound_of_track(uint32_t melodic, sound_t *sound)   /* the track's sound as it is now */
{
    uint32_t knob;
    for (knob = 0; knob < SOUND_KNOBS; knob++) sound->knobs[knob] = song.sounds[melodic][knob];
    sound->cutoff = song.cutoff[melodic];
    sound->decay = song.decay[melodic];
}

static int sound_edited(uint32_t melodic)       /* it is not the one it came from any more */
{
    sound_t now;
    sound_of_track(melodic, &now);
    return memcmp(&now, sound_source(melodic, song.sound_choices[melodic]), sizeof now) != 0;
}

static void user_sounds_load(void)              /* nothing kept, or something broken: both are the first named sound */
{
    uint32_t melodic, place, knob, valid = !slot_load(USER_SOUNDS_SLOT, user_sounds, sizeof user_sounds);
    for (melodic = 0; valid && melodic < MELODIC_COUNT; melodic++)
        for (place = 0; place < USER_SOUNDS; place++) {
            for (knob = 0; knob < SOUND_KNOBS; knob++)
                if (user_sounds[melodic][place].knobs[knob] > sound_knob_max(melodic, knob)) valid = 0;
            if (user_sounds[melodic][place].cutoff > SYNTH_PARAMETER_MAX || user_sounds[melodic][place].decay > SYNTH_PARAMETER_MAX) valid = 0;
        }
    if (valid) return;
    for (melodic = 0; melodic < MELODIC_COUNT; melodic++)
        for (place = 0; place < USER_SOUNDS; place++)
            user_sounds[melodic][place] = PRESETS[melodic].sounds[0];
}

static void melodic_shape(uint32_t melodic, synth_shape_t *shape)   /* (the audio ISR's) the synth as the sound says */
{
    const volatile uint8_t *knobs = song.sounds[melodic];
    shape->wave = knobs[0];
    shape->gain = MELODIC[melodic].gains[knobs[0]];
    shape->damping = SYNTH_DAMPING[knobs[1]];
    shape->attack_rate = SYNTH_ATTACK_RATE[knobs[2]];
    shape->envelope_amount = knobs[3];
    shape->release_rate = MELODIC[melodic].release_rate;
}

static void melodic_note_on(uint32_t melodic, uint32_t value)   /* value: as in song.notes */
{
    if (melodic == MELODIC_PAD) pad_chord_on(value & NOTE_MASK, (value & NOTE_MAJOR) != 0u, song.sounds[MELODIC_PAD][0]);
    else {
        synth_shape_t shape;
        melodic_shape(melodic, &shape);
        synth_note_on(&melodic_voices[melodic], &shape, value);
    }
}

static void melodic_note_off(uint32_t melodic)
{
    if (melodic == MELODIC_PAD) pad_chord_off();
    else synth_note_off(&melodic_voices[melodic]);
}

/* a song with nothing in it but the sounds of the tracks (each its first named one), the tempo and the reverb */
static void empty_song(void)
{
    uint32_t index, track;
    memset((void *)&song, 0, sizeof song);
    for (index = 0; index < PATTERN_COUNT; index++)
        for (track = 0; track < sizeof song.patterns[index].bar_totals; track++)
            song.patterns[index].bar_totals[track] = 1;
    for (track = 0; track < MELODIC_COUNT; track++)
        sound_apply(track, 0);
    for (track = 0; track < MELODIC_COUNT; track++) song.levels[track] = MIX_MAX;   /* the mix as it was before there was one */
    for (track = 0; track < DRUM_COUNT; track++) song.drum_levels[track] = MIX_MAX;
    for (track = 0; track < 1u + MELODIC_COUNT; track++) song.pans[track] = MIX_MAX / 2u;
    song.reverb_sends[1u + MELODIC_ARP] = song.reverb_sends[1u + MELODIC_PAD] = song.reverb_sends[1u + MELODIC_LEAD] = MIX_MAX;
    song.echo[MELODIC_ARP] = 40;
    song.echo[MELODIC_LEAD] = 50;
    song.tempo = 138;
    song.reverb = 35;
}

/* The first song, in A minor: four bars over the chords Am F C G, in five patterns that differ in which tracks
 * play, and a chain that tells them as a song.
 *   A  everything              B  A without the lead         C  the intro: pad and arpeggio alone
 *   D  C with kick and hat     E  the break: pad and lead alone
 * The drums are one bar that goes round under the four of the others.
 * The chain: C D B A B A E D A A C. Patterns F, G and H are empty. */
static void first_song(void)
{
    enum { KICK = 1, HAT = 2, CLAP = 4, BASS = 8, ARP = 16, PAD = 32, LEAD = 64 };
    static const uint8_t PARTS[5] = {KICK | HAT | CLAP | BASS | ARP | PAD | LEAD, KICK | HAT | CLAP | BASS | ARP | PAD,
                                     ARP | PAD, KICK | HAT | ARP | PAD, PAD | LEAD};
    static const uint8_t CHAIN[11] = {3, 4, 2, 1, 2, 1, 5, 4, 1, 1, 3};   /* (as in song.chain: pattern + 1) */
    static const uint8_t BASS_NOTES[BAR_COUNT] = {33, 29, 36, 31};   /* A1 F1 C2 G1 */
    static const uint8_t PAD_CHORDS[BAR_COUNT] = {57, 53 | NOTE_MAJOR, 60 | NOTE_MAJOR, 55 | NOTE_MAJOR};   /* A3 minor, F3, C4, G3 major */
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
        pattern->arp_mode = ARP_MODE(ARP_ORDER_UP_DOWN, 1u);   /* the chord up and down, a beat each way */
        for (bar = 0; bar < BAR_COUNT; bar++) {
            if (parts & BASS)
                for (step = 2; step < STEPS_PER_BAR; step += 4u)             /* with the hat */
                    pattern->notes[MELODIC_BASS][bar][step] = BASS_NOTES[bar];
            if (parts & ARP)
                for (step = 0; step < STEPS_PER_BAR; step++)   /* every sixteenth */
                    pattern->notes[MELODIC_ARP][bar][step] = ARP_HIT;
            if (parts & PAD) pattern->notes[MELODIC_PAD][bar][0] = PAD_CHORDS[bar];
            if (parts & LEAD)
                for (step = 0; step < sizeof LEAD_STEPS; step++)
                    pattern->notes[MELODIC_LEAD][bar][LEAD_STEPS[step]] = LEAD_NOTES[bar][step];
        }
    }
    for (index = 0; index < sizeof CHAIN; index++)
        song.chain[index] = CHAIN[index];
}

/* ---- the other demo songs, DEMO2 and DEMO3 on the songs' page (DEMO1 is the first song, above) */
static void song_notes(uint32_t pattern_index, uint32_t melodic, uint32_t bar, const uint8_t notes[STEPS_PER_BAR])
{
    uint32_t step;
    for (step = 0; step < STEPS_PER_BAR; step++) song.patterns[pattern_index].notes[melodic][bar][step] = notes[step];
}

static void song_arp_hits(uint32_t pattern_index, uint32_t bar, uint32_t every)   /* ARP: a hit every that many steps */
{
    uint32_t step;
    for (step = 0; step < STEPS_PER_BAR; step += every) song.patterns[pattern_index].notes[MELODIC_ARP][bar][step] = ARP_HIT;
}

static void song_bars(uint32_t pattern_index, uint32_t track, uint32_t bars)   /* track: 0 the drums, 1 + melodic */
{
    song.patterns[pattern_index].bar_totals[track] = (uint8_t)bars;
}

static void song_chain(const uint8_t *chain, uint32_t length)   /* letters A.. as 1.. */
{
    uint32_t place;
    for (place = 0; place < length; place++) song.chain[place] = chain[place];
}

/* uplifting trance, D minor, 138 BPM: a galloping PLUCK bass, PLUCK arpeggios, a breakdown of pad and SUPERSAW,
 * a build with a snare roll, the drop.
 *   A  kick, hats, bass            B  A with clap, pad and arpeggio   C  the breakdown: pad and melody
 *   D  the build: pad, arpeggio, the snare roll and toms        E  the drop: everything      F  the outro */
static void demo_uplifting(void)
{
    enum { A, B, C, D, E, F };
    static const uint8_t CHORDS[BAR_COUNT] = {50, 46 | NOTE_MAJOR, 53 | NOTE_MAJOR, 48 | NOTE_MAJOR};   /* Dm Bb F C */
    static const uint8_t ROOTS[BAR_COUNT] = {38, 34, 29, 36};   /* D2 Bb1 F1 C2 */
    static const uint8_t MELODY[BAR_COUNT][STEPS_PER_BAR] = {
        {77, 0, 0, 0, 76, 0, 74, 0, 72, 0, 0, 0, 74, 0, 0, 0},
        {74, 0, 0, 0, 72, 0, 70, 0, 69, 0, 0, 0, 70, 0, 0, 0},
        {72, 0, 0, 0, 69, 0, 72, 0, 77, 0, 0, 0, 76, 0, 0, 0},
        {76, 0, 0, 0, 74, 0, 72, 0, 79, 0, 76, 0, 74, 0, 72, 0}};
    static const uint16_t ROLL[BAR_COUNT] = {0x1111, 0x5555, 0xFFFF, 0xFFFF};
    static const uint8_t CHAIN[] = {A + 1, B + 1, B + 1, C + 1, C + 1, D + 1, E + 1, E + 1, F + 1};
    uint32_t pattern, bar, step;
    empty_song();
    song.tempo = 138;
    song.reverb = 40;
    sound_apply(MELODIC_BASS, 0);               /* PLUCK */
    sound_apply(MELODIC_ARP, 0);           /* PLUCK */
    sound_apply(MELODIC_PAD, 0);                /* WARM, swelling slower */
    song.sounds[MELODIC_PAD][2] = 70;
    song.sounds[MELODIC_PAD][3] = 80;
    sound_apply(MELODIC_LEAD, 0);               /* SUPERSAW */
    song.cutoff[MELODIC_BASS] = 40;
    song.echo[MELODIC_ARP] = 35;
    song.echo[MELODIC_LEAD] = 50;
    for (pattern = A; pattern <= F; pattern++) {
        const int bass = pattern != C && pattern != D, arpeggio = pattern == B || pattern == D || pattern == E;
        const int pad = pattern >= B, melody = pattern == C || pattern == E;   /* (B's pad: the chords for its arpeggio) */
        for (bar = 0; bar < BAR_COUNT; bar++) {
            for (step = 2; bass && step < STEPS_PER_BAR; step += 4u) {   /* the gallop: two sixteenths on each off-beat */
                song.patterns[pattern].notes[MELODIC_BASS][bar][step] = ROOTS[bar];
                song.patterns[pattern].notes[MELODIC_BASS][bar][step + 1u] = ROOTS[bar];
            }
            if (arpeggio) song_arp_hits(pattern, bar, 1u);
            if (pad) song.patterns[pattern].notes[MELODIC_PAD][bar][0] = CHORDS[bar];
            if (melody) song_notes(pattern, MELODIC_LEAD, bar, MELODY[bar]);
        }
        song_bars(pattern, 1u + MELODIC_BASS, BAR_COUNT);
        song_bars(pattern, 1u + MELODIC_ARP, BAR_COUNT);
        song_bars(pattern, 1u + MELODIC_PAD, BAR_COUNT);
        song_bars(pattern, 1u + MELODIC_LEAD, BAR_COUNT);
        song.patterns[pattern].arp_mode = ARP_MODE(ARP_ORDER_UP, 2u);   /* sixteenths up two octaves */
        if (pattern != C && pattern != D) song.patterns[pattern].drum_hits[DRUM_KICK][0] = 0x1111;
        if (pattern == A || pattern == B || pattern == F) song.patterns[pattern].drum_hits[DRUM_HAT][0] = 0x4444;
        if (pattern == B || pattern == E) song.patterns[pattern].drum_hits[DRUM_CLAP][0] = 0x1010;
        if (pattern == E) song.patterns[pattern].drum_hits[DRUM_OPEN_HAT][0] = 0x4444;
    }
    song_bars(D, 0, BAR_COUNT);
    for (bar = 0; bar < BAR_COUNT; bar++) song.patterns[D].drum_hits[DRUM_SNARE][bar] = ROLL[bar];
    song.patterns[D].drum_hits[DRUM_TOM][3] = 0xF000;   /* a tom fill over the roll's last four */
    song_chain(CHAIN, sizeof CHAIN);
}

/* ambient / downtempo, 90 BPM: a SLOW pad over F Am C G, a SOFT arpeggio and a SOFT lead in echo and reverb, a
 * SUB bass, half-time kick and snare.
 *   A  pad and arpeggio            B  A with bass, kick and hats  C  B with lead and snare       D  pad and lead */
static void demo_ambient(void)
{
    enum { A, B, C, D };
    static const uint8_t CHORDS[BAR_COUNT] = {53 | NOTE_MAJOR, 57, 60 | NOTE_MAJOR, 55 | NOTE_MAJOR};   /* F Am C G */
    static const uint8_t ROOTS[BAR_COUNT] = {29, 33, 36, 31};   /* F1 A1 C2 G1 */
    static const uint8_t LEAD[BAR_COUNT][STEPS_PER_BAR] = {
        {72, 0, 0, 0, 0, 0, 0, 0, 69, 0, 0, 0, 0, 0, 0, 0},
        {76, 0, 0, 0, 0, 0, 0, 0, 0, 0, 72, 0, 0, 0, 0, 0},
        {79, 0, 0, 0, 0, 0, 76, 0, 0, 0, 0, 0, 72, 0, 0, 0},
        {74, 0, 0, 0, 0, 0, 0, 0, 71, 0, 0, 0, 0, 0, 0, 0}};
    static const uint8_t CHAIN[] = {A + 1, B + 1, C + 1, C + 1, D + 1, B + 1, C + 1, A + 1};
    uint32_t pattern, bar, step;
    empty_song();
    song.tempo = 90;
    song.reverb = 70;
    sound_apply(MELODIC_BASS, 1);               /* SUB */
    sound_apply(MELODIC_ARP, 3);           /* SOFT */
    sound_apply(MELODIC_PAD, 2);                /* SLOW */
    sound_apply(MELODIC_LEAD, 2);               /* SOFT */
    song.echo[MELODIC_ARP] = 60;
    song.echo[MELODIC_LEAD] = 55;
    for (pattern = A; pattern <= D; pattern++) {
        for (bar = 0; bar < BAR_COUNT; bar++) {
            song.patterns[pattern].notes[MELODIC_PAD][bar][0] = CHORDS[bar];
            if (pattern != D) song_arp_hits(pattern, bar, 2u);   /* eighths */
            if (pattern >= C) song_notes(pattern, MELODIC_LEAD, bar, LEAD[bar]);
            for (step = 0; (pattern == B || pattern == C) && step < 12u; step++)   /* the root, held (a note a step) */
                song.patterns[pattern].notes[MELODIC_BASS][bar][step] = ROOTS[bar];
        }
        song_bars(pattern, 1u + MELODIC_BASS, BAR_COUNT);
        song_bars(pattern, 1u + MELODIC_ARP, BAR_COUNT);
        song_bars(pattern, 1u + MELODIC_PAD, BAR_COUNT);
        song_bars(pattern, 1u + MELODIC_LEAD, BAR_COUNT);
        song.patterns[pattern].arp_mode = ARP_MODE(ARP_ORDER_UP_DOWN, 2u);
        if (pattern == B || pattern == C) {
            song.patterns[pattern].drum_hits[DRUM_KICK][0] = 0x0401;   /* on 1 and the "and" of 3 */
            song.patterns[pattern].drum_hits[DRUM_HAT][0] = 0x4444;
        }
        if (pattern == C) song.patterns[pattern].drum_hits[DRUM_SNARE][0] = 0x0100;   /* on 3: half time */
    }
    song_chain(CHAIN, sizeof CHAIN);
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

static int pattern_valid(const volatile pattern_t *checked)
{
    uint32_t track, melodic, bar, step;
    if (checked->swing > SWING_MAX) return 0;
    for (track = 0; track < sizeof checked->bar_totals; track++)
        if (checked->bar_totals[track] < 1u || checked->bar_totals[track] > BAR_COUNT) return 0;
    for (melodic = 0; melodic < MELODIC_COUNT; melodic++)
        for (bar = 0; bar < BAR_COUNT; bar++)
            for (step = 0; step < STEPS_PER_BAR; step++)
                if (note_gate(checked->gates[melodic][bar], step) > GATE_TENTHS_MAX) return 0;
    return checked->arp_mode <= ARP_MODE_MAX;
}

static int song_valid(const volatile song_t *checked)
{
    uint32_t melodic, index, knob;
    for (melodic = 0; melodic < MELODIC_COUNT; melodic++) {
        if (checked->cutoff[melodic] > SYNTH_PARAMETER_MAX || checked->decay[melodic] > SYNTH_PARAMETER_MAX
            || checked->echo[melodic] > ECHO_SEND_MAX || checked->sound_choices[melodic] >= sound_count(melodic)
            || checked->levels[melodic] > MIX_MAX) return 0;
        for (knob = 0; knob < SOUND_KNOBS; knob++)
            if (checked->sounds[melodic][knob] > sound_knob_max(melodic, knob)) return 0;
    }
    for (index = 0; index < PATTERN_COUNT; index++)
        if (!pattern_valid(&checked->patterns[index])) return 0;
    for (index = 0; index < CHAIN_SLOTS; index++)
        if (checked->chain[index] > PATTERN_COUNT) return 0;
    for (index = 0; index < DRUM_COUNT; index++)
        if (checked->drum_levels[index] > MIX_MAX) return 0;
    for (index = 0; index < 1u + MELODIC_COUNT; index++)
        if (checked->pans[index] > MIX_MAX || checked->reverb_sends[index] > MIX_MAX) return 0;
    if (checked->drum_echo > ECHO_SEND_MAX) return 0;
    return checked->tempo >= TEMPO_MIN && checked->tempo <= TEMPO_MAX && checked->reverb <= REVERB_MAX;
}

/* ---- the songs' places: four that are always there, the three demo songs and an empty one, and eight kept in flash
 * (a blob each, docs/API.md). The song that plays is none of them: it is a copy, kept as the settings. */
#define PLACES_KEPT 8u
enum { PLACE_DEMO, PLACE_DEMO2, PLACE_DEMO3, PLACE_NEW, PLACE_FIRST_KEPT, PLACE_COUNT = PLACE_FIRST_KEPT + PLACES_KEPT };
_Static_assert(PLACES_KEPT <= NBLOB && sizeof(song_t) <= SECTOR_MAX, "a song a blob");

static song_t place_song;                       /* a kept song on its way in: checked before it is the song */

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
    else if (place == PLACE_DEMO3) demo_ambient();
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

/* ---- the sequencer (runs in the audio ISR, so a step starts on its exact frame) */
#define FRAMES_PER_STEP_Q8(tempo) (FS * 15u * 256u / (tempo))   /* a step is a sixteenth note: 60 / tempo / 4 seconds */
#define AUDITION_FRAMES (FS / 5u)               /* a note played by hand sounds this long */
#define BLOCK_FRAMES_MAX 256u                   /* app_audio is never asked for more (docs/API.md) */
#define CLOCK_RING 97u                          /* MIDI clock in: a bar's clocks (96) and the one before */
#define CLOCK_TEMPO_CLOCKS 6u                   /* the tempo is measured once a step's clocks are there */
#define CLOCK_GONE_MS 500u                      /* no clock this long: Juicy's own tempo again */
enum { CLOCK_START_NONE, CLOCK_START_START, CLOCK_START_CONTINUE };

static volatile uint32_t playing;               /* (the main loop writes) */
static volatile uint32_t frames_per_step_q8;    /* (the main loop writes) */
static volatile uint32_t drum_auditions[DRUM_COUNT];   /* counts up when the main loop wants to hear a drum */
static volatile uint32_t note_auditions[MELODIC_COUNT], audition_notes[MELODIC_COUNT];   /* the same for a note (the note is written first) */
static volatile uint32_t note_held[MELODIC_COUNT];   /* the key that plays that note is down: it sounds until the key lets go (the main loop writes) */
static volatile uint32_t chain_on;              /* the chain plays, not the pattern on the screen (the main loop writes) */
static volatile uint32_t muted_tracks;          /* bit t: track t (a page: DRUMS, then the melodic tracks) does not play its pattern (the main loop writes) */
static volatile uint32_t loop_pattern;          /* the pattern on the screen: it plays when the chain does not (the main loop writes) */
static volatile uint32_t playing_step;          /* the step that sounds now, counted from the pattern's bar 1 (the ISR writes) */
static volatile uint32_t playing_pattern;       /* the pattern that sounds now (the ISR writes) */
static volatile uint32_t playing_slot;          /* its place in the chain, CHAIN_SLOTS = it is not from the chain (the ISR writes) */
static volatile uint32_t resume_playing;        /* PLAY goes on from where it stopped, not from the top: MIDI CONTINUE (the main loop writes) */
static volatile uint32_t steps_since_play;      /* the steps started since PLAY (the ISR writes) ... */
static volatile uint32_t step_started_ms;       /* ... and when the last of them did, in fm1_ms (the ISR writes) */

/* the mixer: each track (mono) at its LEVEL and PAN into out, and from there into the echo and the reverb; the echo
 * comes back into both. Gains are Q12 (4096: as it is), so the mix as it was made adds up to the same numbers. */
static void render(int32_t *out, uint32_t frame_count)   /* at most BLOCK_FRAMES_MAX */
{
    static int32_t track_out[BLOCK_FRAMES_MAX], echo_send[BLOCK_FRAMES_MAX], reverb_send[BLOCK_FRAMES_MAX];
    static int32_t echo_return[2u * BLOCK_FRAMES_MAX];   /* (L R L R ...) */
    uint32_t track, i;
    memset(echo_send, 0, frame_count * sizeof echo_send[0]);
    memset(reverb_send, 0, frame_count * sizeof reverb_send[0]);
    memset(echo_return, 0, 2u * frame_count * sizeof echo_return[0]);
    for (track = 0; track < 1u + MELODIC_COUNT; track++) {   /* the drums, then the melodic tracks */
        const uint32_t melodic = track - 1u, pan = song.pans[track];
        const int32_t level = track ? (int32_t)(song.levels[melodic] * 4096u / MIX_MAX) : 4096;   /* (the drums': each drum's) */
        const int32_t left = pan <= MIX_MAX / 2u ? 4096 : (int32_t)((MIX_MAX - pan) * 4096u / (MIX_MAX / 2u));
        const int32_t right = pan >= MIX_MAX / 2u ? 4096 : (int32_t)(pan * 4096u / (MIX_MAX / 2u));
        const int32_t echo_gain = (int32_t)((track ? song.echo[melodic] : song.drum_echo) * 5u);   /* of 512: echo / 100, near enough */
        const int32_t reverb_gain = (int32_t)(song.reverb_sends[track] * 4096u / MIX_MAX);
        memset(track_out, 0, frame_count * sizeof track_out[0]);
        if (!track) {
            int32_t drum_levels[DRUM_COUNT];
            uint32_t drum;
            for (drum = 0; drum < DRUM_COUNT; drum++) drum_levels[drum] = (int32_t)(song.drum_levels[drum] * 4096u / MIX_MAX);
            drums_render(track_out, frame_count, drum_levels);
        } else if (melodic == MELODIC_PAD)
            pad_render(track_out, frame_count, song.cutoff[melodic], song.sounds[melodic][1], song.sounds[melodic][2], song.sounds[melodic][3]);
        else {
            synth_shape_t shape;
            melodic_shape(melodic, &shape);
            synth_render(&melodic_voices[melodic], &shape, track_out, frame_count, song.cutoff[melodic], song.decay[melodic]);
        }
        for (i = 0; i < frame_count; i++) {
            const int32_t sample = (track_out[i] * level) >> 12;
            out[2u * i] += (sample * left) >> 12;
            out[2u * i + 1u] += (sample * right) >> 12;
            echo_send[i] += (sample * echo_gain) >> 9;
            reverb_send[i] += (sample * reverb_gain) >> 12;
        }
    }
    echo_render(echo_return, echo_send, frame_count, ECHO_STEPS * frames_per_step_q8 >> 8);
    for (i = 0; i < frame_count; i++) {         /* the echo: heard, and into the reverb */
        out[2u * i] += echo_return[2u * i];
        out[2u * i + 1u] += echo_return[2u * i + 1u];
        reverb_send[i] += echo_return[2u * i];
    }
    reverb_render(out, reverb_send, frame_count, song.reverb);
    limiter_render(out, frame_count);
}

/* ARP's note for its hit number position over chord (as on the pad's track): the chord's three notes from its root
 * (an octave up while it is below ARP's lowest note), over MODE's octaves, in MODE's order */
static uint32_t arp_note(uint32_t chord, uint32_t mode, uint32_t position)
{
    static uint32_t random_state = 1;
    const uint32_t order = mode / ARP_OCTAVES_MAX, count = PAD_NOTE_COUNT * (mode % ARP_OCTAVES_MAX + 1u);
    const uint32_t intervals[PAD_NOTE_COUNT] = {0, chord & NOTE_MAJOR ? PAD_MAJOR_THIRD : PAD_MINOR_THIRD, PAD_FIFTH};
    uint32_t root = chord & NOTE_MASK, index = position % count;
    while (root < MELODIC[MELODIC_ARP].note_lowest) root += 12u;
    if (order == ARP_ORDER_DOWN) index = count - 1u - index;
    else if (order == ARP_ORDER_UP_DOWN) {      /* up and down again, the top and the bottom once each */
        index = position % (2u * count - 2u);
        if (index >= count) index = 2u * count - 2u - index;
    } else if (order == ARP_ORDER_RANDOM) {
        random_state = random_state * 1664525u + 1013904223u;
        index = (random_state >> 16) % count;
    }
    return root + index / PAD_NOTE_COUNT * 12u + intervals[index % PAD_NOTE_COUNT];
}

static void app_audio(int32_t *out, uint32_t frame_count)
{
    static uint32_t was_playing, next_step, frames_to_step_q8, drum_auditions_done[DRUM_COUNT];
    static uint32_t note_auditions_done[MELODIC_COUNT], audition_frames_left[MELODIC_COUNT];
    static uint32_t pad_pattern_value;          /* the chord the pattern holds on the pad now, 0 = none */
    static uint32_t arp_chord;                  /* the same, muted or not: what ARP plays over (unless the pad's keys play one) */
    static uint32_t arp_chord_used, arp_position;   /* the chord ARP's last hit came from, and the hits since it began */
    static uint32_t pattern_now, chain_position;   /* what plays, and the last place of the chain that played */
    static uint32_t step_waits, swing_frames_left, waiting_bar, waiting_step;   /* a step that has started and not sounded yet (swing) */
    static uint32_t gate_frames_left[MELODIC_COUNT];   /* a note shorter than its step: the frames until it lets go, 0: none */
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
            gate_frames_left[melodic] = 0;
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
        pad_pattern_value = arp_chord = arp_chord_used = 0;
        for (melodic = 0; melodic < MELODIC_COUNT; melodic++) gate_frames_left[melodic] = 0;
        render(out, frame_count);
        return;
    }
    if (!was_playing) {                         /* PLAY: from the top, at once (CONTINUE: on from the step after the last) */
        was_playing = 1;
        if (!resume_playing) {
            next_step = ~0u;                    /* (past any pattern's end: the first step picks the pattern) */
            chain_position = CHAIN_SLOTS - 1u;  /* (the chain starts at its first place) */
        }
        frames_to_step_q8 = 0;
        steps_since_play = 0;
        step_waits = 0;
    }
    while (frames_done < frame_count) {
        uint32_t run_frames;
        if (frames_to_step_q8 < 256u) {         /* less than a frame left: the step starts here */
            if (next_step >= pattern_bars(&song.patterns[pattern_now]) * STEPS_PER_BAR) {   /* the pattern's end (or bars were taken away): which one now? */
                uint32_t tried;
                next_step = 0;
                pad_pattern_value = arp_chord = 0;   /* the pad's chord ends with its pattern (a chord on step 1 takes over without a gap) */
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
            waiting_bar = next_step / STEPS_PER_BAR;   /* (of the pattern: a track with fewer bars goes round) */
            waiting_step = next_step % STEPS_PER_BAR;
            step_waits = 1;                     /* every second step sounds later by the swing: up to 0.6 of a step (80 %) */
            swing_frames_left = waiting_step & 1u ? (song.patterns[pattern_now].swing * frames_per_step_q8 / 50u) >> 8 : 0u;
            playing_step = next_step;
            next_step++;
            step_started_ms = fm1_ms + frames_done * 1000u / FS;
            steps_since_play = steps_since_play + 1u;
            frames_to_step_q8 += frames_per_step_q8;   /* (the fraction carries over: the tempo does not drift) */
        }
        if (step_waits && !swing_frames_left) { /* the step sounds: on its start, a swung one later (always before the next starts) */
            const volatile pattern_t *pattern = &song.patterns[pattern_now];
            const uint32_t bar = waiting_bar, step = waiting_step;
            step_waits = 0;
            for (drum = 0; drum < DRUM_COUNT; drum++)
                if (!(muted_tracks & 1u) && pattern->drum_hits[drum][bar % pattern->bar_totals[0]] >> step & 1u)
                    drums_trigger(drum);
            {                                   /* a chord on the pad's step: ARP starts over on it (the pad comes after ARP below) */
                const uint32_t chord = pattern->notes[MELODIC_PAD][bar % pattern->bar_totals[1u + MELODIC_PAD]][step];
                if (chord) {
                    arp_chord = chord;
                    arp_position = 0;
                }
            }
            for (melodic = 0; melodic < MELODIC_COUNT; melodic++) {   /* a note lasts its step and its ties: an empty step lets it go */
                const uint32_t track_bar = bar % pattern->bar_totals[1u + melodic];
                uint32_t note = pattern->notes[melodic][track_bar][step];
                if (muted_tracks >> (1u + melodic) & 1u) note = 0;   /* muted: as if the steps were empty */
                if (melodic == MELODIC_PAD) {   /* but the pad holds its chord until the next one */
                    if (!note && muted_tracks >> (1u + melodic) & 1u && pad_pattern_value) {   /* (muted: not that one either) */
                        pad_pattern_value = 0;
                        melodic_note_off(melodic);
                    }
                    if (!note) continue;
                    pad_pattern_value = note;
                } else if (note & NOTE_TIE)
                    continue;                   /* tied: the note before goes on */
                if (melodic == MELODIC_ARP && note) {   /* a hit: the chord's next note (the one the pad's keys play, if they do; none: silent) */
                    const uint32_t chord = audition_frames_left[MELODIC_PAD] ? audition_notes[MELODIC_PAD] : arp_chord;
                    if (chord != arp_chord_used) {
                        arp_chord_used = chord;
                        arp_position = 0;
                    }
                    note = chord ? arp_note(chord, pattern->arp_mode, arp_position++) : 0u;
                }
                if (note) melodic_note_on(melodic, note);
                else melodic_note_off(melodic);
                audition_frames_left[melodic] = 0;
                gate_frames_left[melodic] = note ? note_gate(pattern->gates[melodic][track_bar], step) * frames_per_step_q8 / 10u >> 8 : 0u;
                if (note && note_gate(pattern->gates[melodic][track_bar], step) && !gate_frames_left[melodic]) gate_frames_left[melodic] = 1;
            }
        }
        run_frames = frames_to_step_q8 >> 8;
        if (step_waits && run_frames > swing_frames_left) run_frames = swing_frames_left;
        for (melodic = 0; melodic < MELODIC_COUNT; melodic++)
            if (gate_frames_left[melodic] && run_frames > gate_frames_left[melodic]) run_frames = gate_frames_left[melodic];
        if (run_frames > frame_count - frames_done) run_frames = frame_count - frames_done;
        render(out + 2u * frames_done, run_frames);
        frames_done += run_frames;
        frames_to_step_q8 -= run_frames << 8;
        if (step_waits) swing_frames_left -= run_frames;
        for (melodic = 0; melodic < MELODIC_COUNT; melodic++)
            if (gate_frames_left[melodic] && !(gate_frames_left[melodic] -= run_frames)) melodic_note_off(melodic);
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
#define CHAIN_HEIGHT 120u
#define NO_STEP STEPS_PER_BAR
#define NO_BAR BAR_COUNT

/* a page per track, and the one behind HOME. A melodic track's page is MELODIC_FIRST_PAGE + its number */
enum { PAGE_DRUMS, MELODIC_FIRST_PAGE, TRACK_COUNT = MELODIC_FIRST_PAGE + MELODIC_COUNT, PAGE_GLOBAL = TRACK_COUNT, PAGE_SONGS, PAGE_LIBRARY };
_Static_assert(sizeof ((pattern_t *)0)->bar_totals == TRACK_COUNT, "a track's page is its place in bar_totals");

static const char *const PAGE_NAMES[] = {"DRUMS", "BASS", "ARP", "PAD", "LEAD", "JUICY", "SONGS", "LIBRARY"};
static const char *const DRUM_NAMES[DRUM_COUNT] = {"KICK", "HAT", "CLAP", "OPEN HAT", "SNARE", "TOM"};
#define DRUMS_PER_PAGE 3u                       /* the drums' page shows three at a time */
#define DRUM_PAGES (DRUM_COUNT / DRUMS_PER_PAGE)
static const char *const CHORD_NAMES[2] = {"MIN", "MAJ"};
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
    cv_text(4, 100, &FONT_S, "HOLD REC: CLEAR PATTERN", C_LABEL);
    cv_blit(0, BODY_TOP);
}

/* what the songs' page says under its places */
enum { SONGS_NOTE_NONE, SONGS_NOTE_LOAD_ASKED, SONGS_NOTE_SAVE_ASKED, SONGS_NOTE_SAVING, SONGS_NOTE_SAVED, SONGS_NOTE_STOP_FIRST };

static const char *const PLACE_NAMES[PLACE_COUNT] = {"DEMO1", "DEMO2", "DEMO3", "NEW", "1", "2", "3", "4", "5", "6", "7", "8"};

/* the songs' places: a box each (D a demo song, N an empty one, 1..8 those kept), light where there is a song,
 * orange the selected one; what SEQ and REC do with it, or what has just happened */
static void draw_songs(uint32_t selected_place, uint32_t places_with_song, uint32_t note)
{
    static const char *const NOTES[] = {"", "SEQ AGAIN: LOAD IT", "REC AGAIN: SAVE OVER IT", "SAVING", "SAVED", "PRESS STOP FIRST"};
    const int has_song = places_with_song >> selected_place & 1u;
    uint32_t place;
    cv_begin(240, CHAIN_HEIGHT, C_BLACK);
    cv_text(4, 0, &FONT_S, selected_place == PLACE_DEMO ? "DEMO 1: EURODANCE" : selected_place == PLACE_DEMO2 ? "DEMO 2: UPLIFTING"
                           : selected_place == PLACE_DEMO3 ? "DEMO 3: AMBIENT" : selected_place == PLACE_NEW ? "AN EMPTY SONG"
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

static void draw_drum_row(uint32_t drum, uint32_t row, uint32_t hits, int selected, uint32_t marked_step)   /* row: of the page */
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
 * note's row lit. A note above or below what fits is a thin line at that edge. The pad's chords show their third
 * and fifth above the root, dimmer. The cursor's step is framed. */
static void draw_roll(const volatile pattern_t *pattern, uint32_t melodic, uint32_t bar, uint32_t selected_note, uint32_t lowest_note, uint32_t cursor_step)
{
    uint32_t step;
    int32_t selected_y = (int32_t)((lowest_note + ROLL_ROWS - 1u - selected_note) * ROLL_ROW_HEIGHT);
    cv_begin(240, ROLL_HEIGHT, C_BLACK);
    for (step = 0; step < STEPS_PER_BAR; step++) {
        uint32_t value = pattern->notes[melodic][bar][step], note = value & NOTE_MASK, chord_note;
        const int tied = melodic != MELODIC_PAD && value & NOTE_TIE && step;
        const uint32_t gate = note_gate(pattern->gates[melodic][bar], step);
        const int32_t note_x = tied ? step_x(step - 1u) + 12 : step_x(step);   /* tied: one bar with the step before; short: as much of the step */
        const int32_t note_width = gate ? (int32_t)(gate * 12u / 10u + 1u) : step_x(step) + 12 - note_x;   /* (10 %: 2 px, 90 %: 11) */
        cv_rect(step_x(step), 0, 12, (int32_t)ROLL_HEIGHT, step / 4u % 2u ? C_ROLL_EVEN_BEAT : C_ROLL_ODD_BEAT);
        if (melodic == MELODIC_ARP) {           /* ARP's hits have no notes: a lane through the middle */
            if (note) cv_rect(note_x, (int32_t)ROLL_HEIGHT / 3, note_width, (int32_t)ROLL_HEIGHT / 3, C_HIT);
            continue;
        }
        cv_rect(step_x(step), selected_y, 12, (int32_t)ROLL_ROW_HEIGHT - 1, step / 4u % 2u ? C_STEP_EVEN_BEAT : C_STEP_ODD_BEAT);
        if (!note) continue;
        if (note < lowest_note)
            cv_rect(note_x, (int32_t)ROLL_HEIGHT - 3, note_width, 3, C_LABEL);
        else if (note >= lowest_note + ROLL_ROWS)
            cv_rect(note_x, 0, note_width, 3, C_LABEL);
        else
            cv_rect(note_x, (int32_t)((lowest_note + ROLL_ROWS - 1u - note) * ROLL_ROW_HEIGHT), note_width,
                    (int32_t)ROLL_ROW_HEIGHT - 1, note == selected_note ? C_ACCENT : C_HIT);
        if (melodic != MELODIC_PAD) continue;
        for (chord_note = 0; chord_note < 2u; chord_note++) {
            uint32_t above = note + (chord_note ? PAD_FIFTH : value & NOTE_MAJOR ? PAD_MAJOR_THIRD : PAD_MINOR_THIRD);
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

/* a melodic track's bar (bar_notes): where the note that is on a step starts (a long note's first step),
 * STEPS_PER_BAR: none */
static uint32_t note_start(const volatile uint8_t *bar_notes, uint32_t melodic, uint32_t step)
{
    if (!bar_notes[step]) return STEPS_PER_BAR;
    while (melodic != MELODIC_PAD && step > 0u && bar_notes[step] & NOTE_TIE && bar_notes[step - 1u]) step--;
    return step;
}

static uint32_t note_steps(const volatile uint8_t *bar_notes, uint32_t melodic, uint32_t start)   /* how long the note starting there is */
{
    uint32_t steps = 1;
    while (melodic != MELODIC_PAD && start + steps < STEPS_PER_BAR && bar_notes[start + steps] & NOTE_TIE) steps++;
    return steps;
}

static void remove_note(volatile uint8_t *bar_notes, volatile uint8_t *bar_gates, uint32_t melodic, uint32_t start)
{
    const uint32_t steps = note_steps(bar_notes, melodic, start);
    uint32_t step;
    for (step = start; step < start + steps; step++) {
        bar_notes[step] = 0;
        set_note_gate(bar_gates, step, 0);
    }
}

static uint32_t length_steps(uint32_t length) { return length < LENGTH_WHOLE_FIRST ? 1u : length - GATE_TENTHS_MAX; }   /* length: as LENGTH */
static uint32_t length_gate(uint32_t length) { return length < LENGTH_WHOLE_FIRST ? length : 0u; }

/* value on step, length as LENGTH shows it (whole steps to the bar's end at most; the pad's chords: a step), over
 * what was there: a note that started there goes, one whose start it covers keeps its rest */
static void put_note(volatile uint8_t *bar_notes, volatile uint8_t *bar_gates, uint32_t melodic, uint32_t step, uint32_t value, uint32_t length)
{
    uint32_t placed_step;
    if (note_start(bar_notes, melodic, step) == step) remove_note(bar_notes, bar_gates, melodic, step);
    for (placed_step = step; placed_step < step + length_steps(length) && placed_step < STEPS_PER_BAR; placed_step++) {
        bar_notes[placed_step] = (uint8_t)(placed_step == step ? value : value | NOTE_TIE);
        set_note_gate(bar_gates, placed_step, placed_step == step ? length_gate(length) : 0u);
    }
    if (melodic != MELODIC_PAD && placed_step < STEPS_PER_BAR && bar_notes[placed_step] & NOTE_TIE)
        bar_notes[placed_step] = (uint8_t)(bar_notes[placed_step] & NOTE_MASK);
}

static uint32_t keyboard_first_note(uint32_t melodic, uint32_t octave)   /* the leftmost key's note at that octave */
{
    return MELODIC[melodic].note_lowest + KEYS_FIRST_NOTE_ABOVE_LOWEST + octave * 12u - 12u;
}

static void app_main(void)
{
    uint32_t previous_keys = 0, shown_track = PAGE_DRUMS, global_shown = 1, shown_pattern = 0, shown_bar = 0, selected_drum = DRUM_KICK;
    uint32_t selected_notes[MELODIC_COUNT] = {33, 60, 57, 69}, roll_lowest_notes[MELODIC_COUNT] = {28, 56, 53, 65};   /* A1, C4, A3, A4 */
    uint32_t selected_major = 0;                /* the chord KNOB3 shows on the pad's page: 0 minor, 1 major */
    uint32_t selected_lengths[MELODIC_COUNT] = {LENGTH_WHOLE_FIRST, LENGTH_WHOLE_FIRST, LENGTH_WHOLE_FIRST, LENGTH_WHOLE_FIRST};   /* KNOB4: how long a placed note is (not the pad's) */
    uint32_t cursor_steps[MELODIC_COUNT] = {0}; /* SELECT: the cursor's step in the bar on the screen */
    uint32_t keyboard_octaves[MELODIC_COUNT] = {1, 1, 1, 1};   /* where the keys are, 0 .. KEYBOARD_OCTAVES - 1 (1: as they start) */
    uint32_t recording_on = 0;                  /* EDIT + REC on a track's notes: a key puts its note at the cursor */
    uint32_t select_on = 0;                     /* SEL: the keys OP1 .. OP5 pick the page */
    uint32_t edit_on = 0;                       /* EDIT: the lower keys are the steps. Neither: the keys play */
    uint32_t held_key = 0;                      /* the key that played the last note by hand */
    uint32_t oct_down_waits = 0, oct_down_ms = 0;   /* OCT- is down and has not done anything yet; since when */
    uint32_t rec_waits = 0, rec_ms = 0;             /* the same for REC (copy or clear) */
    uint32_t songs_shown = 0;                   /* SAVE: the songs' page */
    uint32_t library_shown = 0;                 /* SAVE again: the library's page */
    uint32_t selected_library_place = 0, library_note = LIBRARY_NOTE_NONE, places_with_pattern = 0;   /* bit p: library place p holds a pattern */
    uint32_t library_unsaved = 0;               /* the library goes to flash as soon as nothing sounds */
    uint32_t splash_shown = 1;                  /* the start screen is there (until SPLASH_MS, or a touch) */
    uint32_t splash_start_ms;                   /* when it came (loading takes a while after power-on) */
    uint32_t clock_times_ms[CLOCK_RING] = {0}, clock_count = 0;   /* MIDI clock in: when the last ones came */
    uint32_t external_clock = 0, external_bpm = 0;   /* a clock comes in: Juicy follows it, at this tempo */
    uint32_t clock_start = CLOCK_START_NONE;     /* START / CONTINUE came: Juicy plays from the next clock */
    uint32_t clock_locked = 0, clocks_since_start = 0;   /* playing from a START / CONTINUE: in step with the clock */
    int32_t clock_correction_q8 = 0;            /* how much shorter a step is, to catch up with the clock (frames, Q8) */
    uint32_t clock_ticks_sent = 0;              /* Juicy the master: the clocks sent since PLAY */
    uint32_t fx_page = FX_PAGE_NOTES;           /* FX: which page of a track (DRUMS has no sound page) */
    uint32_t mix_drawn[DRUM_COUNT] = {0};       /* the mix page's rows */
    uint32_t sound_note = SOUND_NOTE_NONE, user_sounds_unsaved = 0, sound_drawn = 0;
    uint32_t selected_place = PLACE_FIRST_KEPT, songs_note = SONGS_NOTE_NONE;
    uint32_t places_with_song = 1u << PLACE_DEMO | 1u << PLACE_DEMO2 | 1u << PLACE_DEMO3 | 1u << PLACE_NEW;   /* bit p: place p holds a song */
    uint32_t place_to_save = PLACE_COUNT;       /* the place the song goes to as soon as nothing sounds, PLACE_COUNT = none */
    uint32_t unsaved = 0, changed_ms = 0, knob, step, drum, track, key, place;
    uint32_t page_drawn = ~0u, header_drawn = 0, drum_row_drawn[DRUMS_PER_PAGE] = {0}, drum_pages_drawn = 0, roll_drawn = 0, roll_marker_drawn = 0;
    uint32_t knob_drawn[KNOB_COUNT] = {0}, chain_drawn = 0, songs_drawn = 0, library_drawn = 0;

    {                                           /* the song as it was kept: as it is now, or in an older size */
        uint32_t kept = 0;
        while (kept < PATTERN_SIZES_COUNT && settings_load((void *)&song, SONG_SIZE_KEPT(PATTERN_SIZES[kept]))) kept++;
        if (kept == PATTERN_SIZES_COUNT) first_song();
        else if (kept) song_grow((song_t *)&song, PATTERN_SIZES[kept]);
    }
    if (!song_valid(&song))
        first_song();
    for (place = PLACE_FIRST_KEPT; place < PLACE_COUNT; place++)
        if (place_kept_valid(place)) places_with_song |= 1u << place;
    library_load();
    user_sounds_load();
    for (place = 0; place < LIBRARY_PLACES; place++)
        if (!pattern_empty(&library.patterns[place])) places_with_pattern |= 1u << place;
    frames_per_step_q8 = FRAMES_PER_STEP_Q8(song.tempo);
    for (step = 0; step < CHAIN_SLOTS; step++)   /* a song with a chain starts with the chain on */
        if (song.chain[step]) chain_on = 1;
    draw_splash();
    splash_start_ms = fm1_ms;

    for (;;) {
        uint32_t keys, keys_pressed, buttons_pressed, buttons_released, page, step_now, marked_step, marked_bar, marked_slot, state;
        uint32_t step_leds = 0, melodic = 0, keys_glowing = 0, keys_glowing_bright = 0, keys_lit = 0;
        uint32_t shown_note = 0, shown_major = 0, shown_length = LENGTH_WHOLE_FIRST;   /* a track's notes: what the knobs show (the cursor's note, or the next one) */
        uint32_t bar_total;                     /* of what the page shows: its track, or the whole pattern */
        volatile pattern_t *pattern;
        int32_t detents[KNOB_COUNT], presets_detents, algorithm_detents, select_detents;
        int song_changed = 0, keys_edit, keys_play, sound_page, mix_page, notes_page;
        uint8_t midi_status, midi_data1, midi_data2;
        int midi_source;
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        keys_pressed = keys & ~previous_keys;
        previous_keys = keys;
        buttons_pressed = fm1_input_edges(&buttons_released);
        for (knob = 0; knob < KNOB_COUNT; knob++)
            detents[knob] = fm1_enc_take(ENC_KNOB1 + knob);

        while ((midi_source = midi_get(&midi_status, &midi_data1, &midi_data2)) != 0) {   /* clock and transport (USB, TRS) */
            if (midi_status == 0xF8u) {         /* a clock: 24 a quarter note */
                clock_times_ms[clock_count % CLOCK_RING] = fm1_ms;
                clock_count++;
                if (clock_start != CLOCK_START_NONE) {   /* the first clock after START / CONTINUE: the downbeat */
                    resume_playing = clock_start == CLOCK_START_CONTINUE;
                    playing = 1;
                    clock_locked = 1;
                    clocks_since_start = 0;
                    clock_start = CLOCK_START_NONE;
                } else if (clock_locked && playing)
                    clocks_since_start++;
                if (clock_count > CLOCK_TEMPO_CLOCKS) {   /* the tempo: the last bar's clocks (as many as there are at first) */
                    const uint32_t span = clock_count > CLOCK_RING ? CLOCK_RING - 1u : clock_count - 1u;
                    const uint32_t span_ms = fm1_ms - clock_times_ms[(clock_count - 1u - span) % CLOCK_RING];
                    const uint32_t step_q8 = span_ms * 67738u / span;   /* ms over 24 clocks * 44.1 / 4 * 256 */
                    external_bpm = span_ms ? 60000u * span / (span_ms * 24u) : 0u;
                    if (!clock_locked || !playing) clock_correction_q8 = 0;
                    else if (clocks_since_start % 6u == 0u && steps_since_play) {
                        /* a step by the clock: where the ISR's step is against it, a quarter of that put right */
                        const int32_t step_ms_q8 = (int32_t)(step_q8 * 1000u / FS);
                        const int32_t steps_apart = (int32_t)(clocks_since_start / 6u) - (int32_t)(steps_since_play - 1u);
                        const int32_t late_ms_q8 = (int32_t)(step_started_ms - fm1_ms) * 256 + steps_apart * step_ms_q8;
                        clock_correction_q8 = late_ms_q8 * 441 / 40;   /* ms -> frames (44.1), a quarter of it */
                        if (clock_correction_q8 > (int32_t)step_q8 / 4) clock_correction_q8 = (int32_t)step_q8 / 4;
                        if (clock_correction_q8 < -(int32_t)step_q8 / 4) clock_correction_q8 = -(int32_t)step_q8 / 4;
                    }
                    frames_per_step_q8 = (uint32_t)((int32_t)step_q8 - clock_correction_q8);   /* (until the next step's clock) */
                }
            } else if (midi_status == 0xFAu) clock_start = CLOCK_START_START;
            else if (midi_status == 0xFBu) clock_start = CLOCK_START_CONTINUE;
            else if (midi_status == 0xFCu) {
                playing = 0;
                clock_start = CLOCK_START_NONE;
                clock_locked = 0;
            }
        }
        external_clock = clock_count && fm1_ms - clock_times_ms[(clock_count - 1u) % CLOCK_RING] < CLOCK_GONE_MS;
        if (!external_clock) {                  /* our own tempo */
            frames_per_step_q8 = FRAMES_PER_STEP_Q8(song.tempo);
            clock_locked = 0;
        }
        if (buttons_pressed & (1u << BTN_PLAY)) {
            playing = !playing;
            resume_playing = 0;
            clock_locked = 0;                   /* (started by hand: the clock's tempo, not its beat) */
            clock_ticks_sent = 0;
            if (!external_clock) midi_send(playing ? 0xFAu : 0xFCu, 0, 0);   /* the master: START / STOP */
        }
        if (external_clock) clock_ticks_sent = steps_since_play * 6u;   /* (should the clock stop: on as the master from here, no burst) */
        else if (playing && steps_since_play) { /* the master's clock: six a step, each in its time */
            const uint32_t step_ms_q8 = frames_per_step_q8 * 1000u / FS, since_ms = fm1_ms - step_started_ms;
            uint32_t ticks_due = (steps_since_play - 1u) * 6u;
            ticks_due += (int32_t)since_ms < 0 ? 0u : since_ms * 256u * 6u / step_ms_q8 + 1u > 6u ? 6u : since_ms * 256u * 6u / step_ms_q8 + 1u;
            for (; clock_ticks_sent < ticks_due; clock_ticks_sent++) midi_send(0xF8u, 0, 0);
        }
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
        for (track = 0; track < TRACK_COUNT; track++)
            if (select_on && keys_pressed >> MUTE_KEYS[track] & 1u)
                muted_tracks ^= 1u << track;
        for (track = 0; track < TRACK_COUNT; track++)
            if (select_on && keys_pressed >> TRACK_KEYS[track] & 1u) {
                shown_track = track;
                global_shown = songs_shown = library_shown = 0;
            }
        page = songs_shown ? PAGE_SONGS : library_shown ? PAGE_LIBRARY : global_shown ? PAGE_GLOBAL : shown_track;
        if (buttons_pressed & (1u << BTN_FX) && page < TRACK_COUNT)   /* the notes, the sound (not DRUMS), the mix, round */
            fx_page = fx_page == FX_PAGE_MIX ? FX_PAGE_NOTES : page == PAGE_DRUMS ? FX_PAGE_MIX : fx_page + 1u;
        sound_page = fx_page == FX_PAGE_SOUND && page > PAGE_DRUMS && page < TRACK_COUNT;
        mix_page = fx_page == FX_PAGE_MIX && page < TRACK_COUNT;
        notes_page = page >= MELODIC_FIRST_PAGE && page < TRACK_COUNT && !sound_page && !mix_page;
        if (!edit_on || !notes_page) recording_on = 0;
        keys_edit = !select_on && edit_on && !sound_page && !mix_page && !recording_on;   /* what the keys do: SEL comes first, then EDIT, else they play */
        keys_play = !select_on && (!edit_on || sound_page || mix_page);   /* (on a sound or mix page they always play) */
        algorithm_detents = fm1_enc_take(ENC_ALGORITHM);
        select_detents = fm1_enc_take(ENC_SELECT);   /* (taken on every page: turns elsewhere do not pile up for DRUMS) */
        if (!sound_page) sound_note = SOUND_NOTE_NONE;
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
            if (sound_page) {                   /* the knobs shape the sound, ALGORITHM picks one, SEQ / REC keep it */
                uint32_t sound_melodic = page - MELODIC_FIRST_PAGE, user_place;
                if (edit_on && algorithm_detents) {
                    sound_apply(sound_melodic, turned_value(song.sound_choices[sound_melodic], algorithm_detents, 0, sound_count(sound_melodic) - 1u));
                    sound_note = SOUND_NOTE_NONE;
                    song_changed = 1;
                }
                for (knob = 0; knob < KNOB_COUNT; knob++)
                    if (detents[knob]) {
                        song_changed |= turn_setting(&song.sounds[sound_melodic][knob], detents[knob], 0, sound_knob_max(sound_melodic, knob));
                        sound_note = SOUND_NOTE_NONE;
                        detents[knob] = 0;
                    }
                for (user_place = 0; user_place < USER_SOUNDS; user_place++)
                    if (edit_on && buttons_pressed & (1u << (user_place ? BTN_REC : BTN_SEQ))) {
                        if (sound_note != SOUND_NOTE_KEEP_ASKED + user_place) sound_note = SOUND_NOTE_KEEP_ASKED + user_place;
                        else {
                            sound_of_track(sound_melodic, &user_sounds[sound_melodic][user_place]);
                            song.sound_choices[sound_melodic] = (uint8_t)(PRESETS[sound_melodic].count + user_place);
                            user_sounds_unsaved = 1;
                            song_changed = 1;
                            sound_note = SOUND_NOTE_KEPT + user_place;
                        }
                    }
            } else {
                if (mix_page) {                 /* the knobs: LEVEL (the drums': the selected drum's), PAN, REVERB, ECHO */
                    const uint32_t mix_melodic = page - MELODIC_FIRST_PAGE;
                    song_changed |= turn_setting(page == PAGE_DRUMS ? &song.drum_levels[selected_drum] : &song.levels[mix_melodic], detents[0], 0, MIX_MAX);
                    song_changed |= turn_setting(&song.pans[page], detents[1], 0, MIX_MAX);
                    song_changed |= turn_setting(&song.reverb_sends[page], detents[2], 0, MIX_MAX);
                    song_changed |= turn_setting(page == PAGE_DRUMS ? &song.drum_echo : &song.echo[mix_melodic], detents[3], 0, ECHO_SEND_MAX);
                    for (knob = 0; knob < KNOB_COUNT; knob++) detents[knob] = 0;
                }
                if (edit_on && buttons_pressed & (1u << BTN_SEQ)) chain_on = !chain_on;
            }
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
        if (!edit_on || !(page == PAGE_GLOBAL || (page < TRACK_COUNT && !sound_page && !mix_page)))
            rec_waits = 0;                      /* (another page, or EDIT off, while REC is down: it does nothing) */
        else if (buttons_pressed & (1u << BTN_REC)) {   /* REC moves when it is let go: held, it clears */
            rec_waits = 1;
            rec_ms = fm1_ms;
        }
        if (rec_waits && buttons_released & (1u << BTN_REC)) {
            rec_waits = 0;
            if (notes_page) recording_on = !recording_on;
            else if (page == PAGE_GLOBAL && !pattern_empty(&song.patterns[shown_pattern])) {
                uint32_t index;                 /* a copy goes to the first empty pattern, which is then the one on the screen */
                for (index = 0; index < PATTERN_COUNT; index++)
                    if (pattern_empty(&song.patterns[index])) {
                        memcpy((void *)&song.patterns[index], (const void *)&song.patterns[shown_pattern], sizeof(pattern_t));
                        shown_pattern = index;
                        song_changed = 1;
                        break;
                    }
            }
        }
        if (rec_waits && fm1_ms - rec_ms >= HOLD_MS) {
            volatile pattern_t *cleared = &song.patterns[shown_pattern];
            uint32_t bar, cleared_drum, cleared_melodic, cleared_step;   /* held: JUICY empties the pattern, DRUMS the selected drum, a track itself; the bars stay */
            rec_waits = 0;
            for (bar = 0; bar < BAR_COUNT; bar++) {
                for (cleared_drum = 0; cleared_drum < DRUM_COUNT; cleared_drum++)
                    if (page == PAGE_GLOBAL || (page == PAGE_DRUMS && cleared_drum == selected_drum)) cleared->drum_hits[cleared_drum][bar] = 0;
                for (cleared_melodic = 0; cleared_melodic < MELODIC_COUNT; cleared_melodic++)
                    for (cleared_step = 0; cleared_step < STEPS_PER_BAR; cleared_step++)
                        if (page == PAGE_GLOBAL || page == MELODIC_FIRST_PAGE + cleared_melodic) {
                            cleared->notes[cleared_melodic][bar][cleared_step] = 0;
                            set_note_gate(cleared->gates[cleared_melodic][bar], cleared_step, 0);
                        }
            }
            roll_drawn = 0;
            song_changed = 1;
        }
        loop_pattern = shown_pattern;
        pattern = &song.patterns[shown_pattern];
        if (recording_on) {                     /* recording: OCT- / OCT+ move the keys (the cursor goes through the bars) */
            const uint32_t octave_melodic = page - MELODIC_FIRST_PAGE;
            if (buttons_pressed & (1u << BTN_OCT_DOWN) && keyboard_octaves[octave_melodic] > 0u) keyboard_octaves[octave_melodic]--;
            if (buttons_pressed & (1u << BTN_OCT_UP) && keyboard_octaves[octave_melodic] + 1u < KEYBOARD_OCTAVES) keyboard_octaves[octave_melodic]++;
        }
        if (!recording_on && buttons_pressed & (1u << BTN_OCT_DOWN)) {   /* OCT- moves when it is let go: held, it takes the bar away */
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
                for (step = 0; page != PAGE_DRUMS && step < STEPS_PER_BAR / 2u; step++)   /* (two steps' gates a byte) */
                    pattern->gates[page - MELODIC_FIRST_PAGE][bar][step] = bar + 1u < BAR_COUNT ? pattern->gates[page - MELODIC_FIRST_PAGE][bar + 1u][step] : 0u;
            }
            if (pattern->bar_totals[page] > 1u) pattern->bar_totals[page]--;
            roll_drawn = 0;
            song_changed = 1;
        }
        if (!recording_on && buttons_pressed & (1u << BTN_OCT_UP)) {
            if (edit_on && page < TRACK_COUNT && shown_bar + 1u == pattern->bar_totals[page] && pattern->bar_totals[page] < BAR_COUNT) {
                pattern->bar_totals[page]++;    /* EDIT on a track's page: past its last bar is a new one */
                song_changed = 1;
            }
            shown_bar++;
        }
        bar_total = page < TRACK_COUNT ? pattern->bar_totals[page] : pattern_bars(pattern);
        if (shown_bar >= bar_total) shown_bar = bar_total - 1u;   /* the screen never shows a bar that does not play */

        if (page == PAGE_GLOBAL) {
            if (!external_clock) song_changed |= turn_setting(&song.tempo, detents[0], TEMPO_MIN, TEMPO_MAX);
            song_changed |= turn_setting(&song.reverb, detents[1], 0, REVERB_MAX);
            song_changed |= turn_setting(&song.patterns[shown_pattern].swing, detents[2], 0, SWING_MAX);
            for (step = 0; step < CHAIN_SLOTS; step++) {
                if (keys_edit && keys_pressed >> STEP_KEYS[step] & 1u) {   /* the pattern on the screen goes there; it is taken away if it was there */
                    song.chain[step] = (uint8_t)(song.chain[step] == shown_pattern + 1u ? 0u : shown_pattern + 1u);
                    chain_drawn = 0;
                    song_changed = 1;
                }
                if (song.chain[step]) step_leds |= 1u << step;
            }
        } else if (page == PAGE_DRUMS) {
            selected_drum = turned_value(selected_drum, select_detents, 0, DRUM_COUNT - 1u);
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
            uint32_t note_lowest, note_highest, roll_lowest_note, placed_value, chord_rows, rows_above, first_key_note, cursor_start, position;
            uint32_t audition_value = 0;        /* a note to hear (as in song.notes), 0: none */
            volatile uint8_t *bar_notes, *bar_gates;
            melodic = page - MELODIC_FIRST_PAGE;
            note_lowest = MELODIC[melodic].note_lowest;
            note_highest = MELODIC[melodic].note_highest;
            roll_lowest_note = roll_lowest_notes[melodic];
            first_key_note = keyboard_first_note(melodic, keyboard_octaves[melodic]);
            if (notes_page && select_detents) { /* SELECT: the cursor, on through the track's bars (the screen goes along) */
                position = turned_value(shown_bar * STEPS_PER_BAR + cursor_steps[melodic], select_detents, 0, bar_total * STEPS_PER_BAR - 1u);
                shown_bar = position / STEPS_PER_BAR;
                cursor_steps[melodic] = position % STEPS_PER_BAR;
                cursor_start = note_start(pattern->notes[melodic][shown_bar], melodic, cursor_steps[melodic]);
                if (!playing && cursor_start < STEPS_PER_BAR && melodic != MELODIC_ARP)   /* stopped: the note it comes to sounds (an ARP hit has none of its own) */
                    audition_value = pattern->notes[melodic][shown_bar][cursor_start] & (melodic == MELODIC_PAD ? 0xFFu : NOTE_MASK);
            }
            bar_notes = pattern->notes[melodic][shown_bar];
            bar_gates = pattern->gates[melodic][shown_bar];
            song_changed |= turn_setting(&song.cutoff[melodic], detents[1], 0, SYNTH_PARAMETER_MAX);
            if (melodic != MELODIC_PAD) song_changed |= turn_setting(&song.decay[melodic], detents[2], 0, SYNTH_PARAMETER_MAX);
            if (melodic == MELODIC_ARP) {       /* KNOB1: the pattern's MODE (ARP's steps have no notes to turn) */
                song_changed |= turn_setting(&pattern->arp_mode, detents[0], 0, ARP_MODE_MAX);
                detents[0] = 0;
            }
            cursor_start =notes_page && edit_on && !recording_on ? note_start(bar_notes, melodic, cursor_steps[melodic]) : STEPS_PER_BAR;
            if (cursor_start < STEPS_PER_BAR) { /* EDIT, the cursor on a note: the knobs show it and change it (and the next note is like it) */
                const uint32_t value = bar_notes[cursor_start];
                shown_note = melodic == MELODIC_ARP ? selected_notes[melodic] : turned_value(value & NOTE_MASK, detents[0], note_lowest, note_highest);
                shown_major = melodic == MELODIC_PAD ? turned_value((value & NOTE_MAJOR) != 0u, detents[2], 0, 1) : 0u;
                shown_length = note_gate(bar_gates, cursor_start) ? note_gate(bar_gates, cursor_start) : GATE_TENTHS_MAX + note_steps(bar_notes, melodic, cursor_start);
                shown_length = melodic == MELODIC_PAD ? LENGTH_WHOLE_FIRST : turned_value(shown_length, detents[3], 1, GATE_TENTHS_MAX + STEPS_PER_BAR - cursor_start);
                if (detents[0] || (melodic == MELODIC_PAD ? detents[2] : detents[3])) {
                    put_note(bar_notes, bar_gates, melodic, cursor_start, melodic == MELODIC_ARP ? ARP_HIT : shown_note | (shown_major ? NOTE_MAJOR : 0u), shown_length);
                    selected_notes[melodic] = shown_note;
                    if (melodic == MELODIC_PAD) selected_major = shown_major;
                    else selected_lengths[melodic] = shown_length;
                    if (detents[0] || melodic == MELODIC_PAD) audition_value = shown_note | (shown_major ? NOTE_MAJOR : 0u);
                    roll_drawn = 0;
                    song_changed = 1;
                }
            } else {                            /* the knobs set the next note */
                selected_notes[melodic] = turned_value(selected_notes[melodic], detents[0], note_lowest, note_highest);
                if (melodic == MELODIC_PAD) selected_major = turned_value(selected_major, detents[2], 0, 1);
                else selected_lengths[melodic] = turned_value(selected_lengths[melodic], detents[3], 1, LENGTH_MAX);
                if (detents[0] || (melodic == MELODIC_PAD && detents[2]))
                    audition_value = selected_notes[melodic] | (melodic == MELODIC_PAD && selected_major ? NOTE_MAJOR : 0u);
            }
            for (key = 0; key < KEY_COUNT; key++) {   /* played: a key's note sounds while it is down, and is the one EDIT places */
                const uint32_t key_note = first_key_note + key;
                if (!((keys_play || recording_on) && keys_pressed >> key & 1u) || key_note < note_lowest || key_note > note_highest) continue;
                selected_notes[melodic] = key_note;
                held_key = key;
                audition_value = key_note | (melodic == MELODIC_PAD && selected_major ? NOTE_MAJOR : 0u);
                placed_value = melodic == MELODIC_ARP ? ARP_HIT : audition_value;   /* (ARP: the key sounds, a hit is placed) */
                if (recording_on) {             /* recording: the note goes to the cursor, which moves on past it (the pad's: a bar on), round the track */
                    const uint32_t length = melodic == MELODIC_PAD ? LENGTH_WHOLE_FIRST : selected_lengths[melodic];
                    put_note(bar_notes, bar_gates, melodic, cursor_steps[melodic], placed_value, length);
                    position = (shown_bar * STEPS_PER_BAR + cursor_steps[melodic] + (melodic == MELODIC_PAD ? STEPS_PER_BAR : length_steps(length))) % (bar_total * STEPS_PER_BAR);
                    shown_bar = position / STEPS_PER_BAR;
                    cursor_steps[melodic] = position % STEPS_PER_BAR;
                    bar_notes = pattern->notes[melodic][shown_bar];
                    bar_gates = pattern->gates[melodic][shown_bar];
                    roll_drawn = 0;
                    song_changed = 1;
                }
            }
            placed_value = melodic == MELODIC_ARP ? ARP_HIT : selected_notes[melodic] | (melodic == MELODIC_PAD && selected_major ? NOTE_MAJOR : 0u);
            for (step = 0; step < STEPS_PER_BAR; step++) {
                if (keys_edit && keys_pressed >> STEP_KEYS[step] & 1u) {   /* a note on it (or over it) goes; on an empty step the next note goes there */
                    const uint32_t start = note_start(bar_notes, melodic, step);
                    if (start < STEPS_PER_BAR) remove_note(bar_notes, bar_gates, melodic, start);
                    else {
                        put_note(bar_notes, bar_gates, melodic, step, placed_value, melodic == MELODIC_PAD ? LENGTH_WHOLE_FIRST : selected_lengths[melodic]);
                        if (melodic != MELODIC_ARP) audition_value = placed_value;   /* (an ARP hit, as a drum's: silent) */
                    }
                    cursor_steps[melodic] = step;
                    roll_drawn = 0;
                    song_changed = 1;
                }
                if (bar_notes[step]) step_leds |= 1u << step;
            }
            if (cursor_start >= STEPS_PER_BAR) {
                shown_note = selected_notes[melodic];
                shown_major = melodic == MELODIC_PAD ? selected_major : 0u;
                shown_length = selected_lengths[melodic];
            }
            if (audition_value) {
                audition_notes[melodic] = audition_value;
                note_auditions[melodic]++;
            }
            /* the roll follows the note shown, two rows ahead of it while there are notes that way; the pad's
             * roll keeps the whole chord above it in view */
            chord_rows = melodic == MELODIC_PAD ? PAD_FIFTH : 0u;
            rows_above = chord_rows > ROLL_MARGIN_ROWS ? chord_rows : ROLL_MARGIN_ROWS;
            if (shown_note < roll_lowest_note + ROLL_MARGIN_ROWS)
                roll_lowest_note = shown_note < note_lowest + ROLL_MARGIN_ROWS ? note_lowest : shown_note - ROLL_MARGIN_ROWS;
            if (shown_note + rows_above >= roll_lowest_note + ROLL_ROWS)
                roll_lowest_note = (shown_note + rows_above > note_highest + chord_rows ? note_highest + chord_rows : shown_note + rows_above) - ROLL_ROWS + 1u;
            roll_lowest_notes[melodic] = roll_lowest_note;
        }
        for (track = 0; track < MELODIC_COUNT; track++)
            note_held[track] = (keys_play || recording_on) && page == MELODIC_FIRST_PAGE + track && (keys >> held_key & 1u);
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
            for (track = 0; track < TRACK_COUNT; track++) {   /* and the mute keys, lit where the track plays */
                keys_glowing |= 1u << TRACK_KEYS[track] | 1u << MUTE_KEYS[track];
                if (!(muted_tracks >> track & 1u)) keys_lit |= 1u << MUTE_KEYS[track];
            }
            if (!global_shown) keys_lit |= 1u << TRACK_KEYS[shown_track];
        } else if (page == PAGE_SONGS) {        /* no key does anything */
        } else if (page == PAGE_LIBRARY) {      /* the lower keys are the places, lit where there is a pattern */
            for (step = 0; step < LIBRARY_PLACES; step++) {
                keys_glowing |= 1u << STEP_KEYS[step];
                if (places_with_pattern >> step & 1u) keys_lit |= 1u << STEP_KEYS[step];
            }
        } else if (keys_edit) {                 /* the lower keys, lit where there is something; what runs (the step, the chain's place) flips its key, the cursor blinks */
            for (step = 0; step < STEPS_PER_BAR; step++) {
                if (page < TRACK_COUNT && step % 4u == 0u) keys_glowing_bright |= 1u << STEP_KEYS[step];   /* a beat */
                else keys_glowing |= 1u << STEP_KEYS[step];
                if ((step_leds >> step & 1u) ^ (step == (page == PAGE_GLOBAL ? marked_slot : marked_step))
                    ^ (notes_page && step == cursor_steps[melodic] && (fm1_ms / CURSOR_BLINK_MS & 1u)))
                    keys_lit |= 1u << STEP_KEYS[step];
            }
        } else if (page == PAGE_GLOBAL) {       /* playing: where the song is, the step on the lower keys, the bar on OP1 .. OP4 */
            if (playing) {
                const uint32_t bars_playing = pattern_bars(&song.patterns[playing_pattern]);
                for (track = 0; track < bars_playing; track++)
                    keys_glowing |= 1u << TRACK_KEYS[track];
                keys_lit = 1u << STEP_KEYS[step_now % STEPS_PER_BAR] | 1u << TRACK_KEYS[step_now / STEPS_PER_BAR % bars_playing];
            }
        } else if (page == PAGE_DRUMS) {        /* a key per drum, the selected one's lit */
            for (drum = 0; drum < DRUM_COUNT; drum++)
                keys_glowing |= 1u << STEP_KEYS[drum];
            keys_lit = 1u << STEP_KEYS[selected_drum];
        } else if (page != PAGE_GLOBAL) {       /* the keys with a note of the track, the shown note's key lit */
            const uint32_t first_note = keyboard_first_note(melodic, keyboard_octaves[melodic]);
            for (key = 0; key < KEY_COUNT; key++)
                if (first_note + key >= MELODIC[melodic].note_lowest && first_note + key <= MELODIC[melodic].note_highest) keys_glowing |= 1u << key;
            if (shown_note >= first_note && shown_note < first_note + KEY_COUNT)
                keys_lit = 1u << (shown_note - first_note);
        }

        if (splash_shown) {                     /* the start screen: the light runs over the lower keys, the rest is dark
                                                   (before the page's lights below: set and cleared again, they would flash) */
            int touched = buttons_pressed || keys_pressed || presets_detents || algorithm_detents;
            for (knob = 0; knob < KNOB_COUNT; knob++) touched |= detents[knob] != 0;
            for (key = 0; key <= BTN_REC; key++) {   /* (REC: the highest button id) */
                fm1_led_key(key, 0);
                fm1_led_key_glow(key, 0);
            }
            if (!touched && fm1_ms - splash_start_ms < SPLASH_MS) {
                const uint32_t light = (fm1_ms - splash_start_ms) * (STEPS_PER_BAR + SPLASH_TAIL_KEYS) / SPLASH_MS;   /* the lower key it is on, 0 the leftmost */
                for (key = 0; key < KEY_COUNT; key++) {
                    uint32_t behind = ~0u;      /* (keys ahead of it, and the upper keys: a huge number, dark) */
                    for (step = 0; step < STEPS_PER_BAR; step++)
                        if (STEP_KEYS[step] == key) behind = light - step;
                    fm1_led_key(KEY_LED(key), behind == 0u);
                    fm1_led_key_glow_bright(KEY_LED(key), behind >= 1u && behind <= SPLASH_TAIL_KEYS / 2u);
                    fm1_led_key_glow(KEY_LED(key), behind > SPLASH_TAIL_KEYS / 2u && behind <= SPLASH_TAIL_KEYS);
                }
                continue;
            }
            splash_shown = 0;                   /* over: the page from scratch, the buttons that always do something glow */
            lcd_fill(0, 0, 240, 240, C_BLACK);
            page_drawn = ~0u;
            fm1_led_key_glow(BTN_PLAY, 1);
            fm1_led_key_glow(BTN_HOME, 1);
            fm1_led_key_glow(BTN_SEL, 1);
            fm1_led_key_glow(BTN_EDIT, 1);
            fm1_led_key_glow(BTN_SAVE, 1);
        }
        for (key = 0; key < KEY_COUNT; key++) {
            fm1_led_key_glow(KEY_LED(key), keys_glowing >> key & 1u);
            fm1_led_key_glow_bright(KEY_LED(key), keys_glowing_bright >> key & 1u);
            fm1_led_key(KEY_LED(key), keys_lit >> key & 1u);
        }
        fm1_led_key(BTN_PLAY, playing && step_now % 4u == 0u);   /* playing: lit on each beat's first step, the beat */
        fm1_led_key(BTN_HOME, page == PAGE_GLOBAL);
        fm1_led_key(BTN_SAVE, page == PAGE_SONGS || page == PAGE_LIBRARY);
        fm1_led_key_glow(BTN_FX, page < TRACK_COUNT);   /* a track's other pages are there */
        fm1_led_key(BTN_FX, sound_page || mix_page);
        fm1_led_key(BTN_SEQ, chain_on != 0);
        fm1_led_key_glow(BTN_SEQ, page == PAGE_SONGS ? places_with_song >> selected_place & 1u
                                  : page == PAGE_LIBRARY ? places_with_pattern >> selected_library_place & 1u : edit_on != 0);
        fm1_led_key(BTN_SEL, select_on != 0);
        fm1_led_key(BTN_EDIT, edit_on != 0);
        fm1_led_key_glow(BTN_OCT_DOWN, recording_on ? keyboard_octaves[melodic] > 0u : shown_bar > 0u);   /* there is a bar (recording: an octave) that way */
        fm1_led_key_glow(BTN_OCT_UP, recording_on ? keyboard_octaves[melodic] + 1u < KEYBOARD_OCTAVES
                                     : shown_bar + 1u < bar_total || (edit_on && page < TRACK_COUNT && bar_total < BAR_COUNT));
        fm1_led_key(BTN_OCT_DOWN, recording_on && keyboard_octaves[melodic] < 1u);   /* recording: the keys are an octave that way */
        fm1_led_key(BTN_OCT_UP, recording_on && keyboard_octaves[melodic] > 1u);
        fm1_led_key(BTN_REC, recording_on != 0);
        fm1_led_key_glow(BTN_REC, page == PAGE_SONGS ? selected_place >= PLACE_FIRST_KEPT : page == PAGE_LIBRARY || ((page == PAGE_GLOBAL || (page < TRACK_COUNT && !mix_page)) && edit_on));

        /* the screen: only what changed. Each *_drawn holds what is on the screen (+ 1, so 0 means "draw it") */
        if (page + (sound_page ? 16u : 0u) + (mix_page ? 32u : 0u) != page_drawn) {   /* (a sound or mix page is a page of its own) */
            page_drawn = page + (sound_page ? 16u : 0u) + (mix_page ? 32u : 0u);
            for (knob = 0; knob < DRUM_COUNT; knob++) mix_drawn[knob] = 0;
            lcd_fill(0, BODY_TOP, 240, BODY_HEIGHT, C_BLACK);
            header_drawn = roll_drawn = roll_marker_drawn = chain_drawn = songs_drawn = library_drawn = sound_drawn = 0;
            for (drum = 0; drum < DRUMS_PER_PAGE; drum++) drum_row_drawn[drum] = 0;
            drum_pages_drawn = 0;
            for (knob = 0; knob < KNOB_COUNT; knob++) knob_drawn[knob] = 0;
        }
        state = 1u + (shown_bar | bar_total << 4 | marked_bar << 8 | shown_pattern << 12 | edit_on << 16 | muted_tracks << 17);
        if (state != header_drawn) {
            header_drawn = state;
            draw_header(page, shown_pattern, bar_total, shown_bar, marked_bar, edit_on != 0, page < TRACK_COUNT && muted_tracks >> page & 1u);
        }
        if (page == PAGE_GLOBAL) {
            state = 1u + (shown_pattern | marked_slot << 4 | (uint32_t)(chain_on != 0) << 12);   /* (a changed place sets chain_drawn to 0) */
            if (state != chain_drawn) {
                chain_drawn = state;
                draw_chain(shown_pattern, marked_slot, chain_on != 0);
            }
        } else if (page == PAGE_DRUMS && !mix_page) {
            uint32_t row;                       /* the page of the selected drum */
            for (row = 0; row < DRUMS_PER_PAGE; row++) {
                uint32_t row_drum = selected_drum / DRUMS_PER_PAGE * DRUMS_PER_PAGE + row;
                uint32_t hits = pattern->drum_hits[row_drum][shown_bar];
                state = 1u + (hits | marked_step << 16 | (uint32_t)(row_drum == selected_drum) << 24 | row_drum << 25);
                if (state != drum_row_drawn[row]) {
                    drum_row_drawn[row] = state;
                    draw_drum_row(row_drum, row, hits, row_drum == selected_drum, marked_step);
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
        } else if (mix_page) {
            static const char *const MIX_NAMES[4] = {"LEVEL", "PAN", "REVERB", "ECHO"};
            const uint32_t rows = page == PAGE_DRUMS ? DRUM_COUNT : 4u, row_melodic = page - MELODIC_FIRST_PAGE;
            uint32_t row;
            for (row = 0; row < rows; row++) {
                const uint32_t value = page == PAGE_DRUMS ? song.drum_levels[row]
                                     : row == 0u ? song.levels[row_melodic] : row == 1u ? song.pans[page]
                                     : row == 2u ? song.reverb_sends[page] : song.echo[row_melodic];
                const int selected = page == PAGE_DRUMS && row == selected_drum;
                state = 1u + (value | (uint32_t)selected << 8);
                if (state != mix_drawn[row]) {
                    mix_drawn[row] = state;
                    draw_mix_row(row, page == PAGE_DRUMS ? DRUM_NAMES[row] : MIX_NAMES[row], value, page != PAGE_DRUMS && row == 1u, selected);
                }
            }
        } else if (sound_page) {
            state = 1u + (song.sound_choices[melodic] | (uint32_t)sound_edited(melodic) << 4 | sound_note << 5);
            if (state != sound_drawn) {
                sound_drawn = state;
                draw_sound(melodic, sound_edited(melodic), sound_note);
            }
        } else {
            state = 1u + (shown_bar | shown_note << 4 | roll_lowest_notes[melodic] << 12 | shown_pattern << 20 | cursor_steps[melodic] << 23);   /* (a changed step sets roll_drawn to 0) */
            if (state != roll_drawn) {
                roll_drawn = state;
                draw_roll(pattern, melodic, shown_bar, shown_note, roll_lowest_notes[melodic], cursor_steps[melodic]);
            }
            if (1u + marked_step != roll_marker_drawn) {
                roll_marker_drawn = 1u + marked_step;
                draw_roll_marker(marked_step);
            }
        }
        for (knob = 0; knob < KNOB_COUNT; knob++) {
            static const char *const MELODIC_LABELS[KNOB_COUNT] = {"NOTE", "CUTOFF", "DECAY", "LENGTH"};
            static const char *const PAD_LABELS[KNOB_COUNT] = {"NOTE", "CUTOFF", "CHORD", ""};
            static const char *const ARP_LABELS[KNOB_COUNT] = {"MODE", "CUTOFF", "DECAY", "LENGTH"};
            static const char *const MIX_LABELS[KNOB_COUNT] = {"LEVEL", "PAN", "REVERB", "ECHO"};
            static const char *const GLOBAL_LABELS[KNOB_COUNT] = {"TEMPO", "REVERB", "SWING", ""};
            static const char *const SONGS_LABELS[KNOB_COUNT] = {"SONG", "", "", ""};
            static const char *const LIBRARY_LABELS[KNOB_COUNT] = {"PLACE", "", "", ""};
            static const char *const SYNTH_SOUND_LABELS[KNOB_COUNT] = {"WAVE", "RESO", "ATTACK", "ENV"};
            static const char *const PAD_SOUND_LABELS[KNOB_COUNT] = {"SPREAD", "RESO", "ATTACK", "RELEASE"};
            const int pad_shown = page == MELODIC_FIRST_PAGE + MELODIC_PAD, arp_shown = page == MELODIC_FIRST_PAGE + MELODIC_ARP;
            const int mode_shown = arp_shown && notes_page && knob == 0u;   /* (a word: small) */
            const uint32_t melodic_values[KNOB_COUNT] = {arp_shown ? pattern->arp_mode : shown_note, song.cutoff[melodic],
                                                         pad_shown ? shown_major : song.decay[melodic], shown_length};
            const uint32_t global_values[KNOB_COUNT] = {external_clock ? external_bpm : song.tempo, song.reverb,
                                                        50u + song.patterns[shown_pattern].swing, 0u};   /* SWING in %: 50 straight */
            const int wave_shown = sound_page && !pad_shown && knob == 0u;   /* (a word: small) */
            const char *label = page == PAGE_GLOBAL && knob == 0u && external_clock ? "EXT" : mix_page ? MIX_LABELS[knob] : sound_page ? (pad_shown ? PAD_SOUND_LABELS[knob] : SYNTH_SOUND_LABELS[knob]) : page == PAGE_SONGS ? SONGS_LABELS[knob] : page == PAGE_LIBRARY ? LIBRARY_LABELS[knob] : page == PAGE_GLOBAL ? GLOBAL_LABELS[knob] : page == PAGE_DRUMS ? ""   /* (the selected drum's row is orange: that says it) */
                              : pad_shown ? PAD_LABELS[knob] : arp_shown ? ARP_LABELS[knob] : MELODIC_LABELS[knob];
            const uint32_t mix_values[KNOB_COUNT] = {page == PAGE_DRUMS ? song.drum_levels[selected_drum] : song.levels[melodic],
                                                     song.pans[page < TRACK_COUNT ? page : 0u], song.reverb_sends[page < TRACK_COUNT ? page : 0u],
                                                     page == PAGE_DRUMS ? song.drum_echo : song.echo[melodic]};
            uint32_t value = mix_page ? mix_values[knob] : sound_page ? song.sounds[melodic][knob] : page == PAGE_SONGS ? (knob ? 0u : selected_place) : page == PAGE_LIBRARY ? (knob ? 0u : selected_library_place + 1u) : page == PAGE_GLOBAL ? global_values[knob]
                           : page == PAGE_DRUMS ? 0u : melodic_values[knob];
            if (1u + value != knob_drawn[knob]) {
                char number[8];
                const char *text = number;
                if (!label[0]) text = "";
                else if (wave_shown) text = WAVE_NAMES[value];
                else if (mix_page && knob == 1u) {   /* PAN: L50 .. C .. R50 */
                    if (value == MIX_MAX / 2u) text = "C";
                    else {
                        number[0] = value < MIX_MAX / 2u ? 'L' : 'R';
                        format_number(number + 1, value < MIX_MAX / 2u ? MIX_MAX / 2u - value : value - MIX_MAX / 2u);
                    }
                }
                else if (mix_page) format_number(number, value);
                else if (sound_page) format_number(number, value);
                else if (page == PAGE_SONGS) text = PLACE_NAMES[value];
                else if (page == PAGE_LIBRARY) format_number(number, value);
                else if (mode_shown) text = ARP_MODE_NAMES[value];
                else if (page != PAGE_GLOBAL && knob == 0u) format_note(number, value);
                else if (pad_shown && knob == 2u) text = CHORD_NAMES[value];
                else if (page != PAGE_GLOBAL && knob == 3u && value < LENGTH_WHOLE_FIRST) {   /* LENGTH: 10 % .. 90 % of a step */
                    number[0] = (char)('0' + value);
                    number[1] = '0';
                    number[2] = '%';
                    number[3] = 0;
                }
                else if (page != PAGE_GLOBAL && knob == 3u) format_number(number, length_steps(value));
                else format_number(number, value);
                knob_drawn[knob] = 1u + value;
                draw_knob(knob, label, text, wave_shown || mode_shown ? 0 : page == PAGE_SONGS ? value >= PLACE_FIRST_KEPT : mix_page || page != PAGE_DRUMS);
            }
        }

        if (page == PAGE_DRUMS && !mix_page && 1u + selected_drum / DRUMS_PER_PAGE != drum_pages_drawn) {   /* (after the knobs: over their empty places) */
            drum_pages_drawn = 1u + selected_drum / DRUMS_PER_PAGE;
            draw_drum_pages(selected_drum / DRUMS_PER_PAGE);
        }

        if (place_to_save < PLACE_COUNT && !playing && sound_quiet()) {   /* (as below: only when stopped and silent) */
            if (!blob_save(place_to_save - PLACE_FIRST_KEPT, (const void *)&song, sizeof song)) {
                places_with_song |= 1u << place_to_save;
                if (songs_note == SONGS_NOTE_SAVING) songs_note = SONGS_NOTE_SAVED;
            }
            place_to_save = PLACE_COUNT;
        }
        if (user_sounds_unsaved && !playing && sound_quiet()) {   /* (a slot: four sectors erased, ~200 ms) */
            user_sounds_unsaved = 0;
            slot_save(USER_SOUNDS_SLOT, user_sounds, sizeof user_sounds);
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
