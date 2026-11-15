# Juicy

**A groovebox firmware for the M-VAVE FM-1. Five tracks, patterns, a chain and sounds you can shape. Made for
jamming.**

Juicy turns the FM-1 into a small step sequencer in the spirit of Fruity Loops. You get drums, a bass, an
arpeggiator that follows your chords, a pad and a lead, each with its own steps. Everything lives on the panel, so
there are no menus to dig through.

I made it with techno, trance and lo-fi in mind, and I wanted it to be easy to pick up. Switch it on, press PLAY,
and the demo song plays. Press a key, and you hear the track on the screen. The keys light up wherever they do
something, and once you set a key for your song, they also show you which notes fit. That's kinda the whole idea.

<p align="center">
  <img src="docs/juicy-home.png" width="200" alt="The JUICY screen of DEMO1: the chain of patterns, tempo, reverb, swing and key">
  <img src="docs/juicy-drums.png" width="200" alt="The DRUMS screen: kick, hat and clap over 16 steps, GROOVE">
  <img src="docs/juicy-bass.png" width="200" alt="The BASS screen: a piano roll of one bar, NOTE, CUTOFF, DECAY, LENGTH">
  <img src="docs/juicy-sound.png" width="200" alt="The bass's sound page: ACID, with WAVE, RESO, ATTACK and ENV">
</p>

> **Juicy is young and still changing.** It runs on the FM-1, and I've been testing it on mine since 11.11.2026.
> Until there is a 1.0, an update that changes how songs are stored starts over with the demo song. So please don't
> keep anything on it that you can't afford to lose. Juicy also builds for the M-Vave SMK-37, but nobody has tried
> it there yet.

## What's in it

- **Five tracks:** DRUMS (kick, hat, clap, open hat, snare, tom), BASS, ARP (an arpeggiator over the pad's chords),
  PAD (chords) and LEAD.
- **Eight patterns, A to H.** Each track has 1 to 4 bars of 16 steps of its own. A pattern lasts as long as its
  longest track, and the shorter ones repeat. So a one-bar beat under a four-bar bass line just works.
- **A chain** of 16 slots that plays your patterns one after the other. Or you just loop the pattern on the screen.
- **Sounds:** each melodic track comes with a few named sounds (BASS: PLUCK, SUB, ACID, REESE; ARP: PLUCK, SQUARE,
  SAW, SOFT; PAD: WARM, BRIGHT, SLOW; LEAD: SUPERSAW, SQUARE, SOFT), a page of knobs to shape them, and two slots of
  your own (USER1, USER2) that every song can use.
- **A mixer:** level, pan, a tempo-synced echo and a reverb for each track (and each drum), with a limiter that
  keeps the sum clean. On top of that, **every pattern has a mix of its own**. Mute the pad for the part where the
  arpeggio should play alone, or bring a track down in one part only.
- **Mutes** for jamming: drop a track out and bring it back while the song plays.
- **Help for getting started:** drum grooves, chord progressions, bass lines, ARP rhythms, and whole song styles to
  build on.
- **Undo and redo.**
- **Three demo songs** (acid techno, uplifting trance, lo-fi), **8 slots** for your own songs, and **a library of
  16 patterns.** The song you're working on saves itself.
- **Hints:** a track that's still empty in the pattern on the screen tells you what to do
  (`TAP A STEP: A NOTE`, `REC: RECORD THE KEYS`, `KNOB1: A GROOVE` ...), until it holds something.
- **MIDI clock**, in and out.

## Installing it

Juicy installs over USB like every other FM-1 firmware. You need the package: `juicy-<version>.fwsc` from a
release, or `build/juicy.fwsc` if you build it yourself (see below). With Python 3 and
`pip install mido python-rtmidi`:

```sh
python tools/fm1_install.py --info                   # what runs on the connected FM-1
python tools/fm1_install.py build/juicy.fwsc         # asks before it writes, then restarts into Juicy
```

Want to go back to another firmware, or to the official one? Use the [FM-1 Firmware Hub](https://thefirmwarehub.com)
in Chrome or Edge.

If the FM-1 ever doesn't start, hold **OCT-** while you switch it on. That's the USB rescue mode, and any firmware
installs from there. And as always: installing firmware is at your own risk.

## Your first jam

1. Switch it on. The **JUICY** screen shows the demo song's chain. Press **PLAY**, and the song plays while PLAY
   flashes on the beat. Press it again to stop.
2. Press **SEL**, then **OP2** (the second black key from the left). That's the BASS screen. Press **SEL** again,
   and the keys play the bass. Don't worry, nothing you play gets recorded.
3. Play a note you like, then press **EDIT**. The title turns orange, and the 16 lower keys are now the 16 steps of
   the bar on the screen. Tap one to put your last note there. Tap it again to take it away.
4. **OCT+** goes to the next bar. On the last bar, with EDIT on, it adds a new one.
5. **FX** opens the bass's sound page. Turn **ALGORITHM** for another sound, and the four knobs to shape it. Press
   **FX** again for its mix (level, pan, reverb, echo).
6. Press **SEL**, then the black key one octave above OP1. The drums are muted. Do it again, and they're back.

## Finding your way around

The keys do one of three things. Two buttons, SEL and EDIT, switch between them:

| Mode | When | What the keys do |
| --- | --- | --- |
| **Play** | SEL and EDIT are dark | They play the track on the screen. On DRUMS, the six lower keys from the left are the six drums. On BASS, ARP and LEAD, all 27 keys are notes. On PAD, a key plays the chord on that note. Whatever you play last is what EDIT places. |
| **Select** | SEL is lit | **OP1 to OP5** pick DRUMS, BASS, ARP, PAD and LEAD. The five black keys one octave above them **mute** those tracks (lit means it plays). A muted track is gone from the pattern, but you can still play it on the keys. Mutes aren't saved. |
| **Edit** | EDIT is lit, and the title is orange | The 16 lower keys are the 16 steps of the bar on the screen (on JUICY, the 16 slots of the chain). On DRUMS and the other tracks, a step key acts when you let go. Hold it a moment instead, and it accents the step (see [Accents](#accents)). |

SEL and EDIT are never on at the same time. Pressing one switches the other off.

There's one rule that keeps you from breaking your song by accident: **nothing in the song changes without EDIT.**
The knobs, SEQ and REC all wait for it. Without EDIT, you can only play, move around and press PLAY.

Getting around:

- **HOME** takes you to the JUICY screen, and back to the track.
- **FX** flips through a track's pages: its notes, its sound (not on DRUMS) and its mix.
- **PRESETS** (the knob) picks the pattern, A to H, on every screen.
- **OCT- / OCT+** go to the bar before and the next bar (when you let go).
- **SAVE** opens SONGS, then the LIBRARY, then takes you back.
- **MASTER** (the knob) is the FM-1's own volume.

### The lights

A key or button that does something on the current screen **glows** dimly. One that is on, selected or holds
something (a set step, a used chain slot) is **lit**. With EDIT on a track, steps 1, 5, 9 and 13 glow a little
brighter: those are the beats. PLAY flashes on every beat while the song plays.

On the JUICY screen, with SEL and EDIT off, the keys follow the song while it plays. The lower key of the step that
sounds is lit, and OP1 to OP4 show the bars of the playing pattern (they glow, and the one that sounds is lit).

A muted track's name is dark on its screen, whether you muted it with SEL or in the pattern's MIXER.

## Making music

### The song's key

Set a key on the JUICY screen (KNOB4, with EDIT on), and Juicy shows you which notes fit. Notes outside the key
still play. They're just not highlighted.

- **BASS, ARP, LEAD:** the keys of the notes in the key glow brighter. The notes of the pad's chord are lit: with
  EDIT, the chord at the cursor, and while the song plays (EDIT off), the one that sounds. The note the knobs show
  blinks.
- **The roll** (BASS, LEAD): at each step, the rows of the chord's notes are tinted, and the chord's name (like
  `A MIN7`) shows next to the page's name.
- **PAD:** the roots in the key glow brighter. CHORD starts at **AUTO**, so each root gets the chord the key has
  there (in A minor: A MIN, D MIN, F MAJ, B DIM ...). With the cursor on a chord, NOTE moves its root and AUTO keeps
  it fitting. If you turn CHORD, you pick any chord by hand.
- With KEY OFF, everything works as if there was no key. The demo songs have theirs set (DEMO1 A MIN, DEMO2 D MIN,
  DEMO3 C MAJ).

### Drums

On DRUMS, **SELECT** (the knob) picks the drum the keys edit, the orange row. You see three drums at a time, and the
marks at the bottom tell you which page you're on. With EDIT on, a step key puts the selected drum on that step or
takes it away.

Not sure where to start? With EDIT on, turn **KNOB1** (GROOVE) through some well-known grooves: HOUSE, TECHNO,
TRANCE, BREAKS, GARAGE, DNB, DUBSTEP and HIPHOP. Each one is a bar, with its accents. There's also FILL, a bar of
snare and toms that leads into the next part. The groove plays right away instead of your drums, and KNOB4's spot
shows the tempo it usually has (your song's tempo stays the same). Nothing has changed yet, and REC is lit:

- **REC** keeps it. A groove becomes the drums' only bar; FILL goes into the bar on the screen. If the drums already
  held something, REC asks once more (REC AGAIN).
- **Turn back to OWN**, go to another page, turn PRESETS or switch EDIT off, and your drums are back as they were.
- While a groove is only playing, the step keys wait. Keep it first, then change it.

### Notes: bass, lead, pad and arpeggio

BASS and LEAD show a piano roll of the bar on the screen. A long note is one long bar in it. The knobs are NOTE,
CUTOFF, DECAY and LENGTH. LENGTH goes from 10 % to 90 % of a step, or from 1 to 16 steps (up to the end of the bar).

**SELECT** (the knob) moves the **cursor**, a framed step in the roll, through all of the track's bars. That works
with or without EDIT; with EDIT, the cursor's key blinks. While the song is stopped, you hear the note the cursor
lands on (not on ARP).

With EDIT on:

- **A step key** on an empty step puts the next note there: the one you played last, or what NOTE and LENGTH show.
  You hear it, and the cursor jumps there. On a step with a note, or anywhere on a long note, the key takes the whole
  note away.
- **The knobs work on the note under the cursor.** NOTE changes its pitch (you hear it), LENGTH its length, and
  CHORD (on PAD) its kind of chord. On an empty step, they set up the next note instead.

### Chords on the pad

PAD plays chords. **NOTE** is the chord's root, **KNOB2** the CUTOFF, and **CHORD** (KNOB3) the kind of chord:

- AUTO: with a key set, the chord the key has on that note
- MIN, MAJ, MIN7, MAJ7, DOM7, SUS2, SUS4, DIM
- POWER (root and fifth) and ROOT (just the root)
- MADD9 and ADD9: minor and major with an added ninth, voiced close (B C# D F#)

A chord sounds until the next one.

With EDIT on, **KNOB4** (CHORDS) goes through some well-known progressions, one chord per bar, in the song's key
(with KEY OFF, in A minor):

| Name | Chords | In A minor |
|---|---|---|
| EPIC | i VI III VII (the trance anthem) | Am F C G |
| DREAM | i iv VII III | Am Dm G C |
| RISE | VI VII i | F G Am Am |
| POP | I V vi IV | C G Am F |
| MINIMAL | i VII (two bars) | Am G |
| LOFI | ii V I vi with 7ths | Dm7 G7 Cmaj7 Am7 |

A minor progression in a major key (or the other way round) plays in the relative key, so its chords stay in the
key. In C major, EPIC is Am F C G too. The progression plays right away instead of the pad's chords (while stopped,
you hear its first chord). Nothing has changed yet, and REC is lit:

- **REC** keeps it: the pad gets its bars, with a chord on each. If the pad already held chords, REC asks once more
  (REC AGAIN).
- **Turn back to OWN**, go to another page, turn PRESETS or switch EDIT off, and your chords are back as they were.
- While a progression is only playing, NOTE, CHORD, the step keys and recording wait. Keep it first, then change it.

### The arpeggiator

ARP plays the chord the PAD sounds, one note at a time.

- **Its steps are hits, not notes.** A step key puts a hit there or takes it away. Like a drum hit, it's silent when
  you place it. LENGTH works as it does on BASS. When you record, any key puts a hit at the cursor.
- **Each hit plays the next note of the chord** (its one to four notes from the root, from C3 up), in the order
  **MODE** (KNOB1) says: UP, DOWN, UP/DN (up and back down) or RAND, over 1, 2 or 3 octaves. Each pattern has its own
  MODE.
- **A chord on the PAD starts it over** from its first note. Hold a chord on the PAD's keys while the song plays, and
  ARP follows that chord until you let go.
- **No chord, no arpeggio.** While the PAD has no chord (an empty pad track, or a pattern without one), ARP is
  silent. A muted PAD still leads it, so mute the PAD to hear the arpeggio alone.
- On ARP's screen, the keys play notes in its sound, so you can try it out.

MODE has one more setting: **TONES**. Here, every hit plays its own note of the chord, its **tone**, from 1 to 8.
Tones 1 to 4 are the chord's notes from the root up (in the order UP plays them), and 5 to 8 are the same notes an
octave higher. Over B MADD9 (B C# D F#), 1 is B, 2 is C#, 3 is D and 4 is F#.

The fun part: one bar of tones turns into a riff that follows whatever chord the PAD plays. The tones
1 4 2 3 1 1 4 2 3 play B F# C# D B B F# C# D over Bm, and E B F# G E E B F# G over Em.

To set the tones, switch EDIT on and put the cursor on a hit. KNOB1 is now its TONE (on an empty step, it's still
MODE). A new hit gets the last tone you used. The keys of the chord are lit: play one, and the next hit gets that
tone. While recording, it goes straight to the cursor (or lands live). Other keys only sound. The roll draws each hit
at its tone's height, with 1 at the bottom. If you switch to another MODE, the tones stay; they're just not used.

### Bass lines and ARP rhythms

On BASS and ARP, with EDIT on, **ALGORITHM** goes through ready-made parts. Its name shows over the roll for two
seconds (touch ALGORITHM to see it again). They work just like the progressions: they play right away instead of the
track's notes, REC keeps them (and asks again if there are notes), and OWN, another page, PRESETS or EDIT off bring
your notes back. Meanwhile, NOTE or MODE, LENGTH, the step keys and recording wait.

- **BASS:** OFFBEAT (trance: on each beat's "and"), OCTAVES (eighths, up an octave and back), GALLOP (two sixteenths
  on each off-beat), ROLLING (the three sixteenths after each kick) and ACID (short sixteenths with accents). A bass
  line is made from the pad's chords. Each step plays the root, its octave or the fifth of the chord the pad holds
  there, so it never plays a wrong note. It gets as many bars as the pad. Without chords, it plays the key's root
  (with KEY OFF: A).
- **ARP:** STRAIGHT (every sixteenth, the beats accented), EIGHTHS, SYNCOPATED (3 + 3 + 2 sixteenths) and GATE (the
  trance gate: short sixteenths with gaps). Each is a bar of hits, and the notes come from the pad as always.

### Recording

On BASS, ARP, PAD and LEAD, with EDIT on, tap **REC** (it lights up) to record.

- **Step by step**, while the song is stopped: the keys play, and each key puts its note at the cursor with the
  LENGTH shown. Then the cursor moves on by that length (on PAD, by a bar), round the track's bars. Turn SELECT for a
  rest. OCT- and OCT+ move the keys an octave (lit when they are), and a box tells you which notes they play now.
  They stay there when you stop recording. REC again, or EDIT off, stops it.
- **Live**, the same thing while the song plays: a key's note sounds right away and lands on the nearest step (if
  you're a bit early, on the step that's coming). It lasts as long as you hold the key. If that's shorter than a
  step, it gets 10 % to 90 % of one; on PAD, the chord holds until the next one. The notes that are already there
  stay, and a new one goes over whatever it covers. The screen follows the bar that plays. Only the pattern on the
  screen records, and only while it's the one that plays. **GLO** takes the whole take back.

### Accents

An accented step is louder. On BASS, ARP and LEAD, its note is also brighter (the filter opens further), the way an
acid bass bites. With EDIT on, **hold a step key** for a moment (0.6 s) instead of tapping it:

- **DRUMS:** the step's accent goes on or off, for every drum on that step (one accent row, like on the classic drum
  machines). On a step without the selected drum, the drum goes there, accented.
- **BASS, ARP, LEAD:** the note on that step (or the long note over it) gets accented, or not any more. On an empty
  step, the next note goes there, accented. PAD has no accents; holding works like a tap there.
- Accented hits and notes have a **white top** on the screen. Each pattern keeps its own. Clearing a track (holding
  REC) clears its accents too, and clearing on JUICY clears all of them.

### Patterns, bars and the chain

- **PRESETS** picks the pattern, A to H, on every screen.
- **Bars:** OCT- and OCT+ go to the bar before and the next bar. With EDIT on a track, OCT+ past the last bar adds an
  empty one (up to 4). **Hold OCT+** there instead, and the new bar is a copy of the last one (with one bar, the
  first 16 steps are copied into the next 16). **Hold OCT-** to remove the bar on the screen from that track. If it's
  the track's only bar, it gets emptied.
- **The chain:** on JUICY, with EDIT on, a lower key puts the pattern on the screen into that slot of the chain, or
  takes it out. **SEQ** (with EDIT) switches the chain on or off. When it's lit, the chain plays; when it's dark, the
  pattern on the screen loops.
- **Copying a pattern:** on JUICY, with EDIT on, **REC** copies the pattern on the screen into the first empty one.
- **Clearing:** hold **REC** with EDIT on. On JUICY, that clears the whole pattern on the screen. On DRUMS, it clears
  the selected drum (the orange row), and on BASS, ARP, PAD and LEAD, that track in the pattern on the screen. The
  bars stay.
- **Swing** belongs to the pattern: KNOB3 on JUICY, from 50 (straight) to 80, with every second step a bit later.
- **Names:** press FX on JUICY for the NAMES screen. With EDIT on, KNOB1 (LETTER) sets the character at the cursor:
  A to Z, 0 to 9, - and space. SELECT or a lower key moves the cursor. Keys 1 to 8 are the song's name, keys 9 to 16
  the name of the pattern on the screen, 8 characters each.

### Sounds

On BASS, ARP, PAD and LEAD, **FX** opens the track's sound page. With EDIT on, **ALGORITHM** picks the next or
previous sound. The knobs are WAVE, RESO, ATTACK and ENV (on the pad: SPREAD, RESO, ATTACK and RELEASE). A `*` after
the name means you changed it. With EDIT on, **SEQ** keeps the sound as USER1 and **REC** as USER2 (each one asks
twice). The keys always play here.

### Mixing

- **A track's mix page** (FX twice on a track, or once on DRUMS): KNOB1 to KNOB4 are LEVEL, PAN, REVERB and ECHO,
  with a bar for each. On DRUMS, there's one per drum, and LEVEL is the selected drum's (a key or SELECT picks the
  drum). The keys always play here. This mix is the same in every pattern.
- **The pattern's MIXER** (FX twice on JUICY): the pattern on the screen gets its own mix, with a row per track. SELECT
  picks the track (the orange row), and KNOB1 sets its LEVEL in this pattern, from 0 to 100 (with EDIT), on top of
  the track's own level. With EDIT on, the lower keys 1 to 5 (DRUMS, BASS, ARP, PAD, LEAD) **mute** that track in this
  pattern only, or bring it back. Lit means it plays, and a muted row is dark. The mix travels with the pattern into
  copies and into the library.
- **On JUICY**, KNOB1 is the TEMPO and KNOB2 the REVERB.

### Undo

**GLO** is undo: it brings back the song as it was before the last change. It glows when there's something to undo.
Changes less than a second apart count as one, and so does loading a song or recording a take live. Press GLO again
to bring the change back. A box over the page says UNDO or REDO. It doesn't work while a preview, a groove, a
progression, a bass line or a rhythm is playing.

### Starting from a style

On the SONGS screen, with NEW selected, **KNOB2** (STYLE) picks what a new song starts as: EMPTY, or TRANCE
(D minor, 138 BPM), TECHNO (A minor, 130 BPM) or LOFI (C major, 84 BPM, swung). A style is a whole song to build on.
It's made from the grooves, progressions, bass lines and rhythms above, in five parts, chained INTRO BUILD DROP BREAK
BUILD DROP OUTRO:

| Part | What plays |
|---|---|
| INTRO | drums, bass |
| BUILD | drums with a FILL in its last bar, pad, arpeggio |
| DROP | drums, bass, pad, arpeggio |
| BREAK | pad, arpeggio |
| OUTRO | drums, pad |

The LEAD is left free for your melody. Press PLAY to listen first (turning STYLE meanwhile switches between them), and
SEQ twice to load it.

## Saving your work

- **The song you work on** saves itself, two seconds after your last change, once it's stopped and silent (writing to
  the flash would interrupt the sound). It's still there after you switch off.
- **SONGS** (press SAVE): DEMO1 to DEMO3, NEW, and eight slots for whole songs (patterns, chain, sounds and tempo).
  KNOB1 picks the slot. **SEQ** loads (it asks twice), **REC** saves your song there (twice if there's a song in it
  already). You don't need EDIT here, but loading and saving only work while the song is stopped. A slot with a song
  in it shows its name.
- **LIBRARY** (press SAVE twice): 16 patterns, separate from the songs. KNOB1 or a lower key picks the slot. **SEQ**
  puts that pattern into the pattern on the screen (it asks twice if that one holds something, and it works while
  playing). **REC** stores the pattern on the screen there (while stopped). A library pattern is its notes, hits,
  swing and name. It plays with the song's sounds.
- **USER1 and USER2:** your own sounds, two for each melodic track, for every song.

**Listening first:** on SONGS and LIBRARY, PLAY plays the selected slot instead of your song. A song plays as it is
(its tempo, its sounds, its chain). A library pattern plays in place of the pattern on the screen, round and round,
with your song's sounds. Turn to another slot, and that one plays. Press PLAY again, go to another screen, or turn
PRESETS or press SEQ or REC to stop, and your song is exactly as it was.

## Cheat sheet

### Buttons and knobs

| | What it does |
| --- | --- |
| **PLAY** | start and stop, any time. On SONGS and LIBRARY: listen to the selected slot |
| **HOME** | the JUICY screen, and back to the track |
| **SAVE** | SONGS, then LIBRARY, then back |
| **FX** | a track's pages: its notes, its sound (not DRUMS), its mix. On JUICY: NAMES, the pattern's MIXER, and back |
| **OCT- / OCT+** | the bar before / the next bar. With EDIT on a track: OCT+ past the last bar adds one, holding OCT+ adds a copy, holding OCT- removes the bar. While recording: the keys an octave down / up |
| **PRESETS** | the pattern, A to H |
| **SEQ** | with EDIT: the chain on or off |
| **REC** | with EDIT: recording on or off (JUICY: copy the pattern). Held: clear |
| **GLO** | undo, and again: redo |
| **ALGORITHM** | with EDIT: the next sound (sound page), a bass line or an ARP rhythm |
| **SELECT** | the drum (DRUMS), the cursor (BASS, ARP, PAD, LEAD), the track (MIXER), the cursor (NAMES) |
| **MASTER** | the FM-1's volume |

### The screens

| Screen | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| **JUICY** | TEMPO | REVERB | SWING | KEY |
| **NAMES** (FX on JUICY) | LETTER | | | |
| **MIXER** (FX twice on JUICY) | LEVEL | | | |
| **DRUMS** | GROOVE | | | (the groove's usual BPM) |
| **BASS, LEAD** | NOTE | CUTOFF | DECAY | LENGTH |
| **ARP** | MODE (TONES: the hit's TONE) | CUTOFF | DECAY | LENGTH |
| **PAD** | NOTE | CUTOFF | CHORD | CHORDS (progressions) |
| **Sound page** | WAVE (PAD: SPREAD) | RESO | ATTACK | ENV (PAD: RELEASE) |
| **Mix page** | LEVEL | PAN | REVERB | ECHO |
| **SONGS** | the slot | STYLE (on NEW) | | |
| **LIBRARY** | the slot | | | |

## MIDI and USB

- **Following a clock:** when MIDI clock comes in (over USB or the TRS jack), Juicy follows its tempo. The JUICY
  screen shows **EXT** and the tempo it measures. START plays from the top, CONTINUE plays on from where Juicy stopped,
  and STOP stops. Juicy starts on the first clock after START or CONTINUE.
- **Leading:** without an incoming clock, PLAY sends START and STOP over USB, and Juicy sends the clock while it
  plays. So a DAW can follow it.
- **Recording:** over USB, the FM-1 is also a sound card (a line input for your computer or phone), always at full
  level. Turn MASTER down if you want to record with the FM-1 itself silent.

## Building it yourself

Juicy builds in Docker (or natively on Linux, the way Dinghy describes it):

```sh
docker build --platform linux/amd64 -t juicy-build -f tools/Dockerfile tools   # once
docker run --rm -v "${PWD}:/work" juicy-build sh build.sh                     # -> build/juicy.fwsc
docker run --rm -v "${PWD}:/work" juicy-build sh tests/run_tests.sh           # everything; "ALL TESTS PASSED"
docker run --rm -v "${PWD}:/work" juicy-build sh tests/run_host.sh tests/scripts/drums.txt   # on your computer
```

On Windows, `.\build_install.ps1` does it all in PowerShell. It starts Docker Desktop if needed, builds, and installs
on the connected FM-1 (it asks before it writes). The first run also builds the Docker image and a Python venv
(`.venv`) with mido and python-rtmidi.

**Where things are:** [firmware/src/juicy.c](firmware/src/juicy.c) is the panel, the pages and saving. Next to it are
`song.c` (what a song holds), `demos.c`, `sequencer.c` (the sequencer and the mixer, in the audio interrupt),
`pages.c` (what each page does), `view.c` (the lights and the screen) and `screen.c`. The sound lives in `drums.c`,
`synth.c`, `pad.c`, `echo.c`, `reverb.c` and `limiter.c`, on Dinghy's platform. They all build as one: `juicy.c`
includes the rest. The chip has no floating point and no 64-bit division. [docs/RULES.md](docs/RULES.md) has its
rules, and [docs/API.md](docs/API.md) what the platform offers.

**Trying things on your computer:** `tests/run_host.sh` runs Juicy from a script of key presses and knob turns
([tests/scripts/](tests/scripts/)). You get the sound as `build/host/juicy.wav` and the screen as PNGs. It starts with
a song of its own, [tests/test_song.c](tests/test_song.c) (DEMO1 as it was), so a new demo song doesn't change what
the scripts find. There's also a browser version with a clickable FM-1: run `sh web/build.sh` in the container, then
`node web/serve.js`, and open <http://localhost:8137>.

**Checking a change:** `tests/regress.sh` makes sure a change to the code didn't change anything it shouldn't have.
It runs every host script and compares the sound, the screens and the MIDI out with a list kept before. Run
`sh tests/regress.sh record` first, then `sh tests/regress.sh` after the change. It says `SAME`, or lists the scripts
whose output differs.

**Versions:** the number is in [VERSION](VERSION). A plain build shows it on the start screen as `0.1-dev` (identity
`FM-1_8860000`). A release is a tag like `v0.1`: GitHub builds `juicy-0.1.fwsc` (identity `FM-1_8860001`, and `0.1`
on the start screen). After that, `VERSION` moves on to the next number. What changed is in
[CHANGELOG.md](CHANGELOG.md).

## License and credits

Juicy is GPL-3.0-only. You may change it and share it, and whoever gets a firmware built from it gets its source,
too. The details and the third-party parts are in [LICENSING.md](LICENSING.md).

Juicy (the groovebox in `firmware/src/juicy.c` and its sound) is by Daniel Gilbert. It's built on Dinghy by zvenson,
whose platform is [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (@kurogedelic), Hügelton
Instruments, with [SLOOP](https://github.com/isod89/sloop-fm1)'s changes (isod89). The SMK-37 layer is by M. Geisler
(genkigenki), and the loader's screen is after [Skiff](https://github.com/illuminus5/skiff) (illuminus5).

"M-VAVE", "FM-1" and "SMK-37" belong to their owners. Juicy is not made or endorsed by them.
