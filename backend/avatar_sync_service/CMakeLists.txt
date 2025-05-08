cmake_minimum_required(VERSION 3.15)
project(avatar_sync_service VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- 패키지 찾기 ---
find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# gRPC 찾기 (pkg-config 및 IMPORTED_TARGET 사용)
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)
if(NOT GRPC_FOUND)
    message(FATAL_ERROR "gRPC++ library not found using pkg-config. Please install libgrpc++-dev.")
endif()

# WebRTC 라이브러리 찾기 (pkg-config 사용, 변수 기반)
pkg_check_modules(WEBRTC REQUIRED webrtc)
if(NOT WEBRTC_FOUND)
    message(FATAL_ERROR "WebRTC library not found using pkg-config with name 'webrtc'.")
else()
    message(STATUS "WEBRTC_INCLUDE_DIRS (from pkg-config): '${WEBRTC_INCLUDE_DIRS}'")
    message(STATUS "WEBRTC_LIBRARIES (from pkg-config): '${WEBRTC_LIBRARIES}'")
    message(STATUS "WEBRTC_LIBRARY_DIRS (from pkg-config): '${WEBRTC_LIBRARY_DIRS}'") # 라이브러리 경로 확인용
    message(STATUS "WEBRTC_CFLAGS (from pkg-config Cflags): '${WEBRTC_CFLAGS}'")
    message(STATUS "WEBRTC_CFLAGS_OTHER (from pkg-config Cflags_other): '${WEBRTC_CFLAGS_OTHER}'")
    message(STATUS "WEBRTC_DEFINITIONS (from pkg-config): '${WEBRTC_DEFINITIONS}'")
endif()

# OpenSSL 찾기 (find_package 사용)
find_package(OpenSSL)
if(OpenSSL_FOUND)
    message(STATUS "Found OpenSSL: ${OpenSSL_VERSION}")
else()
    message(WARNING "OpenSSL not found. If gRPC or WebRTC requires it explicitly for linking, this might be an issue.")
endif()

# --- Protobuf 및 gRPC 코드 생성 설정 ---
find_program(Protobuf_PROTOC_EXECUTABLE protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin REQUIRED HINTS /usr/bin /usr/local/bin)

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

set(PROTO_SOURCE_DIR "${SOURCE_DIR}/protos")
set(GENERATED_DIR "${BINARY_DIR}/generated_proto_files")
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(AVATAR_SYNC_PROTO_FILE "${PROTO_SOURCE_DIR}/avatar_sync.proto")

set(GENERATED_PB_SRC "${GENERATED_DIR}/avatar_sync.pb.cc")
set(GENERATED_PB_HDR "${GENERATED_DIR}/avatar_sync.pb.h")
set(GENERATED_GRPC_SRC "${GENERATED_DIR}/avatar_sync.grpc.pb.cc")
set(GENERATED_GRPC_HDR "${GENERATED_DIR}/avatar_sync.grpc.pb.h")

add_custom_command(
    OUTPUT ${GENERATED_PB_SRC} ${GENERATED_PB_HDR} ${GENERATED_GRPC_SRC} ${GENERATED_GRPC_HDR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --proto_path=${PROTO_SOURCE_DIR}
            --cpp_out=${GENERATED_DIR}
            --grpc_out=${GENERATED_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
            ${AVATAR_SYNC_PROTO_FILE}
    DEPENDS ${AVATAR_SYNC_PROTO_FILE}
    COMMENT "Generating Protobuf and gRPC sources from ${AVATAR_SYNC_PROTO_FILE}"
    VERBATIM
)

add_custom_target(generate_proto ALL DEPENDS
    ${GENERATED_PB_SRC} ${GENERATED_PB_HDR}
    ${GENERATED_GRPC_SRC} ${GENERATED_GRPC_HDR}
)

# --- 실행 파일 정의 ---
add_executable(avatar_sync_server
    "${SOURCE_DIR}/src/main.cpp"
    "${SOURCE_DIR}/src/avatar_sync_service.cpp"
    ${GENERATED_PB_SRC}
    ${GENERATED_GRPC_SRC}
)
add_dependencies(avatar_sync_server generate_proto)

# --- 인클루드 디렉토리 설정 ---
target_include_directories(avatar_sync_server PUBLIC
    "${SOURCE_DIR}/src"
    "${GENERATED_DIR}"
    ${Protobuf_INCLUDE_DIRS}
    ${WEBRTC_INCLUDE_DIRS}
)
if(OpenSSL_FOUND AND OpenSSL_INCLUDE_DIR)
    target_include_directories(avatar_sync_server PUBLIC ${OpenSSL_INCLUDE_DIR})
endif()

# --- 컴파일 정의 설정 ---
if(WEBRTC_FOUND AND WEBRTC_DEFINITIONS)
    target_compile_definitions(avatar_sync_server PUBLIC ${WEBRTC_DEFINITIONS})
    message(STATUS "Applied WEBRTC_DEFINITIONS to target: ${WEBRTC_DEFINITIONS}")
endif()
target_compile_definitions(avatar_sync_server PUBLIC
    WEBRTC_POSIX
    WEBRTC_LINUX
    WEBRTC_ARCH_LITTLE_ENDIAN
    WEBRTC_HAVE_STDINT_H
)

# --- 라이브러리 검색 경로 추가 ---
if(WEBRTC_FOUND AND WEBRTC_LIBRARY_DIRS)
    # avatar_sync_server 타겟에만 WebRTC 라이브러리 검색 경로를 추가합니다.
    target_link_directories(avatar_sync_server PRIVATE ${WEBRTC_LIBRARY_DIRS})
    message(STATUS "Added WEBRTC_LIBRARY_DIRS to target link path: ${WEBRTC_LIBRARY_DIRS}")
endif()

# --- 라이브러리 링크 ---
target_link_libraries(avatar_sync_server PUBLIC
    Threads::Threads
    PkgConfig::GRPC
    ${Protobuf_LIBRARIES}
    # target_link_directories에서 경로를 추가했으므로,
    # ${WEBRTC_LIBRARIES}는 'webrtc' 이름만 사용해도 링커가 찾을 수 있어야 합니다.
    ${WEBRTC_LIBRARIES}
)
if(OpenSSL_FOUND)
    target_link_libraries(avatar_sync_server PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()