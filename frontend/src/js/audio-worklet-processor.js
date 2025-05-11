// public/audio-worklet-processor.js
class MicProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.buffer = [];
    this.frameSize = 512; // 정확히 32ms @ 16kHz
  }

  process(inputs) {
    const input = inputs[0][0]; // Mono channel
    if (!input) return true;

    this.buffer.push(...input);

    while (this.buffer.length >= this.frameSize) {
      const chunk = this.buffer.slice(0, this.frameSize);
      this.buffer = this.buffer.slice(this.frameSize);

      const int16 = new Int16Array(chunk.length);
      for (let i = 0; i < chunk.length; i++) {
        int16[i] = Math.max(-32768, Math.min(32767, chunk[i] * 32767));
      }

      this.port.postMessage(int16); // int16.length === 512
    }

    return true;
  }
}

registerProcessor('mic-processor', MicProcessor);
