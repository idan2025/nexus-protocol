# Test executables

set(TESTS
    test_identity
    test_packet
    test_crypto
    test_announce
    test_route
    test_fragment
    test_lora
    test_ble
    test_anchor
    test_session
    test_session_node
    test_group
    test_gateway
    test_halow
    test_asf
    test_stress
    test_stress_1k
)

foreach(test ${TESTS})
    add_executable(${test} ${test}.c)
    target_link_libraries(${test} PRIVATE nexus)
    add_test(NAME ${test} COMMAND ${test})
endforeach()
