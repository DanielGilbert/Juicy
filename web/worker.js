// SPDX-License-Identifier: GPL-3.0-only
// The firmware's thread. app_main never returns, so this worker never gets back to its message loop: everything
// crosses over in web_poll (called by tests/web.c once per simulated millisecond) through the shared memory.
onmessage = async (message) => {
    const { layout, controlBuffer, ringBuffer, screenBuffer, wasmBytes } = message.data;
    const control = new Int32Array(controlBuffer);
    const ring = new Int16Array(ringBuffer);
    const screen = new Uint16Array(screenBuffer);
    const ringFrames = ring.length / 2;
    const TARGET_FRAMES = 1536;                 // sound kept ahead of the speaker: ~35 ms
    const SCREEN_EVERY_MS = 16;

    let memory, io, ioSigned, framebuffer;
    let simulatedMs = 0, wallStart = performance.now();

    function bufferedFrames() {
        const write = Atomics.load(control, layout.RING_WRITE), read = Atomics.load(control, layout.RING_READ);
        return (write - read + ringFrames) % ringFrames;
    }

    function web_poll() {
        simulatedMs++;
        // tests/web.c web_io: notes, buttons, enc[7], leds[2], glow[2]
        io[0] = Atomics.load(control, layout.NOTES);
        io[1] = Atomics.load(control, layout.BUTTONS);
        for (let encoder = 0; encoder < 7; encoder++)
            ioSigned[2 + encoder] += Atomics.exchange(control, layout.ENCODERS + encoder, 0);
        for (let word = 0; word < 4; word++)
            Atomics.store(control, layout.LEDS + word, io[9 + word]);

        if (simulatedMs % SCREEN_EVERY_MS === 0) {
            screen.set(framebuffer);
            Atomics.add(control, layout.SCREEN_COUNT, 1);
        }

        // real time: follow the sound card while it plays, the clock while it does not
        if (Atomics.load(control, layout.AUDIO_ON)) {
            while (bufferedFrames() > TARGET_FRAMES) Atomics.wait(control, layout.SLEEP, 0, 2);
            wallStart = performance.now() - simulatedMs;
        } else {
            while (simulatedMs > performance.now() - wallStart) Atomics.wait(control, layout.SLEEP, 0, 1);
        }
    }

    function web_audio(pointer, frames) {
        if (!Atomics.load(control, layout.AUDIO_ON)) return;
        const samples = new Int16Array(memory.buffer, pointer, frames * 2);
        let write = Atomics.load(control, layout.RING_WRITE);
        for (let frame = 0; frame < frames; frame++) {
            ring[2 * write] = samples[2 * frame];
            ring[2 * write + 1] = samples[2 * frame + 1];
            write = (write + 1) % ringFrames;
        }
        Atomics.store(control, layout.RING_WRITE, write);
    }

    const { instance } = await WebAssembly.instantiate(wasmBytes, { env: { web_poll, web_audio } });
    memory = instance.exports.memory;
    io = new Uint32Array(memory.buffer, instance.exports.web_io(), 13);
    ioSigned = new Int32Array(memory.buffer, instance.exports.web_io(), 13);
    framebuffer = new Uint16Array(memory.buffer, instance.exports.web_fb(), 240 * 240);
    instance.exports.web_start();
};
