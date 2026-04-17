cmake_minimum_required(VERSION 3.22.1)
project(nexus_jni C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

# Path to libnexus source
set(NEXUS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

# libnexus core (compiled for Android)
add_library(nexus STATIC
    ${NEXUS_ROOT}/lib/vendor/monocypher/monocypher.c
    ${NEXUS_ROOT}/lib/src/identity.c
    ${NEXUS_ROOT}/lib/src/packet.c
    ${NEXUS_ROOT}/lib/src/crypto.c
    ${NEXUS_ROOT}/lib/src/transport.c
    ${NEXUS_ROOT}/lib/src/announce.c
    ${NEXUS_ROOT}/lib/src/route.c
    ${NEXUS_ROOT}/lib/src/fragment.c
    ${NEXUS_ROOT}/lib/src/anchor.c
    ${NEXUS_ROOT}/lib/src/session.c
    ${NEXUS_ROOT}/lib/src/group.c
    ${NEXUS_ROOT}/lib/src/node.c
    ${NEXUS_ROOT}/lib/src/msgring.c
    ${NEXUS_ROOT}/lib/src/message.c
    ${NEXUS_ROOT}/lib/src/platform/platform_android.c
    ${NEXUS_ROOT}/transports/pipe/nexus_pipe.c
    ${NEXUS_ROOT}/transports/tcp/nexus_tcp_inet.c
    ${NEXUS_ROOT}/transports/udp/nexus_udp_multicast.c
)

target_include_directories(nexus PUBLIC
    ${NEXUS_ROOT}/lib/include
    ${NEXUS_ROOT}/lib/vendor
)

# JNI bridge
add_library(nexus_jni SHARED nexus_jni.cpp)

target_include_directories(nexus_jni PRIVATE
    ${NEXUS_ROOT}/lib/include
    ${NEXUS_ROOT}/lib/vendor
)

target_link_libraries(nexus_jni nexus log)
