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
    test_tcp_inet
    test_udp_mcast
    test_message
    test_msgring
    test_federation
)

foreach(test ${TESTS})
    add_executable(${test} ${test}.c)
    target_link_libraries(${test} PRIVATE nexus)
    # Keep asserts active in Release builds
    target_compile_options(${test} PRIVATE -UNDEBUG)
    add_test(NAME ${test} COMMAND ${test})
endforeach()

# test_message needs math library for fabs()
target_link_libraries(test_message PRIVATE m)

# ---------------------------------------------------------------------------
# Fuzz targets
#
# Two opt-in modes:
#   -DNX_FUZZ=ON            -> link with libFuzzer (+ASan+UBSan). Requires
#                              clang. Produces proper coverage-guided fuzzers.
#   -DNX_FUZZ_STANDALONE=ON -> link with a plain main() that replays a
#                              corpus directory. Works on GCC. Good for
#                              regression replay in CI without clang.
# Both off by default so the normal test build is unaffected.
# ---------------------------------------------------------------------------

option(NX_FUZZ "Build libFuzzer harnesses (requires clang)" OFF)
option(NX_FUZZ_STANDALONE "Build fuzz targets with a standalone corpus-replay driver" OFF)

set(FUZZ_TARGETS fuzz_packet fuzz_announce fuzz_message)

if(NX_FUZZ)
    foreach(tgt ${FUZZ_TARGETS})
        add_executable(${tgt} fuzz/${tgt}.c)
        target_link_libraries(${tgt} PRIVATE nexus)
        target_compile_options(${tgt} PRIVATE
            -fsanitize=fuzzer,address,undefined -g -O1)
        target_link_options(${tgt} PRIVATE
            -fsanitize=fuzzer,address,undefined)
    endforeach()
elseif(NX_FUZZ_STANDALONE)
    foreach(tgt ${FUZZ_TARGETS})
        add_executable(${tgt} fuzz/${tgt}.c fuzz/fuzz_standalone.c)
        target_link_libraries(${tgt} PRIVATE nexus)
        add_test(NAME ${tgt}
                 COMMAND ${tgt} ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus/${tgt})
    endforeach()
endif()
