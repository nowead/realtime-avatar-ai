// frontend/src/js/main.js

import '../css/style.css';
import { initWebSocketConnection, closeWebSocket, sendAudioChunk, sendJsonMessage } from './websocket.js';
import { AudioService, mergeChunks } from './audio.js';
import { SileroVAD } from './sileroVadRunner.js';
import { AvatarService } from './avatar.js';

const startBtn = document.getElementById('startBtn');
const stopBtn = document.getElementById('stopBtn');
const statusEl = document.getElementById('status');
const canvasElement = document.getElementById('renderCanvas');

let vad;
let languageCode = "ko-KR"; // 기본 언어 설정

// 오디오 버퍼링 및 VAD 상태 관련 변수
const MIN_AUDIO_CHUNK_DURATION_MS = 200; // 주기적 오디오 전송 간격 (ms)
let lastPeriodicSendTime = 0;
let accumulatedAudioForPeriodicSend = [];

let vadCurrentlyDetectingSpeech = false; // VAD가 현재 음성을 감지하고 있는지 여부
let vadSegmentAudioChunks = [];       // 현재 VAD 음성 세그먼트에 속한 오디오 청크들
let silentFramesAfterSpeech = 0;      // VAD 음성 감지 후 연속된 침묵 프레임 수
const MAX_SILENT_FRAMES_AFTER_SPEECH = 15; // 이 값 이후 VAD는 발화 종료로 판단 (약 0.5초 침묵)
const MIN_SPEECH_FRAMES_FOR_SEGMENT = 3; // 유효한 발화로 간주하기 위한 최소 음성 프레임 수

let isUserSessionActive = false;      // 사용자가 시작/종료 버튼으로 제어하는 전체 세션 상태
let isSttStreamActiveOnServer = false; // 서버의 STT 스트림이 활성 상태인지 여부 (서버 응답으로 업데이트)
let webSocketReady = false;           // 웹소켓 연결이 성공적으로 열렸는지 여부

// --- WebSocket으로부터 호출될 콜백 함수들 ---
window.handleSttStreamStarted = () => {
    console.log("[Main] 서버로부터 STT 스트림 시작 확인 응답 받음.");
    isSttStreamActiveOnServer = true;
    if (isUserSessionActive) { // 사용자 세션이 아직 활성 상태일 때만 UI 업데이트
        statusEl.textContent = '🟢 STT 활성. 말해주세요.';
    }
};

window.handleSttStreamEnded = (isClosedByClient = false) => {
    console.log(`[Main] 서버로부터 STT 스트림 종료 알림 받음 (클라이언트 요청 종료: ${isClosedByClient}).`);
    isSttStreamActiveOnServer = false;
    // isClosedByClient가 true이면 closeWebSocket에서 이미 UI를 '종료됨'으로 설정했을 수 있음
    if (isUserSessionActive && !isClosedByClient) {
        statusEl.textContent = '🔈 STT 종료. 응답 처리 중... (다음 발화 가능)';
    } else if (!isUserSessionActive && !isClosedByClient) {
        // stopBtn에 의해 사용자 세션이 이미 비활성화된 경우, 서버가 자체적으로 스트림을 닫았을 때
        statusEl.textContent = '⏹️ 세션 종료됨 (서버 STT 자동 종료).';
    }
    // VAD 관련 상태도 초기화하여 다음 발화 준비
    vadCurrentlyDetectingSpeech = false;
    vadSegmentAudioChunks = [];
    silentFramesAfterSpeech = 0;
};
// --- End WebSocket 콜백 ---

AvatarService.initialize(canvasElement);

startBtn.addEventListener('click', async () => {
    if (isUserSessionActive) {
        console.warn("[Main] 이미 세션이 활성화되어 있습니다.");
        return;
    }
    isUserSessionActive = true;
    startBtn.disabled = true;
    stopBtn.disabled = false;
    statusEl.textContent = '🔄 초기화 및 연결 중...';

    // 1. 새 WebSocket 연결 시도
    webSocketReady = await initWebSocketConnection('ws://localhost:8000/ws', languageCode);
    if (!webSocketReady) {
        statusEl.textContent = '오류: WebSocket 연결 실패';
        isUserSessionActive = false;
        startBtn.disabled = false;
        stopBtn.disabled = true;
        return;
    }
    // initWebSocketConnection의 onopen에서 첫 start_stream을 보내고,
    // 서버로부터 'stt_stream_started' 응답을 받으면 window.handleSttStreamStarted가 호출되어
    // isSttStreamActiveOnServer = true가 되고 statusEl도 업데이트됨.
    // 만약 onopen에서 start_stream을 보내지 않는다면, VAD 감지 시 보내야 함.
    // 현재 websocket.js는 onopen에서 start_stream을 보냄.

    // 2. VAD 모델 로드 (최초 한 번)
    if (!vad) {
        vad = new SileroVAD('/models/silero_vad.onnx'); // 모델 경로 확인
        try {
            await vad.loadModel();
            console.log('[Main] SileroVAD 모델 로드 완료.');
        } catch (error) {
            console.error('[Main] SileroVAD 모델 로드 실패:', error);
            statusEl.textContent = '오류: VAD 모델 로드 실패';
            await closeWebSocket(true); // UI 업데이트 포함하여 웹소켓 종료
            isUserSessionActive = false;
            webSocketReady = false;
            startBtn.disabled = false;
            stopBtn.disabled = true;
            return;
        }
    }

    // 3. 상태 변수 초기화 (VAD 및 오디오 버퍼 관련)
    accumulatedAudioForPeriodicSend = [];
    lastPeriodicSendTime = performance.now();
    vadCurrentlyDetectingSpeech = false;
    vadSegmentAudioChunks = [];
    silentFramesAfterSpeech = 0;

    // 4. AudioService 초기화 및 VAD 프레임 처리 콜백 설정
    AudioService.onVADFrame = async (currentPcmChunk) => {
        if (!isUserSessionActive || !vad || !webSocketReady) return; // 사용자 세션, VAD, 웹소켓 모두 준비되어야 함

        const isSpeechInCurrentChunk = await vad.detect(currentPcmChunk);

        if (isUserSessionActive) { // 사용자 세션 활성 중에만 UI 업데이트
            if (isSttStreamActiveOnServer) {
                statusEl.textContent = isSpeechInCurrentChunk ? '🟢 말하는 중...' : '🔈 듣는 중... (STT 활성)';
            } else {
                // STT 스트림이 아직 시작되지 않았거나, 이전 발화 후 종료된 상태
                statusEl.textContent = isSpeechInCurrentChunk ? '🎤 발화 감지! (STT 시작 대기)' : '⏳ 다음 발화 대기 중...';
            }
        }

        if (isSpeechInCurrentChunk) {
            if (!vadCurrentlyDetectingSpeech) { // VAD 기준: 음성 시작
                console.log('[VAD] 음성 시작 감지.');
                vadCurrentlyDetectingSpeech = true;
                vadSegmentAudioChunks = []; // 새 음성 세그먼트 시작
                silentFramesAfterSpeech = 0;

                // ★ 서버의 STT 스트림이 활성화되어 있지 않다면 (예: 초기 연결 직후 또는 이전 발화 처리 완료 후)
                // 새 발화 시작 시 STT 스트림 시작 요청
                if (!isSttStreamActiveOnServer) {
                    console.log(`[Main] 새 발화 시작, "start_stream" 메시지 전송 (언어: ${languageCode}).`);
                    sendJsonMessage({ type: "start_stream", language: languageCode });
                    // isSttStreamActiveOnServer는 서버 응답('stt_stream_started')을 통해 업데이트됨
                    statusEl.textContent = '🔄 STT 스트림 요청 중...';
                }
            }
            // VAD가 음성으로 판단한 청크는 항상 현재 세그먼트와 주기적 전송 버퍼에 누적
            vadSegmentAudioChunks.push(currentPcmChunk);
            accumulatedAudioForPeriodicSend.push(currentPcmChunk);
        } else { // 현재 청크가 침묵
            if (vadCurrentlyDetectingSpeech) { // VAD 기준: 음성 구간 진행 중 첫 침묵 발생
                vadSegmentAudioChunks.push(currentPcmChunk); // 침묵도 일단 현재 VAD 세그먼트에 포함
                accumulatedAudioForPeriodicSend.push(currentPcmChunk);
                silentFramesAfterSpeech++;

                if (silentFramesAfterSpeech >= MAX_SILENT_FRAMES_AFTER_SPEECH) {
                    console.log('[VAD] 음성 후 최대 침묵 프레임 도달. VAD 판단: 발화 종료.');
                    vadCurrentlyDetectingSpeech = false; // VAD 음성 감지 상태 해제
                    
                    // STT 스트림이 서버에서 활성화된 상태일 때만 "utterance_ended" 전송
                    if (isSttStreamActiveOnServer) {
                        // 남아있는 오디오 데이터가 있다면 마저 전송
                        if (accumulatedAudioForPeriodicSend.length > 0) {
                            const mergedAudio = mergeChunks(accumulatedAudioForPeriodicSend);
                            if (mergedAudio.length > 0) {
                                console.log(`[Main] 📤 VAD 발화 종료 감지, 잔여 오디오 전송: ${accumulatedAudioForPeriodicSend.length} 청크`);
                                sendAudioChunk(mergedAudio);
                            }
                            accumulatedAudioForPeriodicSend = [];
                            lastPeriodicSendTime = performance.now(); // 전송 시간 업데이트
                        }
                        
                        console.log('[Main] "utterance_ended" 메시지 전송 (VAD 발화 종료 감지).');
                        sendJsonMessage({ type: "utterance_ended" });
                        // isSttStreamActiveOnServer는 서버 응답('stt_stream_ended_by_server')을 통해 false로 변경됨
                        // statusEl.textContent = '➡️ STT 처리 요청됨...'; // 서버 응답 후 변경될 것임
                    } else {
                        console.log('[Main] VAD 발화 종료 감지, 그러나 서버 STT 스트림이 활성 상태가 아님. "utterance_ended" 보내지 않음.');
                    }
                    // vadSegmentAudioChunks는 여기서 초기화하지 않고, 다음 발화 시작 시 초기화
                }
            } else { // 계속 침묵
                // STT 스트림이 활성화되어 있다면, 주기적 전송을 위해 침묵도 누적할 수 있음 (선택적)
                // 여기서는 VAD가 음성을 감지하지 않는 동안에는 주기적 전송 버퍼에만 추가
                 accumulatedAudioForPeriodicSend.push(currentPcmChunk);
            }
        }

        // 주기적 전송 로직: STT 스트림이 서버에서 활성화된 상태일 때만 오디오 전송
        const now = performance.now();
        if (isSttStreamActiveOnServer && accumulatedAudioForPeriodicSend.length > 0 &&
            (now - lastPeriodicSendTime > MIN_AUDIO_CHUNK_DURATION_MS || accumulatedAudioForPeriodicSend.length >= 5 )) { // 또는 청크 5개 모이면
            const mergedToSend = mergeChunks(accumulatedAudioForPeriodicSend);
            if (mergedToSend.length > 0) {
                // console.log(`[Main] 📤 주기적 전송: ${accumulatedAudioForPeriodicSend.length} 청크 (${mergedToSend.length * 2} bytes)`);
                sendAudioChunk(mergedToSend);
            }
            accumulatedAudioForPeriodicSend = [];
            lastPeriodicSendTime = now;
        }
    };

    try {
        await AudioService.initialize();
        console.log('[Main] AudioService 초기화 완료.');
        // 초기 statusEl.textContent는 websocket.js의 onopen에서 STT 스트림 시작 요청 후,
        // 서버로부터 'stt_stream_started' 응답을 받으면 window.handleSttStreamStarted에 의해 설정됨.
        // 만약 websocket.js onopen에서 start_stream을 보내지 않는다면, 여기서 VAD 감지 후 보내야 함.
    } catch (error) {
        console.error('[Main] AudioService 초기화 실패:', error);
        statusEl.textContent = '오류: 오디오 서비스 초기화 실패';
        await closeWebSocket(true); // UI 업데이트 포함
        isUserSessionActive = false;
        webSocketReady = false;
        startBtn.disabled = false;
        stopBtn.disabled = true;
    }
});

stopBtn.addEventListener('click', async () => {
    if (!isUserSessionActive) {
        console.warn("[Main] 세션이 활성화되어 있지 않아 종료할 수 없습니다.");
        return;
    }

    console.log('[Main] "종료 버튼" 클릭됨.');
    isUserSessionActive = false; // 사용자 세션 비활성화 (가장 먼저)
    startBtn.disabled = false;
    stopBtn.disabled = true;
    statusEl.textContent = '⏹️ 종료 요청 중...';

    AudioService.stop(); // 마이크 입력 즉시 중단

    // 남아있는 오디오 데이터가 있다면 마저 전송 (STT 스트림이 활성 상태였다면)
    if (isSttStreamActiveOnServer && accumulatedAudioForPeriodicSend.length > 0) {
        const mergedAudio = mergeChunks(accumulatedAudioForPeriodicSend);
        if (mergedAudio.length > 0) {
            console.log(`[Main] 📤 "종료 버튼" 클릭, 잔여 오디오 전송: ${accumulatedAudioForPeriodicSend.length} 청크`);
            sendAudioChunk(mergedAudio);
        }
    }
    accumulatedAudioForPeriodicSend = []; // 버퍼 비우기

    // "종료" 버튼 클릭 시, 서버에 현재 STT 스트림을 종료하도록 명시적으로 알림
    // "utterance_ended"는 발화의 끝을 의미하고, "stop_stream"은 클라이언트의 명시적 종료 요청을 의미할 수 있음.
    // 서버 구현에 따라 적절한 메시지 선택. 여기서는 "stop_stream"을 사용.
    if (isSttStreamActiveOnServer) {
        sendJsonMessage({ type: "stop_stream" }); // 또는 "utterance_ended"
        console.log('[Main] "stop_stream" 메시지 전송됨 ("종료 버튼" 클릭).');
        // isSttStreamActiveOnServer는 서버 응답 또는 closeWebSocket에서 false로 처리됨
    } else {
        console.log('[Main] "종료 버튼" 클릭, 그러나 서버 STT 스트림이 활성 상태가 아님. "stop_stream" 보내지 않음.');
    }
    
    // 웹소켓 연결 종료 (모든 응답을 기다리지 않고 즉시 종료)
    await closeWebSocket(true); // UI 업데이트 포함하여 웹소켓 종료
    webSocketReady = false; // 웹소켓 상태 업데이트

    // VAD 관련 상태 초기화
    vadCurrentlyDetectingSpeech = false;
    vadSegmentAudioChunks = [];
    silentFramesAfterSpeech = 0;
    // isSttStreamActiveOnServer는 closeWebSocket 내부에서 window.handleSttStreamEnded를 통해 false로 설정될 수 있음
    // 또는 여기서 명시적으로 false로 설정
    isSttStreamActiveOnServer = false;

    console.log('[Main] 사용자 세션 및 웹소켓 연결 종료됨.');
    statusEl.textContent = '⏹️ 종료됨'; // 최종 상태
});

// 페이지 로드 시 초기 버튼 상태
stopBtn.disabled = true;
startBtn.disabled = false;
statusEl.textContent = '⏳ 대기 중... (시작 버튼을 누르세요)';
