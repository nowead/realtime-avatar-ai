cmake_minimum_required(VERSION 3.16)
project(WebSocketGateway CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # 정적/공유 라이브러리 빌드 시 권장

# ---=[ 빌드 옵션 ]=---
option(BUILD_TESTING "Build unit tests" OFF)
message(STATUS "BUILD_TESTING is set to: ${BUILD_TESTING}")

# ---=[ 경로 설정 ]=---
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(PROTO_SOURCE_DIR "${SOURCE_DIR}/protos")
set(GENERATED_DIR "${BINARY_DIR}/generated_protos")
file(MAKE_DIRECTORY ${GENERATED_DIR})

# ---=[ 의존성 찾기 ]=---
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED) # uSockets SSL, uWebSockets SSL 지원에 필요
find_package(Protobuf REQUIRED)
message(STATUS "Protobuf version: ${Protobuf_VERSION}")
message(STATUS "Protobuf libraries: ${Protobuf_LIBRARIES}")
message(STATUS "Protobuf include_dirs: ${Protobuf_INCLUDE_DIRS}")

pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)
if(NOT GRPC_FOUND)
  message(FATAL_ERROR "gRPC++ library not found using pkg-config (grpc++).")
endif()
message(STATUS "Found gRPC++ using pkg-config (target PkgConfig::GRPC).")

# 코드 생성을 위한 protoc 및 gRPC 플러그인 실행 파일 찾기
if(NOT Protobuf_PROTOC_EXECUTABLE) # find_package(Protobuf)가 설정하기도 함
    find_program(Protobuf_PROTOC_EXECUTABLE protoc)
endif()
find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin HINTS /usr/bin /usr/local/bin)

if(NOT Protobuf_PROTOC_EXECUTABLE)
  message(FATAL_ERROR "protoc compiler not found.")
endif()
if(NOT GRPC_CPP_PLUGIN_EXECUTABLE)
  message(FATAL_ERROR "grpc_cpp_plugin not found.")
endif()
message(STATUS "Using protoc: ${Protobuf_PROTOC_EXECUTABLE}")
message(STATUS "Using gRPC C++ Plugin: ${GRPC_CPP_PLUGIN_EXECUTABLE}")

# --- nlohmann/json (헤더 전용 라이브러리, FetchContent 사용) ---
include(FetchContent)
FetchContent_Declare(
  nlohmann_json_repo
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3 # 필요에 따라 버전 업데이트
)
FetchContent_MakeAvailable(nlohmann_json_repo) # target: nlohmann_json::nlohmann_json

# --- uWebSockets / uSockets (미리 빌드된 라이브러리 및 헤더 사용) ---
# Dockerfile에서 /usr/local/include 와 /usr/local/lib 에 설치됨
find_path(UWEBSOCKETS_INCLUDE_DIR
  NAMES App.h # uWebSockets 헤더 파일 중 하나
  PATHS /usr/local/include/uWebSockets /usr/include/uWebSockets # 시스템 경로도 확인
  DOC "Path to uWebSockets include directory"
)
# uSockets 헤더 경로도 찾아 변수로 설정
find_path(USOCKETS_INCLUDE_DIR
  NAMES libusockets.h # uSockets 헤더 파일 중 하나
  PATHS /usr/local/include/uSockets /usr/include/uSockets # Dockerfile에서 복사한 경로
  DOC "Path to uSockets include directory"
)
find_library(USOCKETS_LIB
  NAMES uSockets # Dockerfile에서 libuSockets.a로 복사함
  PATHS /usr/local/lib /usr/lib
  DOC "Path to uSockets static library (libuSockets.a)"
)

if(NOT UWEBSOCKETS_INCLUDE_DIR)
  message(FATAL_ERROR "uWebSockets headers not found. Searched in /usr/local/include/uWebSockets and /usr/include/uWebSockets.")
endif()
if(NOT USOCKETS_INCLUDE_DIR) # 추가된 uSockets 헤더 경로 검사
  message(FATAL_ERROR "uSockets headers (libusockets.h) not found. Searched in /usr/local/include/uSockets and /usr/include/uSockets.")
endif()
if(NOT USOCKETS_LIB)
  message(FATAL_ERROR "uSockets library (libuSockets.a) not found. Searched in /usr/local/lib and /usr/lib.")
endif()
message(STATUS "Found uWebSockets includes at: ${UWEBSOCKETS_INCLUDE_DIR}")
message(STATUS "Found uSockets includes at: ${USOCKETS_INCLUDE_DIR}") # 추가된 메시지
message(STATUS "Found uSockets library at: ${USOCKETS_LIB}")


# ---=[ Protobuf/gRPC 코드 생성 ]=---
set(STT_PROTO_FILE "${PROTO_SOURCE_DIR}/stt.proto")
set(AVATAR_SYNC_PROTO_FILE "${PROTO_SOURCE_DIR}/avatar_sync.proto")
set(ALL_PROTO_FILES ${STT_PROTO_FILE} ${AVATAR_SYNC_PROTO_FILE})

# 생성될 파일 목록 정의
set(STT_PB_H "${GENERATED_DIR}/stt.pb.h")
set(STT_PB_CC "${GENERATED_DIR}/stt.pb.cc")
set(STT_GRPC_PB_H "${GENERATED_DIR}/stt.grpc.pb.h")
set(STT_GRPC_PB_CC "${GENERATED_DIR}/stt.grpc.pb.cc")
set(AVATAR_SYNC_PB_H "${GENERATED_DIR}/avatar_sync.pb.h")
set(AVATAR_SYNC_PB_CC "${GENERATED_DIR}/avatar_sync.pb.cc")
set(AVATAR_SYNC_GRPC_PB_H "${GENERATED_DIR}/avatar_sync.grpc.pb.h")
set(AVATAR_SYNC_GRPC_PB_CC "${GENERATED_DIR}/avatar_sync.grpc.pb.cc")

set(ALL_GENERATED_SOURCES
    ${STT_PB_CC} ${STT_GRPC_PB_CC}
    ${AVATAR_SYNC_PB_CC} ${AVATAR_SYNC_GRPC_PB_CC}
)
set(ALL_GENERATED_HEADERS
    ${STT_PB_H} ${STT_GRPC_PB_H}
    ${AVATAR_SYNC_PB_H} ${AVATAR_SYNC_GRPC_PB_H}
)

add_custom_command(
  OUTPUT ${ALL_GENERATED_SOURCES} ${ALL_GENERATED_HEADERS}
  COMMAND ${Protobuf_PROTOC_EXECUTABLE}
          --proto_path=${PROTO_SOURCE_DIR}
          --cpp_out=${GENERATED_DIR}
          --grpc_out=${GENERATED_DIR}
          --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
          ${ALL_PROTO_FILES}
  DEPENDS ${ALL_PROTO_FILES}
  COMMENT "Generating C++ sources and headers from .proto files"
  VERBATIM
)

add_custom_target(generate_proto_code ALL
  DEPENDS ${ALL_GENERATED_SOURCES} ${ALL_GENERATED_HEADERS}
  COMMENT "Ensuring Protobuf/gRPC C++ code is generated"
)

# ---=[ 핵심 로직 라이브러리 (gateway_core) ]=---
set(GATEWAY_CORE_SOURCES
  "${SOURCE_DIR}/src/stt_client.cpp"
  "${SOURCE_DIR}/src/websocket_server.cpp"
  "${SOURCE_DIR}/src/avatar_sync_service_impl.cpp"
  ${ALL_GENERATED_SOURCES} # 생성된 proto 소스도 라이브러리에 포함
)

add_library(gateway_core STATIC
  ${GATEWAY_CORE_SOURCES}
)

# gateway_core가 프로토콜 코드 생성에 의존하도록 설정
add_dependencies(gateway_core generate_proto_code)

target_include_directories(gateway_core PUBLIC
  "${SOURCE_DIR}/src"                # 프로젝트 소스 헤더
  "${GENERATED_DIR}"                 # 생성된 proto 헤더
  ${Protobuf_INCLUDE_DIRS}           # Protobuf 시스템 헤더
  # gRPC 헤더는 PkgConfig::GRPC를 통해 자동으로 포함될 것으로 예상됨
  "${UWEBSOCKETS_INCLUDE_DIR}"       # uWebSockets 헤더
  "${USOCKETS_INCLUDE_DIR}"
  "${nlohmann_json_repo_SOURCE_DIR}/include" # nlohmann_json 헤더 (FetchContent 사용 시)
                                         # 또는 nlohmann_json::nlohmann_json 타겟 링크 시 자동 처리
)

target_link_libraries(gateway_core PUBLIC
  Threads::Threads
  PkgConfig::GRPC           # gRPC++ 라이브러리 (링크 및 헤더 경로 포함)
  ${Protobuf_LIBRARIES}     # Protobuf 라이브러리
  ${USOCKETS_LIB}           # uSockets 정적 라이브러리
  OpenSSL::SSL              # OpenSSL
  OpenSSL::Crypto
  ZLIB::ZLIB                # Zlib
  nlohmann_json::nlohmann_json # nlohmann_json (FetchContent로 생성된 타겟)
)

# ---=[ 메인 실행 파일 (WebSocketGateway) ]=---
add_executable(${PROJECT_NAME}
  "${SOURCE_DIR}/src/main.cpp"
)
target_link_libraries(${PROJECT_NAME} PRIVATE gateway_core)


# ---=[ 유닛 테스트 빌드 (BUILD_TESTING 옵션 활성화 시) ]=---
if(BUILD_TESTING)
  message(STATUS "Building internal unit tests for ${PROJECT_NAME}...")

  find_package(GTest REQUIRED)    # Google Test 찾기 (CMake가 자동으로 gtest, gtest_main 타겟 제공)
  enable_testing()                # CTest 활성화 (선택 사항)

  # 유닛 테스트 실행 파일 이름 (Docker.unit_test의 CMD와 일치시킬 것)
  set(UNIT_TEST_EXECUTABLE_NAME gateway_unit_tests)

  add_executable(${UNIT_TEST_EXECUTABLE_NAME}
    "${SOURCE_DIR}/tests/internal_unit_tests.cpp" # 테스트 코드 파일
    # gateway_core 라이브러리를 링크하므로, 해당 소스를 여기에 다시 명시할 필요 없음
  )

  # 유닛 테스트 타겟에 필요한 include 경로 (대부분 gateway_core에서 상속받음)
  # 명시적으로 GTest 헤더 경로 추가
  target_include_directories(${UNIT_TEST_EXECUTABLE_NAME} PRIVATE
    ${GTEST_INCLUDE_DIRS}
    # gateway_core가 PUBLIC으로 include를 제공하므로, 여기서는 중복 명시 안해도 될 수 있음.
    # 단, 테스트 코드 자체에서만 필요한 헤더가 있다면 여기에 추가.
  )

  target_link_libraries(${UNIT_TEST_EXECUTABLE_NAME} PRIVATE
    gateway_core          # 테스트 대상 핵심 로직 라이브러리
    GTest::gtest          # GTest 라이브러리
    GTest::gtest_main     # GTest main 함수 제공 라이브러리
  )

  # CTest를 사용하여 테스트 자동 발견 및 실행 (선택 사항)
  include(GoogleTest)
  gtest_discover_tests(${UNIT_TEST_EXECUTABLE_NAME})

  message(STATUS "Unit test executable: ${UNIT_TEST_EXECUTABLE_NAME} will be built.")
endif()

# ---=[ 완료 메시지 ]=---
message(STATUS "CMake configuration complete for ${PROJECT_NAME}.")