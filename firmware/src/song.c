/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's song: the tracks, what a pattern and a song hold, the sounds, the names; checking a kept song or
 * pattern and growing one kept in an older size; putting notes on a track and taking them away. juicy.c
 * includes it after the sound engines. */

/* ---- the melodic tracks: BASS, ARP and LEAD are each one synth voice (synth.c) whose shape is the track's
 * sound (below), PAD is the pad (pad.c), where a note is the root of a chord. ARP's steps hold no notes: a hit
 * plays the next note of the chord the pad sounds, in the order of the pattern's MODE */
enum { MELODIC_BASS, MELODIC_ARP, MELODIC_PAD, MELODIC_LEAD, MELODIC_COUNT };
#define NOTE_MASK 0x7Fu                         /* a step of a melodic track: the MIDI note, 0 = none ... */
#define CHORD_SHIFT 7u                          /* ... and on the pad's track: the chord (PAD_CHORD_*) above it, root | chord << 7.
                                                   A step holds its lowest bit here, the rest in its gate (the pad's chords have none) */
#define CHORD(root, chord) ((root) | (chord) << CHORD_SHIFT)
#define NOTE_TIE 0x80u                          /* ... and on BASS, ARP, LEAD: the note before goes on through this step (the same note) */
#define ARP_HIT 1u                              /* ARP's step: a hit starts there (its note comes from the chord) */
#define ARP_OCTAVES_MAX 3u                      /* MODE: an order (ARP_ORDER_*) over 1 .. 3 octaves, order * 3 + octaves - 1 */
enum { ARP_ORDER_UP, ARP_ORDER_DOWN, ARP_ORDER_UP_DOWN, ARP_ORDER_RANDOM, ARP_ORDER_COUNT };
#define ARP_MODE_MAX (ARP_ORDER_COUNT * ARP_OCTAVES_MAX - 1u)
#define ARP_MODE(order, octaves) ((order) * ARP_OCTAVES_MAX + (octaves) - 1u)
static const char *const ARP_MODE_NAMES[ARP_MODE_MAX + 1u] = {"UP 1", "UP 2", "UP 3", "DOWN 1", "DOWN 2", "DOWN 3",
                                                             "UP/DN 1", "UP/DN 2", "UP/DN 3", "RAND 1", "RAND 2", "RAND 3"};
#define NAME_LENGTH 8u                          /* a song's or a pattern's name: 8 characters, by number in NAME_CHARACTERS */
static const char NAME_CHARACTERS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";   /* (0: a space; a new name is all spaces) */
#define NAME_CHARACTER_COUNT (sizeof NAME_CHARACTERS - 1u)
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
    uint8_t notes[MELODIC_COUNT][BAR_COUNT][STEPS_PER_BAR];   /* what is on that step: NOTE_MASK, the chord's lowest bit, NOTE_TIE */
    uint8_t bar_totals[1u + MELODIC_COUNT];     /* how many bars a track has, 1..BAR_COUNT: the drums, then the melodic tracks */
    uint8_t swing;                              /* how late every second step sounds, 0..SWING_MAX: SWING 50..80 % */
    uint8_t gates[MELODIC_COUNT][BAR_COUNT][STEPS_PER_BAR / 2u];   /* a step's note shorter than the step: 4 bits a step (the even step low), 0..GATE_TENTHS_MAX; the pad's: the chord's other bits */
    uint8_t arp_mode;                           /* ARP's MODE, 0..ARP_MODE_MAX */
    uint16_t accents[1u + MELODIC_COUNT][BAR_COUNT];   /* bit s: step s is accented. The drums: every drum on it; a melodic track
                                                   (not the pad): the note starting there. A track's place as in bar_totals */
    uint8_t name[NAME_LENGTH];                  /* its name (the NAMES page), NAME_CHARACTERS by number */
} pattern_t;
#define PATTERN_SIZE_BEFORE_GATES 310u          /* a pattern kept before there were gates: all of it up to them */
#define PATTERN_SIZE_BEFORE_ARP 438u            /* ... and before there was ARP (its track was SEQUENCE): all of it up to MODE */
#define PATTERN_SIZE_BEFORE_ACCENTS 440u        /* ... and before there were accents */
#define PATTERN_SIZE_BEFORE_NAMES 480u          /* ... and before there were names */
_Static_assert(offsetof(pattern_t, gates) == PATTERN_SIZE_BEFORE_GATES && offsetof(pattern_t, arp_mode) == PATTERN_SIZE_BEFORE_ARP
               && offsetof(pattern_t, accents) == PATTERN_SIZE_BEFORE_ACCENTS && offsetof(pattern_t, name) == PATTERN_SIZE_BEFORE_NAMES
               && sizeof(pattern_t) == 488u, "what is new comes after what was there");
#define PATTERN_SIZES_COUNT 5u
static const uint32_t PATTERN_SIZES[PATTERN_SIZES_COUNT] = {sizeof(pattern_t), PATTERN_SIZE_BEFORE_NAMES, PATTERN_SIZE_BEFORE_ACCENTS,
                                                            PATTERN_SIZE_BEFORE_ARP, PATTERN_SIZE_BEFORE_GATES};   /* now, then as kept before */

static uint32_t note_gate(const volatile uint8_t *bar_gates, uint32_t step)   /* bar_gates: a track's bar in gates */
{
    return bar_gates[step / 2u] >> (step % 2u * 4u) & 0xFu;
}

static void set_note_gate(volatile uint8_t *bar_gates, uint32_t step, uint32_t gate)
{
    const uint32_t shift = step % 2u * 4u;
    bar_gates[step / 2u] = (uint8_t)((bar_gates[step / 2u] & ~(0xFu << shift)) | gate << shift);
}

/* what is on a melodic track's step as one value: the pad's with all of its chord (the rest of it is in the gate) */
static uint32_t step_value(const volatile uint8_t *bar_notes, const volatile uint8_t *bar_gates, uint32_t melodic, uint32_t step)
{
    return melodic == MELODIC_PAD ? bar_notes[step] | note_gate(bar_gates, step) << 8 : bar_notes[step];
}

/* the chord the pad holds at a step of a pattern (bar: as the pattern plays it, the pad's own bars go round): the
 * last one on the pad's track up to there, as the sequencer holds it (none from the pattern's start on), 0: none */
static uint32_t pad_chord_at(const volatile pattern_t *pattern, uint32_t bar, uint32_t step)
{
    const uint32_t pad_bars = pattern->bar_totals[1u + MELODIC_PAD];
    uint32_t looked;
    bar %= BAR_COUNT;                           /* (a pattern has no more: the search stays short) */
    for (looked = 0; looked < BAR_COUNT * STEPS_PER_BAR; looked++) {
        const uint32_t pad_bar = bar % pad_bars;
        const uint32_t chord = step_value(pattern->notes[MELODIC_PAD][pad_bar], pattern->gates[MELODIC_PAD][pad_bar], MELODIC_PAD, step);
        if (chord) return chord;
        if (step) step--;
        else if (bar) {
            bar--;
            step = STEPS_PER_BAR - 1u;
        } else break;
    }
    return 0;
}

static uint32_t chord_notes(uint32_t chord)     /* bit n: the chord (as on the pad's track, 0: none) has a note n semitones above a C */
{
    const uint32_t type = chord >> CHORD_SHIFT, root = chord & NOTE_MASK;
    uint32_t notes = 0, note;
    for (note = 0; chord && note < PAD_CHORDS[type].note_count; note++)
        notes |= 1u << (root + PAD_CHORDS[type].intervals[note]) % 12u;
    return notes;
}

static void bytes_move_up(void *to, const void *from, uint32_t count)   /* to at or after from: the last byte first (libc.c has no memmove) */
{
    uint8_t *destination = to;
    const uint8_t *source = from;
    while (count-- > 0u) destination[count] = source[count];
}

/* patterns kept in an older size (kept_size), read into the start of where count of them go: each moves to its
 * place (the last first, so none is overwritten before it moved), what is new after it zero (the notes whole steps,
 * MODE UP 1, no accents). Kept before ARP, its track was SEQUENCE, with notes of its own: they go */
static void patterns_grow(pattern_t *patterns, uint32_t count, uint32_t kept_size)
{
    while (count-- > 0u) {
        bytes_move_up(&patterns[count], (const uint8_t *)patterns + count * kept_size, kept_size);
        memset((uint8_t *)&patterns[count] + kept_size, 0, sizeof(pattern_t) - kept_size);
        if (kept_size > PATTERN_SIZE_BEFORE_ARP) continue;
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

/* a song's key: KEY_OFF (no help with the notes), or a root (0 C .. 11 B) and minor or major */
#define KEY_OFF 0u
#define SONG_KEY(root, major) (1u + (root) * 2u + (major))
#define SONG_KEY_MAX SONG_KEY(11u, 1u)
static const uint16_t KEY_SCALES[2] = {0x5ADu, 0xAB5u};   /* bit n: n semitones above the root is in it: natural minor, major */

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
    uint8_t name[NAME_LENGTH];                  /* its name (the NAMES page), as a pattern's */
    uint8_t key;                                /* its key (JUICY's KNOB4): KEY_OFF, or SONG_KEY(root, major); was padding: beyond
                                                   SONG_KEY_MAX it counts as KEY_OFF (song_key) */
} song_t;
/* a song kept with patterns that size: older ones (all before there were names) have no name of their own either */
#define SONG_SIZE_KEPT(pattern_size) ((pattern_size) == sizeof(pattern_t) ? sizeof(song_t)                                       : sizeof(song_t) - PATTERN_COUNT * (sizeof(pattern_t) - (pattern_size)) - NAME_LENGTH)
_Static_assert(offsetof(song_t, patterns) == 0u, "the patterns first: a song kept with smaller ones grows in place");
_Static_assert(sizeof(song_t) == 3984u, "the key took the padding byte: a song is as big as before");

static void song_grow(song_t *grown, uint32_t kept_pattern_size)   /* a song kept with patterns of an older size, read into its start */
{
    bytes_move_up(&grown->chain, (const uint8_t *)grown + PATTERN_COUNT * kept_pattern_size, sizeof(song_t) - offsetof(song_t, chain));
    patterns_grow(grown->patterns, PATTERN_COUNT, kept_pattern_size);
    memset(grown->name, 0, sizeof grown->name);
    grown->key = KEY_OFF;
}

static int name_valid(const volatile uint8_t *name)
{
    uint32_t place;
    for (place = 0; place < NAME_LENGTH; place++)
        if (name[place] >= NAME_CHARACTER_COUNT) return 0;
    return 1;
}

static int name_empty(const volatile uint8_t *name)
{
    uint32_t place;
    for (place = 0; place < NAME_LENGTH; place++)
        if (name[place]) return 0;
    return 1;
}

static void name_text(char *text, const volatile uint8_t *name)   /* text: NAME_LENGTH + 1 bytes (the spaces at the end left out) */
{
    uint32_t place, length = 0;
    for (place = 0; place < NAME_LENGTH; place++) {
        text[place] = NAME_CHARACTERS[name[place]];
        if (name[place]) length = place + 1u;
    }
    text[length] = 0;
}

static void name_set(volatile uint8_t *name, const char *text)   /* (the demo songs') text of NAME_CHARACTERS, up to NAME_LENGTH */
{
    uint32_t place, character;
    for (place = 0; place < NAME_LENGTH; place++) {
        name[place] = 0;
        for (character = 0; *text && character < NAME_CHARACTER_COUNT; character++)
            if (NAME_CHARACTERS[character] == *text) name[place] = (uint8_t)character;
        if (*text) text++;
    }
}

static volatile song_t song;

static uint32_t song_key(void)                  /* the song's key; an old save's padding there counts as KEY_OFF */
{
    return song.key <= SONG_KEY_MAX ? song.key : KEY_OFF;
}

#define PAD_CHORD_AUTO PAD_CHORD_COUNT          /* the next chord's type (never on a step): the one that fits the song's key */

static int note_in_key(uint32_t key, uint32_t note)   /* (KEY_OFF: every note is) */
{
    const uint32_t root = (key - 1u) / 2u, major = (key - 1u) % 2u;
    return key == KEY_OFF || (KEY_SCALES[major] >> ((note % 12u + 12u - root) % 12u) & 1u);
}

/* a chord's type for a root: as chosen, or for PAD_CHORD_AUTO the one the song's key has on that root (its third and
 * fifth in the key: major, minor or diminished; a root outside the key: major), without a key minor (as before) */
static uint32_t pad_chord_for(uint32_t root, uint32_t chord)
{
    const uint32_t key = song_key();
    if (chord != PAD_CHORD_AUTO) return chord;
    if (key == KEY_OFF) return PAD_CHORD_MINOR;
    if (note_in_key(key, root + 4u) && note_in_key(key, root + 7u)) return PAD_CHORD_MAJOR;
    if (note_in_key(key, root + 3u) && note_in_key(key, root + 7u)) return PAD_CHORD_MINOR;
    if (note_in_key(key, root + 3u) && note_in_key(key, root + 6u)) return PAD_CHORD_DIMINISHED;
    return PAD_CHORD_MAJOR;
}

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
    {4, {"PLUCK", "SUB", "ACID", "REESE"},       /* BASS (ACID: the first song's) */
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

static int song_has_chain(void)                 /* a place of the song's chain holds a pattern */
{
    uint32_t slot;
    for (slot = 0; slot < CHAIN_SLOTS; slot++)
        if (song.chain[slot]) return 1;
    return 0;
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

typedef struct {                                /* a pattern's drums, kept aside (while a groove plays in their place) */
    uint16_t hits[DRUM_COUNT][BAR_COUNT], accents[BAR_COUNT];
    uint8_t bars;
} drums_kept_t;

static void drums_keep(const volatile pattern_t *pattern, drums_kept_t *kept)
{
    uint32_t drum, bar;
    for (bar = 0; bar < BAR_COUNT; bar++) {
        for (drum = 0; drum < DRUM_COUNT; drum++) kept->hits[drum][bar] = pattern->drum_hits[drum][bar];
        kept->accents[bar] = pattern->accents[0][bar];
    }
    kept->bars = pattern->bar_totals[0];
}

static void drums_put_back(volatile pattern_t *pattern, const drums_kept_t *kept)
{
    uint32_t drum, bar;
    for (bar = 0; bar < BAR_COUNT; bar++) {
        for (drum = 0; drum < DRUM_COUNT; drum++) pattern->drum_hits[drum][bar] = kept->hits[drum][bar];
        pattern->accents[0][bar] = kept->accents[bar];
    }
    pattern->bar_totals[0] = kept->bars;
}

static int drums_kept_empty(const drums_kept_t *kept)   /* (no hit: nothing to ask about before it is replaced) */
{
    uint32_t drum, bar;
    for (bar = 0; bar < BAR_COUNT; bar++)
        for (drum = 0; drum < DRUM_COUNT; drum++)
            if (kept->hits[drum][bar]) return 0;
    return 1;
}

typedef struct {                                /* a pattern's melodic track, kept aside (while a suggestion plays in its place) */
    uint8_t notes[BAR_COUNT][STEPS_PER_BAR], gates[BAR_COUNT][STEPS_PER_BAR / 2u];
    uint16_t accents[BAR_COUNT];
    uint8_t bars;
} track_kept_t;

static void track_keep(const volatile pattern_t *pattern, uint32_t melodic, track_kept_t *kept)
{
    memcpy(kept->notes, (const void *)pattern->notes[melodic], sizeof kept->notes);
    memcpy(kept->gates, (const void *)pattern->gates[melodic], sizeof kept->gates);
    memcpy(kept->accents, (const void *)pattern->accents[1u + melodic], sizeof kept->accents);
    kept->bars = pattern->bar_totals[1u + melodic];
}

static void track_put_back(volatile pattern_t *pattern, uint32_t melodic, const track_kept_t *kept)
{
    memcpy((void *)pattern->notes[melodic], kept->notes, sizeof kept->notes);
    memcpy((void *)pattern->gates[melodic], kept->gates, sizeof kept->gates);
    memcpy((void *)pattern->accents[1u + melodic], kept->accents, sizeof kept->accents);
    pattern->bar_totals[1u + melodic] = kept->bars;
}

static int track_kept_empty(const track_kept_t *kept)   /* (no note: nothing to ask about before it is replaced) */
{
    uint32_t bar, step;
    for (bar = 0; bar < BAR_COUNT; bar++)
        for (step = 0; step < STEPS_PER_BAR; step++)
            if (kept->notes[bar][step]) return 0;
    return 1;
}

#define ALL_TRACKS (1u + MELODIC_COUNT)         /* pattern_clear: every track */

/* emptied, its bars stay: every track (ALL_TRACKS, the accents too), the drums' drum (the drums' accents are every
 * drum's: they stay), or a melodic track (1 + melodic) with its accents */
static void pattern_clear(volatile pattern_t *pattern, uint32_t track, uint32_t drum)
{
    uint32_t bar, cleared_drum, melodic, step, accents_track;
    for (bar = 0; bar < BAR_COUNT; bar++) {
        for (cleared_drum = 0; cleared_drum < DRUM_COUNT; cleared_drum++)
            if (track == ALL_TRACKS || (track == 0u && cleared_drum == drum)) pattern->drum_hits[cleared_drum][bar] = 0;
        for (melodic = 0; melodic < MELODIC_COUNT; melodic++)
            for (step = 0; step < STEPS_PER_BAR; step++)
                if (track == ALL_TRACKS || track == 1u + melodic) {
                    pattern->notes[melodic][bar][step] = 0;
                    set_note_gate(pattern->gates[melodic][bar], step, 0);
                }
        for (accents_track = 0; accents_track < 1u + MELODIC_COUNT; accents_track++)
            if (track == ALL_TRACKS || (track == accents_track && track != 0u)) pattern->accents[accents_track][bar] = 0;
    }
}

/* a track's bar taken out (track: 0 the drums, 1 + melodic): the bars after it move up, the last one is empty; a track
 * keeps one bar */
static void pattern_remove_bar(volatile pattern_t *pattern, uint32_t track, uint32_t removed_bar)
{
    uint32_t bar, drum, step;
    for (bar = removed_bar; bar < BAR_COUNT; bar++) {
        for (drum = 0; track == 0u && drum < DRUM_COUNT; drum++)
            pattern->drum_hits[drum][bar] = bar + 1u < BAR_COUNT ? pattern->drum_hits[drum][bar + 1u] : 0u;
        for (step = 0; track != 0u && step < STEPS_PER_BAR; step++)
            pattern->notes[track - 1u][bar][step] = bar + 1u < BAR_COUNT ? pattern->notes[track - 1u][bar + 1u][step] : 0u;
        for (step = 0; track != 0u && step < STEPS_PER_BAR / 2u; step++)   /* (two steps' gates a byte) */
            pattern->gates[track - 1u][bar][step] = bar + 1u < BAR_COUNT ? pattern->gates[track - 1u][bar + 1u][step] : 0u;
        pattern->accents[track][bar] = bar + 1u < BAR_COUNT ? pattern->accents[track][bar + 1u] : 0u;
    }
    if (pattern->bar_totals[track] > 1u) pattern->bar_totals[track]--;
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
                if (melodic == MELODIC_PAD ? step_value(checked->notes[melodic][bar], checked->gates[melodic][bar], melodic, step) >> CHORD_SHIFT >= PAD_CHORD_COUNT
                                           : note_gate(checked->gates[melodic][bar], step) > GATE_TENTHS_MAX) return 0;
    return checked->arp_mode <= ARP_MODE_MAX && name_valid(checked->name);
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
    return checked->tempo >= TEMPO_MIN && checked->tempo <= TEMPO_MAX && checked->reverb <= REVERB_MAX && name_valid(checked->name);
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

/* value on step, length as LENGTH shows it (whole steps to the bar's end at most; the pad's chords: a step, the gate
 * holds the rest of the chord), over what was there: a note that started there goes, one whose start it covers keeps its rest */
static void put_note(volatile uint8_t *bar_notes, volatile uint8_t *bar_gates, uint32_t melodic, uint32_t step, uint32_t value, uint32_t length)
{
    uint32_t placed_step;
    if (note_start(bar_notes, melodic, step) == step) remove_note(bar_notes, bar_gates, melodic, step);
    for (placed_step = step; placed_step < step + length_steps(length) && placed_step < STEPS_PER_BAR; placed_step++) {
        bar_notes[placed_step] = (uint8_t)(placed_step == step ? value : value | NOTE_TIE);
        set_note_gate(bar_gates, placed_step, placed_step != step ? 0u : melodic == MELODIC_PAD ? value >> 8 : length_gate(length));
    }
    if (melodic != MELODIC_PAD && placed_step < STEPS_PER_BAR && bar_notes[placed_step] & NOTE_TIE)
        bar_notes[placed_step] = (uint8_t)(bar_notes[placed_step] & NOTE_MASK);
}
