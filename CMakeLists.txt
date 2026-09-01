cmake_minimum_required(VERSION 3.24)
project(peerdesk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(OpenSSL REQUIRED)
find_package(Protobuf REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale)
pkg_check_modules(ARGON2 REQUIRED IMPORTED_TARGET libargon2)

find_path(ASIO_INCLUDE_DIR asio.hpp
    HINTS
        "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include"
        /opt/homebrew/include
        /usr/include
        /usr/local/include
)
if(NOT ASIO_INCLUDE_DIR)
    message(FATAL_ERROR "Asio headers not found (install vcpkg asio or Homebrew asio)")
endif()

set(PEERDESK_HAVE_X11 OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
    pkg_check_modules(XEXT REQUIRED IMPORTED_TARGET xext)
    pkg_check_modules(XDAMAGE REQUIRED IMPORTED_TARGET xdamage)
    pkg_check_modules(XTST REQUIRED IMPORTED_TARGET xtst)
    set(PEERDESK_HAVE_X11 ON)
endif()

protobuf_generate_cpp(PEERDESK_PROTO_SRCS PEERDESK_PROTO_HDRS
    shared/proto/peerdesk.proto
)

add_library(peerdesk_shared STATIC
    shared/src/auth.cpp
    shared/src/map.cpp
    shared/src/cert.cpp
    shared/src/net.cpp
    shared/src/codec.cpp
    shared/src/protocol.cpp
    ${PEERDESK_PROTO_SRCS}
)
target_include_directories(peerdesk_shared PUBLIC
    ${CMAKE_SOURCE_DIR}/shared/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${ASIO_INCLUDE_DIR}
    ${Protobuf_INCLUDE_DIRS}
)
target_compile_definitions(peerdesk_shared PUBLIC
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED
)
target_link_libraries(peerdesk_shared PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    protobuf::libprotobuf
    PkgConfig::FFMPEG
    PkgConfig::ARGON2
    Threads::Threads
)

add_library(peerdesk_host STATIC
    server/users.cpp
    server/capture.cpp
    server/inject.cpp
    server/host_server.cpp
)
target_include_directories(peerdesk_host PUBLIC ${CMAKE_SOURCE_DIR}/server)
target_link_libraries(peerdesk_host PUBLIC peerdesk_shared)
if(PEERDESK_HAVE_X11)
    target_compile_definitions(peerdesk_host PUBLIC PEERDESK_HAVE_X11=1)
    target_link_libraries(peerdesk_host PUBLIC
        PkgConfig::X11 PkgConfig::XEXT PkgConfig::XDAMAGE PkgConfig::XTST
    )
endif()

add_executable(peerdesk-server server/main.cpp)
target_link_libraries(peerdesk-server PRIVATE peerdesk_host)

set(PEERDESK_CLIENT_SOURCES
    client/main.cpp
    client/MainWindow.cpp
    client/SessionWorker.cpp
    client/keymap.cpp
)
if(APPLE)
    add_executable(peerdesk-client MACOSX_BUNDLE ${PEERDESK_CLIENT_SOURCES})
    set_target_properties(peerdesk-client PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "PeerDesk"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.peerdesk.client"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_INFO_STRING "PeerDesk remote desktop client"
    )
else()
    add_executable(peerdesk-client ${PEERDESK_CLIENT_SOURCES})
endif()
target_include_directories(peerdesk-client PRIVATE ${CMAKE_SOURCE_DIR}/client)
target_link_libraries(peerdesk-client PRIVATE peerdesk_shared Qt6::Widgets)

add_executable(peerdesk-smoke tests/peerdesk_smoke.cpp)
target_include_directories(peerdesk-smoke PRIVATE ${CMAKE_SOURCE_DIR}/server)
target_link_libraries(peerdesk-smoke PRIVATE peerdesk_host)

enable_testing()
add_test(NAME peerdesk-smoke COMMAND peerdesk-smoke)

include(GNUInstallDirs)
install(TARGETS peerdesk-server peerdesk-smoke
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(APPLE)
    install(TARGETS peerdesk-client BUNDLE DESTINATION .)
else()
    install(TARGETS peerdesk-client RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/peerdesk-client.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()

set(CPACK_PACKAGE_NAME "peerdesk")
set(CPACK_PACKAGE_VENDOR "PeerDesk")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "LAN/VPN remote desktop for small teams")
set(CPACK_PACKAGE_CONTACT "peerdesk")
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
elseif(UNIX)
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "PeerDesk")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6, libssl3, libargon2-1")
    set(CPACK_DEBIAN_PACKAGE_SECTION "net")
endif()
include(CPack)
