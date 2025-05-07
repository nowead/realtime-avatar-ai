# CMakeLists.txt for stt_service (with optional testing)

# 최소 CMake 버전 설정
cmake_minimum_required(VERSION 3.13)

# 프로젝트 이름 및 버전 설정
project(stt_service VERSION 1.0 LANGUAGES CXX)

# C++ 표준 설정 (예: C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---=[ 빌드 옵션 ]=---
option(BUILD_TESTING "Build the unit tests" OFF) # 테스트 빌드 여부 옵션 (기본값 OFF)

# ---=[ 의존성 찾기 (공통) ]=---

# 필수 패키지 찾기
find_package(Protobuf REQUIRED)
message(STATUS "Protobuf Include Dirs: ${Protobuf_INCLUDE_DIRS}")
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# pkg-config를 사용하여 gRPC++ 라이브러리 정보 조회
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)
if(NOT GRPC_FOUND)
    message(FATAL_ERROR "gRPC++ library not found using pkg-config.")
endif()
message(STATUS "Found gRPC++ using pkg-config.")

# 코드 생성을 위한 protoc 및 gRPC 플러그인 실행 파일 찾기
find_program(Protobuf_PROTOC_EXECUTABLE protoc)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
if(NOT Protobuf_PROTOC_EXECUTABLE OR NOT GRPC_CPP_PLUGIN)
    message(FATAL_ERROR "protoc or grpc_cpp_plugin not found.")
endif()
message(STATUS "Found protoc: ${Protobuf_PROTOC_EXECUTABLE}")
message(STATUS "Found gRPC C++ Plugin: ${GRPC_CPP_PLUGIN}")

# ---=[ 경로 설정 (공통) ]=---

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}) # /app
set(BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}) # /app/build

# .proto 파일 경로 및 이름 정의
set(PROTO_SOURCE_DIR "${SOURCE_DIR}/protos") # /app/proto
set(STT_PROTO_BASENAME "stt")
set(LLM_PROTO_BASENAME "llm")
set(STT_PROTO_FILE "${PROTO_SOURCE_DIR}/${STT_PROTO_BASENAME}.proto")
set(LLM_PROTO_FILE "${PROTO_SOURCE_DIR}/${LLM_PROTO_BASENAME}.proto")

# 생성된 코드 저장 위치
set(GENERATED_DIR "${BINARY_DIR}/generated") # /app/build/generated
file(MAKE_DIRECTORY ${GENERATED_DIR})

# 생성될 파일들의 전체 경로 정의
set(STT_PROTO_GENERATED_CPP "${GENERATED_DIR}/${STT_PROTO_BASENAME}.pb.cc")
set(STT_PROTO_GENERATED_H "${GENERATED_DIR}/${STT_PROTO_BASENAME}.pb.h")
set(STT_GRPC_GENERATED_CPP "${GENERATED_DIR}/${STT_PROTO_BASENAME}.grpc.pb.cc")
set(STT_GRPC_GENERATED_H "${GENERATED_DIR}/${STT_PROTO_BASENAME}.grpc.pb.h")

set(LLM_PROTO_GENERATED_CPP "${GENERATED_DIR}/${LLM_PROTO_BASENAME}.pb.cc")
set(LLM_PROTO_GENERATED_H "${GENERATED_DIR}/${LLM_PROTO_BASENAME}.pb.h")
set(LLM_GRPC_GENERATED_CPP "${GENERATED_DIR}/${LLM_PROTO_BASENAME}.grpc.pb.cc")
set(LLM_GRPC_GENERATED_H "${GENERATED_DIR}/${LLM_PROTO_BASENAME}.grpc.pb.h")

set(ALL_GENERATED_SOURCES
    ${STT_PROTO_GENERATED_CPP} ${STT_GRPC_GENERATED_CPP}
    ${LLM_PROTO_GENERATED_CPP} ${LLM_GRPC_GENERATED_CPP}
)
set(ALL_GENERATED_HEADERS
    ${STT_PROTO_GENERATED_H} ${STT_GRPC_GENERATED_H}
    ${LLM_PROTO_GENERATED_H} ${LLM_GRPC_GENERATED_H}
)

# Azure Speech SDK 경로 설정 (아키텍처 자동 감지 개선)
set(SPEECHSDK_ROOT "$ENV{SPEECHSDK_ROOT}" CACHE PATH "Root directory of the Azure Speech SDK")
if(NOT SPEECHSDK_ROOT OR NOT EXISTS "${SPEECHSDK_ROOT}/include/cxx_api/speechapi_cxx.h")
    message(FATAL_ERROR "Azure Speech SDK not found or SPEECHSDK_ROOT is not set correctly.")
endif()
message(STATUS "Using Azure Speech SDK from: ${SPEECHSDK_ROOT}")
set(SPEECHSDK_CXX_INCLUDE_DIR "${SPEECHSDK_ROOT}/include/cxx_api")
set(SPEECHSDK_C_INCLUDE_DIR "${SPEECHSDK_ROOT}/include/c_api")
if(${TARGET_ARCH} STREQUAL "arm64")
    set(SPEECHSDK_LIBRARY_DIR "${SPEECHSDK_ROOT}/lib/arm64") # Assuming arm64 Linux
elseif(${TARGET_ARCH} STREQUAL "x64" OR ${TARGET_ARCH} STREQUAL "x86_64")
    set(SPEECHSDK_LIBRARY_DIR "${SPEECHSDK_ROOT}/lib/x64") # Assuming x64 Linux
else()
    message(WARNING "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}. Assuming x64.")
    set(SPEECHSDK_LIBRARY_DIR "${SPEECHSDK_ROOT}/lib/x64")
endif()
message(STATUS "Using Speech SDK Library Dir: ${SPEECHSDK_LIBRARY_DIR}")

# ---=[ Protobuf/gRPC 코드 생성 (공통) ]=---

add_custom_command(
    OUTPUT ${ALL_GENERATED_SOURCES} ${ALL_GENERATED_HEADERS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --proto_path=${PROTO_SOURCE_DIR}
            --cpp_out=${GENERATED_DIR}
            --grpc_out=${GENERATED_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
            ${STT_PROTO_FILE}
            ${LLM_PROTO_FILE}
    DEPENDS ${STT_PROTO_FILE} ${LLM_PROTO_FILE}
    COMMENT "Generating Protobuf and gRPC C++ sources from .proto files"
    VERBATIM
)

# 생성된 파일들에 의존하는 Custom Target 정의 (모든 타겟이 사용 가능하도록)
add_custom_target(generate_proto_sources ALL
    DEPENDS ${ALL_GENERATED_SOURCES} ${ALL_GENERATED_HEADERS}
)

# ---=[ 공통 핵심 로직 라이브러리 빌드 ]=---
add_library(stt_core STATIC # 또는 SHARED
    "${SOURCE_DIR}/src/stt_service.cpp"
    "${SOURCE_DIR}/src/azure_stt_client.cpp"
    "${SOURCE_DIR}/src/llm_engine_client.cpp"
    ${ALL_GENERATED_SOURCES} # 생성된 코드 포함
)

# stt_core 타겟이 코드 생성 타겟에 의존하도록 설정
add_dependencies(stt_core generate_proto_sources)

# stt_core 라이브러리에 필요한 include 디렉토리 설정
# PUBLIC: 이 라이브러리를 링크하는 타겟들도 이 include 경로를 사용함
target_include_directories(stt_core PUBLIC
    "${SOURCE_DIR}/src"            # 소스 헤더
    "${GENERATED_DIR}"             # 생성된 proto 헤더
    ${SPEECHSDK_CXX_INCLUDE_DIR}
    ${SPEECHSDK_C_INCLUDE_DIR}
    ${Protobuf_INCLUDE_DIRS}
    # gRPC includes는 PkgConfig::GRPC 링크 시 자동으로 처리될 수 있음
)

# stt_core 라이브러리에 필요한 라이브러리 링크
# INTERFACE 또는 PUBLIC: 이 라이브러리를 링크하는 타겟들도 이 라이브러리들을 링크해야 함
target_link_libraries(stt_core PUBLIC
    Threads::Threads
    PkgConfig::GRPC             # gRPC 라이브러리
    ${Protobuf_LIBRARIES}       # Protobuf 라이브러리
    "${SPEECHSDK_LIBRARY_DIR}/libMicrosoft.CognitiveServices.Speech.core.so" # Azure SDK 코어 라이브러리
)


# ---=[ 메인 실행 파일 빌드 ]=---
add_executable(stt_server
    "${SOURCE_DIR}/src/main.cpp"
)

# 메인 실행 파일을 stt_core 라이브러리에 링크
target_link_libraries(stt_server PRIVATE stt_core)


# ---=[ 유닛 테스트 빌드 (BUILD_TESTING 옵션 활성화 시) ]=---
if(BUILD_TESTING)
    message(STATUS "Building tests...")

    # Google Test 찾기
    find_package(GTest REQUIRED)
    message(STATUS "Found GTest: ${GTEST_LIBRARIES}") # 최신 CMake는 GTest::gtest 사용

    # CTest 활성화
    enable_testing()

    # 유닛 테스트 실행 파일 타겟 정의
    add_executable(unit_tests
        "${SOURCE_DIR}/tests/internal_unit_tests.cpp" # 실제 테스트 파일 경로
        # 공통 소스 코드는 stt_core 라이브러리에서 가져오므로 여기에 추가하지 않음
    )

    # 유닛 테스트를 stt_core 라이브러리와 GTest 라이브러리에 링크
    target_link_libraries(unit_tests PRIVATE
        stt_core        # 핵심 로직 라이브러리 링크
        GTest::gtest      # Google Test 라이브러리
        GTest::gtest_main # Google Test main 함수 포함 라이브러리
    )

    # CTest와 Google Test 연동
    include(GoogleTest)
    gtest_discover_tests(unit_tests)

endif() # BUILD_TESTING

# ---=[ 완료 메시지 ]=---
message(STATUS "CMake configuration complete for stt_service.")
message(STATUS "Build tests is set to: ${BUILD_TESTING}")