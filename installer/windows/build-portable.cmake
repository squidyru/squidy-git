cmake_minimum_required(VERSION 3.16)

foreach(required_variable IN ITEMS PROJECT_SOURCE_DIR BINARY_DIR RUNTIME_DIR STAGING_DIR
                                   APP_VERSION)
    if (NOT DEFINED ${required_variable})
        message(FATAL_ERROR "Missing ${required_variable}")
    endif ()
endforeach ()

set(output_dir "${BINARY_DIR}/dist")

file(REMOVE_RECURSE "${STAGING_DIR}")
file(MAKE_DIRECTORY "${STAGING_DIR}/licenses" "${output_dir}")

file(COPY "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "${STAGING_DIR}")
file(COPY "${PROJECT_SOURCE_DIR}/README.md"
          "${PROJECT_SOURCE_DIR}/README.ru.md"
          "${PROJECT_SOURCE_DIR}/README.zh-CN.md"
     DESTINATION "${STAGING_DIR}")
file(COPY "${PROJECT_SOURCE_DIR}/installer/windows/THIRD-PARTY-NOTICES.txt"
     DESTINATION "${STAGING_DIR}/licenses")
file(COPY "${PROJECT_SOURCE_DIR}/resources/fonts/Noto-Sans-OFL.txt"
     DESTINATION "${STAGING_DIR}/licenses")
file(WRITE "${STAGING_DIR}/portable.marker" "SquidyGit portable edition\n")

file(GLOB runtime_files "${RUNTIME_DIR}/*.exe" "${RUNTIME_DIR}/*.dll")
foreach(runtime_file IN LISTS runtime_files)
    get_filename_component(runtime_name "${runtime_file}" NAME)
    if (runtime_name STREQUAL "SquidyGit.exe"
            OR runtime_name MATCHES ".*[.]dll$"
            OR runtime_name MATCHES "^vc_redist.*[.]exe$")
        file(COPY "${runtime_file}" DESTINATION "${STAGING_DIR}")
    endif ()
endforeach ()

foreach(plugin_dir IN ITEMS generic iconengines imageformats networkinformation platforms styles tls)
    if (IS_DIRECTORY "${RUNTIME_DIR}/${plugin_dir}")
        file(COPY "${RUNTIME_DIR}/${plugin_dir}" DESTINATION "${STAGING_DIR}")
    endif ()
endforeach ()

if (NOT EXISTS "${STAGING_DIR}/SquidyGit.exe")
    message(FATAL_ERROR "SquidyGit.exe was not found in ${RUNTIME_DIR}")
endif ()

set(archive_path
        "${output_dir}/SquidyGit-${APP_VERSION}-windows-x64-portable.zip")
execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar cf "${archive_path}" --format=zip -- .
        WORKING_DIRECTORY "${STAGING_DIR}"
        RESULT_VARIABLE result)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "Failed to create portable archive (exit code ${result})")
endif ()

message(STATUS "Windows portable archive created: ${archive_path}")
