# Roadmap: hand-holding towards a song

Juicy is for the beginner who loves to tinker but needs a hand towards a finished song. Today it makes good sounds
easy and mistakes hard; what it does not do yet is **suggest what fits**: which notes, which drums, which parts, which
structure. This page is the plan for that, in the order to build it. Each step says what it does, how it is played,
what it costs, and what is still open. Tick a step off (`[x]`) when it is built, flashed and liked on the device, and
add a line to [CHANGELOG.md](../CHANGELOG.md) and the panel in [README.md](../README.md) as usual.

Decided with Daniel (2026-11-14): the song's key is set on **JUICY's KNOB4**; notes outside the key stay **playable,
just dimmer** (never dark, never blocked).

## What already works towards it

- **It sounds good at once:** three demo songs in different styles (DEMO1 acid techno, DEMO2 uplifting trance,
  DEMO3 lo-fi), PLAY to listen before loading (songs and library patterns), a library of 16 patterns.
- **Harmony is partly automatic:** PAD makes a whole chord from one note (10 chord types); ARP has no notes to get
  wrong, it plays the pad's chord; each track only offers notes in a sensible range (keys outside it are dark).
- **Named sounds** per track, a `*` when changed, USER1 / USER2 for the player's own.
- **No menus:** keys glow where they do something, beats glow brighter, the cursor blinks; hints on the SONGS and
  LIBRARY pages say what SEQ, REC and PLAY do.
- **Mistakes are hard:** loading or saving over something asks twice, the song saves itself, previews leave the song
  untouched.
- **Song building:** patterns A..H with 1..4 bars per track, copy a pattern, the chain, mutes, names, step
  recording, note length, accents, swing.

## The plan

### 1. [x] The song's key, and harmony on the keys and in the roll

**Done 2026-11-14 (built, flashed, liked on the device).** Decided on the way: in-key notes glow brighter,
out-of-key glow (the keys have no level below glow), the chord's notes are lit and the shown note blinks; the chord is
the cursor's with EDIT, the playing one otherwise; AUTO picks major, minor or diminished from the scale (a root
outside the key: major); the chord's name and the roll's tint only with a key. Script: `tests/scripts/keys.txt`.
On the device the old brighter glow (a 32 us pulse) was hard to tell from the glow and kept the key scan busy ~27 %
of the time; a light test chose a new layer, lit one scan round in four (`fm1_led_quarter` in `hal/fm1_input.h`):
as clear, no flicker, the scan at ~7 %, the longest audio block at 22 % (was 28 %).

The foundation for most of what follows.

- **KEY on JUICY's KNOB4:** OFF, then the 24 keys (C MIN .. B MAJ, shown as e.g. `A MIN`). OFF: everything as today.
- **LEAD and BASS (and ARP's keyboard):** keys of the key's scale glow as today; keys outside it glow dimmer (still
  playable); the notes of the chord that sounds now (the pad's chord at the cursor's step, or while playing the one
  that plays) glow brighter.
- **The roll:** behind the notes, the rows of the chord's notes are tinted at each step, following the pad's chords
  (stopped: the pattern's chords at those steps).
- **PAD:** keys of roots that fit the key glow, the others dimmer. A root played or placed gets the chord type that
  fits the key (in A minor: D gives D MIN, F gives F MAJ) while CHORD shows AUTO; turning CHORD past AUTO still
  picks any type by hand.
- **The chord's name** (e.g. `AM7`, `F`) on the LEAD and BASS pages, so the player sees what they play over.
- **Storage:** one byte in `song_t`, which fits its padding byte (the song stays 3984 B, kept songs still load). A
  key byte that is out of range (old saves' padding) counts as OFF instead of making the song invalid. The demo songs
  get their keys (DEMO1 A MIN, DEMO2 D MIN, DEMO3 C MAJ / A MIN).
- **Open:** whether the brighter chord notes follow the playing step or the cursor while playing (suggestion: the
  cursor when EDIT is on, the playing step otherwise); how the tint looks in the roll (check on the device).

### 2. [x] Drum grooves on KNOB1

**Done 2026-11-14 (built, flashed, liked on the device).** Decided on the way: **KNOB1** instead of ALGORITHM
(its place says GROOVE, KNOB4's the BPM: the header had no room, and the knob places say what the knobs do); the
groove plays at once as a preview, REC keeps it (asks again over drums that held something), OWN / another page /
PRESETS / EDIT off drop it; one bar per groove, FILL into the bar on the screen; no autosave and no step edits while
one only plays. Names: HOUSE TECHNO TRANCE BREAKS GARAGE DNB DUBSTEP HIPHOP FILL. Script: `tests/scripts/grooves.txt`.

- **On DRUMS, ALGORITHM** (unused there) picks a named groove for the pattern on the screen, with its accents:
  FOUR ON FLOOR (house), TECHNO, TRANCE (offbeat open hat), BREAKBEAT, 2-STEP (garage), DNB, HALF-TIME
  (dubstep / trap), BOOM-BAP (hip-hop, lo-fi). The page shows the groove's name and the tempo it usually has; the
  tempo is never changed by it.
- **Asks twice** when the drums already hold something (as loading does), so a groove never wipes work by accident.
- **A FILL** for a pattern's last bar (snare roll, toms) as the last entries, to lead into the next part.
- **Storage:** none (tables in flash). Groove tables are worth checking by ear (render WAVs on the host first).
- **Open:** whether a groove fills one bar or all of the drums' bars (suggestion: one bar, the fill its own entry).

### 3. [x] Chord progressions on PAD

**Done 2026-11-14 (built, flashed, liked on the device).** Decided on the way: **KNOB4** (CHORDS; PAD had no
KNOB4) instead of ALGORITHM, and it works as the grooves do: plays at once (stopped: the first chord sounds), REC
keeps it (asks again over chords), OWN / another page / PRESETS / EDIT off drop it; NOTE, CHORD, the step keys and
recording wait meanwhile. A chord a bar, the pad gets the progression's bars (MINIMAL: two). Names: EPIC (i VI III
VII), DREAM (i iv VII III), RISE (VI VII i), POP (I V vi IV), MINIMAL (i VII), LOFI (ii V I vi, 7ths). The pop one is
I V vi IV, not vi IV I V as planned: that is EPIC's chords. A minor progression in a major key (or the other way) plays
in the relative key, so it stays in the key. Script: `tests/scripts/progressions.txt`.

- **On PAD, ALGORITHM** puts a well-known progression into the pattern's four bars, in the song's key (needs step 1;
  KEY OFF: in A minor): i–VI–III–VII (the "epic" trance one: Am F C G), i–iv–VII–III, VI–VII–i, vi–IV–I–V
  (pop), i–VII (two chords, minimal), ii–V–I–vi with 7ths (lo-fi).
- Asks twice over existing chords. With steps 1 and 2 this already makes a song's skeleton.
- **Storage:** none.

### 4. [x] Undo

**Done 2026-11-14 (built, flashed, liked on the device).** Decided on the way:
**GLO**, and a second press swaps back (redo). Changes less than a second apart are one (a knob turned, a few steps
tapped); loading a song is a change too. Two song copies in RAM, not one: the song before the last change, and the
song as the last change left it (a change is only known after it happened); RAM 51 of 96 KB. Not while a preview, a
groove or a progression plays. Script: `tests/scripts/undo.txt`.

- One level: the last change to the song comes back. A free button: **GLO** (or ENV / LFO; decide on the device).
- **Cost:** a second `song_t` in RAM (about 4 KB; 59 KB are free). Snapshot before each change that sets
  `song_changed` (in pages.c / juicy.c), undo swaps it back.
- **Open:** which button; whether undo of an undo (redo) is the same press (suggestion: yes, a swap).

### 5. [x] Bass and ARP rhythms that follow the pad

**Done 2026-11-14 (built, flashed, liked on the device).** Decided on the way: a text **overlay** first (a box
over the body for 2 s, `overlay_show` in juicy.c, `draw_overlay` in screen.c; the body is not drawn meanwhile and is
drawn again after), as the knob places are full on BASS and ARP; GLO uses it too (UNDO / REDO). ALGORITHM picks, the
overlay names it; it works as the progressions do (PAD's KNOB4, BASS's and ARP's ALGORITHM share one "suggestion" in
juicy.c / pages.c, the tables in demos.c). A bass line takes the pad's bars, an ARP rhythm is one bar. Script:
`tests/scripts/rhythms.txt`.

- **On BASS, ALGORITHM** picks a bass line made from the pad's chord roots: OFFBEAT (trance), OCTAVES, GALLOP,
  ROLLING (sixteenths), ACID (with accents). Never a wrong note: the notes come from the chords.
- **On ARP, ALGORITHM** picks a rhythm of hits: STRAIGHT, SYNCOPATED, TRANCE GATE, EIGHTHS.
- Asks twice over existing notes. **Storage:** none.

### 6. [x] Song templates

**Done 2026-11-14 (built, flashed, "for now, it's fine" on the device).** Decided on the way: called **styles**, picked with **KNOB2** (STYLE) on SONGS when NEW is selected (PLAY
listens, SEQ loads); filled, not empty: each part gets the style's groove, progression, bass line and ARP rhythm
(steps 2, 3, 5), the parts differ by which tracks play (INTRO drums + bass, BUILD drums with a FILL + pad + arp, DROP
all, BREAK pad + arp, OUTRO drums + pad), the LEAD stays empty. TRANCE (D minor 138, EPIC, GALLOP), TECHNO (A minor
130, MINIMAL, ACID), LOFI (C major 84 swung, LOFI, OCTAVES). `style_song` in demos.c. Script:
`tests/scripts/styles.txt`. If it is too much: drop the bass and arp, keep the structure (the plan below).

- **NEW from a template** on the SONGS page: the chain as INTRO, BUILD, DROP, BREAK, DROP, OUTRO, with the patterns
  prepared for each part (named, the break without kick, the build with a fill) but without notes, so the player fills
  them in (or uses steps 2, 3 and 5).
- **Storage:** none (built in code like the demos).

### 7. [x] Live recording

**Done 2026-11-14 (built, flashed, works on the device).** Decided on the way: **REC while playing** (the same
recording as step recording: stopped, at the cursor; playing, live), **no count-in** (the loop goes round, come in
any time), **overdub** (a new note goes over what it covers, the bar as it was keeps the rest). A key sounds at once
and lands on the nearest step's sound (a swung step's too); played a bit early, the step does not start it again
(`live_starts` in sequencer.c). Held, the note grows a step ahead of the song so the steps it covers do not cut it;
let go, it is as long as it was held (under a step: in tenths). A take is one change for GLO. The screen follows the
playing bar; with the chain on, only the pattern on the screen records while it plays. Script:
`tests/scripts/live.txt`.

- Recording while the song plays: keys played land on the nearest step (with the LENGTH they were held), with a
  one-bar count-in (PLAY's light, or a click). Today there is only step recording.
- **Open:** how it starts (REC while playing?), what the count-in sounds like, overdub or replace.

## Smaller ideas, not planned yet

- Hints on the track pages (e.g. "HOLD A STEP: ACCENT"), like the SONGS and LIBRARY pages have.
- Show the keyboard's octave while recording.
- New screenshots in `docs/` (they still show the old first song).

## Room and rules

- **Storage:** a song is 3984 of 4084 B (about 100 B free, plus the one padding byte step 1 uses); a pattern is
  488 B; the library holds 16 patterns in a 16 KB slot. Tables (grooves, progressions) cost flash, not storage.
- **Audio:** the audio interrupt peaks at 24 % of its 5.8 ms (measured 2026-11-13); [RULES.md](RULES.md) asks for
  under 50 %.
- **Free controls:** ALGORITHM on JUICY, DRUMS, PAD and LEAD (step 5 takes it on BASS and ARP), the ENV, LFO and ARP buttons (step 4 takes GLO), JUICY's KNOB4 (step 1
  takes it), the upper keys outside OP1 .. OP5 and the mute keys.
- **Where code goes:** a page's knobs and keys in `pages.c`, what it shows in `view.c`, the data in `song.c`, the
  demo songs in `demos.c` (see the file map at the top of `firmware/src/juicy.c`).
- **Checking:** `sh tests/regress.sh record` before a change, `sh tests/regress.sh` after (SAME, or the expected
  differences); a host script per feature in `tests/scripts/`; listen to tables (grooves, progressions) as WAVs from
  the host before flashing. Ask Daniel before every flash and before taking over the desktop.
