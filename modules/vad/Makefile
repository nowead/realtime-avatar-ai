# 📦 프로젝트 설정
PROJECT_NAME := vad_service
BUILD_DIR    := build
OBJ_DIR      := $(BUILD_DIR)/obj
EXECUTABLE   := $(BUILD_DIR)/$(PROJECT_NAME)
TEST_EXEC    := $(BUILD_DIR)/vad_tests

SRC_DIR      := src
MODEL_DIR    := models # 모델 디렉토리 (참고용)
TEST_DIR     := tests

# ✅ vcpkg 경로 (arm64-osx 기준 - 환경에 맞게 수정)
# VCPKG_ROOT 환경 변수가 설정되어 있다고 가정
VCPKG_INSTALL_DIR := $(VCPKG_ROOT)/installed/arm64-osx
VCPKG_INCLUDE     := $(VCPKG_INSTALL_DIR)/include
VCPKG_LIB         := $(VCPKG_INSTALL_DIR)/lib

# include 경로 설정
INCLUDES     := \
    -I$(SRC_DIR) \
    -I$(VCPKG_INCLUDE) \
    -I$(VCPKG_INCLUDE)/uwebsockets # uWebSockets 헤더 경로 уточнение

CXX          := clang++
CXXFLAGS     := -std=c++20 -Wall -O2 $(INCLUDES) # 최적화 옵션 추가 (선택적)
LDFLAGS      := \
    -L$(VCPKG_LIB) \
    -lusockets \
    -lz \
    -lssl \
    -lcrypto \
    -luv \
    -lpthread \
    -lonnxruntime \
    -lportaudio # PortAudio 라이브러리 링크 (예시, 사용하는 오디오 라이브러리로 변경)
    # 필요한 다른 오디오 라이브러리 플래그 추가

# 전체 원본 소스
SRC_ALL := \
    $(SRC_DIR)/main.cpp \
    $(SRC_DIR)/vad_processor.cpp \
    $(SRC_DIR)/audio_input.cpp \
    $(SRC_DIR)/websocket_client.cpp

# 오브젝트 파일 매핑
OBJS := \
    $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRC_ALL))

# -----------------------------
# 🎯 기본 빌드: all → executables
# -----------------------------
all: $(EXECUTABLE)

# -----------------------------
# 🔧 컴파일: .cpp → .o
# -----------------------------
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# -----------------------------
# 🔗 링크: 모든 오브젝트 → 실행 파일
# -----------------------------
$(EXECUTABLE): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

# -----------------------------
# 🧪 테스트 타겟
# -----------------------------
TEST_SRCS    := $(wildcard $(TEST_DIR)/*.cpp)
# 테스트 대상 소스에 맞는 오브젝트 파일 생성 (예: vad_processor.o 등)
TEST_DEPEND_OBJS := $(OBJ_DIR)/vad_processor.o $(OBJ_DIR)/websocket_client.o # 테스트에 필요한 구현 오브젝트
TEST_OBJS    := $(patsubst $(TEST_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(TEST_SRCS))
TEST_LDFLAGS := -lgtest -pthread # gtest 라이브러리 링크

# 테스트용 오브젝트 컴파일 규칙 (소스 위치가 다르므로 별도 정의)
$(OBJ_DIR)/%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_EXEC): $(TEST_OBJS) $(TEST_DEPEND_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS) $(TEST_LDFLAGS)

test: $(TEST_EXEC)
	@echo "🧪 [test] VAD 서비스 테스트 실행 중..."
	@$(TEST_EXEC)

# -----------------------------
# 🚀 실행
# -----------------------------
run: $(EXECUTABLE)
	@echo "🚀 [run] VAD 서비스 실행 중..."
	@$(EXECUTABLE)

# -----------------------------
# 🔄 코드 포맷
# -----------------------------
format:
	clang-format -i $(shell find $(SRC_DIR) $(TEST_DIR) -name '*.cpp' -or -name '*.h')

# -----------------------------
# ❌ 클린
# -----------------------------
clean:
	@echo "🧹 빌드 디렉토리 정리 중..."
	@rm -rf $(BUILD_DIR)

re: clean all

.PHONY: all clean re run test format

# 타겟 실행 전 디렉토리 생성 보장 (선택적 개선)
$(EXECUTABLE) $(TEST_EXEC): | $(BUILD_DIR) $(OBJ_DIR)
$(BUILD_DIR) $(OBJ_DIR):
	mkdir -p $@