cmake_minimum_required(VERSION 3.16)

get_filename_component(_output_dir "${OUTPUT}" DIRECTORY)
set(_staging "${_output_dir}/_fb2k_component_staging")

file(REMOVE_RECURSE "${_staging}")
file(MAKE_DIRECTORY "${_staging}/x64")

if(ARCH_X86)
    file(COPY "${FOO_DLL}" DESTINATION "${_staging}")
    if(EXTRA_DLL AND EXISTS "${EXTRA_DLL}")
        file(COPY "${EXTRA_DLL}" DESTINATION "${_staging}/x64")
    endif()
else()
    file(COPY "${FOO_DLL}" DESTINATION "${_staging}/x64")
    if(EXTRA_DLL AND EXISTS "${EXTRA_DLL}")
        file(COPY "${EXTRA_DLL}" DESTINATION "${_staging}")
    endif()
endif()

set(_archive_files)
if(EXISTS "${_staging}/foo_deltarec.dll")
    list(APPEND _archive_files "foo_deltarec.dll")
endif()
if(EXISTS "${_staging}/x64/foo_deltarec.dll")
    list(APPEND _archive_files "x64/foo_deltarec.dll")
endif()

if(NOT _archive_files)
    message(FATAL_ERROR "No DLLs found to package in staging directory ${_staging}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${OUTPUT}" --format=zip -- ${_archive_files}
    WORKING_DIRECTORY "${_staging}"
    RESULT_VARIABLE _result
)

file(REMOVE_RECURSE "${_staging}")

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Failed to create .fb2k-component archive (cmake -E tar exited ${_result})")
endif()

message(STATUS "Created: ${OUTPUT}")
