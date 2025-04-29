# 📦 프로젝트 설정
PROJECT_NAME := avatar_sync_service
BUILD_DIR   := build
OBJ_DIR     := $(BUILD_DIR)/obj
EXECUTABLE  := $(BUILD_DIR)/$(PROJECT_NAME)
TEST_EXEC   := $(BUILD_DIR)/avatar_sync_tests

SRC_DIR     := src
PROTO_DIR   := proto
PROTO_FILE  := $(PROTO_DIR)/avatar.proto
GEN_DIR     := $(BUILD_DIR)/generated
TEST_DIR    := tests

# ✅ protoc & gRPC plugin
PROTOC           := protoc
GRPC_CPP_PLUGIN  := /opt/homebrew/bin/grpc_cpp_plugin

# ✅ vcpkg 경로 (arm64-osx 기준)
VCPKG_INCLUDE := $(VCPKG_ROOT)/installed/arm64-osx/include
VCPKG_LIB     := $(VCPKG_ROOT)/installed/arm64-osx/lib

# include 경로 및 pkg-config 설정
INCLUDES    := \
    -I$(SRC_DIR) \
    -I$(GEN_DIR) \
    -I$(VCPKG_INCLUDE) \
    -I$(VCPKG_INCLUDE)/uwebsockets \
    -I$(VCPKG_INCLUDE)/nlohmann

PKG_CFLAGS  := $(shell pkg-config --cflags protobuf grpc++)
PKG_LDFLAGS := $(shell pkg-config --libs protobuf grpc++)

CXX         := clang++
CXXFLAGS    := -std=c++20 -Wall $(INCLUDES) $(PKG_CFLAGS)
LDFLAGS     := $(PKG_LDFLAGS) -L$(VCPKG_LIB) -lusockets -lz -lssl -lcrypto -luv -lpthread

# 자동 생성 파일 (.proto → .cc/.h)
GEN_SRCS := $(GEN_DIR)/avatar.pb.cc \
            $(GEN_DIR)/avatar.grpc.pb.cc
GEN_HDRS := $(GEN_DIR)/avatar.pb.h \
            $(GEN_DIR)/avatar.grpc.pb.h

# 전체 원본 소스
SRC_ALL := \
    $(SRC_DIR)/main.cpp \
    $(SRC_DIR)/avatar_sync_service.cpp \
    $(SRC_DIR)/websocket_dispatcher_uwebs.cpp \
    $(GEN_SRCS)

# 오브젝트 파일 매핑
OBJS := \
    $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, \
        $(filter %.cpp, $(SRC_ALL))) \
    $(patsubst $(GEN_DIR)/%.cc, $(OBJ_DIR)/gen/%.o, \
        $(filter %.cc, $(SRC_ALL)))

# -----------------------------
# 🎯 기본 빌드: all → executables
# -----------------------------
all: $(EXECUTABLE)

# -----------------------------
# 🌐 gRPC 코드 생성 (.proto → .cc/.h)
# -----------------------------
$(GEN_SRCS) $(GEN_HDRS): $(PROTO_FILE)
	@mkdir -p $(GEN_DIR)
	$(PROTOC) --proto_path=$(PROTO_DIR) \
             --cpp_out=$(GEN_DIR) \
             --grpc_out=$(GEN_DIR) \
             --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) \
             $(PROTO_FILE)

# -----------------------------
# 🔧 컴파일: .cpp/.cc → .o
# -----------------------------
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp $(GEN_HDRS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/gen/%.o: $(GEN_DIR)/%.cc $(GEN_HDRS)
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
TEST_OBJS    := $(patsubst $(TEST_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(TEST_SRCS))
PROTO_OBJS   := $(OBJ_DIR)/gen/avatar.pb.o $(OBJ_DIR)/gen/avatar.grpc.pb.o
IMPL_OBJ     := $(OBJ_DIR)/avatar_sync_service.o
TEST_LDFLAGS := -lgtest -pthread

$(TEST_EXEC): $(TEST_OBJS) $(IMPL_OBJ) $(PROTO_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ \
	    $(TEST_OBJS) $(IMPL_OBJ) $(PROTO_OBJS) \
	    $(LDFLAGS) $(TEST_LDFLAGS)

$(OBJ_DIR)/%.o: $(TEST_DIR)/%.cpp $(GEN_HDRS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -std=c++17 -c $< -o $@

test: $(TEST_EXEC)
	@echo "🧪 [test] 통합 테스트 실행 중..."
	@$(TEST_EXEC)

# -----------------------------
# 🚀 실행
# -----------------------------
run: $(EXECUTABLE)
	@echo "🚀 [run] 실행 중..."
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
	rm -rf $(BUILD_DIR)

re: clean all

.PHONY: all clean re run test format
