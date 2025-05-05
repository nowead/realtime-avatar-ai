// js/vad.js
const VadService = {
    ortSession: null,
    modelPath: 'models/silero_vad.onnx',
    threshold: 0.5, // VAD 임계값 (조정 필요)
    isSpeaking: false,
    onVoiceStart: null, // 음성 시작 콜백
    onVoiceEnd: null,   // 음성 종료 콜백
    audioProcessorNode: null, // ScriptProcessorNode 또는 AudioWorkletNode
    audioContext: null,
    sampleRate: 16000, // Silero VAD가 요구하는 샘플 레이트
    bufferSize: 512, // 처리할 오디오 청크 크기 (모델 요구사항 확인)

    async initialize(audioContext, onVoiceStartCallback, onVoiceEndCallback) {
        this.audioContext = audioContext;
        this.onVoiceStart = onVoiceStartCallback;
        this.onVoiceEnd = onVoiceEndCallback;

        try {
            // ONNX Runtime 세션 생성 및 모델 로드
            this.ortSession = await ort.InferenceSession.create(this.modelPath);
            console.log("Silero VAD ONNX model loaded.");
        } catch (e) {
            console.error("Failed to load VAD model:", e);
            throw e;
        }
    },

    startProcessing(micSourceNode) {
        if (!this.ortSession || !this.audioContext) {
            console.error("VAD not initialized or AudioContext missing.");
            return;
        }

        // ScriptProcessorNode (구식, 데모용. AudioWorklet 권장)
        // 참고: ScriptProcessorNode는 메인 스레드에서 실행되어 성능 문제를 일으킬 수 있음
        this.audioProcessorNode = this.audioContext.createScriptProcessor(this.bufferSize, 1, 1);

        this.audioProcessorNode.onaudioprocess = async (audioProcessingEvent) => {
            const inputBuffer = audioProcessingEvent.inputBuffer;
            const inputData = inputBuffer.getChannelData(0); // Mono 오디오 데이터

            // --- 리샘플링 필요 ---
            // AudioContext의 sampleRate를 VAD 모델의 sampleRate(예: 16000Hz)로 변환해야 함
            // 간단한 구현 또는 라이브러리(예: resampler.js) 사용
            const resampledData = this._resample(inputData, this.audioContext.sampleRate, this.sampleRate);

            // --- ONNX 모델 추론 ---
            // 모델 입력 텐서 생성 (모델의 정확한 입력 형식 확인 필요)
            // 예시: const tensorInput = new ort.Tensor('float32', resampledData, [1, resampledData.length]);
            // 내부 상태 (_h, _c) 관리 필요
            // const feeds = { input: tensorInput, /* h: ..., c: ... */ };
            // const results = await this.ortSession.run(feeds);
            // const voiceProbability = results.output.data[0]; // 모델 출력값 (음성 확률)

            // --- VAD 상태 결정 (개선 필요) ---
            // 아래는 매우 단순화된 예시, 실제로는 지연 및 안정성 고려 필요
            // const currentSpeech = voiceProbability > this.threshold;
            const currentSpeech = Math.random() > 0.5; // 임시 더미 로직

            if (currentSpeech && !this.isSpeaking) {
                this.isSpeaking = true;
                if (this.onVoiceStart) this.onVoiceStart();
                console.log("VAD: Voice Started");
            } else if (!currentSpeech && this.isSpeaking) {
                this.isSpeaking = false;
                if (this.onVoiceEnd) this.onVoiceEnd();
                console.log("VAD: Voice Ended");
            }

            // 다음 VAD 추론을 위한 상태 업데이트 (모델별 상이)
            // this._h = results.hn;
            // this._c = results.cn;
        };

        // 마이크 소스 -> VAD 프로세서 연결
        micSourceNode.connect(this.audioProcessorNode);
        // 프로세서는 오디오 목적지로 연결해야 소리가 나옴 (여기서는 VAD 처리만 하므로 연결 불필요)
        this.audioProcessorNode.connect(this.audioContext.destination); // 임시 연결 (디버깅용)
        // 실제로는 VAD 프로세서 결과를 WebRTC로 보내야 함

        console.log("VAD processing started.");
    },

    stopProcessing() {
        if (this.audioProcessorNode) {
            this.audioProcessorNode.disconnect();
            this.audioProcessorNode.onaudioprocess = null; // 이벤트 리스너 제거
            this.audioProcessorNode = null;
            this.isSpeaking = false; // 상태 초기화
            // VAD 모델 내부 상태 초기화
        }
        console.log("VAD processing stopped.");
    },

    // 간단한 리샘플링 함수 (정확성 보장 안됨, 라이브러리 사용 권장)
    _resample(audioData, originalRate, targetRate) {
         if (originalRate === targetRate) return audioData;
         const ratio = originalRate / targetRate;
         const newLength = Math.round(audioData.length / ratio);
         const result = new Float32Array(newLength);
         let offsetResult = 0;
         let offsetBuffer = 0;
         while (offsetResult < result.length) {
             const nextOffsetBuffer = Math.round((offsetResult + 1) * ratio);
             let accum = 0, count = 0;
             for (let i = offsetBuffer; i < nextOffsetBuffer && i < audioData.length; i++) {
                 accum += audioData[i];
                 count++;
             }
             result[offsetResult] = accum / count;
             offsetResult++;
             offsetBuffer = nextOffsetBuffer;
         }
         return result;
    }
};