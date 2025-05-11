// public/audio-worklet-processor.js

// 🎙️ 마이크 입력 처리 (업스트림)
class MicProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.buffer = [];
    this.frameSize = 512; // 32ms @ 16kHz
  }

  process(inputs) {
    const input = inputs[0][0]; // Mono input
    if (!input) return true;

    this.buffer.push(...input);

    while (this.buffer.length >= this.frameSize) {
      const chunk = this.buffer.slice(0, this.frameSize);
      this.buffer = this.buffer.slice(this.frameSize);

      const int16 = new Int16Array(chunk.length);
      for (let i = 0; i < chunk.length; i++) {
        int16[i] = Math.max(-32768, Math.min(32767, chunk[i] * 32767));
      }

      this.port.postMessage(int16); // 서버로 전송할 16bit PCM
    }

    return true;
  }
}

registerProcessor('mic-processor', MicProcessor);

// 🔊 서버에서 받은 오디오 재생 처리 (다운스트림)
class PCMPlayerProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.buffer = new Float32Array(0);

    this.port.onmessage = (event) => {
      const chunk = event.data;
      if (!(chunk instanceof Float32Array)) return;

      const combined = new Float32Array(this.buffer.length + chunk.length);
      combined.set(this.buffer);
      combined.set(chunk, this.buffer.length);
      this.buffer = combined;
    };
  }

  process(_, outputs) {
    const output = outputs[0];
    const channel = output[0];

    if (this.buffer.length >= channel.length) {
      channel.set(this.buffer.subarray(0, channel.length));
      this.buffer = this.buffer.subarray(channel.length);
    } else {
      channel.set(this.buffer);
      channel.fill(0, this.buffer.length);
      this.buffer = new Float32Array(0);
    }

    return true;
  }
}

registerProcessor('pcm-player', PCMPlayerProcessor);
