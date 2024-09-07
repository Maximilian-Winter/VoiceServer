class VoiceProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();
        this.framesPerBuffer = options.processorOptions.framesPerBuffer;
        this.channels = options.processorOptions.channels;
        this.packetInterval = options.processorOptions.packetInterval;
        this.sampleRate = sampleRate;
        this.inputBuffer = new Int16Array(this.framesPerBuffer * this.channels);
        this.inputBufferIndex = 0;
        this.outputBuffer = new Float32Array(this.framesPerBuffer * this.channels);
        this.outputBufferIndex = 0;
        this.lastPacketTime = 0;

        this.port.onmessage = this.handleRemoteAudio.bind(this);
    }

    handleRemoteAudio(event) {
        const remoteAudio = new Int16Array(event.data);
        for (let i = 0; i < remoteAudio.length; i++) {
            this.outputBuffer[this.outputBufferIndex++] = remoteAudio[i] / 32768;
            if (this.outputBufferIndex >= this.outputBuffer.length) {
                this.outputBufferIndex = 0;
            }
        }
    }

    process(inputs, outputs, parameters) {
        const input = inputs[0];
        const output = outputs[0];
        const currentTime = globalThis.currentTime;;

        // Handle input
        if (input.length > 0 && input[0].length > 0) {
            for (let i = 0; i < input[0].length; i++) {
                this.inputBuffer[this.inputBufferIndex++] = Math.floor(input[0][i] * 32767);
                if (this.inputBufferIndex >= this.inputBuffer.length) {
                    this.sendInputBuffer();
                    this.inputBufferIndex = 0;
                }
            }
        }

        // Handle output
        for (let channel = 0; channel < this.channels; channel++) {
            for (let i = 0; i < output[channel].length; i++) {
                output[channel][i] = this.outputBuffer[i];
            }
        }

        // Apply smoothing to reduce clicking
        const smoothingFactor = 0.1;
        for (let i = 1; i < output[0].length; i++) {
            output[0][i] = output[0][i-1] + smoothingFactor * (output[0][i] - output[0][i-1]);
        }

        // Send any remaining input buffer if enough time has passed
        if (currentTime - this.lastPacketTime >= this.packetInterval / 1000) {
            if (this.inputBufferIndex > 0) {
                this.sendInputBuffer();
                this.inputBufferIndex = 0;
            }
            this.lastPacketTime = currentTime;
        }

        return true;
    }

    sendInputBuffer() {
        this.port.postMessage(this.inputBuffer.slice(0, this.inputBufferIndex).buffer, [this.inputBuffer.slice(0, this.inputBufferIndex).buffer]);
    }
}

registerProcessor('voice-processor', VoiceProcessor);