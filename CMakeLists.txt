# Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

cmake_minimum_required(VERSION 3.16)

project(SquidyGit VERSION 0.0.2 LANGUAGES CXX)

set(SQUIDYGIT_BUILD_VERSION "${PROJECT_VERSION}" CACHE STRING
        "Version displayed by the application and used by the update checker")
if (NOT SQUIDYGIT_BUILD_VERSION MATCHES
        "^[0-9]+[.][0-9]+[.][0-9]+(-[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "Invalid SquidyGit build version: ${SQUIDYGIT_BUILD_VERSION}")
endif ()

include(GNUInstallDirs)

set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.10.2 REQUIRED COMPONENTS Concurrent Core Gui LinguistTools Network Svg Widgets)

# PDF preview is optional; without it the Files page uses a byte dump.
find_package(Qt6 6.10.2 QUIET COMPONENTS PdfWidgets)

set(SQUIDYGIT_PLATFORM_IMPLEMENTATION platform/platformservices_generic.cpp)
if (WIN32)
    set(SQUIDYGIT_PLATFORM_IMPLEMENTATION platform/platformservices_windows.cpp)
elseif (APPLE)
    set(SQUIDYGIT_PLATFORM_IMPLEMENTATION platform/platformservices_macos.cpp)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SQUIDYGIT_PLATFORM_IMPLEMENTATION platform/platformservices_linux.cpp)
endif ()

add_library(squidygit_platform STATIC
        platform/platformservices.cpp
        platform/platformservices.h
        ${SQUIDYGIT_PLATFORM_IMPLEMENTATION})
target_include_directories(squidygit_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(squidygit_platform
        PUBLIC Qt6::Core
        PRIVATE Qt6::Gui)
if (WIN32)
    target_link_libraries(squidygit_platform PRIVATE shell32)
endif ()

# Core code must remain independent of Qt Widgets.
add_library(squidygit_core STATIC
        core/gitclient.cpp
        core/gitclient.h
        core/gitparse.cpp
        core/gitparse.h
        core/gitaskpass.cpp
        core/gitaskpass.h
        core/gitprocess.cpp
        core/gitprocess.h
        core/gitredact.cpp
        core/gitredact.h
        core/gittypes.h
        core/diffdocument.cpp
        core/diffdocument.h
        core/graphlayout.cpp
        core/graphlayout.h
        core/repositorysnapshot.cpp
        core/repositorysnapshot.h)

target_include_directories(squidygit_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(squidygit_core
        PUBLIC Qt6::Core
        PRIVATE squidygit_platform)

add_library(squidygit_ui STATIC
        ui/mainwindow.cpp
        ui/mainwindow.h
        ui/repositoryview.cpp
        ui/repositoryview.h
        ui/shellmetrics.h
        ui/repositorywatcher.cpp
        ui/repositorywatcher.h
        ui/commitmodel.cpp
        ui/commitmodel.h
        ui/diffview.cpp
        ui/diffview.h
        ui/filepreview.cpp
        ui/filepreview.h
        ui/filespage.cpp
        ui/filespage.h
        ui/flatcombobox.h
        ui/repositorytabstrip.h
        ui/commitgraph.cpp
        ui/commitgraph.h
        ui/credentialprompter.cpp
        ui/credentialprompter.h
        ui/dialogs.cpp
        ui/dialogs.h
        ui/icons.cpp
        ui/icons.h
        ui/pdfpreview.cpp
        ui/pdfpreview.h
        ui/sourceview.cpp
        ui/sourceview.h
        ui/syntaxhighlighter.cpp
        ui/syntaxhighlighter.h
        ui/theme.cpp
        ui/theme.h
        ui/updatechecker.cpp
        ui/updatechecker.h)

if (APPLE)
    enable_language(OBJCXX)
    target_sources(squidygit_ui PRIVATE
            ui/macosvibrancy.h
            ui/macosvibrancy.mm)
    set_source_files_properties(ui/macosvibrancy.mm PROPERTIES
            COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(squidygit_ui PRIVATE "-framework AppKit")
endif ()

if (WIN32)
    target_sources(squidygit_ui PRIVATE
            ui/windowsbackdrop.h
            ui/windowsbackdrop.cpp)
    target_link_libraries(squidygit_ui PRIVATE dwmapi)
endif ()

target_link_libraries(squidygit_ui PUBLIC
        squidygit_core
        squidygit_platform
        Qt6::Concurrent
        Qt6::Gui
        Qt6::Network
        Qt6::Svg
        Qt6::Widgets)

if (Qt6PdfWidgets_FOUND)
    target_link_libraries(squidygit_ui PUBLIC Qt6::PdfWidgets)
    target_compile_definitions(squidygit_ui PUBLIC SQUIDYGIT_HAS_PDF)
    message(STATUS "Qt PDF module found: PDF revisions are shown as pages")
else ()
    message(STATUS "Qt PDF module missing: PDF revisions are shown as a byte dump")
endif ()

qt_add_resources(squidygit_ui "branding"
        PREFIX "/branding"
        BASE resources
        FILES
            resources/logo/logo.svg
            resources/logo/logo_ghost.svg
            resources/fonts/NotoSans-Regular.ttf
            resources/fonts/NotoSans-SemiBold.ttf
            resources/fonts/NotoSans-Bold.ttf)

add_executable(SquidyGit main.cpp)
target_link_libraries(SquidyGit PRIVATE squidygit_ui)

foreach (squidygit_target IN ITEMS
        squidygit_platform squidygit_core squidygit_ui SquidyGit)
    target_compile_definitions(${squidygit_target} PRIVATE
            SQUIDYGIT_VERSION="${SQUIDYGIT_BUILD_VERSION}")
    if (MSVC)
        target_compile_options(${squidygit_target} PRIVATE /W4 /utf-8)
    else ()
        target_compile_options(${squidygit_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif ()
endforeach ()

# The English strings live in the sources, the other languages are compiled into
# the executable so that a translation is always available next to the binary.
# Scan all source targets for translatable strings.
qt_add_translations(
        TARGETS SquidyGit squidygit_ui squidygit_core
        TS_FILES
            translations/squidygit_ru.ts
            translations/squidygit_zh_CN.ts
        RESOURCE_PREFIX "/i18n")

option(SQUIDYGIT_BUILD_TESTS "Build the test suites" ON)
if (SQUIDYGIT_BUILD_TESTS)
    find_package(Qt6 6.10.2 REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif ()

set_target_properties(SquidyGit PROPERTIES
        WIN32_EXECUTABLE TRUE
        MACOSX_BUNDLE TRUE)

# Without an explicit identifier CMake's Info.plist template leaves the
# bundle fields blank, and a bundle with no identity breaks things quietly —
# notably the Open/Save panel, whose out-of-process picker needs one to work
# at all.
if (APPLE)
    set(SQUIDYGIT_MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/squidygit.icns")
    target_sources(SquidyGit PRIVATE "${SQUIDYGIT_MACOS_ICON}")
    set_source_files_properties("${SQUIDYGIT_MACOS_ICON}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")

    set_target_properties(SquidyGit PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "SquidyGit"
            MACOSX_BUNDLE_GUI_IDENTIFIER "ru.squidy.SquidyGit"
            MACOSX_BUNDLE_BUNDLE_VERSION "${SQUIDYGIT_BUILD_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${SQUIDYGIT_BUILD_VERSION}"
            MACOSX_BUNDLE_LONG_VERSION_STRING "SquidyGit ${SQUIDYGIT_BUILD_VERSION}"
            MACOSX_BUNDLE_COPYRIGHT "© 2026 Sergey Yakunin"
            MACOSX_BUNDLE_ICON_FILE "squidygit.icns")
endif ()

# On Windows, copy the Qt runtime and plugins next to the executable so it can
# be launched directly from CLion. This block is ignored on Linux and macOS.
if (WIN32)
    enable_language(RC)

    # Embed the application icon and the version information into SquidyGit.exe.
    # Explorer, the task bar and the shortcuts created by the installer all read
    # the icon from the executable itself.
    set(SQUIDYGIT_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/squidygit.ico")
    string(REPLACE "." "," SQUIDYGIT_RC_VERSION "${PROJECT_VERSION},0")
    configure_file(resources/squidygit.rc.in
            ${CMAKE_CURRENT_BINARY_DIR}/squidygit.rc @ONLY)
    set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/squidygit.rc
            APPEND PROPERTY OBJECT_DEPENDS ${SQUIDYGIT_ICON_FILE})
    target_sources(SquidyGit PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/squidygit.rc)

    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
            HINTS "${Qt6_DIR}/../../../bin"
            REQUIRED)

    # The Qt catalogs are deployed as well: they hold the translations of the
    # buttons in the standard dialogs.
    add_custom_command(TARGET SquidyGit POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --compiler-runtime
                    "$<TARGET_FILE:SquidyGit>"
            COMMENT "Deploying Qt runtime next to SquidyGit"
            VERBATIM)

    set(PORTABLE_STAGING_DIR "${CMAKE_BINARY_DIR}/portable")
    add_custom_target(windows_portable
            COMMAND "${CMAKE_COMMAND}"
                    "-DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
                    "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
                    "-DRUNTIME_DIR=$<TARGET_FILE_DIR:SquidyGit>"
                    "-DSTAGING_DIR=${PORTABLE_STAGING_DIR}"
                    "-DAPP_VERSION=${PROJECT_VERSION}"
                    -P "${CMAKE_SOURCE_DIR}/installer/windows/build-portable.cmake"
            DEPENDS SquidyGit
            COMMENT "Building SquidyGit ${PROJECT_VERSION} Windows portable archive"
            VERBATIM)

    find_program(ISCC_EXECUTABLE
            NAMES iscc ISCC
            HINTS
                "C:/Program Files (x86)/Inno Setup 6"
                "C:/Program Files/Inno Setup 6")

    if (ISCC_EXECUTABLE)
        set(INSTALLER_STAGING_DIR "${CMAKE_BINARY_DIR}/installer")

        add_custom_target(windows_installer
                COMMAND "${CMAKE_COMMAND}"
                        "-DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
                        "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
                        "-DRUNTIME_DIR=$<TARGET_FILE_DIR:SquidyGit>"
                        "-DSTAGING_DIR=${INSTALLER_STAGING_DIR}"
                        "-DAPP_VERSION=${PROJECT_VERSION}"
                        "-DBUILD_VERSION=${SQUIDYGIT_BUILD_VERSION}"
                        "-DFILE_VERSION=${PROJECT_VERSION}.0"
                        "-DISCC=${ISCC_EXECUTABLE}"
                        -P "${CMAKE_SOURCE_DIR}/installer/windows/build-installer.cmake"
                DEPENDS SquidyGit
                COMMENT "Building SquidyGit ${SQUIDYGIT_BUILD_VERSION} Windows installer"
                VERBATIM)
    else ()
        message(STATUS "Inno Setup not found; windows_installer target disabled")
    endif ()
endif ()

install(TARGETS SquidyGit
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE DESTINATION .)

# Desktop integration. GNOME shows a Dock icon only for windows it can match to
# an installed .desktop file, so the entry is written with an absolute Exec path
# and the icons are checked in as PNG (rendered from Icons::applicationPixmap).
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    foreach (icon_size IN ITEMS 16 32 48 64 128 256 512)
        install(FILES resources/squidygit-${icon_size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${icon_size}x${icon_size}/apps
                RENAME squidygit.png)
    endforeach ()

    install(FILES resources/squidygit.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
            PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

    install(CODE "
        set(staged_prefix \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
        set(desktop_file
            \"\${staged_prefix}/${CMAKE_INSTALL_DATADIR}/applications/squidygit.desktop\")
        file(READ \"\${desktop_file}\" desktop_entry)
        string(REPLACE \"Exec=SquidyGit\"
                       \"Exec=\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/SquidyGit\"
                       desktop_entry \"\${desktop_entry}\")
        file(WRITE \"\${desktop_file}\" \"\${desktop_entry}\")
        message(STATUS \"Installed: \${desktop_file}\")

        if (\"\$ENV{DESTDIR}\" STREQUAL \"\")
            find_program(UPDATE_DESKTOP_DATABASE update-desktop-database)
            if (UPDATE_DESKTOP_DATABASE)
                execute_process(COMMAND \"\${UPDATE_DESKTOP_DATABASE}\"
                                \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/applications\"
                                ERROR_QUIET)
            endif ()
            find_program(UPDATE_ICON_CACHE gtk-update-icon-cache)
            if (UPDATE_ICON_CACHE)
                execute_process(COMMAND \"\${UPDATE_ICON_CACHE}\" -f -t -q
                                \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/icons/hicolor\"
                                ERROR_QUIET)
            endif ()
        endif ()
    ")

    install(FILES LICENSE
            DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/squidygit
            RENAME copyright)
    install(FILES resources/fonts/Noto-Sans-OFL.txt
            DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/squidygit/licenses)
    install(FILES README.md README.ru.md README.zh-CN.md
            DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/squidygit)

    # A packaging revision counts within one upstream version, so 0.0.2 starts
    # over at 1. A prerelease sorts before the release it leads to, and any
    # 0.0.2 sorts after every 0.0.1: 0.0.1-2 < 0.0.2-1~beta.1 < 0.0.2-1.
    set(SQUIDYGIT_DEBIAN_RELEASE 1)
    if (SQUIDYGIT_BUILD_VERSION MATCHES "^[0-9]+[.][0-9]+[.][0-9]+-(.+)$")
        set(SQUIDYGIT_DEBIAN_PRERELEASE "${CMAKE_MATCH_1}")
        string(REPLACE "-" "." SQUIDYGIT_DEBIAN_PRERELEASE
                "${SQUIDYGIT_DEBIAN_PRERELEASE}")
        set(SQUIDYGIT_DEBIAN_RELEASE "1~${SQUIDYGIT_DEBIAN_PRERELEASE}")
    endif ()
    set(SQUIDYGIT_DEBIAN_FULL_VERSION
            "${PROJECT_VERSION}-${SQUIDYGIT_DEBIAN_RELEASE}")
    configure_file(installer/linux/changelog.Debian.in
            ${CMAKE_CURRENT_BINARY_DIR}/changelog.Debian @ONLY)
    configure_file(installer/linux/SquidyGit.1.in
            ${CMAKE_CURRENT_BINARY_DIR}/SquidyGit.1 @ONLY)

    find_program(GZIP_EXECUTABLE gzip)
    if (NOT GZIP_EXECUTABLE)
        message(FATAL_ERROR "gzip is required to build the Linux package documentation")
    endif ()
    foreach (doc_file IN ITEMS changelog.Debian SquidyGit.1)
        execute_process(
                COMMAND ${GZIP_EXECUTABLE} -9n -c ${CMAKE_CURRENT_BINARY_DIR}/${doc_file}
                OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/${doc_file}.gz
                RESULT_VARIABLE gzip_result)
        if (NOT gzip_result EQUAL 0)
            message(FATAL_ERROR "Failed to compress ${doc_file}")
        endif ()
    endforeach ()

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/changelog.Debian.gz
            DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/squidygit)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/SquidyGit.1.gz
            DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "squidygit")
    set(CPACK_PACKAGE_VENDOR "Sergey Yakunin")
    set(CPACK_PACKAGE_CONTACT "Sergey Yakunin <sergey@squidy.ru>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native desktop Git client built with C++20 and Qt 6")
    string(CONCAT CPACK_DEBIAN_PACKAGE_DESCRIPTION
            "SquidyGit is a native graphical Git client for everyday repository work."
            "\nIt combines commit history, staging, branch and remote management, and a"
            "\nbuilt-in log of every Git command in one Qt 6 desktop application.")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(SQUIDYGIT_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/dist" CACHE PATH
            "Directory for generated SquidyGit packages")
    set(CPACK_PACKAGE_DIRECTORY "${SQUIDYGIT_PACKAGE_DIRECTORY}")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_STRIP_FILES TRUE)

    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_DEBIAN_PACKAGE_RELEASE ${SQUIDYGIT_DEBIAN_RELEASE})
    set(CPACK_DEBIAN_PACKAGE_SECTION "vcs")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

    # Release builds link against a Qt that is not installed through apt, so
    # dpkg-shlibdeps cannot map the Qt libraries to packages. Without the
    # dependencies below the package installs and then fails to start.
    # libqt6core6 was renamed to libqt6core6t64 in the 64-bit time_t transition.
    set(SQUIDYGIT_QT_RUNTIME "${Qt6_VERSION_MAJOR}.${Qt6_VERSION_MINOR}")
    string(CONCAT CPACK_DEBIAN_PACKAGE_DEPENDS
            "git"
            ", libqt6core6t64 (>= ${SQUIDYGIT_QT_RUNTIME})"
            " | libqt6core6 (>= ${SQUIDYGIT_QT_RUNTIME})"
            ", libqt6gui6 (>= ${SQUIDYGIT_QT_RUNTIME})"
            ", libqt6network6 (>= ${SQUIDYGIT_QT_RUNTIME})"
            ", libqt6widgets6 (>= ${SQUIDYGIT_QT_RUNTIME})")
    if (Qt6PdfWidgets_FOUND)
        string(APPEND CPACK_DEBIAN_PACKAGE_DEPENDS
                ", libqt6pdf6 (>= ${SQUIDYGIT_QT_RUNTIME})"
                ", libqt6pdfwidgets6 (>= ${SQUIDYGIT_QT_RUNTIME})")
    endif ()

    # Without the Qt catalogs the buttons of the standard dialogs stay English
    # while the rest of the interface is translated.
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-translations-l10n")

    include(CPack)

    # A dedicated target for CLion. It always creates the package from a
    # separate Release build, even when the active CLion profile is Debug.
    set(SQUIDYGIT_DEB_BUILD_DIR "${CMAKE_BINARY_DIR}/deb-release")
    set(SQUIDYGIT_DEB_CMAKE_ARGS
            "-DCMAKE_BUILD_TYPE=Release"
            "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DSQUIDYGIT_BUILD_VERSION=${SQUIDYGIT_BUILD_VERSION}"
            "-DSQUIDYGIT_PACKAGE_DIRECTORY=${CMAKE_BINARY_DIR}/dist"
            "-DSQUIDYGIT_BUILD_TESTS=OFF")
    if (CMAKE_MAKE_PROGRAM)
        list(APPEND SQUIDYGIT_DEB_CMAKE_ARGS
                "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
    endif ()
    if (Qt6_DIR)
        list(APPEND SQUIDYGIT_DEB_CMAKE_ARGS "-DQt6_DIR=${Qt6_DIR}")
    endif ()

    add_custom_target(deb_package
            COMMAND "${CMAKE_COMMAND}"
                    -S "${CMAKE_SOURCE_DIR}"
                    -B "${SQUIDYGIT_DEB_BUILD_DIR}"
                    -G "${CMAKE_GENERATOR}"
                    ${SQUIDYGIT_DEB_CMAKE_ARGS}
            COMMAND "${CMAKE_COMMAND}" --build "${SQUIDYGIT_DEB_BUILD_DIR}" --parallel
            COMMAND "${CMAKE_COMMAND}" --build "${SQUIDYGIT_DEB_BUILD_DIR}" --target package
            COMMENT "Building SquidyGit ${SQUIDYGIT_BUILD_VERSION} DEB package (Release)"
            USES_TERMINAL
            VERBATIM)
endif ()
