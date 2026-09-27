cmake_minimum_required(VERSION 3.20)
project(pprzlink++ LANGUAGES CXX)

include(GNUInstallDirs)
find_package(tinyxml2 REQUIRED)
find_package(Boost 1.69 CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(IVY REQUIRED IMPORTED_TARGET ivy-cpp>=3.18)

set(SOURCE
        pprzlink/BoostSerialPortDevice.cpp
        pprzlink/FieldValue.cpp
        pprzlink/BinaryCodec.cpp
        pprzlink/TextCodec.cpp
        pprzlink/IvyMessageCodec.cpp
        pprzlink/IvyLink.cpp
        pprzlink/Message.cpp
        pprzlink/MessageDefinition.cpp
        pprzlink/MessageDictionary.cpp
        pprzlink/MessageField.cpp
        pprzlink/MessageFieldTypes.cpp
        pprzlink/PprzTransport.cpp
        pprzlink/XbeeTransport.cpp
        pprzlink/XbeeModem.cpp)

set(HEADERS_PPRZLINK
        pprzlink/BoostSerialPortDevice.h
        pprzlink/Device.h
        pprzlink/FieldValue.h
        pprzlink/BinaryCodec.h
        pprzlink/TextCodec.h
        pprzlink/IvyMessageCodec.h
        pprzlink/IvyLink.h
        pprzlink/Message.h
        pprzlink/MessageDefinition.h
        pprzlink/MessageDictionary.h
        pprzlink/MessageField.h
        pprzlink/MessageFieldTypes.h
        pprzlink/PprzTransport.h
        pprzlink/XbeeTransport.h
        pprzlink/XbeeModem.h
        pprzlink/Transport.h)

add_library(pprzlink++_static STATIC ${SOURCE})
add_library(pprzlink++ SHARED ${SOURCE})
foreach(target pprzlink++ pprzlink++_static)
    target_compile_features(${target} PUBLIC cxx_std_23)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(${target} PUBLIC
        PkgConfig::IVY tinyxml2::tinyxml2 Boost::headers Threads::Threads)
endforeach()

install(TARGETS pprzlink++ pprzlink++_static
        EXPORT pprzlink++Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${HEADERS_PPRZLINK} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pprzlink)
install(FILES pprzlink/exceptions/pprzlink_exception.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pprzlink/exceptions)

# Propagate public dependencies to installed and static consumers.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/pprzlink++Config.cmake" [=[
include(CMakeFindDependencyMacro)
find_dependency(tinyxml2)
find_dependency(Boost 1.69 CONFIG)
find_dependency(Threads)
find_dependency(PkgConfig)
pkg_check_modules(IVY REQUIRED IMPORTED_TARGET ivy-cpp>=3.18)
include("${CMAKE_CURRENT_LIST_DIR}/pprzlink++Targets.cmake")
]=])
export(EXPORT pprzlink++Targets
       FILE "${CMAKE_CURRENT_BINARY_DIR}/pprzlink++Targets.cmake")
install(EXPORT pprzlink++Targets DESTINATION ${CMAKE_INSTALL_DATADIR}/pprzlink++/cmake)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pprzlink++Config.cmake"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/pprzlink++/cmake)

option(PPRZLINK_BUILD_EXAMPLES "Build Ivy and serial transport examples" ON)
if(PPRZLINK_BUILD_EXAMPLES)
    add_executable(ivy_round_trip pprzlink/examples/IvyRoundTrip.cpp)
    target_link_libraries(ivy_round_trip PRIVATE pprzlink++)
    target_compile_options(ivy_round_trip PRIVATE -Wall -Wextra -Werror)
    add_executable(serial_messages pprzlink/examples/SerialMessages.cpp)
    target_link_libraries(serial_messages PRIVATE pprzlink++)
    target_compile_options(serial_messages PRIVATE -Wall -Wextra -Werror)
endif()

include(CTest)
if(BUILD_TESTING)
    function(add_pprzlink_test name source)
        add_executable(${name}_test pprzlink/tests/${source})
        target_link_libraries(${name}_test PRIVATE pprzlink++)
        target_compile_options(${name}_test PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ${name} COMMAND ${name}_test)
        set_tests_properties(${name} PROPERTIES TIMEOUT 30)
    endfunction()

    if(PPRZLINK_BUILD_EXAMPLES)
        add_test(NAME ivy_round_trip COMMAND ivy_round_trip
                 "${CMAKE_CURRENT_SOURCE_DIR}/pprzlink/examples/messages.xml")
        set_tests_properties(ivy_round_trip PROPERTIES TIMEOUT 15)
    endif()

    add_pprzlink_test(serialization SerializationTest.cpp)
    add_pprzlink_test(field_value FieldValueTest.cpp)
    add_pprzlink_test(ivy_link IvyLinkTest.cpp)
    add_pprzlink_test(transport TransportTest.cpp)
    add_pprzlink_test(xbee_transport XbeeTransportTest.cpp)
    add_pprzlink_test(xbee_modem XbeeModemTest.cpp)
    add_pprzlink_test(xbee_baudrate XbeeBaudrateTest.cpp)
    add_pprzlink_test(dictionary DictionaryTest.cpp)
    add_pprzlink_test(ivy_message_codec IvyMessageCodecTest.cpp)
    if(UNIX)
        add_pprzlink_test(serial_device SerialDeviceTest.cpp)
        if(PPRZLINK_BUILD_EXAMPLES)
            find_package(Python3 REQUIRED COMPONENTS Interpreter)
            add_test(NAME serial_messages COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_CURRENT_SOURCE_DIR}/pprzlink/tests/SerialMessagesTest.py"
                     $<TARGET_FILE:serial_messages>)
            set_tests_properties(serial_messages PROPERTIES TIMEOUT 30)
        endif()
    endif()
endif()
