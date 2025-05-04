# CMakeLists.txt for stt_service

# 최소 CMake 버전 설정
cmake_minimum_required(VERSION 3.13)

# 프로젝트 이름 및 버전 설정 (stt_service로 변경)
project(stt_service VERSION 1.0 LANGUAGES CXX)

# C++ 표준 설정 (예: C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---=[ 의존성 찾기 ]=---

# 필수 패키지 찾기
find_package(Protobuf REQUIRED)
message(STATUS "Protobuf Include Dirs: ${Protobuf_INCLUDE_DIRS}")
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# pkg-config를 사용하여 gRPC++ 라이브러리 정보 조회 (링크용)
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)
if(NOT GRPC_FOUND)
    message(FATAL_ERROR "gRPC++ library not found using pkg-config.")
endif()
message(STATUS "Found gRPC++ using pkg-config for linking.")

# 코드 생성을 위한 protoc 및 gRPC 플러그인 실행 파일 찾기
find_program(Protobuf_PROTOC_EXECUTABLE protoc)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
if(NOT Protobuf_PROTOC_EXECUTABLE OR NOT GRPC_CPP_PLUGIN)
    message(FATAL_ERROR "protoc or grpc_cpp_plugin not found.")
endif()
message(STATUS "Found protoc: ${Protobuf_PROTOC_EXECUTABLE}")
message(STATUS "Found gRPC C++ Plugin: ${GRPC_CPP_PLUGIN}")

# ---=[ 경로 설정 ]=---

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}) # /app
set(BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}) # /app/build

# .proto 파일 경로 및 이름 정의 (stt.proto, llm.proto)
set(PROTO_SOURCE_DIR "${SOURCE_DIR}/protos") # /app/proto
set(STT_PROTO_BASENAME "stt")
set(LLM_PROTO_BASENAME "llm")
set(STT_PROTO_FILE "${PROTO_SOURCE_DIR}/${STT_PROTO_BASENAME}.proto")
set(LLM_PROTO_FILE "${PROTO_SOURCE_DIR}/${LLM_PROTO_BASENAME}.proto") # llm_engine proto 파일 경로

# 생성된 코드 저장 위치
set(GENERATED_DIR "${BINARY_DIR}/generated") # /app/build/generated
file(MAKE_DIRECTORY ${GENERATED_DIR})

# 생성될 파일들의 전체 경로 정의 (stt 및 llm_engine 모두 포함)
set(STT_PROTO_GENERATED_CPP "${GENERATED_DIR}/${STT_PROTO_BASENAME}.pb.cc")
set(STT_PROTO_GENERATED_H "${GENERATED_DIR}/${STT_PROTO_BASENAME}.pb.h")
set(STT_GRPC_GENERATED_CPP "${GENERATED_DIR}/${STT_PROTO_BASENAME}.grpc.pb.cc")
set(STT_GRPC_GENERATED_H "${GENERATED_DIR}/${STT_PROTO_BASENAME}.grpc.pb.h")

set(LLM_PROTO_GENERATED_CPP "${GENERATED_DIR}/${LLM_PROTO_BASENAME}.pb.cc") # llm 생성 파일
set(LLM_PROTO_GENERATED_H "${GENERATED_DIR}/${LLM_PROTO_BASENAME}.pb.h")
set(LLM_GRPC_GENERATED_CPP "${GENERATED_DIR}/${LLM_PROTO_BASENAME}.grpc.pb.cc")
set(LLM_GRPC_GENERATED_H "${GENERATED_DIR}/${LLM_PROTO_BASENAME}.grpc.pb.h")

set(ALL_GENERATED_SOURCES
    ${STT_PROTO_GENERATED_CPP} ${STT_GRPC_GENERATED_CPP}
    ${LLM_PROTO_GENERATED_CPP} ${LLM_GRPC_GENERATED_CPP} # llm 생성 소스 추가
)
set(ALL_GENERATED_HEADERS
    ${STT_PROTO_GENERATED_H} ${STT_GRPC_GENERATED_H}
    ${LLM_PROTO_GENERATED_H} ${LLM_GRPC_GENERATED_H} # llm 생성 헤더 추가
)

# Azure Speech SDK 경로 설정
set(SPEECHSDK_ROOT "$ENV{SPEECHSDK_ROOT}" CACHE PATH "Root directory of the Azure Speech SDK")
if(NOT SPEECHSDK_ROOT OR NOT EXISTS "${SPEECHSDK_ROOT}/include/cxx_api/speechapi_cxx.h")
    message(FATAL_ERROR "Azure Speech SDK not found or SPEECHSDK_ROOT is not set correctly.")
endif()
message(STATUS "Using Azure Speech SDK from: ${SPEECHSDK_ROOT}")
set(SPEECHSDK_CXX_INCLUDE_DIR "${SPEECHSDK_ROOT}/include/cxx_api")
set(SPEECHSDK_C_INCLUDE_DIR "${SPEECHSDK_ROOT}/include/c_api")
set(SPEECHSDK_LIBRARY_DIR "${SPEECHSDK_ROOT}/lib/arm64") # Assuming x64 Linux

# ---=[ Protobuf/gRPC 코드 생성 (Custom Command 사용) ]=---

# 하나의 Custom Command로 두 proto 파일 처리
add_custom_command(
    OUTPUT ${ALL_GENERATED_SOURCES} ${ALL_GENERATED_HEADERS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --proto_path=${PROTO_SOURCE_DIR}       # Proto 파일 검색 경로
            --cpp_out=${GENERATED_DIR}            # .pb.cc/.pb.h 출력 경로
            --grpc_out=${GENERATED_DIR}           # .grpc.pb.cc/.grpc.pb.h 출력 경로
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN} # gRPC 플러그인 지정
            ${STT_PROTO_FILE}                     # 입력 proto 파일 1
            ${LLM_PROTO_FILE}                     # 입력 proto 파일 2 (llm 추가)
    DEPENDS ${STT_PROTO_FILE} ${LLM_PROTO_FILE}   # proto 파일 변경 시 재실행
    COMMENT "Generating Protobuf and gRPC C++ sources from .proto files"
    VERBATIM
)

# 생성된 파일들에 의존하는 Custom Target 정의
add_custom_target(generate_proto_sources ALL
    DEPENDS ${ALL_GENERATED_SOURCES} ${ALL_GENERATED_HEADERS}
)

# ---=[ 실행 파일 빌드 ]=---

# 실행 파일 타겟 정의 (stt_server로 변경, 소스 파일 목록 수정)
add_executable(stt_server
    "${SOURCE_DIR}/src/main.cpp"
    "${SOURCE_DIR}/src/stt_service.cpp"         # stt 서비스 구현체
    "${SOURCE_DIR}/src/azure_stt_client.cpp"    # azure stt 클라이언트
    "${SOURCE_DIR}/src/llm_engine_client.cpp"   # llm engine 클라이언트 추가
    ${ALL_GENERATED_SOURCES}                   # 생성된 모든 proto 소스 파일 포함
)

# stt_server 타겟이 코드 생성 타겟에 의존하도록 설정
add_dependencies(stt_server generate_proto_sources)

# 필요한 include 디렉토리 설정
target_include_directories(stt_server PRIVATE
    "${SOURCE_DIR}/src"            # /app/src
    "${GENERATED_DIR}"             # /app/build/generated (생성된 .h 파일 위치)
    ${SPEECHSDK_CXX_INCLUDE_DIR}
    ${SPEECHSDK_C_INCLUDE_DIR}
    ${Protobuf_INCLUDE_DIRS}
    # ${GRPC_INCLUDE_DIRS} # PkgConfig::GRPC 가 처리
)

# 필요한 라이브러리 링크 (TTS와 동일)
target_link_libraries(stt_server PRIVATE
    Threads::Threads
    PkgConfig::GRPC             # gRPC 라이브러리
    ${Protobuf_LIBRARIES}       # Protobuf 라이브러리
    "${SPEECHSDK_LIBRARY_DIR}/libMicrosoft.CognitiveServices.Speech.core.so" # Azure SDK 코어 라이브러리
)

# ---=[ 완료 메시지 ]=---
message(STATUS "CMake configuration complete for stt_service.")