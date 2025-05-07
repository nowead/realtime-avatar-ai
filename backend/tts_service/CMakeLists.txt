# CMakeLists.txt for tts_service (with optional testing)

# 최소 CMake 버전 설정
cmake_minimum_required(VERSION 3.13) # 이전 로그에서 3.20 사용, 호환성을 위해 3.13으로 유지하거나 필요시 상향

# 프로젝트 이름 및 버전 설정
project(tts_service VERSION 1.0 LANGUAGES CXX)

# C++ 표준 설정 (예: C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---=[ 빌드 옵션 ]=---
# Dockerfile에서 BUILD_TESTING_ARG로 전달되거나, 여기서 직접 ON/OFF 가능
option(BUILD_TESTING "Build the unit tests" OFF)

# ---=[ 의존성 찾기 (공통) ]=---

# 필수 패키지 찾기
find_package(Protobuf REQUIRED)
message(STATUS "Protobuf Include Dirs: ${Protobuf_INCLUDE_DIRS}")
find_package(PkgConfig REQUIRED) # gRPC 및 기타 라이브러리 검색에 사용
find_package(Threads REQUIRED)   # 스레드 지원

# pkg-config를 사용하여 gRPC++ 라이브러리 정보 조회
# 이전 CMakeLists.txt에서는 gRPC CONFIG 모드 실패 시 PkgConfig 사용
# 여기서는 PkgConfig를 우선적으로 사용하거나, 이전처럼 CONFIG 모드 먼저 시도 후 PkgConfig 사용 가능
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)
if(NOT GRPC_FOUND)
    message(FATAL_ERROR "gRPC++ library not found using pkg-config.")
endif()
message(STATUS "Found gRPC++ using pkg-config (via PkgConfig::GRPC).")

# 코드 생성을 위한 protoc 및 gRPC 플러그인 실행 파일 찾기
# find_package(Protobuf)가 Protobuf_PROTOC_EXECUTABLE을 설정해야 함
if(NOT Protobuf_PROTOC_EXECUTABLE)
    find_program(Protobuf_PROTOC_EXECUTABLE protoc) # 만약을 위해 한번 더 검색
endif()

find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin HINTS /usr/bin /usr/local/bin) # 경로 힌트 추가
if(NOT Protobuf_PROTOC_EXECUTABLE)
    message(FATAL_ERROR "protoc not found. Please ensure it is in your PATH or set Protobuf_PROTOC_EXECUTABLE.")
endif()
if(NOT GRPC_CPP_PLUGIN_EXECUTABLE)
    message(FATAL_ERROR "grpc_cpp_plugin not found. Please ensure it is in your PATH.")
endif()
message(STATUS "Using protoc: ${Protobuf_PROTOC_EXECUTABLE}")
message(STATUS "Using gRPC C++ Plugin: ${GRPC_CPP_PLUGIN_EXECUTABLE}")

# ---=[ 경로 설정 (공통) ]=---

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}) # /app (Dockerfile 컨텍스트 기준)
set(BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}) # /app/build (Dockerfile 컨텍스트 기준)

# .proto 파일 경로 및 이름 정의 (tts_service에 맞게 수정)
set(PROTO_SOURCE_DIR "${SOURCE_DIR}/protos")
set(TTS_PROTO_BASENAME "tts")
set(AVATAR_SYNC_PROTO_BASENAME "avatar_sync") # tts_service는 avatar_sync.proto도 사용

set(TTS_PROTO_FILE "${PROTO_SOURCE_DIR}/${TTS_PROTO_BASENAME}.proto")
set(AVATAR_SYNC_PROTO_FILE "${PROTO_SOURCE_DIR}/${AVATAR_SYNC_PROTO_BASENAME}.proto")

# 생성된 코드 저장 위치
set(GENERATED_DIR "${BINARY_DIR}/generated_proto_files") # 디렉토리 이름 변경 (선택)
file(MAKE_DIRECTORY ${GENERATED_DIR})

# 생성될 파일들의 전체 경로 정의
set(TTS_PROTO_GENERATED_CPP "${GENERATED_DIR}/${TTS_PROTO_BASENAME}.pb.cc")
set(TTS_PROTO_GENERATED_H "${GENERATED_DIR}/${TTS_PROTO_BASENAME}.pb.h")
set(TTS_GRPC_GENERATED_CPP "${GENERATED_DIR}/${TTS_PROTO_BASENAME}.grpc.pb.cc")
set(TTS_GRPC_GENERATED_H "${GENERATED_DIR}/${TTS_PROTO_BASENAME}.grpc.pb.h")

set(AVATAR_SYNC_PROTO_GENERATED_CPP "${GENERATED_DIR}/${AVATAR_SYNC_PROTO_BASENAME}.pb.cc")
set(AVATAR_SYNC_PROTO_GENERATED_H "${GENERATED_DIR}/${AVATAR_SYNC_PROTO_BASENAME}.pb.h")
set(AVATAR_SYNC_GRPC_GENERATED_CPP "${GENERATED_DIR}/${AVATAR_SYNC_PROTO_BASENAME}.grpc.pb.cc")
set(AVATAR_SYNC_GRPC_GENERATED_H "${GENERATED_DIR}/${AVATAR_SYNC_PROTO_BASENAME}.grpc.pb.h")

set(ALL_PROTO_FILES ${TTS_PROTO_FILE} ${AVATAR_SYNC_PROTO_FILE})

set(ALL_GENERATED_SOURCES
    ${TTS_PROTO_GENERATED_CPP} ${TTS_GRPC_GENERATED_CPP}
    ${AVATAR_SYNC_PROTO_GENERATED_CPP} ${AVATAR_SYNC_GRPC_GENERATED_CPP}
)
set(ALL_GENERATED_HEADERS
    ${TTS_PROTO_GENERATED_H} ${TTS_GRPC_GENERATED_H}
    ${AVATAR_SYNC_PROTO_GENERATED_H} ${AVATAR_SYNC_GRPC_GENERATED_H}
)

# --- Azure Speech SDK 설정 ---
# (기존 tts_service의 CMakeLists.txt 내용과 유사하게 설정)
set(SPEECHSDK_ROOT "$ENV{SPEECHSDK_ROOT}" CACHE PATH "Root directory of the Azure Speech SDK")
if(NOT SPEECHSDK_ROOT OR NOT EXISTS "${SPEECHSDK_ROOT}/include/cxx_api/speechapi_cxx_speech_synthesizer.h")
    message(FATAL_ERROR "Azure Speech SDK for TTS not found or SPEECHSDK_ROOT is not set correctly. Expected header: speechapi_cxx_speech_synthesizer.h")
endif()
message(STATUS "Using Azure Speech SDK (for TTS) from: ${SPEECHSDK_ROOT}")
set(SPEECHSDK_CXX_INCLUDE_DIR "${SPEECHSDK_ROOT}/include/cxx_api")
set(SPEECHSDK_C_INCLUDE_DIR "${SPEECHSDK_ROOT}/include/c_api")

# TARGET_ARCH 변수는 CMake 외부에서 (예: Docker 빌드 인자) 전달되어야 함
if(NOT DEFINED TARGET_ARCH)
    message(WARNING "TARGET_ARCH is not defined. Assuming x64 for Speech SDK.")
    set(TARGET_ARCH "x64") # 기본값 설정 또는 오류 처리
endif()

if(${TARGET_ARCH} STREQUAL "arm64")
    set(SPEECHSDK_LIBRARY_DIR "${SPEECHSDK_ROOT}/lib/arm64")
elseif(${TARGET_ARCH} STREQUAL "x64" OR ${TARGET_ARCH} STREQUAL "x86_64")
    set(SPEECHSDK_LIBRARY_DIR "${SPEECHSDK_ROOT}/lib/x64")
else()
    message(WARNING "Unsupported architecture for Speech SDK: ${TARGET_ARCH}. Assuming x64.")
    set(SPEECHSDK_LIBRARY_DIR "${SPEECHSDK_ROOT}/lib/x64")
endif()
message(STATUS "Using Speech SDK Library Dir: ${SPEECHSDK_LIBRARY_DIR}")
set(SPEECHSDK_LIBRARY_CORE "${SPEECHSDK_LIBRARY_DIR}/libMicrosoft.CognitiveServices.Speech.core.so")
if(NOT EXISTS ${SPEECHSDK_LIBRARY_CORE})
    message(FATAL_ERROR "Azure Speech SDK core library not found at: ${SPEECHSDK_LIBRARY_CORE}")
endif()

# ---=[ Protobuf/gRPC 코드 생성 (add_custom_command 사용) ]=---

# 각 proto 파일에 대해 개별적으로 custom command를 정의하거나,
# 모든 proto 파일을 한 번의 protoc 호출로 처리할 수 있습니다.
# 여기서는 모든 파일을 한 번에 처리합니다.
add_custom_command(
    OUTPUT ${ALL_GENERATED_SOURCES} ${ALL_GENERATED_HEADERS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --proto_path=${PROTO_SOURCE_DIR}
            --cpp_out=${GENERATED_DIR}
            --grpc_out=${GENERATED_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
            ${ALL_PROTO_FILES} # 모든 .proto 파일 목록
    DEPENDS ${ALL_PROTO_FILES} # 모든 .proto 파일에 대한 의존성
    COMMENT "Generating Protobuf and gRPC C++ sources from .proto files"
    VERBATIM # Windows에서 명령어 줄 문제 방지
)

# 생성된 파일들에 의존하는 Custom Target 정의 (빌드 순서 보장)
# 이 타겟은 다른 타겟이 DEPENDS로 참조하거나, add_dependencies로 연결될 수 있습니다.
add_custom_target(generate_proto_code ALL # 'ALL' 키워드로 항상 실행되도록 하거나, 필요에 따라 제거
    DEPENDS ${ALL_GENERATED_SOURCES} ${ALL_GENERATED_HEADERS}
    COMMENT "Ensuring Protobuf/gRPC code is generated"
)

# ---=[ 핵심 로직 라이브러리 빌드 (tts_core) ]=---
add_library(tts_core STATIC # 또는 SHARED
    # tts_service의 소스 파일들
    "${SOURCE_DIR}/src/tts_service.cpp"
    "${SOURCE_DIR}/src/avatar_sync_client.cpp"
    "${SOURCE_DIR}/src/azure_tts_engine.cpp"
    # 생성된 Protobuf/gRPC 소스 파일들
    ${ALL_GENERATED_SOURCES}
)

# tts_core 타겟이 코드 생성 완료에 의존하도록 설정
add_dependencies(tts_core generate_proto_code)

target_include_directories(tts_core PUBLIC
    "${SOURCE_DIR}/src"            # 사용자 정의 헤더 파일 경로
    "${GENERATED_DIR}"             # 생성된 Protobuf/gRPC 헤더 파일 경로
    ${Protobuf_INCLUDE_DIRS}       # Protobuf 헤더 파일 경로
    # gRPC 헤더는 PkgConfig::GRPC 통해 자동으로 포함될 수 있음 (pkg_check_modules IMPORTED_TARGET 사용 시)
    # 명시적으로 추가하려면: ${GRPC_INCLUDE_DIRS} (pkg_check_modules에서 설정된 변수 사용)
    ${SPEECHSDK_CXX_INCLUDE_DIR}
    ${SPEECHSDK_C_INCLUDE_DIR}
)

target_link_libraries(tts_core PUBLIC
    Threads::Threads
    PkgConfig::GRPC             # gRPC 라이브러리 (링크 및 헤더 경로 포함)
    ${Protobuf_LIBRARIES}       # Protobuf 라이브러리
    ${SPEECHSDK_LIBRARY_CORE}   # Azure Speech SDK 코어 라이브러리
)

# Speech SDK가 추가로 필요로 하는 시스템 라이브러리 (Linux 기준)
if(UNIX AND NOT APPLE)
    target_link_libraries(tts_core PUBLIC asound ssl crypto)
endif()


# ---=[ 메인 실행 파일 빌드 (tts_server) ]=---
add_executable(tts_server
    "${SOURCE_DIR}/src/main.cpp"
)
target_link_libraries(tts_server PRIVATE tts_core) # tts_core의 PUBLIC 의존성 상속


# ---=[ 유닛 테스트 빌드 (BUILD_TESTING 옵션 활성화 시) ]=---
if(BUILD_TESTING)
    message(STATUS "Building internal unit tests for tts_service...")

    find_package(GTest REQUIRED) # Google Test 찾기
    enable_testing()             # CTest 활성화

    add_executable(unit_tests
        "${SOURCE_DIR}/tests/internal_unit_tests.cpp"
        # 생성된 코드를 사용하는 테스트라면, 해당 헤더를 include 할 수 있도록
        # target_include_directories에 "${GENERATED_DIR}" 추가 필요
    )

    # 유닛 테스트 타겟에 생성된 proto 헤더 경로 추가
    target_include_directories(unit_tests PRIVATE
        "${GENERATED_DIR}"
    )

    target_link_libraries(unit_tests PRIVATE
        tts_core        # 테스트 대상 라이브러리
        GTest::gtest
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(unit_tests) # 테스트 자동 발견
endif()

# ---=[ 완료 메시지 ]=---
message(STATUS "CMake configuration complete for tts_service.")
message(STATUS "Build tests (BUILD_TESTING) is set to: ${BUILD_TESTING}")