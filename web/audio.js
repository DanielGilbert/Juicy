// SPDX-License-Identifier: GPL-3.0-only
// The speaker's side of the ring web/worker.js fills: 16-bit stereo frames at 44.1 kHz.
class JuicyOutput extends AudioWorkletProcessor {
    constructor(options) {
        super();
        const { layout, controlBuffer, ringBuffer } = options.processorOptions;
        this.layout = layout;
        this.control = new Int32Array(controlBuffer);
        this.ring = new Int16Array(ringBuffer);
        this.ringFrames = this.ring.length / 2;
    }

    process(inputs, outputs) {
        const left = outputs[0][0], right = outputs[0][1];
        let read = Atomics.load(this.control, this.layout.RING_READ);
        const write = Atomics.load(this.control, this.layout.RING_WRITE);
        const available = (write - read + this.ringFrames) % this.ringFrames;
        if (available < left.length) return true;       // not enough yet: silence
        for (let frame = 0; frame < left.length; frame++) {
            left[frame] = this.ring[2 * read] / 32768;
            right[frame] = this.ring[2 * read + 1] / 32768;
            read = (read + 1) % this.ringFrames;
        }
        Atomics.store(this.control, this.layout.RING_READ, read);
        return true;
    }
}
registerProcessor("juicy-output", JuicyOutput);
