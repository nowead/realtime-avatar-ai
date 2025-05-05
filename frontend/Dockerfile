# --- Stage 1: Node.js 환경에서 애플리케이션 빌드 ---
    FROM node:18-alpine AS builder
    WORKDIR /app
    
    # package.json과 package-lock.json 파일을 빌드 컨테이너로 복사
    # package-lock.json이 있으면 빌드 재현성 보장 (없어도 에러는 안남)
    COPY package-lock.json* ./
    COPY package.json ./
    
    # Docker 빌드 컨테이너 내부에서 의존성 설치 (로컬 X)
    # lock 파일이 있으면 'npm ci'가 더 빠르고 안정적일 수 있음
    # RUN npm ci --only=production # 프로덕션 의존성만 설치 시
    RUN npm install # devDependencies 포함 설치 (빌드에 필요하므로)
    
    # 소스 코드 전체를 빌드 컨테이너로 복사
    COPY . .
    
    # Docker 빌드 컨테이너 내부에서 빌드 실행 (로컬 X)
    # package.json의 "build" 스크립트가 Webpack 등을 실행
    RUN npm run build
    
    # --- Stage 2: 빌드 결과물을 Nginx로 서빙 ---
    FROM nginx:1.27-alpine
    WORKDIR /usr/share/nginx/html
    RUN rm -rf ./*
    
    # Stage 1(builder)에서 빌드된 결과물(/app/dist 내용)만 최종 이미지로 복사
    COPY --from=builder /app/dist .
    
    EXPOSE 80
    # CMD ["nginx", "-g", "daemon off;"] # Base 이미지에 포함됨