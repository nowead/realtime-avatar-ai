cmake_minimum_required(VERSION 3.15)
project(tts_service)

set(CMAKE_CXX_STANDARD 17)

# -----------------------------
# ✅ 패키지 찾기
# -----------------------------
find_package(gRPC REQUIRED)
find_package(pybind11 REQUIRED)
find_package(GTest REQUIRED)

include_directories(
    ${Protobuf_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}
    src
    proto
    tests
)

# -----------------------------
# ✅ .proto 파일 경로 및 생성 디렉토리 지정
# -----------------------------
set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(PROTO_FILE ${PROTO_SRC_DIR}/tts.proto)
set(GENERATED_SRC_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_SRC_DIR})

# -----------------------------
# ✅ protoc 및 grpc_cpp_plugin 수동으로 찾기
# -----------------------------
find_program(Protobuf_PROTOC_EXECUTABLE protoc)
if(NOT Protobuf_PROTOC_EXECUTABLE)
    message(FATAL_ERROR "protoc not found")
endif()

find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
if(NOT GRPC_CPP_PLUGIN)
    message(FATAL_ERROR "grpc_cpp_plugin not found")
endif()

# -----------------------------
# ✅ .proto → C++ 파일 생성 명령
# -----------------------------
add_custom_command(
    OUTPUT
        ${GENERATED_SRC_DIR}/tts.pb.cc
        ${GENERATED_SRC_DIR}/tts.pb.h
        ${GENERATED_SRC_DIR}/tts.grpc.pb.cc
        ${GENERATED_SRC_DIR}/tts.grpc.pb.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_SRC_DIR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --proto_path=${PROTO_SRC_DIR}
        --cpp_out=${GENERATED_SRC_DIR}
        --grpc_out=${GENERATED_SRC_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "🏦 Generating gRPC & protobuf code"
    VERBATIM
)

add_custom_target(generate_proto_code DEPENDS
    ${GENERATED_SRC_DIR}/tts.pb.cc
    ${GENERATED_SRC_DIR}/tts.grpc.pb.cc
)

# -----------------------------
# ✅ 메인 실행 타겟
# -----------------------------
set(APP_SRC
    src/main.cpp
    src/tts_service.cpp
    src/tts_engine.cpp
    ${GENERATED_SRC_DIR}/tts.pb.cc
    ${GENERATED_SRC_DIR}/tts.grpc.pb.cc
)

add_executable(tts_service ${APP_SRC})
add_dependencies(tts_service generate_proto_code)

target_include_directories(tts_service PRIVATE ${GENERATED_SRC_DIR})

target_link_libraries(tts_service
    gRPC::grpc++
    protobuf::libprotobuf
    pybind11::embed
)

# -----------------------------
# ✅ 테스트 타겟
# -----------------------------
add_executable(grpc_tests
    tests/test_grpc_tts.cpp
    src/tts_service.cpp
    src/tts_engine.cpp
    ${GENERATED_SRC_DIR}/tts.pb.cc
    ${GENERATED_SRC_DIR}/tts.grpc.pb.cc
)

target_include_directories(grpc_tests PRIVATE ${GENERATED_SRC_DIR})
target_link_libraries(grpc_tests
    GTest::gtest_main
    gRPC::grpc++
    protobuf::libprotobuf
    pybind11::embed
)

add_test(NAME GRPC_TTS_Integration_Test COMMAND grpc_tests)

# -----------------------------
# 🧹 C++ 코드 포맷용 타겟
# -----------------------------
find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE ALL_CXX_SOURCE_FILES src/*.cpp src/*.h tests/*.cpp)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${ALL_CXX_SOURCE_FILES}
        COMMENT "Running clang-format on C++ sources"
    )
else()
    message(WARNING "clang-format not found. 'make format' will be unavailable.")
endif()
