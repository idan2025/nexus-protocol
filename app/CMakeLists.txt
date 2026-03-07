# nexusd -- NEXUS daemon with TCP Internet transport

add_executable(nexusd nexusd.c)
target_link_libraries(nexusd PRIVATE nexus)
