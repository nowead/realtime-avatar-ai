# CMakeLists.txt for llm_engine (with Unit Test Support)

# 최소 CMake 버전 설정
cmake_minimum_required(VERSION 3.13) # FetchContent, GTest requires recent versions

# 프로젝트 이름 및 버전 설정
project(llm_engine VERSION 1.0 LANGUAGES CXX)

# C++ 표준 설정
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---=[ 의존성 찾기 및 관리 ]=---
include(FetchContent)

# 필수 시스템 패키지 찾기
find_package(Protobuf REQUIRED)
message(STATUS "Protobuf Include Dirs: ${Protobuf_INCLUDE_DIRS}")
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# gRPC++ 라이브러리 정보 조회
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)
if(NOT GRPC_FOUND)
    message(FATAL_ERROR "gRPC++ library not found using pkg-config.")
endif()
message(STATUS "Found gRPC++ using pkg-config for linking.")

# protoc 및 gRPC 플러그인 실행 파일 찾기
find_program(Protobuf_PROTOC_EXECUTABLE protoc)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
if(NOT Protobuf_PROTOC_EXECUTABLE OR NOT GRPC_CPP_PLUGIN)
    message(FATAL_ERROR "protoc or grpc_cpp_plugin not found.")
endif()
message(STATUS "Found protoc: ${Protobuf_PROTOC_EXECUTABLE}")
message(STATUS "Found gRPC C++ Plugin: ${GRPC_CPP_PLUGIN}")

# nlohmann_json (JSON 라이브러리) FetchContent 사용
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)
message(STATUS "Using nlohmann_json from FetchContent")

# cpr (HTTP 클라이언트 라이브러리) FetchContent 사용
FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.10.5
)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CPR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CPR_FORCE_USE_SYSTEM_CURL OFF CACHE BOOL "" FORCE)
set(CPR_ENABLE_SSL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cpr)
message(STATUS "Using cpr from FetchContent")

# ---=[ 경로 설정 ]=---
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(PROTO_SOURCE_DIR "${SOURCE_DIR}/protos")
set(LLM_PROTO_BASENAME "llm")
set(TTS_PROTO_BASENAME "tts")
set(LLM_PROTO_FILE "${PROTO_SOURCE_DIR}/${LLM_PROTO_BASENAME}.proto")
set(TTS_PROTO_FILE "${PROTO_SOURCE_DIR}/${TTS_PROTO_BASENAME}.proto")
set(GENERATED_DIR "${BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${GENERATED_DIR})

# 생성될 파일 경로 정의... (이전과 동일)
set(LLM_PROTO_GENERATED_CPP "${GENERATED_DIR}/${LLM_PROTO_BASENAME}.pb.cc")
# ... (다른 생성 파일 경로) ...
set(TTS_PROTO_GENERATED_H "${GENERATED_DIR}/${TTS_PROTO_BASENAME}.grpc.pb.h")

set(ALL_GENERATED_SOURCES ...) # 이전과 동일
set(ALL_GENERATED_HEADERS ...) # 이전과 동일

# ---=[ Protobuf/gRPC 코드 생성 ]=---
add_custom_command( # 이전과 동일
    OUTPUT ${ALL_GENERATED_SOURCES} ${ALL_GENERATED_HEADERS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE} ...
    ${LLM_PROTO_FILE} ${TTS_PROTO_FILE}
    DEPENDS ${LLM_PROTO_FILE} ${TTS_PROTO_FILE} ...
)
add_custom_target(generate_proto_sources ALL # 이전과 동일
    DEPENDS ${ALL_GENERATED_SOURCES} ${ALL_GENERATED_HEADERS}
)

# ---=[ 메인 실행 파일 빌드 ]=---
add_executable(llm_server
    "${SOURCE_DIR}/src/main.cpp"
    "${SOURCE_DIR}/src/llm_service.cpp"
    "${SOURCE_DIR}/src/tts_client.cpp"
    "${SOURCE_DIR}/src/openai_client.cpp"
    ${ALL_GENERATED_SOURCES}
)
add_dependencies(llm_server generate_proto_sources)

target_include_directories(llm_server PRIVATE # 이전과 동일
    "${SOURCE_DIR}/src"
    "${GENERATED_DIR}"
    ${Protobuf_INCLUDE_DIRS}
    ${json_SOURCE_DIR}/include
    ${cpr_SOURCE_DIR}/include
)

target_link_libraries(llm_server PRIVATE # 이전과 동일
    Threads::Threads
    PkgConfig::GRPC
    ${Protobuf_LIBRARIES}
    nlohmann_json::nlohmann_json
    cpr::cpr
)

# ---=[ 유닛 테스트 빌드 설정 ( 조건부 ) ]=---
option(BUILD_TESTING "Build unit tests" ON) # 테스트 빌드 옵션

if(BUILD_TESTING)
    message(STATUS "Unit testing enabled.")

    # Google Test 찾기 (FetchContent 또는 find_package 사용 가능)
    # 여기서는 find_package 사용 (시스템에 설치된 gtest-dev 필요 - Dockerfile에서 설치함)
    find_package(GTest REQUIRED)
    if(NOT GTest_FOUND)
        message(FATAL_ERROR "Google Test not found. Please install libgtest-dev.")
    endif()
    message(STATUS "Found Google Test: ${GTEST_INCLUDE_DIRS}")

    # CTest 모듈 포함
    include(CTest)
    enable_testing()

    # 테스트 실행 파일 정의
    add_executable(unit_tests
        "${SOURCE_DIR}/tests/internal_unit_tests.cpp"
        # 테스트 대상 소스 파일 포함 방법:
        # 방법 1: 필요한 소스 파일 직접 추가 (중복 빌드 발생 가능성)
        # "${SOURCE_DIR}/src/tts_client.cpp"
        # "${SOURCE_DIR}/src/openai_client.cpp"
        # ...
        # 방법 2: 핵심 로직을 별도 라이브러리로 만들고 링크 (더 좋음)
        # add_library(llm_core STATIC src/llm_service.cpp src/tts_client.cpp ...)
        # target_link_libraries(unit_tests PRIVATE llm_core)
        # target_link_libraries(llm_server PRIVATE llm_core)

        # 프로토콜 생성 파일은 포함해야 함 (gRPC 클라이언트/메시지 등 사용 시)
        ${ALL_GENERATED_SOURCES}
    )

    # 테스트 실행 파일에 필요한 include 경로 설정
    target_include_directories(unit_tests PRIVATE
        "${SOURCE_DIR}/src"
        "${GENERATED_DIR}"
        ${Protobuf_INCLUDE_DIRS}
        ${json_SOURCE_DIR}/include
        ${cpr_SOURCE_DIR}/include
        ${GTEST_INCLUDE_DIRS} # Google Test 헤더 경로
    )

    # 테스트 실행 파일에 필요한 라이브러리 링크
    target_link_libraries(unit_tests PRIVATE
        Threads::Threads
        PkgConfig::GRPC             # gRPC
        ${Protobuf_LIBRARIES}       # Protobuf
        nlohmann_json::nlohmann_json # JSON
        cpr::cpr                    # CPR (HTTP Client)
        GTest::gtest                # Google Test (main)
        GTest::gmock                # Google Mock (선택 사항)
    )

    # CTest에 테스트 추가
    # add_test(NAME llm_unit_tests COMMAND unit_tests) # CTest 사용 시 활성화

    # unit_tests 타겟이 프로토콜 생성에 의존하도록 설정
    add_dependencies(unit_tests generate_proto_sources)

endif()

# ---=[ 완료 메시지 ]=---
message(STATUS "CMake configuration complete for llm_engine.")