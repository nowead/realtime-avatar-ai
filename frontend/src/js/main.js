// main.js

import '../css/style.css';
import { initWebSocketConnection, closeWebSocket } from './websocket.js';
import { AudioService, mergeChunks } from './audio.js';
import { SileroVAD } from './sileroVadRunner.js';
import { AvatarService } from './avatar.js'; // ✅ 아바타 초기화

const startBtn = document.getElementById('startBtn');
const stopBtn = document.getElementById('stopBtn');
const statusEl = document.getElementById('status');
const canvasElement = document.getElementById('renderCanvas');

let vad;
let activeBuffer = [];
let silenceFrames = 0;
let isSpeaking = false;

// ✅ 페이지 로드 시 즉시 아바타 초기화
AvatarService.initialize(canvasElement);

startBtn.addEventListener('click', async () => {
  startBtn.disabled = true;
  stopBtn.disabled = false;
  statusEl.textContent = '🔄 연결 중...';

  initWebSocketConnection('wss://your.websocket.gateway/ws');

  vad = new SileroVAD('/models/silero_vad.onnx');
  await vad.loadModel();

  AudioService.onVADFrame = async (int16Chunk) => {
    const isSpeech = await vad.detect(int16Chunk);
    statusEl.textContent = isSpeech ? '🟢 말하는 중...' : '🔈 듣는 중...';

    if (isSpeech) {
      if (!isSpeaking) {
        isSpeaking = true;
        silenceFrames = 0;
        activeBuffer = [];
      }
      activeBuffer.push(int16Chunk);
    } else if (isSpeaking) {
      silenceFrames++;
      if (silenceFrames < 6) {
        activeBuffer.push(int16Chunk);
      } else {
        isSpeaking = false;
        if (activeBuffer.length >= 3) {
          const merged = mergeChunks(activeBuffer);
          console.log(`📤 Sending ${activeBuffer.length} chunks`);
          sendAudioChunk(merged); // 직접 구현 필요
        }
        activeBuffer = [];
        silenceFrames = 0;
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
