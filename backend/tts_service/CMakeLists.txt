# CMakeLists.txt for tts_service (mv proto build + Custom Commands - 모든 문법 오류 수정)

# 최소 CMake 버전 설정
cmake_minimum_required(VERSION 3.13)

# 프로젝트 이름 및 버전 설정
project(tts_service VERSION 1.0 LANGUAGES CXX)

# C++ 표준 설정 (예: C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---=[ 의존성 찾기 ]=---

# 필수 패키지 찾기
find_package(Protobuf REQUIRED)    # protoc 경로 및 링크 라이브러리 위해 필요
find_package(PkgConfig REQUIRED) # gRPC 링크 위해 필요
find_package(Threads REQUIRED)

# pkg-config를 사용하여 gRPC++ 라이브러리 정보 조회 (링크용)
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)
# gRPC를 찾지 못했을 경우 에러 메시지 출력 및 종료
if(NOT GRPC_FOUND)
    message(FATAL_ERROR "gRPC++ library not found using pkg-config. Is libgrpc++-dev installed correctly?")
endif() # ***** 첫 번째 누락된 endif() *****
message(STATUS "Found gRPC++ using pkg-config for linking.")

# 코드 생성을 위한 protoc 및 gRPC 플러그인 실행 파일 찾기
# Protobuf_PROTOC_EXECUTABLE 변수가 find_package(Protobuf) 로 설정되지 않았을 경우 직접 찾기
if(NOT Protobuf_PROTOC_EXECUTABLE)
    find_program(Protobuf_PROTOC_EXECUTABLE protoc)
endif()
# gRPC C++ 플러그인 찾기
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
# 둘 중 하나라도 찾지 못했을 경우 에러 메시지 출력 및 종료
if(NOT Protobuf_PROTOC_EXECUTABLE OR NOT GRPC_CPP_PLUGIN)
    message(FATAL_ERROR "protoc or grpc_cpp_plugin not found. Ensure protobuf-compiler and protobuf-compiler-grpc are installed.")
endif() # ***** 관련 if문에 대한 endif() 추가 (가독성 및 안전성) *****
message(STATUS "Found protoc: ${Protobuf_PROTOC_EXECUTABLE}")
message(STATUS "Found gRPC C++ Plugin: ${GRPC_CPP_PLUGIN}")

# ---=[ 경로 설정 ]=---

# 소스 코드 경로 (CMakeLists.txt 위치)
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}) # /app
# 빌드 경로 (CMake 실행 위치)
set(BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}) # /app/build

# .proto 파일 경로 (빌드 디렉토리 안으로 이동되었음)
set(PROTO_DIR_IN_BUILD "${BINARY_DIR}/proto")
set(PROTO_FILE_BASENAME "tts") # .proto 파일 이름 (확장자 제외)
set(PROTO_FILE "${PROTO_DIR_IN_BUILD}/${PROTO_FILE_BASENAME}.proto") # /app/build/proto/tts.proto

# 생성된 코드 저장 위치
set(GENERATED_DIR "${BINARY_DIR}/generated") # /app/build/generated
file(MAKE_DIRECTORY ${GENERATED_DIR})

# 생성될 파일들의 전체 경로 정의
set(PROTO_GENERATED_CPP "${GENERATED_DIR}/${PROTO_FILE_BASENAME}.pb.cc")
set(PROTO_GENERATED_H "${GENERATED_DIR}/${PROTO_FILE_BASENAME}.pb.h")
set(GRPC_GENERATED_CPP "${GENERATED_DIR}/${PROTO_FILE_BASENAME}.grpc.pb.cc")
set(GRPC_GENERATED_H "${GENERATED_DIR}/${PROTO_FILE_BASENAME}.grpc.pb.h")
set(ALL_GENERATED_SOURCES ${PROTO_GENERATED_CPP} ${GRPC_GENERATED_CPP})
set(ALL_GENERATED_HEADERS ${PROTO_GENERATED_H} ${GRPC_GENERATED_H})

# Azure Speech SDK 경로 설정
set(SPEECHSDK_ROOT "$ENV{SPEECHSDK_ROOT}" CACHE PATH "Root directory of the Azure Speech SDK")
# Azure Speech SDK 경로 또는 헤더 파일 존재 유무 확인
if(NOT SPEECHSDK_ROOT OR NOT EXISTS "${SPEECHSDK_ROOT}/include/cxx_api/speechapi_cxx.h")
    message(FATAL_ERROR "Azure Speech SDK not found or SPEECHSDK_ROOT is not set correctly. Please set the SPEECHSDK_ROOT environment variable or use -DSPEECHSDK_ROOT=/path/to/sdk.")
endif() # ***** 두 번째 누락된 endif() *****
message(STATUS "Using Azure Speech SDK from: ${SPEECHSDK_ROOT}")
set(SPEECHSDK_CXX_INCLUDE_DIR "${SPEECHSDK_ROOT}/include/cxx_api")
set(SPEECHSDK_C_INCLUDE_DIR "${SPEECHSDK_ROOT}/include/c_api")
set(SPEECHSDK_LIBRARY_DIR "${SPEECHSDK_ROOT}/lib/x64")

# ---=[ Protobuf/gRPC 코드 생성 (Custom Command 사용) ]=---

# protoc 명령어를 직접 실행하는 Custom Command 정의
add_custom_command(
    OUTPUT ${ALL_GENERATED_SOURCES} ${ALL_GENERATED_HEADERS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --proto_path=${PROTO_DIR_IN_BUILD}        # Proto 파일 검색 경로
            --cpp_out=${GENERATED_DIR}            # .pb.cc/.pb.h 출력 경로
            --grpc_out=${GENERATED_DIR}           # .grpc.pb.cc/.grpc.pb.h 출력 경로
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN} # gRPC 플러그인 지정
            ${PROTO_FILE}                         # 입력 proto 파일
    DEPENDS ${PROTO_FILE}                         # proto 파일 변경 시 재실행
    COMMENT "Generating Protobuf and gRPC C++ sources from ${PROTO_FILE}"
    VERBATIM                                      # 명령어 문자열 변경 없이 그대로 사용
)

# 위 custom command가 생성하는 파일들에 의존하는 Custom Target 정의
add_custom_target(generate_proto_sources ALL # 기본 빌드 시 항상 실행 시도
    DEPENDS ${ALL_GENERATED_SOURCES} ${ALL_GENERATED_HEADERS} # 생성 파일 의존
)

# ---=[ 실행 파일 빌드 ]=---

# 실행 파일 타겟 정의
add_executable(tts_server
    "${SOURCE_DIR}/src/main.cpp"
    "${SOURCE_DIR}/src/tts_service.cpp"
    "${SOURCE_DIR}/src/azure_tts_client.cpp"
    ${ALL_GENERATED_SOURCES} # 생성된 소스 파일 명시적 추가
)

# tts_server 타겟이 generate_proto_sources 타겟에 의존하도록 설정
# 즉, 컴파일 전에 코드 생성이 완료되도록 보장
add_dependencies(tts_server generate_proto_sources)

# 필요한 include 디렉토리 설정
target_include_directories(tts_server PRIVATE
    "${SOURCE_DIR}/src"            # /app/src
    "${GENERATED_DIR}"             # /app/build/generated (tts.pb.h 여기 있음)
    ${SPEECHSDK_CXX_INCLUDE_DIR}   # Azure C++ SDK 헤더
    ${SPEECHSDK_C_INCLUDE_DIR}     # Azure C SDK 헤더
    ${Protobuf_INCLUDE_DIRS}       # 시스템 Protobuf 헤더
    # ${GRPC_INCLUDE_DIRS}         # PkgConfig::GRPC 타겟이 처리하므로 명시적 추가 불필요
)

# 필요한 라이브러리 링크
target_link_libraries(tts_server PRIVATE
    Threads::Threads
    PkgConfig::GRPC             # gRPC 라이브러리 (pkg-config 타겟)
    ${Protobuf_LIBRARIES}       # Protobuf 라이브러리 (find_package 변수)
    "${SPEECHSDK_LIBRARY_DIR}/libMicrosoft.CognitiveServices.Speech.core.so" # Azure SDK 코어 라이브러리
)

# ---=[ 완료 메시지 ]=---
message(STATUS "CMake configuration complete. Using custom commands for proto generation. Build with 'make'.")