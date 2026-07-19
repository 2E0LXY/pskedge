cmake_minimum_required(VERSION 3.21)

# This version number drives the actual Windows Installer ProductVersion
# (see CPACK_PACKAGE_VERSION below) and, combined with the fixed
# CPACK_WIX_UPGRADE_GUID later in this file, is what Windows Installer
# uses to detect "this is a newer version, do a proper upgrade" versus
# "this is the same product already installed, do a repair". It stayed at
# 0.2.0 across 8 separate tagged releases (each claiming to fix
# something) because bumping the git tag alone does not bump this - found
# only when a real install of a 'new' release visibly changed nothing,
# because Windows Installer had been treating every one of those releases
# as a repair of the same already-installed 0.2.0 product, not an
# upgrade. Bump this for every release that should actually replace
# installed files, not just the git tag.
project(PSKedge VERSION 0.5.16 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
enable_testing()

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia Network WebSockets)

add_library(psk-core
    src/dsp/BlockSyncCodec.cpp
    src/dsp/BlockSyncCodec.h
    src/dsp/Bpsk31Codec.cpp
    src/dsp/Bpsk31Codec.h
    src/dsp/Bpsk31StreamDecoder.cpp
    src/dsp/Bpsk31StreamDecoder.h
    src/dsp/ConvCode.cpp
    src/dsp/ConvCode.h
    src/dsp/Crc16.cpp
    src/dsp/Crc16.h
    src/dsp/CwCodec.cpp
    src/dsp/CwCodec.h
    src/dsp/PskVaricode.cpp
    src/dsp/PskVaricode.h
    src/dsp/QpskConvCode.cpp
    src/dsp/QpskConvCode.h
    src/dsp/ft8/Crc14.cpp
    src/dsp/ft8/Crc14.h
    src/dsp/ft8/Ldpc174_91.cpp
    src/dsp/ft8/Ldpc174_91.h
    src/dsp/ft8/Ldpc174_91_data.h
    src/dsp/ft8/SourceEncoding.cpp
    src/dsp/ft8/SourceEncoding.h
    src/dsp/Qpsk31Codec.cpp
    src/dsp/Qpsk31Codec.h
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
    src/LevelMeter.cpp
    src/LevelMeter.h
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
    src/remote/RemoteControlServer.cpp
    src/remote/RemoteControlServer.h
    $<$<PLATFORM_ID:Windows>:src/pskedge.rc>
)

target_link_libraries(pskedge PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Network Qt6::WebSockets psk-core)

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

install(TARGETS pskedge RUNTIME DESTINATION bin)

if(WIN32)
    set_target_properties(pskedge PROPERTIES WIN32_EXECUTABLE ON)

    # Bundle the Qt runtime automatically as part of `cmake --install`, so
    # a CPack/NSIS package built from the install tree is a genuine
    # standalone installer rather than needing a separate manual
    # windeployqt step in CI (which only produced a raw folder to zip, not
    # an installer). windeployqt normally lives alongside qmake in the Qt
    # bin directory - Qt6_DIR points at <QtRoot>/lib/cmake/Qt6, so the Qt
    # bin directory is three levels up from there.
    #
    # This MUST come after install(TARGETS pskedge ...) above: CMake runs
    # install() rules in the order they appear in this file, and
    # windeployqt needs pskedge.exe to already be sitting in the staged
    # install/CPack tree before it can deploy DLLs next to it. An earlier
    # version of this file got that ordering backwards despite this same
    # comment already warning about it - CPack failed on the real Windows
    # CI runner with '"...\bin\pskedge.exe" does not exist' because the
    # windeployqt install(CODE) block ran before install(TARGETS pskedge)
    # had copied the executable into place. Verify the actual file order
    # (grep '^install\|^if(WIN32)' this file) rather than trusting a
    # comment describes what the code actually does, which is exactly how
    # this got missed the first time.
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

set(CPACK_PACKAGE_NAME "pskedge")
set(CPACK_PACKAGE_VENDOR "2E0LXY")
set(CPACK_PACKAGE_CONTACT "2E0LXY")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "PSKedge - coherent, weak-signal-capable PSK31 transceiver, backward compatible with standard PSK31")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "pskedge-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")

# CPACK_RESOURCE_FILE_LICENSE is picked up by the WiX generator (a .txt
# is auto-converted to RTF for the MSI license-acceptance page - see
# https://cmake.org/cmake/help/latest/cpack_gen/wix.html) so this alone
# is what makes the Windows installer show a click-through EULA. Debian
# packages don't have an equivalent interactive license step (dpkg/apt
# just install; there's no MSI-style acceptance dialog), so the DEB
# package instead gets the license file installed into its standard
# documentation location below, for anyone who wants to read it - this
# is the conventional Debian approach, not a workaround.
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")
install(FILES LICENSE.txt DESTINATION share/doc/pskedge)

if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "2E0LXY")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libgcc-s1, libstdc++6, libgl1")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

if(WIN32)
    # MSI via the WiX Toolset instead of NSIS. Switched after NSIS's
    # MUI2 dynamically-loaded bitmap macros (MUI_HEADERIMAGE_INIT,
    # MUI_WELCOMEFINISHPAGE_INIT) failed installer compilation with "no
    # files found" for files confirmed to exist at the referenced path -
    # see git history for the investigation. MSI is also the more
    # standard Windows installer format (Programs and Features integration,
    # Group Policy deployment support) than an NSIS-produced Setup.exe.
    #
    # CPack's default is WiX Toolset v3 (candle.exe/light.exe), which is
    # what ships pre-installed on GitHub's windows-2022 runner image - not
    # assumed, checked via a diagnostic step in the CI workflow before
    # relying on it. Not explicitly setting CPACK_WIX_VERSION here to use
    # that default rather than pin a version this project hasn't verified.
    set(CPACK_GENERATOR "WIX")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "PSKedge")
    set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/assets/pskedge.ico")
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "PSKedge")
    set(CPACK_WIX_PROPERTY_ARPHELPLINK "https://github.com/2E0LXY/pskedge")
    set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "https://github.com/2E0LXY/pskedge")

    # No "Launch PSKedge now" checkbox - removed per explicit request.
    # The custom-action chain that implemented it (assets/wix_launch_patch.xml,
    # since deleted - see git history) never actually worked, and its own
    # comments admitted real uncertainty about whether INSTALLFOLDER was
    # even the correct WiX directory Id for this generator, unconfirmed
    # against CPack's actual generated .wxs. Simplest reliable fix: the
    # Start Menu shortcut CPack already creates is a normal, working way
    # to launch the app after install, without a custom action that has
    # never actually functioned.

    # CPACK_WIX_PROGRAM_MENU_FOLDER above only names the Start Menu folder
    # - it does not itself create any shortcut. Actually creating the
    # Start Menu and Desktop shortcuts needs these WiX-generator-specific
    # install properties (confirmed via CMake's own CPACK_DESKTOP_SHORTCUTS
    # docs, which state it's "currently only supported by the CPack WIX
    # Generator", and cross-checked against CMake's own WiX generator
    # source rather than assumed from the variable name alone). The
    # install-relative path must match install(TARGETS pskedge RUNTIME
    # DESTINATION bin) below.
    set_property(
        INSTALL "bin/$<TARGET_FILE_NAME:pskedge>"
        PROPERTY CPACK_START_MENU_SHORTCUTS "PSKedge"
    )
    set_property(
        INSTALL "bin/$<TARGET_FILE_NAME:pskedge>"
        PROPERTY CPACK_DESKTOP_SHORTCUTS "PSKedge"
    )

    # Upgrade GUID must stay IDENTICAL across every future release - it's
    # how Windows Installer recognises "this is an update to an existing
    # install" rather than a conflicting parallel install. Generated once
    # (a real random UUID, not a hand-typed placeholder) and must never be
    # regenerated or changed after this.
    set(CPACK_WIX_UPGRADE_GUID "CD3190DF-98B2-402F-934D-A21D8A1EEAC6")

    # No custom banner/dialog images - removed per explicit request after
    # repeated placement issues across several earlier attempts were
    # never fully resolved (see git history: backgrounds, icon-only
    # corner placement, light-vs-dark text legibility, and still "wrong
    # place" after all of that). WiX's own plain default UI is used
    # instead, which is simpler and reliably correct rather than another
    # attempt at a fix that has a track record of not sticking.
endif()

include(CPack)
