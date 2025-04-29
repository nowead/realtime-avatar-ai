// src/websocket_client.ts

// import { EventEmitter } from './event_emitter'; // <<<--- 이 라인을 삭제하세요!

// --- Simple EventEmitter Implementation ---
// You might want to use a more robust library like 'mitt' or 'eventemitter3'
type Listener<T = any> = (data: T) => void;

// EventEmitter 클래스를 WebSocketClient 클래스 **앞에** 정의하거나 별도 파일로 분리 후 import
class EventEmitter {
    private listeners: { [event: string]: Listener[] } = {};

    /** 이벤트 리스너 등록 */
    on<T>(event: string, listener: Listener<T>): void {
        if (!this.listeners[event]) {
            this.listeners[event] = [];
        }
        this.listeners[event].push(listener);
    }

    /** 이벤트 리스너 제거 */
    off<T>(event: string, listener: Listener<T>): void {
        if (!this.listeners[event]) return;
        this.listeners[event] = this.listeners[event].filter(l => l !== listener);
    }

    /** 이벤트 발생 */
    emit<T>(event: string, data?: T): void {
        if (!this.listeners[event]) return;
        this.listeners[event].forEach(listener => {
            try {
                 listener(data as T);
            } catch (error) {
                 console.error(`이벤트 "${event}" 리스너 오류:`, error);
            }
        });
    }
}


// 이제 EventEmitter가 먼저 정의되었으므로 extends 가능
export class WebSocketClient extends EventEmitter {
    private ws: WebSocket | null = null;
    private url: string;
    private sessionId: string;
    private reconnectInterval: number = 5000; // 5 seconds
    private reconnectTimeoutId: number | null = null;

    constructor(url: string, sessionId: string) {
        super(); // EventEmitter 생성자 호출
        this.url = url;
        this.sessionId = sessionId;
    }

    // ... (connect, registerSession, send, close, scheduleReconnect, clearReconnectTimer 메소드는 이전과 동일) ...
     connect() {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            console.log("WebSocket이 이미 열려 있습니다.");
            return;
        }
        console.log(`${this.url}에 연결 시도 중...`);
        this.ws = new WebSocket(this.url);
        this.ws.onopen = (event) => {
            console.log("✅ WebSocket 연결 성공.");
            this.clearReconnectTimer();
            this.registerSession();
            this.emit('open', event);
        };
        this.ws.onmessage = (event) => {
            try {
                const message = JSON.parse(event.data);
                if (message.type === 'avatar_sync' && message.visemes) {
                    this.emit('avatar_sync', {
                        format: message.format,
                        visemes: message.visemes
                    });
                } else {
                    console.warn("알 수 없는 메시지 타입 수신:", message.type);
                     this.emit('message', message);
                }
            } catch (error) {
                console.error("WebSocket 메시지 파싱 오류:", error, event.data);
                 if (event.data instanceof Blob) {
                     this.emit('binary_message', event.data);
                 }
            }
        };
        this.ws.onerror = (event) => {
            console.error("WebSocket 오류:", event);
            this.emit('error', event);
        };
        this.ws.onclose = (event) => {
            console.log(`❌ WebSocket 연결 종료됨. 코드: ${event.code}, 이유: ${event.reason}`);
            this.ws = null;
            this.emit('close', event);
            this.scheduleReconnect();
        };
    }
     private registerSession() {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            const registrationMessage = JSON.stringify({
                type: "register",
                session_id: this.sessionId
            });
            console.log("➡️ 등록 메시지 전송:", registrationMessage);
            this.ws.send(registrationMessage);
        } else {
            console.warn("세션을 등록할 수 없습니다. WebSocket이 열려 있지 않습니다.");
        }
    }
     send(data: string | ArrayBufferLike | Blob | ArrayBufferView) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(data);
        } else {
            console.error("메시지를 보낼 수 없습니다. WebSocket이 열려 있지 않습니다.");
        }
    }
     close() {
        this.clearReconnectTimer();
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
    }
     private scheduleReconnect() {
        if (this.reconnectTimeoutId) return;
        console.log(`${this.reconnectInterval / 1000}초 후 재연결 예약...`);
        this.reconnectTimeoutId = window.setTimeout(() => {
            this.reconnectTimeoutId = null;
            this.connect();
        }, this.reconnectInterval);
    }
     private clearReconnectTimer() {
         if (this.reconnectTimeoutId) {
             window.clearTimeout(this.reconnectTimeoutId);
             this.reconnectTimeoutId = null;
         }
     }
}

// EventEmitter 클래스 정의는 위로 이동했습니다.