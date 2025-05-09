// js/webrtc.js
const WebRTCService = {
    peerConnection: null,
    signalingServerUrl: "ws://<backend_ip_or_hostname>:8443", // 시그널링 서버 주소 필요!
    websocket: null,
    localStream: null,
    remoteAudioElement: null, // 원격 오디오 재생용
    onRemoteTrackCallback: null, // 원격 오디오 트랙 수신 콜백
    onDataChannelMessageCallback: null, // 데이터 채널 메시지 수신 콜백
    dataChannel: null,

    initialize(onRemoteTrack, onDataChannelMessage) {
        this.onRemoteTrackCallback = onRemoteTrack;
        this.onDataChannelMessageCallback = onDataChannelMessage;

        // 원격 오디오 재생용 엘리먼트 생성
        this.remoteAudioElement = new Audio();
        this.remoteAudioElement.autoplay = true;
    },

    async connect(localAudioStream) {
        this.localStream = localAudioStream;

        // 1. 시그널링 서버 연결
        this.websocket = new WebSocket(this.signalingServerUrl);
        this.websocket.onopen = () => {
            console.log("Signaling server connected.");
            // 연결 시작 메시지 전송 (필요시)
            // this.sendMessage({ type: 'join', clientId: 'user-' + Date.now() });
            this.createPeerConnection(); // 웹소켓 연결 후 PeerConnection 생성
        };
        this.websocket.onmessage = async (message) => {
            const data = JSON.parse(message.data);
            console.log("Received signaling message:", data);
            switch (data.type) {
                case 'offer':
                    await this.handleOffer(data.offer);
                    break;
                case 'answer':
                    await this.handleAnswer(data.answer);
                    break;
                case 'candidate':
                    await this.handleCandidate(data.candidate);
                    break;
                // 다른 시그널링 메시지 처리
            }
        };
        this.websocket.onerror = (error) => {
            console.error("Signaling server error:", error);
        };
        this.websocket.onclose = () => {
            console.log("Signaling server disconnected.");
            this.closeConnection(); // 웹소켓 끊어지면 WebRTC도 정리
        };
    },

    createPeerConnection() {
         // STUN/TURN 서버 설정 (NAT 통과에 필수)
        const iceServers = [
            { urls: 'stun:stun.l.google.com:19302' } // 예시 STUN 서버
            // TURN 서버 추가 필요 (릴레이용)
        ];
        const configuration = { iceServers: iceServers };
        this.peerConnection = new RTCPeerConnection(configuration);

        // 로컬 오디오 트랙 추가
        if (this.localStream) {
            this.localStream.getTracks().forEach(track => {
                this.peerConnection.addTrack(track, this.localStream);
                console.log("Local audio track added.");
            });
        } else {
            console.warn("Local audio stream not available when creating peer connection.");
        }

        // ICE Candidate 핸들러
        this.peerConnection.onicecandidate = (event) => {
            if (event.candidate) {
                console.log("Sending ICE candidate:", event.candidate);
                this.sendMessage({ type: 'candidate', candidate: event.candidate });
            }
        };

        // 원격 트랙 수신 핸들러
        this.peerConnection.ontrack = (event) => {
            console.log("Remote track received:", event.track.kind);
            if (event.track.kind === 'audio') {
                if (this.onRemoteTrackCallback) {
                     // 원격 오디오 스트림을 Audio 객체에 연결
                    this.remoteAudioElement.srcObject = event.streams[0];
                    this.onRemoteTrackCallback(event.streams[0]);
                }
            }
            // 비디오 트랙 등 다른 종류의 트랙 처리 가능
        };

        // 데이터 채널 설정 (립싱크 데이터용)
        // 송신 측에서 채널 생성
        this.dataChannel = this.peerConnection.createDataChannel('lipSyncDataChannel', { ordered: true });
        this.dataChannel.onopen = () => console.log("Data channel opened.");
        this.dataChannel.onclose = () => console.log("Data channel closed.");
        this.dataChannel.onerror = (error) => console.error("Data channel error:", error);
        // 메시지 수신은 ondatachannel 핸들러에서 설정 (아래)

         // 수신 측에서 데이터 채널 수신 핸들러
        this.peerConnection.ondatachannel = (event) => {
            console.log('Data channel received:', event.channel.label);
            const receiveChannel = event.channel;
            receiveChannel.onmessage = (event) => {
                 console.log("Data channel message received:", event.data);
                 try {
                     const messageData = JSON.parse(event.data);
                     if (this.onDataChannelMessageCallback) {
                         this.onDataChannelMessageCallback(messageData);
                     }
                 } catch (e) {
                     console.error("Failed to parse data channel message:", e);
                 }
            };
            receiveChannel.onopen = () => console.log("Received data channel opened.");
            receiveChannel.onclose = () => console.log("Received data channel closed.");
        };


        // 연결 상태 변경 핸들러
        this.peerConnection.onconnectionstatechange = () => {
            console.log(`Peer Connection state: ${this.peerConnection.connectionState}`);
            // 연결 끊김 또는 실패 시 처리
            if (this.peerConnection.connectionState === 'failed' || this.peerConnection.connectionState === 'disconnected' || this.peerConnection.connectionState === 'closed') {
               // this.closeConnection();
            }
        };

         console.log("RTCPeerConnection created.");
        // 필요시 여기서 Offer 생성 및 전송 시작 가능
        // this.createOffer();
    },

    async createOffer() {
        if (!this.peerConnection) return;
        try {
            const offer = await this.peerConnection.createOffer();
            await this.peerConnection.setLocalDescription(offer);
            console.log("Offer created and set as local description.");
            this.sendMessage({ type: 'offer', offer: offer });
        } catch (e) {
            console.error("Failed to create offer:", e);
        }
    },

    async handleOffer(offerSdp) {
        if (!this.peerConnection) this.createPeerConnection(); // PeerConnection 없으면 생성
        try {
            await this.peerConnection.setRemoteDescription(new RTCSessionDescription(offerSdp));
            console.log("Remote offer set.");
            const answer = await this.peerConnection.createAnswer();
            await this.peerConnection.setLocalDescription(answer);
            console.log("Answer created and set as local description.");
            this.sendMessage({ type: 'answer', answer: answer });
        } catch (e) {
            console.error("Failed to handle offer:", e);
        }
    },

    async handleAnswer(answerSdp) {
        if (!this.peerConnection) return;
        try {
            await this.peerConnection.setRemoteDescription(new RTCSessionDescription(answerSdp));
            console.log("Remote answer set.");
        } catch (e) {
            console.error("Failed to set remote answer:", e);
        }
    },

    async handleCandidate(candidate) {
        if (!this.peerConnection) return;
        try {
            await this.peerConnection.addIceCandidate(new RTCIceCandidate(candidate));
            console.log("ICE candidate added.");
        } catch (e) {
            console.error("Error adding received ICE candidate:", e);
        }
    },

    // VAD 상태에 따라 오디오 트랙 활성화/비활성화 (선택적)
    // 또는 데이터 채널로 VAD 상태 전송
    setAudioEnabled(enabled) {
        if (this.localStream) {
            this.localStream.getAudioTracks().forEach(track => {
                track.enabled = enabled;
            });
            console.log(`Local audio track ${enabled ? 'enabled' : 'disabled'}`);

            // 데이터 채널로 VAD 상태 알림 (백엔드 구현에 따라)
            if (this.dataChannel && this.dataChannel.readyState === 'open') {
                this.dataChannel.send(JSON.stringify({ type: 'vad_status', speaking: enabled }));
            }
        }
    },

    sendMessage(message) {
        if (this.websocket && this.websocket.readyState === WebSocket.OPEN) {
            this.websocket.send(JSON.stringify(message));
        } else {
            console.error("Cannot send message, WebSocket is not open.");
        }
    },

    closeConnection() {
        if (this.dataChannel) {
            this.dataChannel.close();
            this.dataChannel = null;
        }
        if (this.peerConnection) {
            this.peerConnection.close();
            this.peerConnection = null;
        }
        if (this.websocket) {
            this.websocket.close();
            this.websocket = null;
        }
        if (this.remoteAudioElement) {
            this.remoteAudioElement.srcObject = null; // 오디오 스트림 연결 해제
        }
        console.log("WebRTC connection closed.");
    }
};