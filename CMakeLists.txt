cmake_minimum_required(VERSION 3.21)

project(PSKedge VERSION 0.2.0 LANGUAGES CXX)

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

# Copy assets/ (splash screen image, if present - see assets/README.md)
# next to the built executable, and again at install time, so
# QCoreApplication::applicationDirPath() + "/assets/..." resolves the same
# way whether running from the build tree or an installed copy.
add_custom_command(TARGET pskedge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:pskedge>/assets"
)
install(DIRECTORY assets/ DESTINATION bin/assets)

add_executable(psk-core-selftest
    tools/psk_core_selftest.cpp
)

target_link_libraries(psk-core-selftest PRIVATE psk-core)

add_test(NAME psk-core-selftest COMMAND psk-core-selftest)

if(WIN32)
    set_target_properties(pskedge PROPERTIES WIN32_EXECUTABLE ON)

    # Bundle the Qt runtime automatically as part of `cmake --install`, so
    # a CPack/NSIS package built from the install tree is a genuine
    # standalone installer rather than needing a separate manual
    # windeployqt step in CI (which only produced a raw folder to zip, not
    # an installer). windeployqt normally lives alongside qmake in the Qt
    # bin directory - Qt6_DIR points at <QtRoot>/lib/cmake/Qt6, so the Qt
    # bin directory is three levels up from there.
    get_target_property(_qt_qmake_location Qt6::qmake IMPORTED_LOCATION)
    if(_qt_qmake_location)
        get_filename_component(_qt_bin_dir "${_qt_qmake_location}" DIRECTORY)
    endif()
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}" "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        install(CODE "
            message(STATUS \"Running windeployqt on the installed executable\")
            execute_process(
                COMMAND \"${WINDEPLOYQT_EXECUTABLE}\" --release --no-translations --no-compiler-runtime \"\${CMAKE_INSTALL_PREFIX}/bin/pskedge.exe\"
                RESULT_VARIABLE _windeployqt_result
            )
            if(NOT _windeployqt_result EQUAL 0)
                message(WARNING \"windeployqt failed (exit code \${_windeployqt_result}) - the installed package may be missing Qt runtime DLLs\")
            endif()
        ")
    else()
        message(WARNING "windeployqt not found - the installed/packaged executable will be missing Qt runtime DLLs unless deployed separately")
    endif()
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

if(WIN32)
    # NSIS produces a real Setup.exe installer (Start Menu shortcut,
    # uninstaller, install-directory picker) rather than a raw folder to
    # unzip. Requires makensis to be available on the build machine/CI
    # runner (installed separately - see .github/workflows/build.yml).
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "PSKedge")
    set(CPACK_NSIS_PACKAGE_NAME "PSKedge")
    set(CPACK_NSIS_DISPLAY_NAME "PSKedge")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\PSKedge.lnk' '$INSTDIR\\\\bin\\\\pskedge.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\PSKedge.lnk'")
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_CONTACT "2E0LXY")
    set(CPACK_NSIS_HELP_LINK "https://github.com/2E0LXY/pskedge")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/2E0LXY/pskedge")

    # Installer branding images - all optional. See assets/README.md for
    # the exact formats/dimensions NSIS requires. CPack/NSIS errors out at
    # configure time if these variables point at a file that doesn't
    # exist, so each is only set when the corresponding asset is present -
    # the installer still builds correctly (just without custom branding)
    # if these haven't been added yet.
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/installer_header.bmp")
        set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/assets/installer_header.bmp")
        set(CPACK_NSIS_MUI_HEADERIMAGE "${CMAKE_SOURCE_DIR}/assets/installer_header.bmp")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/installer_welcome.bmp")
        set(CPACK_NSIS_MUI_WELCOMEFINISHPAGE_BITMAP "${CMAKE_SOURCE_DIR}/assets/installer_welcome.bmp")
        set(CPACK_NSIS_MUI_UNWELCOMEFINISHPAGE_BITMAP "${CMAKE_SOURCE_DIR}/assets/installer_welcome.bmp")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/pskedge.ico")
        set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/assets/pskedge.ico")
        set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/assets/pskedge.ico")
    endif()
endif()

include(CPack)
