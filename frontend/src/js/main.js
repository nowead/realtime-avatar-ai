// js/main.js
document.addEventListener('DOMContentLoaded', () => {
    const canvas = document.getElementById('renderCanvas');
    const startButton = document.getElementById('startButton');
    const stopButton = document.getElementById('stopButton');
    const statusDiv = document.getElementById('status');

    let isSessionActive = false;

    // 1. 아바타 서비스 초기화
    AvatarService.initialize(canvas);
    setStatus('Initialized. Ready to start.');

    // 시작 버튼 클릭
    startButton.addEventListener('click', async () => {
        if (isSessionActive) return;
        isSessionActive = true;
        startButton.disabled = true;
        stopButton.disabled = false;
        setStatus('Starting session...');

        try {
            // 2. 오디오 서비스 초기화 및 마이크 시작
            await AudioService.initialize();
            const micStream = await AudioService.startMicrophone();
            const audioContext = AudioService.getAudioContext();
            const micSourceNode = AudioService.micSource; // AudioNode 가져오기

            // 3. WebRTC 서비스 초기화 및 연결
            WebRTCService.initialize(
                (remoteStream) => { // 원격 오디오 트랙 수신 시
                    setStatus('Receiving remote audio...');
                    // 오디오 재생은 WebRTCService 내부에서 처리
                },
                (data) => { // 데이터 채널 메시지 수신 시 (립싱크)
                    // console.log("Received data:", data);
                    if (data.type === 'lipsync') {
                        AvatarService.updateLipSync(data);
                    }
                     // 다른 타입의 데이터 메시지 처리
                }
            );
            await WebRTCService.connect(micStream); // 시그널링 및 WebRTC 연결 시작

             // 4. VAD 서비스 초기화 및 시작 (WebRTC 연결 후 또는 병렬 처리)
             // VAD 콜백 정의
             const handleVoiceStart = () => {
                 setStatus('Speaking detected...');
                 // WebRTCService.setAudioEnabled(true); // 오디오 트랙 활성화 또는 VAD 상태 전송
             };
             const handleVoiceEnd = () => {
                 setStatus('Silence detected...');
                  // WebRTCService.setAudioEnabled(false); // 오디오 트랙 비활성화 또는 VAD 상태 전송
             };
             // VAD 초기화 및 마이크 노드 연결
            await VadService.initialize(audioContext, handleVoiceStart, handleVoiceEnd);
            VadService.startProcessing(micSourceNode);

            setStatus('Session started. Listening...');
             // 초기 Offer 생성 (만약 클라이언트가 Offer를 보내는 역할이라면)
            WebRTCService.createOffer();


        } catch (error) {
            console.error("Failed to start session:", error);
            setStatus(`Error: ${error.message}`);
            await stopSession(); // 오류 발생 시 세션 정리
        }
    });

    // 중지 버튼 클릭
    stopButton.addEventListener('click', async () => {
        await stopSession();
    });

    // 세션 중지 함수
    async function stopSession() {
        if (!isSessionActive) return;
        setStatus('Stopping session...');
        VadService.stopProcessing();
        WebRTCService.closeConnection();
        AudioService.stopMicrophone();

        isSessionActive = false;
        startButton.disabled = false;
        stopButton.disabled = true;
        setStatus('Session stopped. Idle.');
        // 아바타 입 모양 초기화 (선택 사항)
        AvatarService.updateLipSync({ type: 'lipsync', blendshapes: {} });
    }

    // 상태 업데이트 함수
    function setStatus(message) {
        console.log(`Status: ${message}`);
        statusDiv.textContent = `Status: ${message}`;
    }

});