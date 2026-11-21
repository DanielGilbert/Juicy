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

## Smaller ideas

- [x] Hints on the track pages (e.g. "HOLD A STEP: ACCENT"), like the SONGS and LIBRARY pages have. Done
  2026-11-14: while the track is empty in the pattern on the screen (the roll's, or at the right of the drums' names).
  Script: `tests/scripts/hints.txt`.
- [x] Show the keyboard's octave while recording. Done 2026-11-14: OCT- / OCT+ show the keys' notes in the overlay.
- [x] New screenshots in `docs/` (they still show the old first song). Done 2026-11-14 from the host: DEMO1.

## Next: hypnotic and ambient sounds

Decided with Daniel (2026-11-21): Juicy gets the sounds for hypnotic, ambient electronic music in the style of Caught
In Joy's "Magnetosphere" (YouTube CylrQLde8lQ). Measured from five 2-minute excerpts: everything on A (a pedal note,
A minor with B♭: A Phrygian), very dark (85..97 % of the energy below 250 Hz, almost nothing above 4 kHz), on a strict
grid (eighths or quarters at 95..110 BPM, repeating for many bars), slow swells (6..33 dB in 2 minutes) and filter
sweeps. Juicy's grid fits; its sounds lack movement. Built for the device directly (no WAV renders first). **Old
saves need not load** (Juicy is beta): songs, the library and user sounds of an older size start fresh.

### 1. [x] Room: a song in two sectors

**Built and flashed 2026-11-21, "works, nice" on the device.** RAM 51.5 -> 31.3 KB, pool 165 -> 189 KB; every host script gives what it
gave before (`tests/regress.sh`: SAME). On the device: songs saved by 0.1 start fresh (FM-1: the song you work on
too); the library and USER1 / USER2 stay (their slots did not move). To check there: save, power off, load.

- A song is one 4 KiB sector today (4084 B, 4032 used). Settings and blobs become two sectors each (8180 B).
- FM-1: settings and blobs move into the free part of the app's store after the slots (0xB7000..0xC9000); SMK-37:
  settings 0x9D000, blobs 0x9F000..0xAEFFF (up to the owner tag).
- The song's copies the audio never reads (a place on its way in, the preview's backup, undo's two) go into the pool:
  RAM stays as it is.
- The code that grows old sizes (song_grow, patterns_grow, PATTERN_SIZES) goes.

### 2. [x] An LFO per melodic track

**Built and flashed 2026-11-21, "works, nice" on the device.** Decided with Daniel: the **LFO** button opens a melodic track's LFO page (LFO
or FX: back); RATE in time with the song only (1/4 bar .. 16 bars); the song's LFO per track, and **a pattern can have
its own** (REC with EDIT on the LFO page; REC again: the song's). Decided on the way: TARGET CUTOFF, PITCH (DEPTH
squared: 10 is 12 cents, 100 an octave) or LEVEL (glides as a pattern's mix does: a SQUARE does not click); SHAPE
SINE, TRIANGLE, SAW UP, SAW DOWN, SQUARE, RANDOM, DRIFT; all LFOs at DEPTH 0 to start, RATE 4 BARS. Not part of a
sound (USER1 / USER2, the presets): picking another sound keeps the LFO. Playing, each step puts the LFOs where it is
(counted from PLAY); stopped, they run on at the tempo. The cutoff now moves in 1/256 of its steps (no zipper
steps). The page shows the wave as deep as DEPTH. `lfo.c`; script `tests/scripts/lfo.txt` (the WAV: the bass on
and off every 0.46 s at 130 BPM, the pad's filter once a bar). Song 4192 B, pattern 512 B: library patterns of 0.1
start fresh too. RAM 31.8 KB.

### 3. [x] A fuller synth

**Built and flashed 2026-11-21, "sounds fine" on the device.** Decided with Daniel: a **second sound page, OSC** (FX: notes, sound, OSC, mix);
the pad gets SUB and GLIDE too; new named sounds. Decided on the way: a sound is eight knobs now (`SOUND_KNOBS`, a page
shows four), so ALGORITHM, SEQ / REC (USER1 / USER2) and the `*` work on both pages. SUB: a square an octave down
(the pad's: under the chord's root, 100 about as loud as two of its notes); OSC2: the first's wave (2 SAWS: one saw)
DETUNE (13 cents), OCT -1, FIFTH (exactly 3/2), OCT +1, and its LEVEL; the synth's three together as loud (RMS) as
the first alone. GLIDE: 5 ms .. 1.5 s (time constant), only when the note before still sounds; the pad's notes glide
each to its place in the next chord (a note the chord before did not have: at once). New sounds MOOG (bass: saw, sub
60, detuned saw 50, glide 12), SEQ (arp: saw and a fifth), DEEP (pad: sub 70, slow, glide 30), MONO (lead: two saws,
glide 30). With everything off, every sound is byte-identical to before (`tests/regress.sh`). Script
`tests/scripts/osc.txt` (the WAV: the bass glides F1 to F2 in ~150 ms, the pad's sub on F1 under F2). The scripts that
went to a mix page with FX twice go three times now. Song 4208 B: songs of the build before start fresh (the library
stays), as do USER1 / USER2.

### 4. [x] A drone

**Built and flashed 2026-11-21; louder after "quite subtle", then "that's better".** Decided with Daniel: a **DRONE page on JUICY** (FX: NAMES, MIXER, DRONE,
JUICY), knobs LEVEL, NOTE, TONE, SWELL; NOTE picks the key's root (KEY OFF: A) or the pad's chord's root, each in
octave 1 .. 3 (KEY 1..3, CHORD 1..3; CHORD before the first chord: the key's); **a sixth row in the pattern's MIXER**
(key 6 mutes it there); **plays only while the song plays**. Decided on the way: two saws 3.4 cents apart each way and a
sine an octave down, through a soft filter (TONE 100: CUTOFF 90); fades in and out in about 2 s (it fades out in the
last pattern's mix, so a muted one stays silent); glides ~150 ms between roots; SWELL is the LFOs' DRIFT over 8 bars on
its level (100: down to nothing) and filter (+-15 steps); into the reverb at 60 %, no echo, in the middle; LEVEL 0 to
start (no drone), NOTE KEY 2, TONE 50, SWELL 50. After the first flash (Daniel: "quite subtle"): it was 12 dB
under a pad chord to the ear (A-weighted; its energy sat at 55 and 110 Hz under a filter at 130 Hz); TONE's range went
up (100: CUTOFF 90, was 70), TONE 50 to start (was 30), DRONE_GAIN 45000 (was 20000): now -1.3 dB, breathing. MIXER rows 19 px to fit six. `drone.c`; script
`tests/scripts/drone.txt` (the WAV: in over 2 s, breathing, out after STOP, silent when muted, A2 with A1 under it).
Song 4212 B; the pattern stays 512 B (the row took a padding byte) but its fields after the mix moved, so the
store's magic went from "JUCY" to "JUC2" (store.h: counted up for such changes): everything kept by older builds
starts fresh, USER1 / USER2 too.

### 5. [x] Stereo echo

**Built and flashed 2026-11-21, fine for now on the device** (Daniel, with DEMO1's ARP alone: "very subtle", he likes
that; maybe rethink the ranges later: the tracks' ECHO sends, how loud a repeat on one side is). Decided with Daniel: an **ECHO page on JUICY** (FX: NAMES, MIXER, DRONE, ECHO,
JUICY) with TIME, FEEDBACK (shown as REPEATS: FEEDBACK did not fit its place), WIDTH, TONE; songs start with a ping-pong
(WIDTH 100) a little darker (TONE 30), so the demos' echo changed too. Decided on the way: two lines of 1 s (the pool's
room: 300 of 344 KB now), each feeding the other (the left gets the sends); WIDTH 0 is both lines in both ears, which
is the old echo (checked: at WIDTH 0, TONE 0 the WAV differs from the old one by 4 of 32767 at most, rounding); TIME
1/16, 1/8, 1/8 DOT (as it was), 1/4, 1/4 DOT (no 1/2: 2 s at 60 BPM do not fit; longer than 1 s is cut to 1 s); REPEATS
0..100 is a feedback of 0..230/256 (61: the old 140); TONE a one-pole low-pass in the loop, 16 kHz to 600 Hz; the
reverb gets both sides' echo (half each). Script `tests/scripts/pingpong.txt` (the WAV: repeats 2..5 right, left,
right, left; WIDTH 0 both sides). Song 4216 B.

### 6. [ ] A generative sequence

**Built and flashed 2026-11-21, waiting for Daniel's ear.** Decided with Daniel: on **ARP**, as **MODE GEN** (after TONES), set on a **GEN
page behind the ARP button** (ARP or FX: back; REC with EDIT: MODE GEN / UP 1); **its own rhythm** (DENSITY) instead of
the placed hits; NOTES **CHORD or SCALE** (one knob as the drone's: CHORD 1..3, SCALE 1..3 = octaves); **per pattern**.
Decided on the way: a loop of 16 random 16-bit values, LENGTH 1..16 of them, counted from PLAY (a loop of 5 or 7
drifts against the bar); each time a step comes round, CHANGE % is the chance of a new value; the low byte over
DENSITY is a rest (so DENSITY takes away and gives back the same notes), the high byte the note: CHORD as MODE UP over
the octaves (no chord: silent), SCALE the key's (KEY OFF: A minor) from its root at C3 or above; a GEN note lasts its
step, no accents. The loop lives in the ISR (not saved: a new one at each power-on); the page draws it, the step that
plays orange. Starting values: CHANGE 10, CHORD 2, DENSITY 70, LENGTH 16 (`pattern_defaults`). Script
`tests/scripts/gen.txt` (the WAV: CHANGE 0 and LENGTH 7 repeat every 7 steps, CHANGE 100 a new note each step, all in
A minor; a rest can carry the echo of the note three steps before). Pattern 516 B, song 4248 B: songs and the library
start fresh.

## Room and rules

- **Storage:** a song is 4248 of 8180 B (two sectors since 2026-11-21, about 4 KB free); a pattern is
  516 B; the library holds 16 patterns in a 16 KB slot. Tables (grooves, progressions) cost flash, not storage.
- **Audio:** the audio interrupt peaks at 24 % of its 5.8 ms (measured 2026-11-13); [RULES.md](RULES.md) asks for
  under 50 %.
- **Free controls:** ALGORITHM on JUICY, DRUMS, PAD and LEAD (step 5 takes it on BASS and ARP), the ENV and ARP buttons (step 4 takes GLO, the LFO page LFO), JUICY's KNOB4 (step 1
  takes it), the upper keys outside OP1 .. OP5 and the mute keys.
- **Where code goes:** a page's knobs and keys in `pages.c`, what it shows in `view.c`, the data in `song.c`, the
  demo songs in `demos.c` (see the file map at the top of `firmware/src/juicy.c`).
- **Checking:** `sh tests/regress.sh record` before a change, `sh tests/regress.sh` after (SAME, or the expected
  differences); a host script per feature in `tests/scripts/`; listen to tables (grooves, progressions) as WAVs from
  the host before flashing. Ask Daniel before every flash and before taking over the desktop.
