cmake_minimum_required(VERSION 3.16) # FetchContent 사용을 위해 버전 상향
project(webrtc_gateway VERSION 1.0 LANGUAGES CXX)

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
    message(FATAL_ERROR "gRPC++ library not found using pkg-config. Please install libgrpc++-dev and grpc-proto.")
else()
    message(STATUS "Found gRPC: ${GRPC_LIBRARIES} version ${GRPC_VERSION}")
endif()

# WebRTC 라이브러리 찾기 (pkg-config 사용)
# 실제 시스템 환경에 따라 webrtc.pc 파일이 존재하거나,
# 수동으로 경로를 설정해야 할 수 있습니다.
pkg_check_modules(WEBRTC REQUIRED webrtc)
if(NOT WEBRTC_FOUND)
    message(WARNING "WebRTC library not found using pkg-config with name 'webrtc'. \
                     You may need to install libwebrtc-dev or set WEBRTC_INCLUDE_DIRS and WEBRTC_LIBRARIES manually.")
else()
    message(STATUS "WEBRTC_INCLUDE_DIRS (from pkg-config): '${WEBRTC_INCLUDE_DIRS}'")
    message(STATUS "WEBRTC_LIBRARIES (from pkg-config): '${WEBRTC_LIBRARIES}'")
endif()

# Boost 찾기 (WebSocket, Asio, System, Thread 사용)
# Boost.Beast는 Boost 1.70 이상부터 정식 포함되었으므로 버전을 명시하는 것이 좋습니다.
# 컴포넌트에 beast를 추가하여 명시적으로 찾도록 시도할 수 있으나, 헤더 온리 부분이 많아 system, filesystem 등으로 충분할 수 있습니다.
find_package(Boost 1.71.0 REQUIRED COMPONENTS system filesystem program_options thread date_time)
if(Boost_FOUND)
    message(STATUS "Found Boost: ${Boost_INCLUDE_DIRS} (version ${Boost_VERSION_STRING})")
else()
    message(FATAL_ERROR "Boost not found. Please install libboost-dev (or specific components) version 1.71 or higher.")
endif()

# OpenSSL 찾기 (gRPC, Boost.Asio 등에서 필요할 수 있음)
find_package(OpenSSL)
if(OpenSSL_FOUND)
    message(STATUS "Found OpenSSL: ${OpenSSL_VERSION} (Includes: ${OpenSSL_INCLUDE_DIR}, Libs: ${OpenSSL_LIBRARIES})")
else()
    message(WARNING "OpenSSL not found. If gRPC, WebRTC, or Boost.Asio (for TLS) requires it explicitly for linking, this might be an issue.")
endif()

# --- FetchContent로 외부 라이브러리 관리 (nlohmann-json, yaml-cpp) ---
include(FetchContent)

# nlohmann-json
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3 # 최신 릴리즈 태그 사용 권장
)
FetchContent_MakeAvailable(nlohmann_json)
if(nlohmann_json_SOURCE_DIR)
    message(STATUS "nlohmann_json fetched to ${nlohmann_json_SOURCE_DIR}")
else()
    message(WARNING "nlohmann_json could not be fetched.")
endif()

# yaml-cpp
FetchContent_Declare(
  yaml_cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG yaml-cpp-0.8.0 # 최신 릴리즈 태그 사용 권장
)
FetchContent_MakeAvailable(yaml_cpp) # yaml-cpp는 빌드가 필요할 수 있음
if(yaml_cpp_SOURCE_DIR)
    message(STATUS "yaml-cpp fetched to ${yaml_cpp_SOURCE_DIR}")
    # yaml-cpp는 기본적으로 정적 라이브러리로 빌드됩니다.
    # set(YAML_CPP_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE) # 필요시 명시적 설정
else()
    message(WARNING "yaml-cpp could not be fetched.")
endif()

# --- Protobuf 및 gRPC 코드 생성 설정 ---
find_program(Protobuf_PROTOC_EXECUTABLE protoc REQUIRED)
get_target_property(GRPC_CPP_PLUGIN_EXECUTABLE_PATH PkgConfig::GRPC gRPC_CPP_PLUGIN_EXECUTABLE) # 변수 이름 변경
if (NOT GRPC_CPP_PLUGIN_EXECUTABLE_PATH)
    # 시스템에 따라 grpc_cpp_plugin 경로를 직접 찾아야 할 수 있음
    find_program(GRPC_CPP_PLUGIN_EXECUTABLE_PATH grpc_cpp_plugin HINTS /usr/bin /usr/local/bin)
endif()
if(NOT GRPC_CPP_PLUGIN_EXECUTABLE_PATH)
    message(FATAL_ERROR "grpc_cpp_plugin not found. Please ensure it's in your PATH or set GRPC_CPP_PLUGIN_EXECUTABLE_PATH.")
endif()
message(STATUS "Using protoc: ${Protobuf_PROTOC_EXECUTABLE}")
message(STATUS "Using grpc_cpp_plugin: ${GRPC_CPP_PLUGIN_EXECUTABLE_PATH}")


set(PROTO_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/protos")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_proto_files")
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(AVATAR_SYNC_PROTO_FILE "${PROTO_SOURCE_DIR}/avatar_sync.proto")
set(STT_PROTO_FILE "${PROTO_SOURCE_DIR}/stt.proto")

set(PROTO_FILES
    ${AVATAR_SYNC_PROTO_FILE}
    ${STT_PROTO_FILE}
)

set(GENERATED_PB_SRCS "")
set(GENERATED_PB_HDRS "")
set(GENERATED_GRPC_SRCS "")
set(GENERATED_GRPC_HDRS "")

foreach(PROTO_FILE_ITEM ${PROTO_FILES}) # 변수명 변경 (PROTO_FILE -> PROTO_FILE_ITEM)
    get_filename_component(PROTO_FILENAME ${PROTO_FILE_ITEM} NAME_WE)
    list(APPEND GENERATED_PB_SRCS "${GENERATED_DIR}/${PROTO_FILENAME}.pb.cc")
    list(APPEND GENERATED_PB_HDRS "${GENERATED_DIR}/${PROTO_FILENAME}.pb.h")
    list(APPEND GENERATED_GRPC_SRCS "${GENERATED_DIR}/${PROTO_FILENAME}.grpc.pb.cc")
    list(APPEND GENERATED_GRPC_HDRS "${GENERATED_DIR}/${PROTO_FILENAME}.grpc.pb.h")
endforeach()

add_custom_command(
    OUTPUT ${GENERATED_PB_SRCS} ${GENERATED_PB_HDRS} ${GENERATED_GRPC_SRCS} ${GENERATED_GRPC_HDRS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --proto_path=${PROTO_SOURCE_DIR}
            --cpp_out=${GENERATED_DIR}
            --grpc_out=${GENERATED_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE_PATH}
            ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating Protobuf and gRPC C++ sources from .proto files"
    VERBATIM
)

add_custom_target(generate_proto ALL DEPENDS
    ${GENERATED_PB_SRCS} ${GENERATED_PB_HDRS}
    ${GENERATED_GRPC_SRCS} ${GENERATED_GRPC_HDRS}
)

# --- 소스 파일 목록 ---
set(PROJECT_SOURCES
    "src/main.cpp"
    "src/grpc_clients/SttClient.cpp"
    "src/grpc_clients/TtsClient.cpp"
    "src/rtc/PeerConnectionManager.cpp"
    "src/signaling/WebSocketServer.cpp"
    "src/signaling/WebSocketSession.cpp"
    # 생성된 소스 파일은 add_executable에서 직접 사용하기보다,
    # generate_proto 타겟이 먼저 빌드되도록 add_dependencies를 통해 관리합니다.
    # ${GENERATED_PB_SRCS} 와 ${GENERATED_GRPC_SRCS} 를 여기에 포함해도 되지만,
    # 빌드 시스템이 의존성을 더 잘 파악하도록 아래와 같이 구성하는 것이 일반적입니다.
)

# --- 실행 파일 정의 ---
add_executable(webrtc_gateway) # 소스 목록은 target_sources로 추가

# 실행 파일에 소스 추가 (생성된 파일 포함)
target_sources(webrtc_gateway PRIVATE
    "src/main.cpp"
    "src/grpc_clients/SttClient.cpp"
    "src/grpc_clients/TtsClient.cpp"
    "src/rtc/PeerConnectionManager.cpp"
    "src/signaling/WebSocketServer.cpp"
    "src/signaling/WebSocketSession.cpp"
    ${GENERATED_PB_SRCS}
    ${GENERATED_GRPC_SRCS}
)

add_dependencies(webrtc_gateway generate_proto) # 프로토콜 파일이 먼저 생성되도록 의존성 추가

# --- 인클루드 디렉토리 설정 ---
target_include_directories(webrtc_gateway PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"  # 프로젝트 내부 헤더
    "${GENERATED_DIR}"                 # 생성된 proto 헤더
    ${Protobuf_INCLUDE_DIRS}           # Protobuf 헤더
    # GRPC_INCLUDE_DIRS는 PkgConfig::GRPC를 통해 자동으로 전파되는 경우가 많습니다.
    # 명시적으로 추가해야 한다면 ${GRPC_INCLUDE_DIRS} 를 사용하세요.
    ${nlohmann_json_SOURCE_DIR}/include # nlohmann-json 헤더 (FetchContent 사용 시)
    ${yaml_cpp_SOURCE_DIR}/include     # yaml-cpp 헤더 (FetchContent 사용 시)
    ${Boost_INCLUDE_DIRS}              # Boost 헤더
)
if(WEBRTC_FOUND AND WEBRTC_INCLUDE_DIRS)
    target_include_directories(webrtc_gateway PUBLIC ${WEBRTC_INCLUDE_DIRS})
endif()
if(OpenSSL_FOUND AND OpenSSL_INCLUDE_DIR)
    target_include_directories(webrtc_gateway PUBLIC ${OpenSSL_INCLUDE_DIR})
endif()

# --- 컴파일 정의 설정 ---
if(WEBRTC_FOUND AND WEBRTC_DEFINITIONS)
    target_compile_definitions(webrtc_gateway PUBLIC ${WEBRTC_DEFINITIONS})
    message(STATUS "Applied WEBRTC_DEFINITIONS to target: ${WEBRTC_DEFINITIONS}")
endif()
# 필요한 WebRTC 관련 정의 추가 (환경에 따라 다를 수 있음)
target_compile_definitions(webrtc_gateway PRIVATE
    WEBRTC_POSIX # 또는 WEBRTC_MAC, WEBRTC_WIN 등 환경에 맞게
    WEBRTC_LINUX # 프로젝트가 리눅스 환경을 주로 타겟한다면 명시
    # WEBRTC_HAVE_SCTP # WebRTC DataChannel 사용 시 필요할 수 있음
    # EIGEN_MPL2_ONLY # Eigen 라이브러리 사용 시 라이선스 관련 정의
    # BOOST_ASIO_USE_OPENSSL # Boost.Asio에서 TLS/SSL 사용 시 (필요시 활성화)
)

# --- 라이브러리 검색 경로 추가 ---
if(WEBRTC_FOUND AND WEBRTC_LIBRARY_DIRS)
    target_link_directories(webrtc_gateway PRIVATE ${WEBRTC_LIBRARY_DIRS})
    message(STATUS "Added WEBRTC_LIBRARY_DIRS to target link path: ${WEBRTC_LIBRARY_DIRS}")
endif()

# --- 라이브러리 링크 ---
target_link_libraries(webrtc_gateway PUBLIC
    Threads::Threads
    PkgConfig::GRPC      # gRPC (grpc++, gpr, address_sorting, upb 등 관련 라이브러리 포함)
    ${Protobuf_LIBRARIES} # Protobuf 라이브러리 (보통 libprotobuf)
    yaml-cpp             # yaml-cpp (FetchContent로 빌드된 타겟 이름)
    Boost::system
    Boost::filesystem
    Boost::program_options
    Boost::thread
    Boost::date_time
    # Boost.Beast는 헤더 온리 컴포넌트가 많지만, Boost.Asio 등에 의존하므로 해당 링크 필요
)
if(WEBRTC_FOUND AND WEBRTC_LIBRARIES)
    # WEBRTC_LIBRARIES가 'webrtc'와 같은 단순 이름이면 target_link_directories 덕분에 찾아짐
    # 만약 풀 경로이거나 여러 라이브러리면 그대로 사용
    target_link_libraries(webrtc_gateway PUBLIC ${WEBRTC_LIBRARIES})
endif()
if(OpenSSL_FOUND)
    # OpenSSL::SSL, OpenSSL::Crypto 타겟은 find_package(OpenSSL)이 잘 정의해줄 때 사용 가능
    # 구버전 CMake나 시스템 설정에 따라 ${OpenSSL_LIBRARIES} 또는
    # ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} 등을 사용해야 할 수도 있습니다.
    target_link_libraries(webrtc_gateway PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

# --- 설치 (선택 사항) ---
# install(TARGETS webrtc_gateway DESTINATION bin)
# install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/config.yml" DESTINATION etc/webrtc_gateway)
# install(FILES "${AVATAR_SYNC_PROTO_FILE}" "${STT_PROTO_FILE}" DESTINATION share/webrtc_gateway/protos)

message(STATUS "Configuration done. You can now build with 'cmake --build .'")