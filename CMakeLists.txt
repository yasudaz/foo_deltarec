cmake_minimum_required(VERSION 3.16)
project(foo_deltarec LANGUAGES CXX RC VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FOO_DELTAREC_VERSION "0.2.0")
set(VERSION_MAJOR "0")
set(VERSION_MINOR "2")
set(VERSION_PATCH "0")
set(VERSION_BUILD "0")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86)$" OR
   CMAKE_VS_PLATFORM_NAME STREQUAL "Win32" OR
   MSVC_CXX_ARCHITECTURE_ID STREQUAL "X86")
    set(TARGET_ARCH_X86 TRUE)
else()
    set(TARGET_ARCH_X86 FALSE)
endif()

add_compile_definitions(
    WIN32
    _WIN32
    _WIN32_WINNT=0x0600
    WINVER=0x0600
    NOMINMAX
    UNICODE
    _UNICODE
    FOOBAR2000_HAVE_CFG_VAR_LEGACY=1
    FOOBAR2000_TARGET_VERSION=82
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS
)
if(NOT TARGET_ARCH_X86)
    add_compile_definitions(WIN64 _WIN64)
endif()

# ---- foobar2000 SDK discovery / auto-download ----------------------------
set(FB2K_SDK_DIR "" CACHE PATH "Path to an existing foobar2000 SDK directory")

if(FB2K_SDK_DIR AND EXISTS "${FB2K_SDK_DIR}/pfc")
    set(SDK_DIR "${FB2K_SDK_DIR}")
else()
    set(_FB2K_DEPS_DIR "${CMAKE_SOURCE_DIR}/.deps/fb2k-sdk")
    file(GLOB _EXISTING_SDKS "${_FB2K_DEPS_DIR}/SDK-*")
    set(_FOUND_LOCAL_SDK "")
    foreach(_cand IN LISTS _EXISTING_SDKS)
        if(EXISTS "${_cand}/pfc")
            set(_FOUND_LOCAL_SDK "${_cand}")
        endif()
    endforeach()

    if(_FOUND_LOCAL_SDK)
        set(SDK_DIR "${_FOUND_LOCAL_SDK}")
        message(STATUS "foobar2000 SDK: using cached ${SDK_DIR}")
    else()
        set(_FB2K_SDK_PAGE "${_FB2K_DEPS_DIR}/_sdk_page.html")
        file(MAKE_DIRECTORY "${_FB2K_DEPS_DIR}")
        file(DOWNLOAD "https://www.foobar2000.org/SDK" "${_FB2K_SDK_PAGE}" TIMEOUT 30 STATUS _dl_status)
        file(READ "${_FB2K_SDK_PAGE}" _sdk_page_content)
        string(REGEX MATCH "SDK-([0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9])\\.7z" _ "${_sdk_page_content}")
        if(NOT CMAKE_MATCH_1)
            message(FATAL_ERROR "Could not determine latest foobar2000 SDK version")
        endif()
        set(_resolved_version "${CMAKE_MATCH_1}")
        set(_FB2K_SDK_EXTRACT_DIR "${_FB2K_DEPS_DIR}/SDK-${_resolved_version}")
        set(_sdk_url "https://www.foobar2000.org/downloads/SDK-${_resolved_version}.7z")
        set(_sdk_archive "${_FB2K_DEPS_DIR}/SDK-${_resolved_version}.7z")
        message(STATUS "Downloading foobar2000 SDK ${_resolved_version} ...")
        file(MAKE_DIRECTORY "${_FB2K_SDK_EXTRACT_DIR}")
        file(DOWNLOAD "${_sdk_url}" "${_sdk_archive}" SHOW_PROGRESS TIMEOUT 120)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E tar xf "${_sdk_archive}"
            WORKING_DIRECTORY "${_FB2K_SDK_EXTRACT_DIR}"
        )
        file(REMOVE "${_sdk_archive}")
        set(SDK_DIR "${_FB2K_SDK_EXTRACT_DIR}")
    endif()
endif()

file(GLOB SDK_PFC_SRCS "${SDK_DIR}/pfc/*.cpp")
list(FILTER SDK_PFC_SRCS EXCLUDE REGEX ".*pfc-fb2k-hooks\\.cpp$")

file(GLOB SDK_FB2K_ALL "${SDK_DIR}/foobar2000/SDK/*.cpp")
list(FILTER SDK_FB2K_ALL EXCLUDE REGEX ".*\\.mm$")

set(SDK_CLIENT_SRC "${SDK_DIR}/foobar2000/foobar2000_component_client/component_client.cpp")

configure_file(
    "${CMAKE_SOURCE_DIR}/src/foo_deltarec.rc.in"
    "${CMAKE_BINARY_DIR}/foo_deltarec.rc"
    @ONLY
)

add_library(foo_deltarec SHARED
    ${SDK_PFC_SRCS}
    ${SDK_FB2K_ALL}
    ${SDK_CLIENT_SRC}
    src/main.cpp
    "${CMAKE_BINARY_DIR}/foo_deltarec.rc"
)

target_include_directories(foo_deltarec PRIVATE
    ${SDK_DIR}
    ${SDK_DIR}/foobar2000
    ${CMAKE_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(foo_deltarec PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:/O2>
        $<$<CONFIG:Debug>:/Zi>
        /W1 /wd4005 /wd4996 /fp:precise /utf-8
    )
    if(NOT TARGET_ARCH_X86)
        set_source_files_properties(src/main.cpp PROPERTIES COMPILE_FLAGS "/arch:AVX2")
    endif()
    set_source_files_properties(${SDK_DIR}/pfc/audio_math.cpp PROPERTIES COMPILE_FLAGS "/arch:AVX")

    if(TARGET_ARCH_X86)
        set(_FB2K_SHARED_LIB "${SDK_DIR}/foobar2000/shared/shared-Win32.lib")
    else()
        set(_FB2K_SHARED_LIB "${SDK_DIR}/foobar2000/shared/shared-x64.lib")
    endif()
    target_link_libraries(foo_deltarec PRIVATE
        comctl32.lib comdlg32.lib shell32.lib ole32.lib uuid.lib shlwapi.lib winmm.lib
        "${_FB2K_SHARED_LIB}"
    )
    target_link_options(foo_deltarec PRIVATE /DLL)
endif()

set_target_properties(foo_deltarec PROPERTIES
    OUTPUT_NAME "foo_deltarec"
    PREFIX      ""
    SUFFIX      ".dll"
)

add_custom_command(TARGET foo_deltarec POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DFOO_DLL=$<TARGET_FILE:foo_deltarec>"
        "-DOUTPUT=${CMAKE_BINARY_DIR}/foo_deltarec.fb2k-component"
        "-DARCH_X86=${TARGET_ARCH_X86}"
        -P "${CMAKE_SOURCE_DIR}/cmake/package_component.cmake"
    COMMENT "Packaging foo_deltarec.fb2k-component"
    VERBATIM
)
