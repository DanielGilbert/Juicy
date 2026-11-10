// SPDX-License-Identifier: GPL-3.0-only
// The page's side of the browser FM-1: the panel in, the screen and the LEDs out, the sound to the speaker.
// The firmware runs in web/worker.js; the three share `control` (the indices in `layout`), the sound ring and the screen.
const layout = { NOTES: 0, BUTTONS: 1, ENCODERS: 2, LEDS: 9, RING_WRITE: 13, RING_READ: 14, AUDIO_ON: 15, SCREEN_COUNT: 16, SLEEP: 17 };
const RING_FRAMES = 16384;
const BUTTON_SHORTCUTS = { Space: 12, ArrowLeft: 0, ArrowRight: 1 };
const FIRST_NOTE = 53;                          // key 0 = F3
const KEY_COUNT = 27;
const WHITE_KEY_COUNT = 16;
const WHITE_CODES = ["KeyZ", "KeyX", "KeyC", "KeyV", "KeyB", "KeyN", "KeyM", "Comma", "Period", "Slash", "KeyQ", "KeyW", "KeyE", "KeyR", "KeyT", "KeyY"];
const BLACK_CODES = ["KeyS", "KeyD", "KeyF", "KeyG", "KeyH", "KeyJ", "KeyK", "KeyL", "Semicolon", "Quote", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7"];
const BLACK_PRINTS = ["OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "PIT", "GLO", "MONO", "POLY", ""];   // what the FM-1 prints on its upper keys
const DRAG_PIXELS_PER_DETENT = 10;
const DEGREES_PER_DETENT = 15;

const statusLine = document.getElementById("status");
const isBlackNote = (note) => [1, 3, 6, 8, 10].includes(note % 12);
const codeLabel = (code) => code.replace(/^Key|^Digit/, "").replace("Comma", ",").replace("Period", ".").replace("Slash", "/").replace("Semicolon", ";").replace("Quote", "'");

if (!crossOriginIsolated) {
    statusLine.textContent = "This page needs its own server: run  node web/serve.js  and open http://localhost:8137";
    throw new Error("not cross-origin isolated");
}

const controlBuffer = new SharedArrayBuffer(4 * 32);
const ringBuffer = new SharedArrayBuffer(2 * 2 * RING_FRAMES);
const screenBuffer = new SharedArrayBuffer(2 * 240 * 240);
const control = new Int32Array(controlBuffer);
const screenPixels = new Uint16Array(screenBuffer);

// ---- keys and buttons: held = by the mouse or the keyboard; a bit set in control while either holds it
const heldBy = { [layout.NOTES]: new Map(), [layout.BUTTONS]: new Map() };
const elements = { [layout.NOTES]: [], [layout.BUTTONS]: [] };

function setHeld(word, bit, source, down) {
    const sources = heldBy[word].get(bit) || new Set();
    if (down) sources.add(source); else sources.delete(source);
    heldBy[word].set(bit, sources);
    if (sources.size) Atomics.or(control, word, 1 << bit); else Atomics.and(control, word, ~(1 << bit));
    elements[word][bit].classList.toggle("held", sources.size > 0);
}

function pressable(element, word, bit) {
    elements[word][bit] = element;
    element.addEventListener("pointerdown", (event) => {
        startAudio();
        if (event.shiftKey && word === layout.BUTTONS) {            // Shift+click: stays down until the next one
            setHeld(word, bit, "latch", !heldBy[word].get(bit)?.has("latch"));
            return;
        }
        element.setPointerCapture(event.pointerId);
        setHeld(word, bit, "mouse", true);
    });
    for (const type of ["pointerup", "pointercancel"])
        element.addEventListener(type, () => setHeld(word, bit, "mouse", false));
}

for (const button of document.querySelectorAll("[data-button]"))
    pressable(button, layout.BUTTONS, Number(button.dataset.button));

const keyBox = document.getElementById("keys");
const keyCodes = {};                            // keyboard code -> key
for (let key = 0, white = 0, black = 0; key < KEY_COUNT; key++) {
    const isBlack = isBlackNote(FIRST_NOTE + key);
    const code = isBlack ? BLACK_CODES[white - 1] : WHITE_CODES[white];
    const element = document.createElement("div"), print = document.createElement("div"), shortcut = document.createElement("div");
    element.className = "key " + (isBlack ? "black" : "white");
    element.style.left = (isBlack ? white : white + 0.5) * 100 / WHITE_KEY_COUNT + "%";   // an upper key sits over the gap
    print.className = "print";
    print.textContent = isBlack ? BLACK_PRINTS[black] : "";
    shortcut.className = "shortcut";
    shortcut.textContent = codeLabel(code);
    element.append(print, shortcut);
    keyBox.append(element);
    pressable(element, layout.NOTES, key);
    keyCodes[code] = key;
    if (isBlack) black++; else white++;
}

for (const [type, down] of [["keydown", true], ["keyup", false]])
    addEventListener(type, (event) => {
        if (event.repeat || event.ctrlKey || event.altKey || event.metaKey) return;
        if (event.code in keyCodes) setHeld(layout.NOTES, keyCodes[event.code], "keyboard", down);
        else if (event.code in BUTTON_SHORTCUTS) setHeld(layout.BUTTONS, BUTTON_SHORTCUTS[event.code], "keyboard", down);
        else return;
        event.preventDefault();
        if (down) startAudio();
    });

// ---- knobs: the wheel or a drag, a detent at a time. The encoders go to the firmware; MASTER is the page's volume
function turnable(knob, onDetents) {
    let angle = 0, dragFrom = null;
    const turn = (detents) => {
        startAudio();
        angle += onDetents(detents) * DEGREES_PER_DETENT;
        knob.style.transform = `rotate(${angle}deg)`;
    };
    knob.addEventListener("wheel", (event) => { event.preventDefault(); turn(event.deltaY < 0 ? 1 : -1); }, { passive: false });
    knob.addEventListener("pointerdown", (event) => { knob.setPointerCapture(event.pointerId); dragFrom = event.clientY; });
    knob.addEventListener("pointermove", (event) => {
        if (dragFrom === null) return;
        const detents = Math.trunc((dragFrom - event.clientY) / DRAG_PIXELS_PER_DETENT);
        if (detents) { dragFrom -= detents * DRAG_PIXELS_PER_DETENT; turn(detents); }
    });
    for (const type of ["pointerup", "pointercancel"])
        knob.addEventListener(type, () => { dragFrom = null; });
    return turn;
}

for (const knob of document.querySelectorAll("[data-encoder]"))
    turnable(knob, (detents) => { Atomics.add(control, layout.ENCODERS + Number(knob.dataset.encoder), detents); return detents; });

const MASTER_STEPS = 18;                        // a pot, not an encoder: it stops at both ends
let masterStep = MASTER_STEPS, masterGain;
turnable(document.querySelector("[data-master]"), (detents) => {
    const before = masterStep;
    masterStep = Math.max(0, Math.min(MASTER_STEPS, masterStep + detents));
    if (masterGain) masterGain.gain.value = (masterStep / MASTER_STEPS) ** 2;
    return masterStep - before;
});

// ---- the sound: only after the first click or key (browsers allow it no earlier)
let audioContext;
async function startAudio() {
    if (audioContext) return;
    audioContext = new AudioContext({ sampleRate: 44100 });
    await audioContext.audioWorklet.addModule("audio.js");
    const output = new AudioWorkletNode(audioContext, "juicy-output", {
        numberOfInputs: 0, outputChannelCount: [2], processorOptions: { layout, controlBuffer, ringBuffer },
    });
    masterGain = audioContext.createGain();
    masterGain.gain.value = (masterStep / MASTER_STEPS) ** 2;
    output.connect(masterGain).connect(audioContext.destination);
    Atomics.store(control, layout.AUDIO_ON, 1);
    statusLine.textContent = "Running, sound on.";
}

// ---- the screen and the LEDs, each frame
const screenContext = document.getElementById("screen").getContext("2d");
const image = screenContext.createImageData(240, 240);
let screenDrawn = -1;
function frame() {
    const count = Atomics.load(control, layout.SCREEN_COUNT);
    if (count !== screenDrawn) {
        screenDrawn = count;
        for (let pixel = 0; pixel < 240 * 240; pixel++) {
            const colour = screenPixels[pixel];
            image.data[4 * pixel] = (colour >> 11) * 255 / 31;
            image.data[4 * pixel + 1] = (colour >> 5 & 63) * 255 / 63;
            image.data[4 * pixel + 2] = (colour & 31) * 255 / 31;
            image.data[4 * pixel + 3] = 255;
        }
        screenContext.putImageData(image, 0, 0);
    }
    const ledBit = (firstWord, id) => (Atomics.load(control, firstWord + (id >> 5)) >>> (id & 31)) & 1;
    for (let id = 0; id < 14 + KEY_COUNT; id++) {   // LED ids: 0..13 the buttons, 14..40 the keys
        const element = id < 14 ? elements[layout.BUTTONS][id] : elements[layout.NOTES][id - 14];
        element.classList.toggle("lit", ledBit(layout.LEDS, id) === 1);
        element.classList.toggle("glow", ledBit(layout.LEDS + 2, id) === 1);
    }
    requestAnimationFrame(frame);
}

(async () => {
    const response = await fetch("juicy.wasm");
    if (!response.ok) {
        statusLine.textContent = "No build/web/juicy.wasm yet: run web/build.sh";
        return;
    }
    const worker = new Worker("worker.js");
    worker.onerror = (event) => { statusLine.textContent = "The firmware stopped: " + event.message; };
    worker.postMessage({ layout, controlBuffer, ringBuffer, screenBuffer, wasmBytes: await response.arrayBuffer() });
    statusLine.textContent = "Running. Click or press a key for sound.";
    requestAnimationFrame(frame);
})();
