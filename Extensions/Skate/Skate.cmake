# Tests stay outside src: each donor source owns its own main().
# This extension file is included before WowExtensions exists; directory linkage applies to that later target.
link_libraries(ole32)
option(WOWDECOMP_SKATE_TESTS "Build the byte-preserved Skate logic tests" OFF)
if (WOWDECOMP_SKATE_TESTS)
    enable_testing()
    file(GLOB skate_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/tests/*-tests.cpp")
    set(skate_test_targets "")
    set(skate_donor_integration_tests
        air-root-engine-tests air-sync-tests authored-collision-tests bail-entry-tests
        bail-recovery-tests engine-readiness-tests ground-movement-tests motion-feedback-tests
        natural-momentum-tests onfoot-terrain-tests passenger-input-tests pose-continuation-tests
        shadow-tests steering-strength-tests vanilla-reference-tests
        # Captured, game-derived board/body frame and schema files are absent.
        air-board-validation-tests wall-jump-tests)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # The unchanged test deliberately casts x86 thiscall/fastcall fixtures.
        list(APPEND skate_donor_integration_tests flight-handoff-tests)
    endif()
    foreach(source ${skate_tests})
        get_filename_component(test ${source} NAME_WE)
        # These donor integration programs require their original x86 executable/assets/recordings.
        if (test IN_LIST skate_donor_integration_tests)
            continue()
        endif()
        set(target "skate-${test}")
        add_executable(${target} ${source})
        if(test STREQUAL "worker-tests" OR test STREQUAL "timing-tests")
            target_sources(${target} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/SkateHelper.cpp")
        elseif(test STREQUAL "debug-overlay-tests" OR test STREQUAL "peer-group-tests" OR test STREQUAL "peer-delivery-tests")
            target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}")
            target_link_libraries(${target} PRIVATE lua)
            target_sources(${target} PRIVATE "${CMAKE_SOURCE_DIR}/Engine/Source/Base/ConvertUTF.cpp")
        endif()
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src" "${CMAKE_CURRENT_LIST_DIR}/src/SkateLogic")
        target_compile_options(${target} PRIVATE /UNDEBUG)
        if(test STREQUAL "worker-tests" OR test STREQUAL "timing-tests")
            if(NOT TARGET skate-bridge-fixture)
                add_executable(skate-bridge-fixture "${CMAKE_CURRENT_LIST_DIR}/tests/fixtures/bridge.cpp")
                target_include_directories(skate-bridge-fixture PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
            endif()
            add_dependencies(${target} skate-bridge-fixture)
            add_test(NAME ${target} COMMAND ${target} $<TARGET_FILE:skate-bridge-fixture>)
        elseif(test STREQUAL "debug-overlay-tests")
            add_test(NAME ${target} COMMAND ${target} "${CMAKE_CURRENT_LIST_DIR}/src/Skate.cpp")
        else()
            add_test(NAME ${target} COMMAND ${target})
        endif()
        list(APPEND skate_test_targets ${target})
    endforeach()
    add_custom_target(SkateTests DEPENDS ${skate_test_targets})
endif()
