/* SPDX-License-Identifier: GPL-3.0-only */
/* Juicy's demo songs: DEMO1 (the first song, what a new FM-1 plays), DEMO2, DEMO3. juicy.c includes it after
 * song.c. */

/* ---- the demo songs on the songs' page: DEMO1 (the first song), DEMO2, DEMO3 */
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

static void song_names(const char *song_name, const char *const *pattern_names, uint32_t count)   /* the song's, its first patterns' */
{
    uint32_t pattern;
    name_set(song.name, song_name);
    for (pattern = 0; pattern < count; pattern++) name_set(song.patterns[pattern].name, pattern_names[pattern]);
}

static void song_note(uint32_t pattern_index, uint32_t melodic, uint32_t bar, uint32_t step, uint32_t note, uint32_t steps, uint32_t gate)
{   /* a note that many steps long (one step: gate tenths of it, 0 the whole step) */
    uint32_t tied;
    for (tied = 0; tied < steps && step + tied < STEPS_PER_BAR; tied++)
        song.patterns[pattern_index].notes[melodic][bar][step + tied] = (uint8_t)(tied ? note | NOTE_TIE : note);
    set_note_gate(song.patterns[pattern_index].gates[melodic][bar], step, steps == 1u ? gate : 0u);
}

static void song_chord(uint32_t pattern_index, uint32_t bar, uint32_t step, uint32_t chord)   /* chord: CHORD(root, PAD_CHORD_*) */
{
    song.patterns[pattern_index].notes[MELODIC_PAD][bar][step] = (uint8_t)chord;
    set_note_gate(song.patterns[pattern_index].gates[MELODIC_PAD][bar], step, chord >> 8);
}

static void song_arp_steps(uint32_t pattern_index, uint32_t bar, uint32_t steps, uint32_t gate)   /* steps: bit s a hit */
{
    uint32_t step;
    for (step = 0; step < STEPS_PER_BAR; step++)
        if (steps >> step & 1u) song_note(pattern_index, MELODIC_ARP, bar, step, ARP_HIT, 1u, gate);
}

/* The first song (DEMO1, what a new FM-1 plays): acid techno, A, 130 BPM: an ACID bass in sixteenths with accents (the bite), POWER chords on the pad with a
 * SQUARE arpeggio climbing them over three octaves, a break on the root alone with the arpeggio jumping octaves.
 *   A  kick, hats, acid     B  A with clap, power chords, the arpeggio     C  B with open hats
 *   D  the break: no kick, the root alone, random octaves, a snare roll */
static void first_song(void)
{
    enum { A, B, C, D };
    static const uint8_t ACID[2][STEPS_PER_BAR] = {   /* 0: rest */
        {33, 33, 45, 0, 33, 36, 33, 45, 31, 33, 45, 0, 33, 40, 38, 36},
        {33, 0, 45, 33, 36, 33, 43, 33, 33, 45, 0, 40, 33, 45, 48, 47}};
    static const uint16_t ACID_ACCENTS[2] = {1u << 0 | 1u << 5 | 1u << 10 | 1u << 13, 1u << 0 | 1u << 5 | 1u << 9 | 1u << 14};
    static const uint32_t POWER[BAR_COUNT] = {CHORD(45, PAD_CHORD_POWER), CHORD(45, PAD_CHORD_POWER), CHORD(41, PAD_CHORD_POWER), CHORD(43, PAD_CHORD_POWER)};
    static const uint16_t ROLL[BAR_COUNT] = {0x1111, 0x5555, 0x5555, 0xFFFF};
    static const uint8_t CHAIN[] = {A + 1, B + 1, C + 1, D + 1, C + 1, C + 1, A + 1};
    uint32_t pattern, bar, step;
    empty_song();
    song.tempo = 130;
    song.reverb = 30;
    sound_apply(MELODIC_BASS, 2);               /* ACID */
    sound_apply(MELODIC_ARP, 1);                /* SQUARE */
    sound_apply(MELODIC_PAD, 1);                /* BRIGHT */
    song.echo[MELODIC_ARP] = 50;
    song.levels[MELODIC_PAD] = 70;
    for (pattern = A; pattern <= D; pattern++) {
        volatile pattern_t *filled = &song.patterns[pattern];   /* (the pattern being filled) */
        for (bar = 0; bar < 2u; bar++) {
            for (step = 0; step < STEPS_PER_BAR; step++)
                if (ACID[bar][step]) song_note(pattern, MELODIC_BASS, bar, step, ACID[bar][step], 1, 6);
            filled->accents[1u + MELODIC_BASS][bar] = ACID_ACCENTS[bar];
        }
        song_bars(pattern, 1u + MELODIC_BASS, 2u);
        for (bar = 0; pattern != A && bar < BAR_COUNT; bar++) {
            song_chord(pattern, bar, 0, pattern == D ? CHORD(45, PAD_CHORD_ROOT) : POWER[bar]);
            song_arp_steps(pattern, bar, pattern == D ? 0x5555 : 0xFFFF, 4);
            filled->accents[1u + MELODIC_ARP][bar] = 0x1111;
        }
        song_bars(pattern, 1u + MELODIC_ARP, BAR_COUNT);
        song_bars(pattern, 1u + MELODIC_PAD, BAR_COUNT);
        filled->arp_mode = pattern == D ? ARP_MODE(ARP_ORDER_RANDOM, 3u) : ARP_MODE(ARP_ORDER_UP, 3u);
        filled->drum_hits[DRUM_HAT][0] = 0x4444;
        filled->accents[0][0] = 0x4444;
        if (pattern != D) filled->drum_hits[DRUM_KICK][0] = 0x1111;
        if (pattern == B || pattern == C) filled->drum_hits[DRUM_CLAP][0] = 0x1010;
        if (pattern == C) filled->drum_hits[DRUM_OPEN_HAT][0] = 0x4444;
    }
    song_bars(D, 0, BAR_COUNT);
    for (bar = 0; bar < BAR_COUNT; bar++) {
        song.patterns[D].drum_hits[DRUM_HAT][bar] = 0x4444;
        song.patterns[D].drum_hits[DRUM_SNARE][bar] = ROLL[bar];
        song.patterns[D].accents[0][bar] = bar == 3u ? 0x8888 : 0x4444;   /* the roll's last bar: a lift on each beat's end */
    }
    {
        static const char *const NAMES[] = {"GROOVE", "POWER", "PEAK", "BREAK"};
        song_names("ACID", NAMES, sizeof NAMES / sizeof NAMES[0]);
        song.key = SONG_KEY(9u, 0u);            /* A minor */
    }
    song_chain(CHAIN, sizeof CHAIN);
}

/* uplifting trance, D minor, 138 BPM: a galloping PLUCK bass, PLUCK arpeggios, a breakdown of pad and SUPERSAW,
 * a build with a snare roll, the drop.
 *   A  kick, hats, bass            B  A with clap, pad and arpeggio   C  the breakdown: pad and melody
 *   D  the build: pad, arpeggio, the snare roll and toms        E  the drop: everything      F  the outro */
static void demo_uplifting(void)
{
    enum { A, B, C, D, E, F };
    static const uint8_t CHORDS[BAR_COUNT] = {50, CHORD(46, PAD_CHORD_MAJOR), CHORD(53, PAD_CHORD_MAJOR), CHORD(48, PAD_CHORD_MAJOR)};   /* Dm Bb F C */
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
    {
        static const char *const NAMES[] = {"INTRO", "GROOVE", "BREAK", "BUILD", "DROP", "OUTRO"};
        song_names("UPLIFT", NAMES, sizeof NAMES / sizeof NAMES[0]);
        song.key = SONG_KEY(2u, 0u);            /* D minor */
    }
    song_chain(CHAIN, sizeof CHAIN);
}

/* lo-fi / neo-soul, 84 BPM, swung: Dm7 G7 Cmaj7 and Asus4 resolving to Am7 on a WARM pad, a SOFT arpeggio down
 * over them, a SUB bass, a lazy boom-bap with accented hats, a SOFT melody that leans on the sus.
 *   A  pad and arpeggio     B  A with drums and bass     C  B with the melody     D  pad, melody, hats */
static void demo_lofi(void)
{
    enum { A, B, C, D };
    static const uint32_t CHORDS[BAR_COUNT] = {CHORD(50, PAD_CHORD_MINOR7), CHORD(55, PAD_CHORD_DOMINANT7), CHORD(48, PAD_CHORD_MAJOR7), CHORD(45, PAD_CHORD_SUS4)};
    static const uint8_t ROOTS[BAR_COUNT] = {38, 31, 36, 33};   /* D2 G1 C2 A1 */
    static const uint8_t LEAD[BAR_COUNT][3][3] = {   /* step, note, steps */
        {{0, 69, 4}, {6, 72, 2}, {8, 74, 6}},
        {{0, 77, 3}, {4, 74, 2}, {8, 71, 6}},
        {{0, 76, 6}, {8, 74, 2}, {10, 72, 4}},
        {{0, 74, 6}, {8, 72, 6}, {14, 0, 0}}};
    static const uint8_t CHAIN[] = {A + 1, B + 1, C + 1, D + 1, C + 1, A + 1};
    uint32_t pattern, bar, note;
    empty_song();
    song.tempo = 84;
    song.reverb = 60;
    sound_apply(MELODIC_BASS, 1);               /* SUB */
    sound_apply(MELODIC_ARP, 3);                /* SOFT */
    sound_apply(MELODIC_PAD, 0);                /* WARM */
    sound_apply(MELODIC_LEAD, 2);               /* SOFT */
    song.echo[MELODIC_ARP] = 55;
    song.echo[MELODIC_LEAD] = 45;
    for (pattern = A; pattern <= D; pattern++) {
        volatile pattern_t *filled = &song.patterns[pattern];   /* (the pattern being filled) */
        for (bar = 0; bar < BAR_COUNT; bar++) {
            song_chord(pattern, bar, 0, CHORDS[bar]);
            if (bar == 3u) song_chord(pattern, bar, 8, CHORD(45, PAD_CHORD_MINOR7));   /* the sus resolves */
            if (pattern != D) song_arp_steps(pattern, bar, 0x5555, 7);   /* eighths */
            if (pattern == B || pattern == C) {
                song_note(pattern, MELODIC_BASS, bar, 0, ROOTS[bar], 6, 0);
                song_note(pattern, MELODIC_BASS, bar, 7, ROOTS[bar], 1, 6);
                song_note(pattern, MELODIC_BASS, bar, 10, ROOTS[bar] + 7u, 4, 0);
            }
            for (note = 0; (pattern == C || pattern == D) && note < 3u; note++)
                if (LEAD[bar][note][2]) song_note(pattern, MELODIC_LEAD, bar, LEAD[bar][note][0], LEAD[bar][note][1], LEAD[bar][note][2], 0);
            filled->accents[1u + MELODIC_ARP][bar] = 0x0101;   /* the arpeggio leans on 1 and 3 */
        }
        song_bars(pattern, 1u + MELODIC_BASS, BAR_COUNT);
        song_bars(pattern, 1u + MELODIC_ARP, BAR_COUNT);
        song_bars(pattern, 1u + MELODIC_PAD, BAR_COUNT);
        song_bars(pattern, 1u + MELODIC_LEAD, BAR_COUNT);
        filled->arp_mode = pattern == C ? ARP_MODE(ARP_ORDER_RANDOM, 2u) : ARP_MODE(ARP_ORDER_DOWN, 2u);
        filled->swing = 15;                          /* 65 % */
        if (pattern == B || pattern == C) {
            filled->drum_hits[DRUM_KICK][0] = 0x0481;    /* 1, the "and" of 2, 3's "and" */
            filled->drum_hits[DRUM_SNARE][0] = 0x1010;
        }
        if (pattern != A) filled->drum_hits[DRUM_HAT][0] = 0x5555;
        filled->accents[0][0] = 0x4444;              /* the offbeat hats */
    }
    {
        static const char *const NAMES[] = {"INTRO", "GROOVE", "MELODY", "FLOAT"};
        song_names("LO-FI", NAMES, sizeof NAMES / sizeof NAMES[0]);
        song.key = SONG_KEY(0u, 1u);            /* C major (Dm7 G7 Cmaj7 Am7: ii V I vi) */
    }
    song_chain(CHAIN, sizeof CHAIN);
}

/* ---- the drum grooves (KNOB1 on DRUMS): well-known ones, a bar each with its accents, and a FILL for a bar that leads
 * into the next part. GROOVE_OWN is the pattern's own drums. Steps as in drum_hits: bit s, step s */
enum { GROOVE_OWN, GROOVE_HOUSE, GROOVE_TECHNO, GROOVE_TRANCE, GROOVE_BREAKS, GROOVE_GARAGE, GROOVE_DNB, GROOVE_DUBSTEP,
       GROOVE_HIPHOP, GROOVE_FILL, GROOVE_COUNT };
static const struct {
    const char *name;
    uint8_t tempo;                              /* the tempo it usually has (shown, never set); 0: none */
    uint16_t hits[DRUM_COUNT], accents;         /* KICK, HAT, CLAP, OPEN HAT, SNARE, TOM; the accent row */
} GROOVES[GROOVE_COUNT] = {
    {"OWN", 0, {0}, 0},
    {"HOUSE", 124, {0x1111, 0xAAAA, 0x1010, 0x4444, 0, 0}, 0x4444},     /* four on the floor, clap on 2 and 4, open hat between */
    {"TECHNO", 130, {0x1111, 0xEEEE, 0x1010, 0, 0, 0}, 0x4444},         /* driving sixteenths of hat */
    {"TRANCE", 138, {0x1111, 0x2222, 0x1010, 0x4444, 0, 0}, 0x4444},    /* the offbeat open hat */
    {"BREAKS", 130, {0x0441, 0x5555, 0, 0, 0x1090, 0}, 0x1010},         /* a broken kick, a ghost snare */
    {"GARAGE", 132, {0x0401, 0xCCCC, 0x1010, 0, 0, 0}, 0x4444},         /* 2-step: no kick on 2 and 4 */
    {"DNB", 174, {0x0401, 0x5555, 0, 0, 0x1010, 0}, 0x1010},            /* drum and bass: the two-step at speed */
    {"DUBSTEP", 140, {0x0401, 0x5555, 0, 0, 0x0100, 0}, 0x0100},        /* half time: the snare on 3 */
    {"HIPHOP", 90, {0x0481, 0x5555, 0, 0, 0x1010, 0}, 0x1010},          /* boom-bap */
    {"FILL", 0, {0x0011, 0x0055, 0x0010, 0, 0x0F00, 0xF000}, 0x1100},  /* half a beat, then snare and toms */
};

/* a groove into the pattern's drums: a groove is their one bar (the others emptied); FILL goes into that bar */
static void groove_put(volatile pattern_t *pattern, uint32_t groove, uint32_t bar)
{
    uint32_t drum, other;
    if (groove != GROOVE_FILL) {
        pattern->bar_totals[0] = 1;
        for (other = 1; other < BAR_COUNT; other++) {
            for (drum = 0; drum < DRUM_COUNT; drum++) pattern->drum_hits[drum][other] = 0;
            pattern->accents[0][other] = 0;
        }
        bar = 0;
    }
    for (drum = 0; drum < DRUM_COUNT; drum++) pattern->drum_hits[drum][bar] = GROOVES[groove].hits[drum];
    pattern->accents[0][bar] = GROOVES[groove].accents;
}

/* ---- the chord progressions (KNOB4 on PAD): well-known ones, a chord a bar, in the song's key (KEY OFF: A minor).
 * A progression is a minor or a major one; in a key of the other kind it plays in the relative key (A minor's is
 * C major: the same notes), so its chords stay in the key. PROGRESSION_OWN is the pattern's own chords */
enum { PROGRESSION_OWN, PROGRESSION_EPIC, PROGRESSION_DREAM, PROGRESSION_RISE, PROGRESSION_POP, PROGRESSION_MINIMAL,
       PROGRESSION_LOFI, PROGRESSION_COUNT };
#define PROGRESSION_ROOT_LOWEST 45u             /* the roots go A2 .. G#3 (as the demo songs' chords) */
static const struct {
    const char *name;
    uint8_t major;                              /* 0: from the minor key's root (i), 1: from the major key's (I) */
    uint8_t chord_count;                        /* the pad's bars, a chord on each */
    uint8_t roots[BAR_COUNT];                   /* semitones above that root */
    uint8_t types[BAR_COUNT];                   /* PAD_CHORD_* */
} PROGRESSIONS[PROGRESSION_COUNT] = {
    {"OWN", 0, 0, {0}, {0}},
    {"EPIC", 0, 4, {0, 8, 3, 10}, {PAD_CHORD_MINOR, PAD_CHORD_MAJOR, PAD_CHORD_MAJOR, PAD_CHORD_MAJOR}},   /* i VI III VII: Am F C G, the trance anthem */
    {"DREAM", 0, 4, {0, 5, 10, 3}, {PAD_CHORD_MINOR, PAD_CHORD_MINOR, PAD_CHORD_MAJOR, PAD_CHORD_MAJOR}},  /* i iv VII III: Am Dm G C */
    {"RISE", 0, 4, {8, 10, 0, 0}, {PAD_CHORD_MAJOR, PAD_CHORD_MAJOR, PAD_CHORD_MINOR, PAD_CHORD_MINOR}},   /* VI VII i: F G Am, up to home */
    {"POP", 1, 4, {0, 7, 9, 5}, {PAD_CHORD_MAJOR, PAD_CHORD_MAJOR, PAD_CHORD_MINOR, PAD_CHORD_MAJOR}},     /* I V vi IV: C G Am F */
    {"MINIMAL", 0, 2, {0, 10}, {PAD_CHORD_MINOR, PAD_CHORD_MAJOR}},                                        /* i VII: Am G */
    {"LOFI", 1, 4, {2, 7, 0, 9}, {PAD_CHORD_MINOR7, PAD_CHORD_DOMINANT7, PAD_CHORD_MAJOR7, PAD_CHORD_MINOR7}},   /* ii V I vi with 7ths: Dm7 G7 Cmaj7 Am7 */
};

/* a progression into the pattern's pad: its chords on the first step of each of its bars (the pad has as many),
 * everything else emptied */
static void progression_put(volatile pattern_t *pattern, uint32_t progression)
{
    const uint32_t key = song_key() != KEY_OFF ? song_key() : SONG_KEY(9u, 0u);   /* (no key: A minor) */
    const uint32_t key_root = (key - 1u) / 2u, key_major = (key - 1u) % 2u;
    const uint32_t progression_root = key_root + (PROGRESSIONS[progression].major == key_major ? 0u : key_major ? 9u : 3u);   /* (the relative key's root) */
    uint32_t bar, step;
    pattern->bar_totals[1u + MELODIC_PAD] = PROGRESSIONS[progression].chord_count;
    for (bar = 0; bar < BAR_COUNT; bar++) {
        for (step = 0; step < STEPS_PER_BAR; step++) {
            pattern->notes[MELODIC_PAD][bar][step] = 0;
            set_note_gate(pattern->gates[MELODIC_PAD][bar], step, 0);
        }
        if (bar < PROGRESSIONS[progression].chord_count) {
            const uint32_t pitch_class = (progression_root + PROGRESSIONS[progression].roots[bar]) % 12u;
            const uint32_t chord = CHORD(PROGRESSION_ROOT_LOWEST + (pitch_class + 12u - PROGRESSION_ROOT_LOWEST % 12u) % 12u, PROGRESSIONS[progression].types[bar]);
            pattern->notes[MELODIC_PAD][bar][0] = (uint8_t)chord;
            set_note_gate(pattern->gates[MELODIC_PAD][bar], 0, chord >> 8);
        }
    }
}
