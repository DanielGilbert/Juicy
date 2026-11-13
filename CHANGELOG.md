# Changelog

What changed in each version of Juicy. The version being worked on is in [VERSION](VERSION); a build that is not a
release shows it on the start screen with `-dev` (`0.1-dev`).

## 0.1 (not released yet)

The first version: Juicy on the FM-1, tested on the device since 11.11.2026.

- Five tracks: DRUMS (kick, hat, clap, open hat, snare, tom), BASS, ARP, PAD (chords) and LEAD, each with
  1 to 4 bars of 16 steps.
- Patterns A to H, and a chain of 16 places that plays them one after the other.
- Named sounds for every melodic track, a sound page of knobs to shape them, and two places of your own (USER1,
  USER2) for every song.
- A mixer: level (each drum's), pan, echo and reverb send per track; a tempo-synced echo, a reverb and a limiter.
- Mutes for jamming.
- Three demo songs, eight places for songs of your own, a library of 16 patterns; the song you work on saves itself.
- MIDI clock: follows an incoming clock (USB or TRS) with START, CONTINUE and STOP; leads with START, STOP and clock
  over USB when nothing comes in.
- The keys glow where they do something on the screen shown.
- The JUICY screen: KNOB2 is REVERB; the PATTERN knob is gone (PRESETS picks the pattern on every screen).
- DRUMS: the SELECT knob picks the drum the keys edit, EDIT or not, also on its mix page; KNOB1 does nothing there.
- Clearing: hold REC with EDIT on to empty the pattern on the screen (JUICY), the selected drum (DRUMS) or a track;
  its bars stay. A tap of REC on JUICY still copies the pattern, now when REC is let go.
- Swing per pattern: KNOB3 on the JUICY screen, 50 (straight) to 80, delays every second step. It travels with the
  pattern into copies and the library; songs and library patterns kept before load with it straight.
- Note length: KNOB4 (LENGTH) on BASS, ARP and LEAD, 1 to 16 steps, for the notes EDIT places; a note ends with
  its bar. Songs and patterns kept before play as they did.
- Notes reworked: SELECT moves a step cursor on BASS, ARP, PAD and LEAD; with EDIT the knobs change the note
  under it (pitch, length, chord), a step key takes a note away or puts the next one on an empty step (no more
  replacing a note by pressing with another one), and placing or turning NOTE lets you hear it.
- Step recording: EDIT, then a tap of REC on a track: the keys put their notes at the cursor, which moves on;
  OCT- / OCT+ move the keys an octave.
- Notes shorter than a step: LENGTH goes below 1, 10 % to 90 % of a step; such a note is narrower in the roll. Songs,
  song places and library patterns kept before load with all their notes as they were.
- SEQUENCE is now ARP, an arpeggiator: its steps are hits that play the PAD's chord one note at a time, in the order
  of MODE (KNOB1, per pattern): UP, DOWN, UP/DN or RAND, over 1 to 3 octaves. A chord on the PAD starts it over; a
  muted PAD still leads it. The demo songs have new arpeggios (DEMO2's pattern B now has the pad). Songs, song places
  and library patterns kept before load without their SEQUENCE notes.
- Start screen: the light runs over the 16 lower keys from left to right, starting when the screen comes; the buttons
  stay dark until it is over.
