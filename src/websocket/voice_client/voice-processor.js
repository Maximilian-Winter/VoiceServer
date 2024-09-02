class VoiceProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();
        this.framesPerBuffer = options.processorOptions.framesPerBuffer;
        this.channels = options.processorOptions.channels;
        this.packetInterval = options.processorOptions.packetInterval;
        this.actualSampleRate = options.processorOptions.actualSampleRate;
        this.buffer = new Float32Array(this.framesPerBuffer * this.channels);
        this.bufferIndex = 0;
        this.lastPacketTime = currentTime;
        this.resampleRatio = 44100 / this.actualSampleRate;
    }

    process(inputs, outputs, parameters) {
        const input = inputs[0];
        const output = outputs[0];

        if (input.length === 0 || input[0].length === 0) {
            // No input, fill buffer with silence
            const samplesToFill = Math.min(128, this.buffer.length - this.bufferIndex);
            this.buffer.fill(0, this.bufferIndex, this.bufferIndex + samplesToFill);
            this.bufferIndex += samplesToFill;
        } else {
            const channelCount = Math.min(input.length, this.channels);

            for (let i = 0; i < input[0].length; i++) {
                for (let channel = 0; channel < channelCount; channel++) {
                    const inputValue = input[channel][i];
                    if (output[channel]) {
                        output[channel][i] = inputValue;
                    }

                    // Simple linear resampling
                    this.buffer[Math.floor(this.bufferIndex * this.resampleRatio)] = inputValue;
                    this.bufferIndex++;

                    this.sendBufferIfNeeded();
                }
            }
        }

        this.sendBufferIfNeeded();
        return true;
    }

    sendBufferIfNeeded() {
        const now = currentTime;
        if (now - this.lastPacketTime >= this.packetInterval / 1000) {
            // Send the buffer to the main thread
            this.port.postMessage(this.buffer.slice(0, this.bufferIndex));
            this.bufferIndex = 0;
            this.lastPacketTime = now;
        }
    }
}

registerProcessor('voice-processor', VoiceProcessor);