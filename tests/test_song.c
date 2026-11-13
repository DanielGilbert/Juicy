/* SPDX-License-Identifier: GPL-3.0-only */
/* The host scripts' song (tests/scripts): the first song as it was on 2026-11-13 (acid techno, A, 130 BPM), kept here
 * so that a new DEMO1 does not change what the scripts find. Its patterns:
 *   A  GROOVE: kick, hats, the ACID bass (two bars)    B  POWER: A with clap, POWER chords on the pad, ARP UP 3
 *   C  PEAK: B with open hats                          D  BREAK: no kick, the root alone, ARP RAND 3, a snare roll
 * The chain: A B C D C C A. Patterns E to H are empty. juicy.c includes this after demos.c, on a computer only;
 * host.c keeps it as the song (the settings) before Juicy starts. */

static void test_song(void)
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
    }
    song_chain(CHAIN, sizeof CHAIN);
}

static void test_song_keep(void)                /* (host.c, before app_main): the song kept, as Juicy keeps it (quietly) */
{
    test_song();
    host_set[0] = 1;
    host_set[1] = (uint8_t)sizeof song;
    host_set[2] = (uint8_t)(sizeof song >> 8);
    host_set[3] = (uint8_t)(sizeof song >> 16);
    memcpy(host_set + 4, (const void *)&song, sizeof song);
}
