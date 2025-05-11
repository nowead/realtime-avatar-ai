// src/js/audio.js

// 오디오 청크 병합 유틸
function mergeChunks(chunks) {
  const totalLength = chunks.reduce((sum, c) => sum + c.length, 0);
  const merged = new Int16Array(totalLength);
  let offset = 0;
  for (const chunk of chunks) {
    merged.set(chunk, offset);
    offset += chunk.length;
  }
  return merged;
}

// 오디오 서비스 객체
const AudioService = {
  audioContext: null,
  micStream: null,
  workletNode: null,
  onVADFrame: null,

  async initialize(sampleRate = 16000) {
    this.audioContext = new AudioContext({ sampleRate });

    // 🔥 Webpack 환경에서는 public/ 디렉토리 기준 경로여야 함!
    await this.audioContext.audioWorklet.addModule('/worklets/audio-worklet-processor.js');

    this.micStream = await navigator.mediaDevices.getUserMedia({ audio: true });
    const source = this.audioContext.createMediaStreamSource(this.micStream);

    this.workletNode = new AudioWorkletNode(this.audioContext, 'mic-processor');
    source.connect(this.workletNode);

    this.workletNode.port.onmessage = (e) => {
      if (this.onVADFrame) {
        this.onVADFrame(e.data); // e.data: Int16Array
      } else {
        console.warn('[AudioService] onVADFrame not set!');
      }
    };

    console.log('[AudioService] AudioWorklet initialized.');
  },

  stop() {
    if (this.workletNode) {
      this.workletNode.disconnect();
      this.workletNode = null;
    }
    if (this.micStream) {
      this.micStream.getTracks().forEach(track => track.stop());
      this.micStream = null;
    }
    if (this.audioContext) {
      this.audioContext.close();
      this.audioContext = null;
    }
    console.log('[AudioService] stopped.');
  }
};

export { AudioService, mergeChunks };
