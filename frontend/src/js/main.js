// frontend/src/js/main.js

import '../css/style.css';
import { initWebSocketConnection, closeWebSocket, sendAudioChunk } from './websocket.js';
import { AudioService, mergeChunks } from './audio.js';
import { SileroVAD } from './sileroVadRunner.js';
import { AvatarService } from './avatar.js';

const startBtn = document.getElementById('startBtn');
const stopBtn = document.getElementById('stopBtn');
const statusEl = document.getElementById('status');
const canvasElement = document.getElementById('renderCanvas');

let vad;

// --- 오디오 버퍼링 및 VAD 상태 관리를 위한 변수 ---
let activeSegmentChunks = [];        // 현재 음성 세그먼트를 구성하는 모든 청크 (패딩 포함)
let preSpeechPaddingChunks = [];     // VAD가 음성을 감지하기 전의 N개 청크를 저장하는 버퍼
const PAD_BEFORE_SPEECH_CHUNKS = 3;  // 음성 시작 전에 추가할 비음성 청크 수 (예: 3 chunks * 32ms/chunk = ~96ms)
const MAX_SILENCE_AFTER_SPEECH_CHUNKS = 6; // 음성 감지 후, 세그먼트를 종료하기까지 허용할 최대 연속 비음성 청크 수 (예: 6 * 32ms = ~192ms)
const MIN_TOTAL_CHUNKS_FOR_SEGMENT = 5;  // 전송할 최소 전체 청크 수 (패딩 포함, 너무 짧은 음성은 무시)

let vadCurrentlyDetectingSpeech = false; // VAD가 현재 음성으로 판단 중인지 여부 (세그먼트 진행 중)
let silentChunksCountSinceLastSpeech = 0; // 마지막 음성 감지 후 연속된 비음성 청크 수
// -------------------------------------------------

// (playInt16Audio 함수는 이전 답변과 동일하게 존재한다고 가정합니다)
function playInt16Audio(int16Array, audioContext) {
  // ... (이전 답변의 playInt16Audio 함수 내용) ...
  if (!audioContext || audioContext.state === 'closed') {
    console.warn('[Playback] AudioContext가 없거나 닫혀있어 오디오를 재생할 수 없습니다.');
    return;
  }
  const float32Array = new Float32Array(int16Array.length);
  for (let i = 0; i < int16Array.length; i++) {
    float32Array[i] = int16Array[i] / 32768.0;
  }
  const audioBuffer = audioContext.createBuffer(1, float32Array.length, audioContext.sampleRate);
  audioBuffer.copyToChannel(float32Array, 0);
  const sourceNode = audioContext.createBufferSource();
  sourceNode.buffer = audioBuffer;
  sourceNode.connect(audioContext.destination);
  sourceNode.start();
  console.log(`[Playback] 병합된 오디오 청크 재생 시작 (길이: ${int16Array.length} 샘플)`);
}


AvatarService.initialize(canvasElement);

startBtn.addEventListener('click', async () => {
  startBtn.disabled = true;
  stopBtn.disabled = false;
  statusEl.textContent = '🔄 연결 중...';

  initWebSocketConnection('ws://localhost:8000/ws'); // 실제 URL로 변경

  vad = new SileroVAD('/models/silero_vad.onnx');
  await vad.loadModel();

  // 상태 변수 초기화
  activeSegmentChunks = [];
  preSpeechPaddingChunks = [];
  vadCurrentlyDetectingSpeech = false;
  silentChunksCountSinceLastSpeech = 0;

  AudioService.onVADFrame = async (currentChunk) => {
    const isSpeechInCurrentChunk = await vad.detect(currentChunk); // 현재 청크에 대한 VAD 결과
    statusEl.textContent = isSpeechInCurrentChunk ? '🟢 말하는 중...' : '🔈 듣는 중...';

    if (isSpeechInCurrentChunk) {
      if (!vadCurrentlyDetectingSpeech) {
        // --- 상태 전환: 침묵 -> 음성 (음성 세그먼트 시작) ---
        vadCurrentlyDetectingSpeech = true; // 음성 활동 시작으로 표시
        // preSpeechPaddingChunks에 저장된 (음성 시작 직전의) 청크들과 현재 음성 청크로 새 세그먼트 시작
        activeSegmentChunks = [...preSpeechPaddingChunks, currentChunk];
        // preSpeechPaddingChunks는 이 세그먼트의 시작에 사용되었으므로, 다음 감지를 위해 자연스럽게 다시 채워지거나,
        // 명시적으로 비워도 됩니다 (현재 로직에서는 다음 침묵 상태에서 다시 채워짐).
      } else {
        // --- 상태 유지: 음성 계속 ---
        activeSegmentChunks.push(currentChunk); // 현재 음성 청크를 세그먼트에 추가
      }
      silentChunksCountSinceLastSpeech = 0; // 음성이 감지되었으므로 침묵 카운터 리셋
    } else { // 현재 청크가 음성이 아닌 경우
      if (vadCurrentlyDetectingSpeech) {
        // --- 상태 전환 시도: 음성 -> 침묵 (세그먼트가 아직 활성 상태일 수 있음) ---
        activeSegmentChunks.push(currentChunk); // 이 비음성 청크도 일단 세그먼트에 추가 (후행 패딩의 일부)
        silentChunksCountSinceLastSpeech++;

        if (silentChunksCountSinceLastSpeech >= MAX_SILENCE_AFTER_SPEECH_CHUNKS) {
          // --- 음성 세그먼트 종료: 충분한 시간 동안 침묵이 지속됨 ---
          vadCurrentlyDetectingSpeech = false; // 음성 활동 종료로 표시

          if (activeSegmentChunks.length >= MIN_TOTAL_CHUNKS_FOR_SEGMENT) {
            const merged = mergeChunks(activeSegmentChunks);
            console.log(`[Main] 📤 Segment Ended. Sending ${activeSegmentChunks.length} chunks (total samples: ${merged.length}).`);
            
            // 병합된 오디오 재생 (테스트용)
            if (AudioService.audioContext) {
              playInt16Audio(merged, AudioService.audioContext);
            }
            sendAudioChunk(merged); // 서버로 전송
          } else {
            console.log(`[Main] Segment too short after padding, discarding. Chunks: ${activeSegmentChunks.length}`);
          }
          activeSegmentChunks = []; // 다음 세그먼트를 위해 버퍼 비우기
          // silentChunksCountSinceLastSpeech는 다음 음성 시작 시 0으로 리셋됨.
          // preSpeechPaddingChunks는 아래의 '지속적인 침묵' 로직에서 관리됨.
        }
      } else {
        // --- 상태 유지: 지속적인 침묵 (활성 음성 세그먼트 없음) ---
        preSpeechPaddingChunks.push(currentChunk); // 최근 비음성 청크를 pre-padding 버퍼에 저장
        if (preSpeechPaddingChunks.length > PAD_BEFORE_SPEECH_CHUNKS) {
          preSpeechPaddingChunks.shift(); // 버퍼 크기 유지 (가장 오래된 것 제거)
        }
      }
    }
  };

  await AudioService.initialize();
  statusEl.textContent = '🟢 활성화됨';
});

stopBtn.addEventListener('click', () => {
  startBtn.disabled = false;
  stopBtn.disabled = true;
  statusEl.textContent = '⏹️ 종료됨';

  AudioService.stop();
  closeWebSocket();
});