# 📦 프로젝트 설정
PROJECT_NAME := tts_service
BUILD_DIR := build
EXECUTABLE := $(BUILD_DIR)/$(PROJECT_NAME)
TEST_EXEC := $(BUILD_DIR)/grpc_tests

SRC_DIR := src
PROTO_DIR := proto
PROTO_FILE := $(PROTO_DIR)/tts.proto
GEN_DIR := $(BUILD_DIR)/generated
TEST_DIR := tests

SPEECHSDK_ROOT := /Users/mindaewon/speechsdk
FRAMEWORK_PATH := $(SPEECHSDK_ROOT)/MicrosoftCognitiveServicesSpeech.xcframework/macos-arm64_x86_64
FRAMEWORK := MicrosoftCognitiveServicesSpeech

# ✅ gRPC plugin
PROTOC := protoc
GRPC_CPP_PLUGIN := /opt/homebrew/bin/grpc_cpp_plugin

# ✅ GTest 수동 설정 (pkg-config 지원되지 않음)
GTEST_DIR := /opt/homebrew/opt/googletest

# 🔧 include & link 설정
INCLUDES := -I$(SRC_DIR) -I$(GEN_DIR) \
            -I$(FRAMEWORK_PATH)/$(FRAMEWORK).framework/Headers \
            -I$(GTEST_DIR)/include \
            -I/opt/homebrew/include -I/opt/homebrew/include/grpcpp

PKG_CFLAGS := $(shell pkg-config --cflags protobuf grpc++)
PKG_LDFLAGS := $(shell pkg-config --libs protobuf grpc++)

CXX := clang++
CXXFLAGS := -std=c++17 -Wall $(INCLUDES) $(PKG_CFLAGS) -F$(FRAMEWORK_PATH)
LDFLAGS := $(PKG_LDFLAGS) -framework $(FRAMEWORK) \
           -L$(GTEST_DIR)/lib -lgtest -lgtest_main -lpthread
# 자동 생성 파일
GEN_SRCS := $(GEN_DIR)/tts.pb.cc $(GEN_DIR)/tts.grpc.pb.cc
GEN_HDRS := $(GEN_DIR)/tts.pb.h $(GEN_DIR)/tts.grpc.pb.h

# 전체 소스
SRC_FILES := $(SRC_DIR)/main.cpp $(SRC_DIR)/azure_tts_client.cpp $(SRC_DIR)/tts_service.cpp $(GEN_SRCS)
TEST_FILES := $(TEST_DIR)/test_tts_service.cpp $(SRC_DIR)/azure_tts_client.cpp $(SRC_DIR)/tts_service.cpp $(GEN_SRCS)

# 🧱 기본 빌드
all: $(EXECUTABLE)

# 🏗 gRPC 코드 생성
$(GEN_SRCS) $(GEN_HDRS): $(PROTO_FILE)
	@mkdir -p $(GEN_DIR)
	$(PROTOC) --proto_path=$(PROTO_DIR) \
		--cpp_out=$(GEN_DIR) \
		--grpc_out=$(GEN_DIR) \
		--plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) \
		$(PROTO_FILE)

# 🛠 메인 타겟 빌드
$(EXECUTABLE): $(SRC_FILES) $(GEN_HDRS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SRC_FILES) -o $@ $(LDFLAGS)

# 🧪 테스트 타겟 빌드
$(TEST_EXEC): $(TEST_FILES) $(GEN_SRCS) $(GEN_HDRS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(TEST_FILES) -o $@ $(LDFLAGS)

# ▶️ 실행
run: $(EXECUTABLE)
	@echo "🚀 [run] 실행 중..."
	@env DYLD_FRAMEWORK_PATH=$(FRAMEWORK_PATH) $(EXECUTABLE)

# 🧪 테스트 실행
test: $(TEST_EXEC)
	@echo "🧪 [test] 단위 테스트 실행 중..."
	@DYLD_FRAMEWORK_PATH=$(FRAMEWORK_PATH) $(TEST_EXEC)

# 🧼 코드 포맷
format:
	@echo "🧼 [format] clang-format 실행 중..."
	clang-format -i $(shell find $(SRC_DIR) $(TEST_DIR) -name '*.cpp' -or -name '*.h')

# 🧹 클린
clean:
	@echo "🧹 [clean] 빌드 디렉토리 삭제"
	rm -rf $(BUILD_DIR)

# ♻️ 전체 재빌드
re: clean all

.PHONY: all clean re run test format
