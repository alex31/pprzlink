cmake_minimum_required(VERSION 3.20)
project(pprzlink++ LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
option(PPRZLINK_WITH_IVY "Build the optional Ivy adapter" ON)
option(PPRZLINK_BUILD_EXAMPLES "Build the public API examples" ON)
option(PPRZLINK_BUILD_LINK "Build the OCaml-compatible ground link agent (Unix)" ${PPRZLINK_WITH_IVY})

find_package(tinyxml2 REQUIRED)
find_package(Boost 1.69 CONFIG REQUIRED)
find_package(Threads REQUIRED)
if(PPRZLINK_WITH_IVY)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(IVY REQUIRED IMPORTED_TARGET ivy-cpp>=3.18)
endif()
if(PPRZLINK_BUILD_LINK AND NOT PPRZLINK_WITH_IVY)
    message(FATAL_ERROR "link++ requires Ivy; set PPRZLINK_BUILD_LINK=OFF for a build without Ivy")
endif()

set(CORE_SOURCES
    pprzlink/FieldValue.cpp pprzlink/BinaryCodec.cpp pprzlink/TextCodec.cpp
    pprzlink/IvyMessageCodec.cpp pprzlink/LegacyIvyCodec.cpp
    pprzlink/Message.cpp pprzlink/MessageDefinition.cpp pprzlink/MessageDictionary.cpp
    pprzlink/MessageField.cpp pprzlink/MessageFieldTypes.cpp
    pprzlink/PprzTransport.cpp pprzlink/PprzFrameCodec.cpp
    pprzlink/XbeeTransport.cpp pprzlink/XbeeModem.cpp)
set(IO_SOURCES pprzlink/BoostSerialPortDevice.cpp pprzlink/UdpTransport.cpp)
set(PUBLIC_HEADERS
    pprzlink/Device.h pprzlink/FieldValue.h pprzlink/BinaryCodec.h pprzlink/TextCodec.h
    pprzlink/IvyMessageCodec.h pprzlink/Message.h pprzlink/MessageDefinition.h
    pprzlink/MessageDictionary.h pprzlink/MessageField.h pprzlink/MessageFieldTypes.h
    pprzlink/PprzTransport.h pprzlink/PprzFrameCodec.h pprzlink/TransportStatistics.h
    pprzlink/XbeeTransport.h pprzlink/XbeeModem.h pprzlink/Transport.h
    pprzlink/ReceivedMessage.h pprzlink/BoostSerialPortDevice.h pprzlink/UdpTransport.h)
if(UNIX)
    list(APPEND IO_SOURCES pprzlink/PosixFileDevice.cpp)
    list(APPEND PUBLIC_HEADERS pprzlink/PosixFileDevice.h)
endif()

function(configure_pprzlink_target target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
endfunction()

# Compile each implementation once. Public component archives and the historical
# shared/static libraries reuse those objects, without exporting object targets.
add_library(pprzlink_core_objects OBJECT ${CORE_SOURCES})
configure_pprzlink_target(pprzlink_core_objects)
target_link_libraries(pprzlink_core_objects PRIVATE tinyxml2::tinyxml2 Boost::headers)
add_library(pprzlink_core STATIC $<TARGET_OBJECTS:pprzlink_core_objects>)
configure_pprzlink_target(pprzlink_core)
target_link_libraries(pprzlink_core PUBLIC tinyxml2::tinyxml2 Boost::headers)
add_library(pprzlink::core ALIAS pprzlink_core)

add_library(pprzlink_io_objects OBJECT ${IO_SOURCES})
configure_pprzlink_target(pprzlink_io_objects)
target_link_libraries(pprzlink_io_objects PRIVATE pprzlink_core Threads::Threads)
add_library(pprzlink_io STATIC $<TARGET_OBJECTS:pprzlink_io_objects>)
configure_pprzlink_target(pprzlink_io)
target_link_libraries(pprzlink_io PUBLIC pprzlink_core Threads::Threads)
add_library(pprzlink::io ALIAS pprzlink_io)

set(LEGACY_OBJECTS $<TARGET_OBJECTS:pprzlink_core_objects> $<TARGET_OBJECTS:pprzlink_io_objects>)
set(LEGACY_LIBRARIES tinyxml2::tinyxml2 Boost::headers Threads::Threads)
if(PPRZLINK_WITH_IVY)
    add_library(pprzlink_ivy_objects OBJECT pprzlink/IvyLink.cpp)
    configure_pprzlink_target(pprzlink_ivy_objects)
    target_link_libraries(pprzlink_ivy_objects PRIVATE pprzlink_core PkgConfig::IVY)
    add_library(pprzlink_ivy STATIC $<TARGET_OBJECTS:pprzlink_ivy_objects>)
    configure_pprzlink_target(pprzlink_ivy)
    target_link_libraries(pprzlink_ivy PUBLIC pprzlink_core PkgConfig::IVY Threads::Threads)
    add_library(pprzlink::ivy ALIAS pprzlink_ivy)
    list(APPEND LEGACY_OBJECTS $<TARGET_OBJECTS:pprzlink_ivy_objects>)
    list(APPEND LEGACY_LIBRARIES PkgConfig::IVY)
    list(APPEND PUBLIC_HEADERS pprzlink/IvyLink.h)
endif()

add_library(pprzlink++ SHARED ${LEGACY_OBJECTS})
add_library(pprzlink++_static STATIC ${LEGACY_OBJECTS})
foreach(target pprzlink++ pprzlink++_static)
    configure_pprzlink_target(${target})
    target_link_libraries(${target} PUBLIC ${LEGACY_LIBRARIES})
endforeach()

set(PPRZLINK_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/pprzlink++/cmake")
function(export_pprzlink_component component)
    set(target pprzlink_${component})
    set_target_properties(${target} PROPERTIES EXPORT_NAME ${component})
    install(TARGETS ${target} EXPORT ${target}Targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(EXPORT ${target}Targets NAMESPACE pprzlink:: DESTINATION ${PPRZLINK_CMAKE_INSTALL_DIR})
    export(EXPORT ${target}Targets NAMESPACE pprzlink:: FILE "${CMAKE_CURRENT_BINARY_DIR}/${target}Targets.cmake")
endfunction()
export_pprzlink_component(core)
export_pprzlink_component(io)
if(PPRZLINK_WITH_IVY)
    export_pprzlink_component(ivy)
endif()
install(TARGETS pprzlink++ pprzlink++_static EXPORT pprzlink++Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(EXPORT pprzlink++Targets DESTINATION ${PPRZLINK_CMAKE_INSTALL_DIR})
export(EXPORT pprzlink++Targets FILE "${CMAKE_CURRENT_BINARY_DIR}/pprzlink++Targets.cmake")
install(FILES ${PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pprzlink)
install(FILES pprzlink/exceptions/pprzlink_exception.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pprzlink/exceptions)
install(FILES pprzlink/detail/CheckedNumber.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pprzlink/detail)
configure_package_config_file(cmake/pprzlink++Config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/pprzlink++Config.cmake" INSTALL_DESTINATION ${PPRZLINK_CMAKE_INSTALL_DIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pprzlink++Config.cmake" DESTINATION ${PPRZLINK_CMAKE_INSTALL_DIR})

if(PPRZLINK_BUILD_EXAMPLES)
    add_subdirectory(pprzlink/examples)
endif()
if(PPRZLINK_BUILD_LINK AND UNIX)
    add_subdirectory(apps/link)
endif()
if(BUILD_TESTING)
    if(PPRZLINK_WITH_IVY AND UNIX)
        add_executable(link_test_bus pprzlink/tests/LinkTestBus.cpp)
        target_link_libraries(link_test_bus PRIVATE pprzlink++)
        target_compile_options(link_test_bus PRIVATE -Wall -Wextra -Werror)
    endif()
    function(add_pprzlink_test name source)
        add_executable(${name}_test pprzlink/tests/${source})
        target_link_libraries(${name}_test PRIVATE pprzlink::core)
        target_compile_options(${name}_test PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ${name} COMMAND ${name}_test)
        set_tests_properties(${name} PROPERTIES TIMEOUT 30)
    endfunction()

    if(PPRZLINK_BUILD_EXAMPLES AND PPRZLINK_WITH_IVY)
        add_test(NAME ivy_round_trip COMMAND ivy_round_trip
                 "${CMAKE_CURRENT_SOURCE_DIR}/pprzlink/examples/messages.xml")
        set_tests_properties(ivy_round_trip PROPERTIES TIMEOUT 15)
    endif()

    add_pprzlink_test(serialization SerializationTest.cpp)
    add_pprzlink_test(field_value FieldValueTest.cpp)
    add_pprzlink_test(field_conversion FieldConversionTest.cpp)
    add_pprzlink_test(receive ReceiveTest.cpp)
    add_pprzlink_test(udp_transport UdpTransportTest.cpp)
    target_link_libraries(udp_transport_test PRIVATE pprzlink::io)
    if(PPRZLINK_WITH_IVY)
        add_pprzlink_test(ivy_link IvyLinkTest.cpp)
        target_link_libraries(ivy_link_test PRIVATE pprzlink::ivy)
    endif()
    add_pprzlink_test(transport TransportTest.cpp)
    add_pprzlink_test(xbee_transport XbeeTransportTest.cpp)
    add_pprzlink_test(xbee_modem XbeeModemTest.cpp)
    add_pprzlink_test(xbee_baudrate XbeeBaudrateTest.cpp)
    add_pprzlink_test(dictionary DictionaryTest.cpp)
    add_pprzlink_test(ivy_message_codec IvyMessageCodecTest.cpp)
    add_pprzlink_test(link_library LinkLibraryTest.cpp)
    if(PPRZLINK_BUILD_LINK AND UNIX)
        add_executable(link_policy_test pprzlink/tests/LinkPolicyTest.cpp)
        target_link_libraries(link_policy_test PRIVATE pprzlink_agent)
        target_compile_options(link_policy_test PRIVATE -Wall -Wextra -Werror)
        add_test(NAME link_policy COMMAND link_policy_test)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(PPRZLINK_OCAML_REFERENCE "" CACHE FILEPATH "Optional OCaml link for differential tests")
        set(reference_args)
        if(PPRZLINK_OCAML_REFERENCE)
            list(APPEND reference_args --ocaml "${PPRZLINK_OCAML_REFERENCE}")
        endif()
        add_test(NAME link_agent COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/pprzlink/tests/LinkParityTest.py"
                 --cpp $<TARGET_FILE:link++> --bus $<TARGET_FILE:link_test_bus> ${reference_args})
        set_tests_properties(link_agent PROPERTIES TIMEOUT 120)
    endif()
    if(UNIX)
        add_pprzlink_test(serial_device SerialDeviceTest.cpp)
        target_link_libraries(serial_device_test PRIVATE pprzlink::io)
        if(PPRZLINK_BUILD_EXAMPLES)
            find_package(Python3 REQUIRED COMPONENTS Interpreter)
            add_test(NAME serial_messages COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_CURRENT_SOURCE_DIR}/pprzlink/tests/SerialMessagesTest.py"
                     $<TARGET_FILE:serial_messages>)
            set_tests_properties(serial_messages PROPERTIES TIMEOUT 30)
            set(client_ivy_args)
            if(PPRZLINK_WITH_IVY)
                list(APPEND client_ivy_args --ivy $<TARGET_FILE:ivy_receiver> --bus $<TARGET_FILE:link_test_bus>)
            endif()
            add_test(NAME client_examples COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_CURRENT_SOURCE_DIR}/pprzlink/tests/ClientExamplesTest.py"
                     --serial $<TARGET_FILE:serial_aircraft> --udp $<TARGET_FILE:udp_recorder> ${client_ivy_args})
            set_tests_properties(client_examples PROPERTIES TIMEOUT 40)
        endif()
    endif()
endif()
