cmake_minimum_required(VERSION 3.16)

foreach(required_variable IN ITEMS PROJECT_SOURCE_DIR BINARY_DIR RUNTIME_DIR STAGING_DIR
                                   APP_VERSION BUILD_VERSION FILE_VERSION ISCC)
    if (NOT DEFINED ${required_variable})
        message(FATAL_ERROR "Missing ${required_variable}")
    endif ()
endforeach ()

set(payload_dir "${STAGING_DIR}/app")
set(output_dir "${BINARY_DIR}/dist")

file(REMOVE_RECURSE "${STAGING_DIR}")
file(MAKE_DIRECTORY "${payload_dir}/licenses" "${output_dir}")

file(COPY "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "${payload_dir}")
file(RENAME "${payload_dir}/LICENSE" "${payload_dir}/LICENSE.txt")
file(COPY "${PROJECT_SOURCE_DIR}/installer/windows/THIRD-PARTY-NOTICES.txt"
     DESTINATION "${payload_dir}/licenses")

# Copy the executable, Qt DLLs, compiler runtime and plugin directories that
# windeployqt placed in the build directory. Build-system files are excluded.
file(GLOB runtime_files "${RUNTIME_DIR}/*.exe" "${RUNTIME_DIR}/*.dll")
foreach(runtime_file IN LISTS runtime_files)
    get_filename_component(runtime_name "${runtime_file}" NAME)
    if (runtime_name STREQUAL "SquidyGit.exe"
            OR runtime_name MATCHES ".*[.]dll$"
            OR runtime_name MATCHES "^vc_redist.*[.]exe$")
        file(COPY "${runtime_file}" DESTINATION "${payload_dir}")
    endif ()
endforeach ()

foreach(plugin_dir IN ITEMS generic iconengines imageformats networkinformation platforms styles tls)
    if (IS_DIRECTORY "${RUNTIME_DIR}/${plugin_dir}")
        file(COPY "${RUNTIME_DIR}/${plugin_dir}" DESTINATION "${payload_dir}")
    endif ()
endforeach ()

if (NOT EXISTS "${payload_dir}/SquidyGit.exe")
    message(FATAL_ERROR "SquidyGit.exe was not found in ${RUNTIME_DIR}")
endif ()

set(output_name "SquidyGit-${APP_VERSION}-windows-x64")
foreach(path_variable IN ITEMS payload_dir output_dir)
    file(TO_NATIVE_PATH "${${path_variable}}" ${path_variable}_native)
endforeach ()
file(TO_NATIVE_PATH "${PROJECT_SOURCE_DIR}/resources/squidygit.ico" icon_native)
file(TO_NATIVE_PATH "${payload_dir}/LICENSE.txt" license_native)

execute_process(
        COMMAND "${ISCC}" /Qp
                "/DAppVersion=${BUILD_VERSION}"
                "/DFileVersion=${FILE_VERSION}"
                "/DSourceDir=${payload_dir_native}"
                "/DIconFile=${icon_native}"
                "/DLicenseFile=${license_native}"
                "/DOutputDir=${output_dir_native}"
                "/DOutputBaseFilename=${output_name}"
                "${PROJECT_SOURCE_DIR}/installer/windows/squidygit.iss"
        RESULT_VARIABLE result)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "Inno Setup failed with exit code ${result}")
endif ()

message(STATUS "Windows installer created: ${output_dir}/${output_name}.exe")
