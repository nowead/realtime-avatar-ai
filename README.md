# Realtime Avatar AI

실시간 음성 대화가 가능한 3D 아바타 AI 시스템입니다. 사용자의 음성을 인식하여 AI가 응답하고, 3D 아바타가 실시간으로 립싱크 애니메이션을 표현합니다.

## 아키텍처 개요

```text
사용자 브라우저
      │ WebSocket (Audio + JSON)
      ▼
┌─────────────────────┐
│  WebSocket Gateway  │ :8000 (WS), :50055 (gRPC)
└──────────┬──────────┘
           │ gRPC
    ┌──────┼──────┐
    ▼      ▼      ▼
  [STT]  [LLM]  [TTS]
  :50052 :50053 :50054
    │      │      │
  Azure  OpenAI  Azure
  Speech  API   Speech
```

### 데이터 흐름

1. 사용자 발화 → Silero VAD 감지 → 오디오 청크 → WebSocket → STT 서비스
2. STT 서비스 → Azure Speech SDK → 텍스트 인식 → LLM 서비스 (gRPC)
3. LLM 서비스 → OpenAI API (gpt-4o) → 응답 텍스트 → TTS 서비스 (gRPC)
4. TTS 서비스 → Azure TTS → 오디오 + 비세임(Viseme) 데이터 → WebSocket Gateway (gRPC)
5. WebSocket Gateway → 프론트엔드 → 오디오 재생 + 아바타 립싱크

## 기술 스택

### 백엔드 (C++17)

- **gRPC / Protobuf** - 마이크로서비스 간 통신
- **uWebSockets** - WebSocket 서버
- **Azure Cognitive Services Speech SDK** - STT / TTS
- **OpenAI API** - LLM (gpt-4o)
- **CMake** - 빌드 시스템
- **Docker** - 컨테이너화 (멀티스테이지 빌드)

### 프론트엔드 (JavaScript)

- **Babylon.js** - 3D 렌더링 및 아바타 시각화
- **ONNX Runtime Web** - Silero VAD 추론
- **Web Audio API** - 마이크 입력 / 오디오 재생
- **Webpack 5 / Babel 7** - 빌드 도구
- **Nginx** - 정적 파일 서빙

## 프로젝트 구조

```text
realtime-avatar-ai/
├── backend/
│   ├── websocket_gateway/   # WebSocket 서버 + AvatarSync gRPC 서버
│   ├── stt_service/         # 음성 인식 서비스 (Azure Speech)
│   ├── llm_engine/          # 언어 모델 서비스 (OpenAI)
│   ├── tts_service/         # 음성 합성 서비스 (Azure TTS)
│   ├── protos/              # gRPC Protocol Buffer 정의
│   ├── docker-compose.yml
│   ├── Makefile
│   └── .env
└── frontend/
    ├── src/
    │   ├── js/
    │   │   ├── main.js                    # 앱 상태 관리, VAD 처리, 버튼 제어
    │   │   ├── websocket.js               # WebSocket 연결, 오디오 워크렛 설정
    │   │   ├── avatar.js                  # Babylon.js 아바타, 모프 타겟
    │   │   ├── audio.js                   # 오디오 유틸리티
    │   │   ├── sileroVadRunner.js         # ONNX VAD 추론
    │   │   └── audio-worklet-processor.js
    │   └── css/
    ├── public/index.html
    ├── webpack.config.js
    ├── package.json
    └── Dockerfile
```

## 사전 요구사항

- Docker & Docker Compose
- Azure Cognitive Services 계정 (Speech 리소스)
- OpenAI API 키
- Node.js 18+ (프론트엔드 로컬 개발 시)

## 환경 변수 설정

### `backend/stt_service/.env`

```env
AZURE_SPEECH_KEY=<Azure Speech API 키>
AZURE_SPEECH_REGION=koreacentral
LLM_ENGINE_ADDRESS=llm-service:50053
```

### `backend/llm_engine/.env`

```env
OPENAI_API_KEY=sk-proj-<OpenAI API 키>
TTS_SERVICE_ADDRESS=tts-service:50054
OPENAI_MODEL=gpt-4o
```

### `backend/tts_service/.env`

```env
AZURE_SPEECH_KEY=<Azure Speech API 키>
AZURE_SPEECH_REGION=koreacentral
AVATAR_SYNC_SERVICE_ADDRESS=websocket_gateway:50055
```

### `backend/.env`

```env
TARGET_ARCH=x64   # x64 또는 arm64
```

## 실행 방법

### 백엔드

```bash
cd backend

# 이미지 빌드 (아키텍처 자동 감지: x64 / arm64)
make build

# 서비스 시작
make up

# 로그 확인
make logs

# 서비스 상태 확인
make ps

# 서비스 중지
make stop
```

### 프론트엔드

```bash
cd frontend

# Docker 이미지 빌드
make build

# 컨테이너 실행 (포트 8080)
make run
```

브라우저에서 `http://localhost:8080` 접속

### 로컬 개발 (프론트엔드)

```bash
cd frontend
npm install
npm run dev   # Webpack dev server 시작
```

## 서비스 포트

| 서비스 | 포트 | 프로토콜 |
| ------ | ---- | -------- |
| WebSocket Gateway | 8000 | WebSocket |
| WebSocket Gateway | 50055 | gRPC (AvatarSync) |
| STT Service | 50052 | gRPC |
| LLM Engine | 50053 | gRPC |
| TTS Service | 50054 | gRPC |
| Frontend | 8080 | HTTP |

## gRPC 서비스 정의

`backend/protos/` 디렉토리에 위치:

| 파일 | 서비스 | 설명 |
| ---- | ------ | ---- |
| `stt.proto` | STTService | 오디오 스트림 수신 → 텍스트 인식 |
| `llm.proto` | LLMService | 텍스트 스트림 수신 → 응답 생성 |
| `tts.proto` | TTSService | 텍스트 스트림 수신 → 오디오 + 비세임 합성 |
| `avatar_sync.proto` | AvatarSyncService | 오디오 + 비세임 데이터 → 프론트엔드 전달 |

## 모니터링

- **헬스체크:** `http://localhost:8000/healthz`
- **메트릭:** `http://localhost:8000/metrics`
- gRPC 헬스 프로브를 통한 백엔드 서비스 상태 확인
