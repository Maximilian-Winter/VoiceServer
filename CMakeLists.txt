# chat_server/CMakeLists.txt
cmake_minimum_required(VERSION 3.5)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
project(voice_chat)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)

# Define ASIO_STANDALONE to avoid needing Boost
add_definitions(-DASIO_STANDALONE)

# Set Asio include directory
set(ASIO_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/external/asio-1-28-0/asio/include/)

# Set PortAudio directory
set(PORTAUDIO_DIR ${CMAKE_SOURCE_DIR}/external/portaudio)


# Add PortAudio subdirectory
add_subdirectory(${PORTAUDIO_DIR})

file(GLOB SERVER_SOURCES "src/server/*.cpp" "src/server/*.h")
file(GLOB CLIENT_SOURCES "src/client/*.cpp" "src/client/*.h")
# Voice Chat Server
add_executable(voice_server
        ${SERVER_SOURCES}
)

target_include_directories(voice_server PRIVATE
        ${ASIO_INCLUDE_DIR}
        ${PORTAUDIO_DIR}/include
        ${OPENSSL_INCLUDE_DIR}
        external
        src/common
)

target_link_libraries(voice_server PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
)

set_target_properties(voice_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Voice Chat Client
add_executable(voice_client
        ${CLIENT_SOURCES}
)

target_include_directories(voice_client PRIVATE
        ${ASIO_INCLUDE_DIR}
        ${PORTAUDIO_DIR}/include
        external
        src/common
)

target_link_libraries(voice_client PRIVATE
        portaudio_static
)

set_target_properties(voice_client PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

file(GLOB WEBSOCKET_SERVER_SOURCES "src/websocket/basic_text/*.cpp" "src/basic_text/websocket/*.h")
# Voice Chat Client
add_executable(text_websocket_server
        ${WEBSOCKET_SERVER_SOURCES}
)

target_include_directories(text_websocket_server PRIVATE
        ${ASIO_INCLUDE_DIR}
        ${OPENSSL_INCLUDE_DIR}
        external
        src/common
)

target_link_libraries(text_websocket_server PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
)

set_target_properties(text_websocket_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)