#!/bin/bash

PROJECT_NAME="tts_service"
BUILD_DIR="build"

echo "📦 [1/2] 빌드 디렉토리 생성 및 CMake 초기화"
rm -rf ${BUILD_DIR}
mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}
cmake ..
echo "✅ CMake 설정 완료"

echo "📦 [2/2] 빌드 실행"
cmake --build .
echo "🎉 빌드 완료! 실행 파일은 build/${PROJECT_NAME} 에 생성됩니다."
