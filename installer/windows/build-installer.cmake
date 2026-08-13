cmake_minimum_required(VERSION 3.16)

foreach(required_variable IN ITEMS PROJECT_SOURCE_DIR BINARY_DIR RUNTIME_DIR STAGING_DIR
                                   APP_VERSION BINARYCREATOR)
    if (NOT DEFINED ${required_variable})
        message(FATAL_ERROR "Missing ${required_variable}")
    endif ()
endforeach ()

set(config_dir "${STAGING_DIR}/config")
set(packages_dir "${STAGING_DIR}/packages")
set(package_root "${packages_dir}/ru.squidy.squidygit")
set(meta_dir "${package_root}/meta")
set(data_dir "${package_root}/data")
set(output_dir "${BINARY_DIR}/dist")

file(REMOVE_RECURSE "${STAGING_DIR}")
file(MAKE_DIRECTORY "${config_dir}" "${meta_dir}" "${data_dir}/licenses" "${output_dir}")

file(COPY "${PROJECT_SOURCE_DIR}/installer/windows/config/config.xml"
     DESTINATION "${config_dir}")
file(COPY "${PROJECT_SOURCE_DIR}/resources/squidygit-128.png"
     DESTINATION "${config_dir}")
file(COPY "${PROJECT_SOURCE_DIR}/installer/windows/packages/ru.squidy.squidygit/meta/package.xml"
          "${PROJECT_SOURCE_DIR}/installer/windows/packages/ru.squidy.squidygit/meta/installscript.qs"
     DESTINATION "${meta_dir}")

# Keep the installer metadata synchronized with the version from CMakeLists.txt.
set(config_file "${config_dir}/config.xml")
file(READ "${config_file}" config_xml)
string(REGEX REPLACE "<Version>[^<]+</Version>"
       "<Version>${APP_VERSION}</Version>" config_xml "${config_xml}")
file(WRITE "${config_file}" "${config_xml}")

set(package_file "${meta_dir}/package.xml")
file(READ "${package_file}" package_xml)
string(TIMESTAMP release_date "%Y-%m-%d" UTC)
string(REGEX REPLACE "<Version>[^<]+</Version>"
       "<Version>${APP_VERSION}</Version>" package_xml "${package_xml}")
string(REGEX REPLACE "<ReleaseDate>[^<]+</ReleaseDate>"
       "<ReleaseDate>${release_date}</ReleaseDate>" package_xml "${package_xml}")
file(WRITE "${package_file}" "${package_xml}")

file(COPY "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "${data_dir}")
file(COPY "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "${meta_dir}")
file(RENAME "${meta_dir}/LICENSE" "${meta_dir}/LICENSE.txt")
file(COPY "${PROJECT_SOURCE_DIR}/installer/windows/THIRD-PARTY-NOTICES.txt"
     DESTINATION "${data_dir}/licenses")

# Copy the executable, Qt DLLs, compiler runtime and plugin directories that
# windeployqt placed in the build directory. Build-system files are excluded.
file(GLOB runtime_files "${RUNTIME_DIR}/*.exe" "${RUNTIME_DIR}/*.dll")
foreach(runtime_file IN LISTS runtime_files)
    get_filename_component(runtime_name "${runtime_file}" NAME)
    if (runtime_name STREQUAL "SquidyGit.exe"
            OR runtime_name MATCHES ".*[.]dll$"
            OR runtime_name MATCHES "^vc_redist.*[.]exe$")
        file(COPY "${runtime_file}" DESTINATION "${data_dir}")
    endif ()
endforeach ()

foreach(plugin_dir IN ITEMS generic iconengines imageformats networkinformation platforms styles tls)
    if (IS_DIRECTORY "${RUNTIME_DIR}/${plugin_dir}")
        file(COPY "${RUNTIME_DIR}/${plugin_dir}" DESTINATION "${data_dir}")
    endif ()
endforeach ()

if (NOT EXISTS "${data_dir}/SquidyGit.exe")
    message(FATAL_ERROR "SquidyGit.exe was not found in ${RUNTIME_DIR}")
endif ()

set(installer_path "${output_dir}/SquidyGit-${APP_VERSION}-windows-x64.exe")
execute_process(
        COMMAND "${BINARYCREATOR}" --offline-only
                -c "${config_dir}/config.xml"
                -p "${packages_dir}"
                "${installer_path}"
        RESULT_VARIABLE result)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "binarycreator failed with exit code ${result}")
endif ()

message(STATUS "Windows installer created: ${installer_path}")
