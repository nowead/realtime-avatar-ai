// frontend/src/js/websocket.js
import { AvatarService } from './avatar.js';

let socket = null;
let audioContext = null;
let micNode = null;
let playerNode = null;
let currentSessionId = null;
let languageCodeForStream = "ko-KR";
let isAudioContextResumed = false;

export async function initWebSocketConnection(url, language = "ko-KR") {
    if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) {
        console.warn('[WebSocket] 이전 연결 종료 중...');
        await closeWebSocket(false);
    }

    languageCodeForStream = language;

    const ready = await initializeAudioContext();
    if (!ready) {
        console.error("[WebSocket] AudioContext 초기화 실패");
        return false;
    }

    await setupAudioWorklet();

    return new Promise((resolve) => {
        const localSocket = new WebSocket(url);
        localSocket.binaryType = "arraybuffer";

        localSocket.onopen = () => {
            console.log("[WebSocket] 연결 완료:", url);
            socket = localSocket;
            sendJsonMessage({ type: "start_stream", language: languageCodeForStream });
            resolve(true);
        };

        localSocket.onmessage = (event) => {
            if (!socket || socket !== localSocket) return;

            if (event.data instanceof ArrayBuffer) {
                const int16Array = new Int16Array(event.data);
                const float32Array = new Float32Array(int16Array.length);
                for (let i = 0; i < int16Array.length; i++) {
                    float32Array[i] = int16Array[i] / 32768.0;
                }
                if (playerNode) {
                    playerNode.port.postMessage(float32Array);
                }
            } else if (typeof event.data === "string") {
                try {
                    const msg = JSON.parse(event.data);
                    if (msg.type === "viseme") {
                        AvatarService.applyViseme(msg.visemeId);
                    } else if (msg.type === "session_info") {
                        currentSessionId = msg.sessionId;
                        console.log("[WebSocket] 세션 ID:", currentSessionId);
                    } else if (msg.type === "stt_stream_started" && typeof window.handleSttStreamStarted === 'function') {
                        window.handleSttStreamStarted();
                    } else if (msg.type === "stt_stream_ended_by_server" && typeof window.handleSttStreamEnded === 'function') {
                        window.handleSttStreamEnded();
                    } else if (msg.type === "stream_stopping_acknowledged") {
                        const statusEl = document.getElementById('status');
                        if (statusEl) statusEl.textContent = '⏹️ 스트림 중지됨 (서버 확인)';
                    } else if (msg.type === "error") {
                        console.error("[WebSocket] 서버 오류:", msg.message);
                        const statusEl = document.getElementById('status');
                        if (statusEl) statusEl.textContent = `오류: ${msg.message}`;
                    }
                } catch (e) {
                    console.error("[WebSocket] JSON 파싱 실패:", e);
                }
            }
        };

        localSocket.onclose = (event) => {
            console.log(`[WebSocket] 연결 종료. 코드=${event.code}, 이유="${svToString(event.reason)}"`);
            if (socket === localSocket) {
                socket = null;
                currentSessionId = null;
            }
            resolve(false);
        };

        localSocket.onerror = (err) => {
            console.error("[WebSocket] 오류 발생:", err);
            if (socket === localSocket) {
                socket = null;
                currentSessionId = null;
            }
            resolve(false);
        };
    });
}

async function initializeAudioContext() {
    if (!audioContext || audioContext.state === 'closed') {
        audioContext = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 16000 });
        isAudioContextResumed = false;
    }

    if (audioContext.state === 'suspended') {
        try {
            await audioContext.resume();
            isAudioContextResumed = true;
        } catch (e) {
            console.warn("[AudioContext] resume 실패. 사용자 클릭 대기 중...");
            const onClickResume = async () => {
                if (audioContext && audioContext.state === 'suspended') {
                    await audioContext.resume();
                    isAudioContextResumed = true;
                    console.log("[AudioContext] 사용자 클릭으로 resume");
                }
                document.body.removeEventListener("click", onClickResume);
            };
            document.body.addEventListener("click", onClickResume, { once: true });
            alert("오디오 재생을 위해 화면을 클릭해주세요.");
            return false;
        }
    } else {
        isAudioContextResumed = true;
    }

    return isAudioContextResumed;
}

async function setupAudioWorklet() {
    await audioContext.audioWorklet.addModule('/worklets/audio-worklet-processor.js');

    // 🎙️ MicProcessor
    micNode = new AudioWorkletNode(audioContext, 'mic-processor');
    micNode.port.onmessage = (event) => {
        const int16 = event.data;
        if (socket && socket.readyState === WebSocket.OPEN) {
            socket.send(int16.buffer);
        }
    };

    const stream = await navigator.mediaDevices.getUserMedia({
        audio: { sampleRate: 16000, channelCount: 1 }
    });
    const micSource = audioContext.createMediaStreamSource(stream);
    micSource.connect(micNode);

    // 🔊 PCMPlayerProcessor
    playerNode = new AudioWorkletNode(audioContext, 'pcm-player');
    playerNode.connect(audioContext.destination);

    console.log("[AudioWorklet] mic-processor 및 pcm-player 초기화 완료");
}

export async function closeWebSocket(updateUI = true) {
    console.log("[WebSocket] 연결 종료 중...");
    if (socket) {
        socket.close(1000, "Client requested disconnect");
        socket = null;
    }
    currentSessionId = null;

    if (updateUI) {
        const statusEl = document.getElementById('status');
        if (statusEl) statusEl.textContent = '⏹️ 종료됨';
        const startBtn = document.getElementById('startBtn');
        const stopBtn = document.getElementById('stopBtn');
        if (startBtn) startBtn.disabled = false;
        if (stopBtn) stopBtn.disabled = true;
        if (typeof window.handleSttStreamEnded === 'function') {
            window.handleSttStreamEnded(true);
        }
    }
}

export function sendJsonMessage(jsonObject) {
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify(jsonObject));
    } else {
        console.warn("[WebSocket] JSON 메시지 전송 실패. 소켓 상태:", socket?.readyState ?? 'null');
    }
}

export function sendAudioChunk(int16Array) {
    if (socket && socket.readyState === WebSocket.OPEN && int16Array?.buffer) {
        socket.send(int16Array.buffer);
    }
}

function svToString(data) {
    if (typeof data === 'string') return data;
    if (data instanceof ArrayBuffer) {
        try {
            return new TextDecoder().decode(data);
        } catch {
            return `[Binary data, length: ${data.byteLength}]`;
        }
    }
    return String(data);
}
