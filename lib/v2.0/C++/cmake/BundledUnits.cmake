# Build the pinned submodule in a function so its options and C++14 standard
# do not change the parent project's C++23 targets or dependency search paths.
function(pprzlink_add_bundled_units)
    get_filename_component(pprzlink_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../.." ABSOLUTE)
    set(units_source "${pprzlink_root}/third_party/llnl_units")
    if(NOT EXISTS "${units_source}/units/units.cpp")
        message(FATAL_ERROR
            "LLNL/units submodule is not initialized. From the pprzlink root run:\n"
            "  git submodule update --init third_party/llnl_units\n"
            "An installed package may instead be selected with PPRZLINK_USE_SYSTEM_UNITS=ON.")
    endif()

    set(CMAKE_CXX_STANDARD 14)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(UNITS_INSTALL ON)
    set(UNITS_HEADER_ONLY OFF)
    set(UNITS_ENABLE_TESTS OFF)
    set(UNITS_BUILD_SHARED_LIBRARY OFF)
    set(UNITS_BUILD_STATIC_LIBRARY ON)
    set(UNITS_BUILD_OBJECT_LIBRARY OFF)
    set(UNITS_BUILD_CONVERTER_APP OFF)
    set(UNITS_BUILD_WEBSERVER OFF)
    set(UNITS_BUILD_PYTHON_LIBRARY OFF)
    set(UNITS_BUILD_CXX_MODULE OFF)
    set(UNITS_BUILD_FUZZ_TARGETS OFF)
    add_subdirectory("${units_source}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/llnl_units")

    # Install LLNL's package with the SDK so static clients find the exact
    # dependency without a separate installation or a checkout of the sources.
    install(FILES "${units_source}/LICENSE" "${units_source}/NOTICE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/pprzlink++/LLNL-units")
endfunction()
