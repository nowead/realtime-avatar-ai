import * as ort from 'onnxruntime-web';

export class SileroVAD {
  constructor(modelPath = '/models/silero_vad.onnx', sampleRate = 16000) {
    this.modelPath = modelPath;
    this.sampleRate = sampleRate;
    this.session = null;
    this.hidden = null; // Tensor: shape [2, 1, 128]
  }

  async loadModel() {
    this.session = await ort.InferenceSession.create(this.modelPath);

    // 정확히 256개 (2×1×128) float32 값으로 초기화
    const stateData = new Float32Array(2 * 1 * 128).fill(0);
    this.hidden = new ort.Tensor('float32', stateData, [2, 1, 128]);

    console.log('[SileroVAD] Model loaded. Initial state shape:', this.hidden.dims);
  }

  async detect(pcmChunk) {
    // Int16Array → Float32 [-1, 1] 범위로 정규화
    const floatData = new Float32Array(pcmChunk.length);
    for (let i = 0; i < pcmChunk.length; i++) {
      floatData[i] = pcmChunk[i] / 32768;
    }

    const input = new ort.Tensor('float32', floatData, [1, pcmChunk.length]);
    const sr = new ort.Tensor('int64', new BigInt64Array([BigInt(this.sampleRate)]), []);

    const feeds = {
      input: input,
      sr: sr,
      state: this.hidden,
    };

    const results = await this.session.run(feeds);
    // console.log('[DEBUG] result keys:', Object.keys(results));

    const speechProb = results.output?.data?.[0] ?? 0.0;

    // ✅ state 또는 stateN 자동 탐지
    const stateKey = Object.keys(results).find(k => k.toLowerCase().includes('state') && k !== 'output');

    if (!stateKey || !results[stateKey] || !results[stateKey].dims || results[stateKey].dims.length !== 3) {
      console.warn('[VAD] Invalid state returned. Skipping update. Got:', results[stateKey]?.dims);
      return speechProb > 0.5;
    }

    this.hidden = results[stateKey]; // ex: results.stateN
    return speechProb > 0.5;
  }
}
