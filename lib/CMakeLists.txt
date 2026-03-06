# libnexus -- NEXUS protocol core library

# Object library (compiled once with -fPIC)
add_library(nexus_objs OBJECT
    vendor/monocypher/monocypher.c
    src/platform/platform_posix.c
    src/identity.c
    src/packet.c
    src/crypto.c
    src/transport.c
    src/announce.c
    src/route.c
    src/fragment.c
    src/anchor.c
    src/session.c
    src/group.c
    src/node.c
    ../transports/serial/nexus_serial.c
    ../transports/tcp/nexus_tcp.c
    ../transports/lora/nexus_lora.c
    ../transports/lora/nexus_lora_asf.c
    ../transports/ble/nexus_ble.c
    ../transports/wifi/nexus_halow.c
    ../transports/pipe/nexus_pipe.c
)
set_target_properties(nexus_objs PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(nexus_objs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

# Static library (for C tests + firmware)
add_library(nexus STATIC $<TARGET_OBJECTS:nexus_objs>)
target_include_directories(nexus PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
find_package(Threads REQUIRED)
target_link_libraries(nexus PUBLIC Threads::Threads)

# Shared library (for Python ctypes)
add_library(nexus_shared SHARED $<TARGET_OBJECTS:nexus_objs>)
set_target_properties(nexus_shared PROPERTIES OUTPUT_NAME nexus)
target_include_directories(nexus_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_link_libraries(nexus_shared PUBLIC Threads::Threads)
