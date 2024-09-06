class VoiceProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();
        this.framesPerBuffer = options.processorOptions.framesPerBuffer;
        this.channels = options.processorOptions.channels;
        this.packetInterval = options.processorOptions.packetInterval;
        this.actualSampleRate = options.processorOptions.actualSampleRate;
        this.buffer = new Float32Array(this.framesPerBuffer * this.channels);
        this.bufferIndex = 0;
        this.lastSendTime = currentTime;
    }

    process(inputs, outputs, parameters) {
        const input = inputs[0];
        const currentBuffer = this.buffer;

        for (let i = 0; i < input[0].length; i++) {
            for (let channel = 0; channel < this.channels; channel++) {
                currentBuffer[this.bufferIndex++] = input[channel][i];
            }

            if (this.bufferIndex >= this.framesPerBuffer) {
                this.port.postMessage(currentBuffer);
                this.bufferIndex = 0;
            }
        }

        return true;
    }
}

registerProcessor('voice-processor', VoiceProcessor);