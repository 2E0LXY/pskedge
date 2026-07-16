cmake_minimum_required(VERSION 3.21)

project(PSKedge VERSION 0.01.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
enable_testing()

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia Network)

add_library(psk-core
    src/dsp/BlockSyncCodec.cpp
    src/dsp/BlockSyncCodec.h
    src/dsp/Bpsk31Codec.cpp
    src/dsp/Bpsk31Codec.h
    src/dsp/ConvCode.cpp
    src/dsp/ConvCode.h
    src/dsp/Crc16.cpp
    src/dsp/Crc16.h
    src/dsp/PskVaricode.cpp
    src/dsp/PskVaricode.h
    src/dsp/SimpleFFT.cpp
    src/dsp/SimpleFFT.h
    src/dsp/SyncSequence.cpp
    src/dsp/SyncSequence.h
)

target_include_directories(psk-core PUBLIC src)

add_executable(pskedge
    src/AppConfig.h
    src/audio/AudioEngine.cpp
    src/audio/AudioEngine.h
    src/cat/CatController.cpp
    src/cat/CatController.h
    src/DecodedLinesWidget.cpp
    src/DecodedLinesWidget.h
    src/DecoderTableModel.cpp
    src/DecoderTableModel.h
    src/MacroEngine.cpp
    src/MacroEngine.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/SignalTypes.h
    src/WaterfallWidget.cpp
    src/WaterfallWidget.h
    src/main.cpp
)

target_link_libraries(pskedge PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Network psk-core)

add_executable(psk-core-selftest
    tools/psk_core_selftest.cpp
)

target_link_libraries(psk-core-selftest PRIVATE psk-core)

add_test(NAME psk-core-selftest COMMAND psk-core-selftest)

if(WIN32)
    set_target_properties(pskedge PROPERTIES WIN32_EXECUTABLE ON)
endif()

install(TARGETS pskedge RUNTIME DESTINATION bin)

set(CPACK_PACKAGE_NAME "pskedge")
set(CPACK_PACKAGE_VENDOR "2E0LXY")
set(CPACK_PACKAGE_CONTACT "2E0LXY")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "PSKedge - coherent, weak-signal-capable PSK31 transceiver, backward compatible with standard PSK31")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "pskedge-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")

if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "2E0LXY")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libgcc-s1, libstdc++6, libgl1")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)
