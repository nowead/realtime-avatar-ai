# 1단계: 애플리케이션 빌드
FROM node:18-alpine AS builder

# 작업 디렉토리 설정
WORKDIR /app

# 의존성 설치를 위해 package.json 및 package-lock.json (존재하는 경우) 복사
COPY package.json ./
# package-lock.json이 있다면 다음 줄의 주석을 해제하세요.
COPY package-lock.json ./

# 의존성 설치
RUN npm install

# 나머지 소스 코드 복사
COPY . .

# 애플리케이션 빌드 (package.json의 빌드 스크립트 사용)
RUN npm run build

# 2단계: Nginx를 사용하여 서비스
FROM nginx:alpine

# webpack.config.js에 따르면 빌드 결과물은 'dist' 폴더에 생성됩니다.
COPY --from=builder /app/dist /usr/share/nginx/html

# Nginx가 수신 대기할 포트 노출 (기본값 80)
EXPOSE 80

# Nginx 실행 명령
CMD ["nginx", "-g", "daemon off;"]