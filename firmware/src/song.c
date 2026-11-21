/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's song: the tracks, what a pattern and a song hold, the sounds, the names; checking a kept song or
 * pattern; putting notes on a track and taking them away. juicy.c
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
#define ARP_HIT 1u                              /* ARP's step: a hit starts there (its note comes from the chord) ... */
#define ARP_TONES_MAX 8u                        /* ... the hit is its tone, 1 .. 8 (ARP_HIT: 1): in MODE TONES the chord's note it plays */
#define ARP_OCTAVES_MAX 3u                      /* MODE: an order (ARP_ORDER_*) over 1 .. 3 octaves, order * 3 + octaves - 1 */
enum { ARP_ORDER_UP, ARP_ORDER_DOWN, ARP_ORDER_UP_DOWN, ARP_ORDER_RANDOM, ARP_ORDER_COUNT };
#define ARP_MODE_TONES (ARP_ORDER_COUNT * ARP_OCTAVES_MAX)   /* MODE TONES: each hit plays its own tone of the chord, not the next one */
#define ARP_MODE_GEN (ARP_MODE_TONES + 1u)      /* MODE GEN: ARP plays itself, a loop of notes that changes now and then (the GEN page) */
#define ARP_MODE_MAX ARP_MODE_GEN
#define ARP_MODE(order, octaves) ((order) * ARP_OCTAVES_MAX + (octaves) - 1u)
static const char *const ARP_MODE_NAMES[ARP_MODE_MAX + 1u] = {"UP 1", "UP 2", "UP 3", "DOWN 1", "DOWN 2", "DOWN 3",
                                                             "UP/DN 1", "UP/DN 2", "UP/DN 3", "RAND 1", "RAND 2", "RAND 3", "TONES", "GEN"};
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
#define ECHO_TIMES 5u                           /* the echo's TIME: a sixteenth, an eighth, a dotted eighth, a quarter, a dotted quarter */
static const uint8_t ECHO_TIME_STEPS[ECHO_TIMES] = {1, 2, 3, 4, 6};
#define SOUND_KNOBS 8u                          /* a sound's two pages. SOUND: BASS, ARP, LEAD: WAVE, RESO, ATTACK, ENV; PAD: SPREAD, RESO,
                                                   ATTACK, RELEASE. OSC: BASS, ARP, LEAD: SUB, OSC2, LEVEL (OSC2's), GLIDE; PAD: SUB, -, -, GLIDE */
enum { SOUND_KNOB_WAVE, SOUND_KNOB_RESONANCE, SOUND_KNOB_ATTACK, SOUND_KNOB_ENVELOPE, SOUND_KNOB_SUB, SOUND_KNOB_OSC2,
       SOUND_KNOB_OSC2_LEVEL, SOUND_KNOB_GLIDE };   /* (the pad's first is SPREAD, its fourth RELEASE) */
#define SOUND_PAGE_KNOBS 4u                     /* a page's: SOUND the first four, OSC the next */

#define MIXER_TRACKS (1u + MELODIC_COUNT + 1u)   /* what a pattern's mix has a level and a mute for: the drums, the melodic tracks, the drone */
#define DRONE_MIXER_TRACK (1u + MELODIC_COUNT)

typedef struct {                                /* a melodic track's LFO (lfo.c), the knobs of its page */
    uint8_t rate;                               /* 0..LFO_RATE_COUNT - 1: a cycle of 4 << rate steps */
    uint8_t depth;                              /* 0..LFO_DEPTH_MAX: 0 it does nothing */
    uint8_t target;                             /* LFO_TARGET_* */
    uint8_t shape;                              /* LFO_SHAPE_* */
} lfo_t;

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
    uint8_t mixer_levels[MIXER_TRACKS];         /* its own mix (the MIXER page): each track's level in this pattern, 0..MIX_MAX, on top
                                                   of the track's LEVEL (100: as LEVEL says). A track's place as in bar_totals, then the drone */
    uint8_t mixer_mutes;                        /* bit t: track t (as there) is muted in this pattern, as SEL's mute keys mute it everywhere */
    lfo_t lfos[MELODIC_COUNT];                  /* a melodic track's own LFO in this pattern, where lfo_own says so */
    uint8_t lfo_own;                            /* bit m: melodic track m has its own LFO here (else it plays the song's) */
    uint8_t gen_change;                         /* ARP's MODE GEN (the GEN page): CHANGE 0..GEN_PARAMETER_MAX, how likely a step changes, */
    uint8_t gen_notes;                          /* NOTES 0..GEN_NOTES_MAX (GEN_NOTES_*), */
    uint8_t gen_density;                        /* DENSITY 0..GEN_PARAMETER_MAX, how many steps play, */
    uint8_t gen_length;                         /* LENGTH 1..GEN_LENGTH_MAX, the loop's steps */
} pattern_t;
/* GEN's NOTES: the notes of the chord ARP plays over, or of the song's key (KEY OFF: A minor), over 1 .. 3 octaves */
#define GEN_OCTAVES 3u
enum { GEN_NOTES_CHORD, GEN_NOTES_SCALE = GEN_OCTAVES, GEN_NOTES_MAX = 2u * GEN_OCTAVES - 1u };
#define GEN_PARAMETER_MAX 100u
#define GEN_LENGTH_MAX 16u

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

/* ARP's tone (1 .. ARP_TONES_MAX) of a chord (as on the pad's track, not 0): the chord's notes from its root up, as
 * MODE UP plays them (the root an octave up while it is below ARP's lowest note), then on an octave higher */
static uint32_t arp_tone_note(uint32_t chord, uint32_t tone)
{
    const uint32_t type = chord >> CHORD_SHIFT, note_count = PAD_CHORDS[type].note_count, index = (tone - 1u) % ARP_TONES_MAX;
    uint32_t root = chord & NOTE_MASK;
    while (root < MELODIC[MELODIC_ARP].note_lowest) root += 12u;
    return root + index / note_count * 12u + PAD_CHORDS[type].intervals[index % note_count];
}

/* the tone a note is of a chord: the tone that plays that very note, else the lowest one of its name (an octave
 * apart), 0: none (the note is not in the chord) */
static uint32_t arp_note_tone(uint32_t chord, uint32_t note)
{
    uint32_t tone;
    for (tone = 1; tone <= ARP_TONES_MAX; tone++)
        if (arp_tone_note(chord, tone) == note) return tone;
    for (tone = 1; tone <= ARP_TONES_MAX; tone++)
        if (arp_tone_note(chord, tone) % 12u == note % 12u) return tone;
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

static void mixer_levels_full(volatile pattern_t *pattern)   /* its mix: every track as its LEVEL says */
{
    uint32_t track;
    for (track = 0; track < sizeof pattern->mixer_levels; track++) pattern->mixer_levels[track] = MIX_MAX;
}

static void pattern_defaults(volatile pattern_t *pattern)   /* (a new pattern's, besides its zeros) its mix full, GEN calm */
{
    mixer_levels_full(pattern);
    pattern->gen_change = 10;
    pattern->gen_notes = GEN_NOTES_CHORD + 1u;
    pattern->gen_density = 70;
    pattern->gen_length = GEN_LENGTH_MAX;
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
    lfo_t lfos[MELODIC_COUNT];                  /* each melodic track's LFO (a pattern may have its own: lfo_own) */
    uint8_t drone_level;                        /* the drone (JUICY's DRONE page): its LEVEL 0..MIX_MAX (0: none), */
    uint8_t drone_note;                         /* its NOTE 0..DRONE_NOTE_MAX (DRONE_NOTE_*), */
    uint8_t drone_tone, drone_swell;            /* its TONE and SWELL 0..SYNTH_PARAMETER_MAX */
    uint8_t echo_time;                          /* the echo (JUICY's ECHO page): its TIME 0..ECHO_TIMES - 1 (ECHO_TIME_STEPS), */
    uint8_t echo_feedback, echo_width, echo_tone;   /* its FEEDBACK, WIDTH and TONE 0..ECHO_PARAMETER_MAX */
} song_t;

/* the drone's NOTE: the song key's root (KEY OFF: A) or the root of the pad's chord (before its first: the key's), in
 * one of three octaves */
#define DRONE_OCTAVES 3u
enum { DRONE_NOTE_KEY, DRONE_NOTE_CHORD = DRONE_OCTAVES, DRONE_NOTE_MAX = 2u * DRONE_OCTAVES - 1u };
#define DRONE_NOTE(octave, root) (12u * (2u + (octave)) + (root))   /* MIDI; octave 0..2: C1 .. B1 to C3 .. B3 */

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

/* the LFO a melodic track plays with in that pattern: the pattern's own, or the song's */
static volatile lfo_t *lfo_of(volatile pattern_t *pattern, uint32_t melodic)
{
    return pattern->lfo_own >> melodic & 1u ? &pattern->lfos[melodic] : &song.lfos[melodic];
}

static uint32_t song_key(void)                  /* the song's key; an old save's padding there counts as KEY_OFF */
{
    return song.key <= SONG_KEY_MAX ? song.key : KEY_OFF;
}

static uint32_t drone_note_for(uint32_t chord_root)   /* the note the drone holds (MIDI); chord_root: the pad's last chord's root 0..11, 12 none */
{
    const uint32_t key = song_key(), octave = song.drone_note % DRONE_OCTAVES;
    const uint32_t root = song.drone_note >= DRONE_NOTE_CHORD && chord_root < 12u ? chord_root : key == KEY_OFF ? 9u : (key - 1u) / 2u;
    return DRONE_NOTE(octave, root);
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

/* ---- the sounds of the melodic tracks. A sound is the eight knobs of a track's two sound pages and its CUTOFF and
 * DECAY. Each track has a few named ones and two of the player's own (USER1, USER2: in flash, slot 1, for every
 * song). The song keeps each track's sound as it is, and which one it came from. */
#define PRESETS_MAX 5u
#define USER_SOUNDS 2u
#define USER_SOUNDS_SLOT 1u                     /* (the library's is 0) */

typedef struct { uint8_t knobs[SOUND_KNOBS]; uint8_t cutoff, decay; } sound_t;

static const struct {
    uint32_t count;
    const char *names[PRESETS_MAX];
    sound_t sounds[PRESETS_MAX];
} PRESETS[MELODIC_COUNT] = {
    {5, {"PLUCK", "SUB", "ACID", "REESE", "MOOG"},   /* BASS (ACID: the first song's) */
     {{{SYNTH_WAVE_SAW, 50, 0, 32}, 45, 35}, {{SYNTH_WAVE_SOFT, 0, 0, 0}, 30, 70},
      {{SYNTH_WAVE_SAW, 92, 0, 70}, 15, 30}, {{SYNTH_WAVE_SAWS, 30, 0, 10}, 40, 80},
      {{SYNTH_WAVE_SAW, 45, 0, 35, 60, SYNTH_OSC2_DETUNE, 50, 12}, 28, 65}}},   /* (a saw, its sub, a second saw a little higher, a short glide) */
    {5, {"PLUCK", "SQUARE", "SAW", "SOFT", "SEQ"},   /* ARP */
     {{{SYNTH_WAVE_PULSE, 50, 0, 64}, 50, 25}, {{SYNTH_WAVE_SQUARE, 50, 0, 40}, 45, 30},
      {{SYNTH_WAVE_SAW, 60, 0, 50}, 40, 25}, {{SYNTH_WAVE_SOFT, 20, 30, 10}, 60, 45},
      {{SYNTH_WAVE_SAW, 55, 0, 55, 0, SYNTH_OSC2_FIFTH, 45, 0}, 32, 30}}},   /* (a dark pluck with a fifth over it) */
    {4, {"WARM", "BRIGHT", "SLOW", "DEEP"},      /* PAD: SPREAD, RESO, ATTACK, RELEASE; no DECAY */
     {{{64, 0, 51, 72}, 50, 0}, {{90, 45, 40, 72}, 70, 0}, {{64, 0, 85, 90}, 45, 0},
      {{80, 10, 80, 92, 70, 0, 0, 30}, 32, 0}}},   /* (slow, dark, a sub under it, chords that glide) */
    {4, {"SUPERSAW", "SQUARE", "SOFT", "MONO"},  /* LEAD */
     {{{SYNTH_WAVE_SAWS, 50, 0, 32}, 65, 55}, {{SYNTH_WAVE_SQUARE, 40, 0, 30}, 60, 55},
      {{SYNTH_WAVE_SOFT, 20, 45, 10}, 70, 70},
      {{SYNTH_WAVE_SAW, 35, 5, 30, 0, SYNTH_OSC2_DETUNE, 70, 30}, 50, 75}}},   /* (two saws, a glide from note to note) */
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

static uint32_t sound_knob_max(uint32_t melodic, uint32_t knob)   /* WAVE and OSC2 are one of theirs, the rest 0..100 (the pad's OSC2 knobs: none) */
{
    if (melodic == MELODIC_PAD) return knob == SOUND_KNOB_OSC2 || knob == SOUND_KNOB_OSC2_LEVEL ? 0u : SYNTH_PARAMETER_MAX;
    return knob == SOUND_KNOB_WAVE ? SYNTH_WAVE_COUNT - 1u : knob == SOUND_KNOB_OSC2 ? SYNTH_OSC2_COUNT - 1u : SYNTH_PARAMETER_MAX;
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
    for (index = 0; index < PATTERN_COUNT; index++) {
        for (track = 0; track < sizeof song.patterns[index].bar_totals; track++)
            song.patterns[index].bar_totals[track] = 1;
        pattern_defaults(&song.patterns[index]);
    }
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
    for (track = 0; track < MELODIC_COUNT; track++) song.lfos[track].rate = LFO_RATE_DEFAULT;   /* (DEPTH 0: the LFOs do nothing) */
    song.drone_note = DRONE_NOTE_KEY + 1u;      /* (LEVEL 0: no drone; given one, the key's root in octave 2, dark, breathing) */
    song.drone_tone = 50;
    song.drone_swell = 50;
    song.echo_time = 2u;                        /* the echo: a dotted eighth, its FEEDBACK as it always was (61: 140 of 256), */
    song.echo_feedback = 61;
    song.echo_width = ECHO_PARAMETER_MAX; /* side to side (a ping-pong), a little darker each time */
    song.echo_tone = 30;
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

static int track_empty(const volatile pattern_t *pattern, uint32_t track)   /* track: 0 the drums, 1 + a melodic track */
{
    uint32_t bar, drum, step;
    for (bar = 0; bar < BAR_COUNT; bar++) {
        for (drum = 0; track == 0u && drum < DRUM_COUNT; drum++)
            if (pattern->drum_hits[drum][bar]) return 0;
        for (step = 0; track > 0u && step < STEPS_PER_BAR; step++)
            if (pattern->notes[track - 1u][bar][step]) return 0;
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

/* a track's bar (track: 0 the drums, 1 + melodic) copied over another of its bars, with its accents */
static void pattern_copy_bar(volatile pattern_t *pattern, uint32_t track, uint32_t from_bar, uint32_t to_bar)
{
    uint32_t drum, step;
    for (drum = 0; track == 0u && drum < DRUM_COUNT; drum++)
        pattern->drum_hits[drum][to_bar] = pattern->drum_hits[drum][from_bar];
    for (step = 0; track != 0u && step < STEPS_PER_BAR; step++)
        pattern->notes[track - 1u][to_bar][step] = pattern->notes[track - 1u][from_bar][step];
    for (step = 0; track != 0u && step < STEPS_PER_BAR / 2u; step++)   /* (two steps' gates a byte) */
        pattern->gates[track - 1u][to_bar][step] = pattern->gates[track - 1u][from_bar][step];
    pattern->accents[track][to_bar] = pattern->accents[track][from_bar];
}

static int lfo_valid(const volatile lfo_t *checked)
{
    return checked->rate < LFO_RATE_COUNT && checked->depth <= LFO_DEPTH_MAX && checked->target < LFO_TARGET_COUNT && checked->shape < LFO_SHAPE_COUNT;
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
    for (track = 0; track < sizeof checked->mixer_levels; track++)
        if (checked->mixer_levels[track] > MIX_MAX) return 0;
    for (melodic = 0; melodic < MELODIC_COUNT; melodic++)
        if (!lfo_valid(&checked->lfos[melodic])) return 0;
    if (checked->gen_change > GEN_PARAMETER_MAX || checked->gen_notes > GEN_NOTES_MAX || checked->gen_density > GEN_PARAMETER_MAX
        || checked->gen_length < 1u || checked->gen_length > GEN_LENGTH_MAX) return 0;
    return checked->arp_mode <= ARP_MODE_MAX && name_valid(checked->name) && checked->mixer_mutes >> sizeof checked->mixer_levels == 0u
           && checked->lfo_own >> MELODIC_COUNT == 0u;
}

static int song_valid(const volatile song_t *checked)
{
    uint32_t melodic, index, knob;
    for (melodic = 0; melodic < MELODIC_COUNT; melodic++) {
        if (checked->cutoff[melodic] > SYNTH_PARAMETER_MAX || checked->decay[melodic] > SYNTH_PARAMETER_MAX
            || checked->echo[melodic] > ECHO_SEND_MAX || checked->sound_choices[melodic] >= sound_count(melodic)
            || checked->levels[melodic] > MIX_MAX || !lfo_valid(&checked->lfos[melodic])) return 0;
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
    if (checked->echo_time >= ECHO_TIMES || checked->echo_feedback > ECHO_PARAMETER_MAX || checked->echo_width > ECHO_PARAMETER_MAX
        || checked->echo_tone > ECHO_PARAMETER_MAX) return 0;
    if (checked->drone_level > MIX_MAX || checked->drone_note > DRONE_NOTE_MAX || checked->drone_tone > SYNTH_PARAMETER_MAX
        || checked->drone_swell > SYNTH_PARAMETER_MAX) return 0;
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
