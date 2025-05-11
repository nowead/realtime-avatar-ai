import { AvatarService } from './avatar.js';
let socket;
let audioContext;
let mediaRecorder;

export function initWebSocketConnection(url) {
  socket = new WebSocket(url);

  socket.binaryType = "arraybuffer";

  socket.onopen = () => {
    console.log("WebSocket connection opened.");
  };

  socket.onmessage = async (event) => {
    if (typeof event.data === "string") {
      const visemeData = JSON.parse(event.data);
      console.log("Received viseme data:", visemeData);
      if (visemeData.type === 'lipsync') {
        AvatarService.updateLipSync(visemeData);
      }
    } else {
      const arrayBuffer = event.data;
      const audioBuffer = await audioContext.decodeAudioData(arrayBuffer);
      const source = audioContext.createBufferSource();
      source.buffer = audioBuffer;
      source.connect(audioContext.destination);
      source.start();
    }
  };

  socket.onclose = () => {
    console.log("WebSocket connection closed.");
  };

  socket.onerror = (error) => {
    console.error("WebSocket error:", error);
  };
}

export function closeWebSocket() {
  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.close();
  }
}

export function sendAudioChunk(int16Array) {
  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(int16Array.buffer);  // ⚠️ buffer 전송
    console.log('[WebSocket] Sent audio chunk:', int16Array.length);
  } else {
    console.warn('[WebSocket] Cannot send, socket not open.');
  }
}
