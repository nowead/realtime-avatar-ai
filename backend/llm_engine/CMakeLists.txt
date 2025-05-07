cmake_minimum_required(VERSION 3.13)
project(llm_engine LANGUAGES CXX)
option(BUILD_TESTING "Build unit tests" OFF)
set(SAVED_BUILD_TESTING ${BUILD_TESTING})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_VERBOSE_MAKEFILE ON)

# ---=[ 의존성 찾기 ]=---
find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(PC_GRPC REQUIRED IMPORTED_TARGET grpc++)
if(NOT PC_GRPC_FOUND)
    message(FATAL_ERROR "gRPC++ library not found using pkg-config. Ensure libgrpc++-dev is installed.")
endif()
message(STATUS "Found gRPC++ via pkg-config. Target: PkgConfig::PC_GRPC")

if(NOT Protobuf_PROTOC_EXECUTABLE)
    message(FATAL_ERROR "protoc not found. find_package(Protobuf) might have failed or protoc is not in PATH.")
endif()
find_program(GRPC_CPP_PLUGIN_EXEC grpc_cpp_plugin)
if(NOT GRPC_CPP_PLUGIN_EXEC)
    message(FATAL_ERROR "grpc_cpp_plugin not found. Ensure protobuf-compiler-grpc is installed or grpc_cpp_plugin is in PATH.")
endif()
message(STATUS "Found protoc: ${Protobuf_PROTOC_EXECUTABLE}")
message(STATUS "Found gRPC C++ Plugin: ${GRPC_CPP_PLUGIN_EXEC}")

# ---=[ 경로 및 파일 이름 설정 ]=---
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

set(PROTO_SOURCE_DIR "${SOURCE_DIR}/protos")
set(LLM_PROTO_BASENAME "llm")
set(TTS_PROTO_BASENAME "tts")
set(LLM_PROTO_FILE "${PROTO_SOURCE_DIR}/${LLM_PROTO_BASENAME}.proto")
set(TTS_PROTO_FILE "${PROTO_SOURCE_DIR}/${TTS_PROTO_BASENAME}.proto")

set(GENERATED_DIR "${BINARY_DIR}/generated_protos")
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(LLM_PROTO_GENERATED_CPP "${GENERATED_DIR}/${LLM_PROTO_BASENAME}.pb.cc")
set(LLM_PROTO_GENERATED_H "${GENERATED_DIR}/${LLM_PROTO_BASENAME}.pb.h")
set(LLM_GRPC_GENERATED_CPP "${GENERATED_DIR}/${LLM_PROTO_BASENAME}.grpc.pb.cc")
set(LLM_GRPC_GENERATED_H "${GENERATED_DIR}/${LLM_PROTO_BASENAME}.grpc.pb.h")

set(TTS_PROTO_GENERATED_CPP "${GENERATED_DIR}/${TTS_PROTO_BASENAME}.pb.cc")
set(TTS_PROTO_GENERATED_H "${GENERATED_DIR}/${TTS_PROTO_BASENAME}.pb.h")
set(TTS_GRPC_GENERATED_CPP "${GENERATED_DIR}/${TTS_PROTO_BASENAME}.grpc.pb.cc")
set(TTS_GRPC_GENERATED_H "${GENERATED_DIR}/${TTS_PROTO_BASENAME}.grpc.pb.h")

set(ALL_GENERATED_SOURCES
    ${LLM_PROTO_GENERATED_CPP} ${LLM_GRPC_GENERATED_CPP}
    ${TTS_PROTO_GENERATED_CPP} ${TTS_GRPC_GENERATED_CPP}
)
set(ALL_GENERATED_HEADERS
    ${LLM_PROTO_GENERATED_H} ${LLM_GRPC_GENERATED_H}
    ${TTS_PROTO_GENERATED_H} ${TTS_GRPC_GENERATED_H}
)

# ---=[ Protobuf/gRPC 코드 생성 (add_custom_command 사용) ]=---
add_custom_command(
    OUTPUT ${ALL_GENERATED_SOURCES} ${ALL_GENERATED_HEADERS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --proto_path=${PROTO_SOURCE_DIR}
            --cpp_out=${GENERATED_DIR}
            --grpc_out=${GENERATED_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXEC}
            ${LLM_PROTO_FILE}
            ${TTS_PROTO_FILE}
    DEPENDS ${LLM_PROTO_FILE} ${TTS_PROTO_FILE}
    COMMENT "Generating Protobuf and gRPC C++ sources for llm_engine"
    VERBATIM
)

add_custom_target(generate_llm_engine_proto_sources ALL
    DEPENDS ${ALL_GENERATED_SOURCES} ${ALL_GENERATED_HEADERS}
)

# FetchContent (nlohmann/json, cpr)
include(FetchContent)
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
  cpr_project
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG 1.10.5
  GIT_SHALLOW TRUE
  CMAKE_ARGS
    -DCPR_BUILD_TESTS=OFF -DCPR_BUILD_EXAMPLES=OFF -DCPR_GENERATE_COVERAGE=OFF
    -DCPR_USE_SYSTEM_GTEST=ON -DCPR_FORCE_USE_SYSTEM_CURL=ON -DCPR_ENABLE_SSL=ON
)
FetchContent_MakeAvailable(cpr_project)

# ---=[ llm_core 라이브러리 빌드 ]=---
add_library(llm_core STATIC
    src/llm_service.cpp
    src/openai_client.cpp
    src/tts_client.cpp
    ${ALL_GENERATED_SOURCES}
)
add_dependencies(llm_core generate_llm_engine_proto_sources)

target_include_directories(llm_core PUBLIC
    "${SOURCE_DIR}/src"
    "${GENERATED_DIR}"
    "${json_SOURCE_DIR}/include"
)

target_link_libraries(llm_core PUBLIC
    Threads::Threads
    PkgConfig::PC_GRPC
    ${Protobuf_LIBRARIES}
    cpr::cpr
)

# ---=[ llm_server 실행 파일 빌드 ]=---
add_executable(llm_server src/main.cpp)
target_link_libraries(llm_server PRIVATE llm_core)

# ---=[ 유닛 테스트 빌드 (BUILD_TESTING 옵션 활성화 시) ]=---
if(SAVED_BUILD_TESTING)
    message(STATUS "Building tests...")
    # Google Test/Mock 찾기 (FetchContent를 사용하거나 시스템 GTest 사용 가능)
    # 여기서는 FetchContent를 사용하여 일관된 버전 관리
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0 # 안정적인 태그 사용 권장
    )
    # 시스템 GTest 대신 FetchContent 버전 사용 강제 (선택 사항)
    # set(gtest_force_fetch ON CACHE BOOL "Force fetching GTest via FetchContent" FORCE)
    FetchContent_MakeAvailable(googletest)
    message(STATUS "Using GTest/GMock from FetchContent")

    enable_testing() # CTest 활성화

    add_executable(unit_tests tests/internal_unit_tests.cpp)

    # unit_tests 타겟에 GTest, GMock, GTest_Main 라이브러리 링크
    target_link_libraries(unit_tests PRIVATE
        llm_core
        GTest::gtest      # Google Test 라이브러리
        GTest::gmock      # Google Mock 라이브러리 (추가됨)
        GTest::gtest_main # Google Test main 함수 포함 라이브러리
    )
    include(GoogleTest) # CTest와 Google Test 연동을 위한 모듈 포함
    gtest_discover_tests(unit_tests) # 테스트 자동 발견
endif()

# ---=[ 완료 메시지 ]=---
message(STATUS "CMake configuration complete for llm_engine.")
message(STATUS "Build tests is set to: ${BUILD_TESTING}")
message(STATUS "Protobuf libraries: ${Protobuf_LIBRARIES}")