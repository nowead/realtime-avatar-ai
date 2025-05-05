// js/audio.js
const AudioService = {
    audioContext: null,
    micStream: null,
    micSource: null,
    // 필요한 오디오 노드들 (VAD 처리용, WebRTC 전송용 등)

    async initialize() {
        if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
            throw new Error('getUserMedia is not supported in this browser.');
        }
        this.audioContext = new (window.AudioContext || window.webkitAudioContext)();
        console.log(`AudioContext sample rate: ${this.audioContext.sampleRate}`);
        // Silero VAD는 특정 샘플 레이트(예: 16000Hz)를 요구할 수 있음 -> 리샘플링 필요
    },

    async startMicrophone() {
        if (!this.audioContext) await this.initialize();
        this.micStream = await navigator.mediaDevices.getUserMedia({ audio: true, video: false });
        this.micSource = this.audioContext.createMediaStreamSource(this.micStream);
        console.log("Microphone access granted.");
        // VAD 및 WebRTC 모듈로 스트림 또는 오디오 데이터 전달 설정
        return this.micStream;
    },

    stopMicrophone() {
        if (this.micStream) {
            this.micStream.getTracks().forEach(track => track.stop());
            this.micStream = null;
        }
        if (this.micSource) {
            this.micSource.disconnect();
            this.micSource = null;
        }
        // 오디오 컨텍스트 닫기 (필요시)
        // if (this.audioContext && this.audioContext.state !== 'closed') {
        //     this.audioContext.close();
        //     this.audioContext = null;
        // }
        console.log("Microphone stopped.");
    },

    getAudioContext() {
        return this.audioContext;
    },

    getMicStream() {
        return this.micStream;
    }
};