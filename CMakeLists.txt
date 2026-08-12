# Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

cmake_minimum_required(VERSION 3.16)

project(SquidyGit VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Concurrent Core Gui Widgets)

add_executable(SquidyGit
        main.cpp
        mainwindow.cpp
        mainwindow.h
        repositoryview.cpp
        repositoryview.h
        gitclient.cpp
        gitclient.h
        diffview.cpp
        diffview.h
        commitgraph.cpp
        commitgraph.h
        dialogs.cpp
        dialogs.h
        icons.cpp
        icons.h
        theme.cpp
        theme.h)

target_link_libraries(SquidyGit PRIVATE
        Qt6::Concurrent
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

if (MSVC)
    target_compile_options(SquidyGit PRIVATE /W4 /utf-8)
else ()
    target_compile_options(SquidyGit PRIVATE -Wall -Wextra -Wpedantic)
endif ()

set_target_properties(SquidyGit PROPERTIES
        WIN32_EXECUTABLE TRUE
        MACOSX_BUNDLE TRUE)

# On Windows, copy the Qt runtime and plugins next to the executable so it can
# be launched directly from CLion. This block is ignored on Linux and macOS.
if (WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
            HINTS "${Qt6_DIR}/../../../bin"
            REQUIRED)

    add_custom_command(TARGET SquidyGit POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --compiler-runtime
                    "$<TARGET_FILE:SquidyGit>"
            COMMENT "Deploying Qt runtime next to SquidyGit"
            VERBATIM)

    find_program(BINARYCREATOR_EXECUTABLE binarycreator
            HINTS
                "C:/Qt/Tools/QtInstallerFramework/4.11/bin"
                "C:/Qt/Tools/QtInstallerFramework/4.10/bin")

    if (BINARYCREATOR_EXECUTABLE)
        set(INSTALLER_STAGING_DIR "${CMAKE_BINARY_DIR}/installer")
        set(INSTALLER_PACKAGE_DATA
                "${INSTALLER_STAGING_DIR}/packages/ru.squidy.squidygit/data")

        add_custom_target(windows_installer
                COMMAND "${CMAKE_COMMAND}"
                        "-DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
                        "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
                        "-DSTAGING_DIR=${INSTALLER_STAGING_DIR}"
                        "-DAPP_VERSION=${PROJECT_VERSION}"
                        "-DBINARYCREATOR=${BINARYCREATOR_EXECUTABLE}"
                        -P "${CMAKE_SOURCE_DIR}/installer/windows/build-installer.cmake"
                DEPENDS SquidyGit
                COMMENT "Building SquidyGit ${PROJECT_VERSION} Windows installer"
                VERBATIM)
    else ()
        message(STATUS "Qt Installer Framework not found; windows_installer target disabled")
    endif ()
endif ()

install(TARGETS SquidyGit
        RUNTIME DESTINATION bin
        BUNDLE DESTINATION .)

# Desktop integration. GNOME shows a Dock icon only for windows it can match to
# an installed .desktop file, so the entry is written with an absolute Exec path
# and the icons are checked in as PNG (rendered from Icons::applicationPixmap).
if (UNIX AND NOT APPLE)
    foreach (icon_size IN ITEMS 16 32 48 64 128 256 512)
        install(FILES resources/squidygit-${icon_size}.png
                DESTINATION share/icons/hicolor/${icon_size}x${icon_size}/apps
                RENAME squidygit.png)
    endforeach ()

    install(CODE "
        file(READ \"${CMAKE_CURRENT_SOURCE_DIR}/resources/squidygit.desktop\" desktop_entry)
        string(REPLACE \"Exec=SquidyGit\"
                       \"Exec=\${CMAKE_INSTALL_PREFIX}/bin/SquidyGit\"
                       desktop_entry \"\${desktop_entry}\")
        file(WRITE \"\${CMAKE_INSTALL_PREFIX}/share/applications/squidygit.desktop\"
             \"\${desktop_entry}\")
        message(STATUS \"Installed: \${CMAKE_INSTALL_PREFIX}/share/applications/squidygit.desktop\")

        find_program(UPDATE_DESKTOP_DATABASE update-desktop-database)
        if (UPDATE_DESKTOP_DATABASE)
            execute_process(COMMAND \"\${UPDATE_DESKTOP_DATABASE}\"
                            \"\${CMAKE_INSTALL_PREFIX}/share/applications\"
                            ERROR_QUIET)
        endif ()
        find_program(UPDATE_ICON_CACHE gtk-update-icon-cache)
        if (UPDATE_ICON_CACHE)
            execute_process(COMMAND \"\${UPDATE_ICON_CACHE}\" -f -t -q
                            \"\${CMAKE_INSTALL_PREFIX}/share/icons/hicolor\"
                            ERROR_QUIET)
        endif ()
    ")
endif ()
