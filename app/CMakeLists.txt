# nexusd -- NEXUS daemon with TCP Internet transport

add_executable(nexusd nexusd.c)
target_link_libraries(nexusd PRIVATE nexus)

# pillard -- NEXUS pillar server (dedicated public Internet relay)
add_executable(pillard pillard.c)
target_link_libraries(pillard PRIVATE nexus)
