cmake_minimum_required(VERSION 3.20)
project(TheCrewReborn LANGUAGES C CXX)

get_filename_component(TCR_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-ffile-prefix-map=${TCR_REPO_ROOT}=.>)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(TCR_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_library(tcr_zlib STATIC
  ${TCR_THIRD_PARTY}/zlib/adler32.c
  ${TCR_THIRD_PARTY}/zlib/compress.c
  ${TCR_THIRD_PARTY}/zlib/crc32.c
  ${TCR_THIRD_PARTY}/zlib/deflate.c
  ${TCR_THIRD_PARTY}/zlib/infback.c
  ${TCR_THIRD_PARTY}/zlib/inffast.c
  ${TCR_THIRD_PARTY}/zlib/inflate.c
  ${TCR_THIRD_PARTY}/zlib/inftrees.c
  ${TCR_THIRD_PARTY}/zlib/trees.c
  ${TCR_THIRD_PARTY}/zlib/uncompr.c
  ${TCR_THIRD_PARTY}/zlib/zutil.c)
target_include_directories(tcr_zlib SYSTEM PUBLIC ${TCR_THIRD_PARTY}/zlib)
if(NOT MSVC)
  target_compile_options(tcr_zlib PRIVATE -w)
endif()

file(GLOB TCR_MBEDTLS_SOURCES ${TCR_THIRD_PARTY}/mbedtls/library/*.c)
add_library(tcr_mbedtls STATIC ${TCR_MBEDTLS_SOURCES})
target_include_directories(tcr_mbedtls SYSTEM PUBLIC ${TCR_THIRD_PARTY}/mbedtls/include
                                                     ${CMAKE_CURRENT_SOURCE_DIR}/src/config)
target_include_directories(tcr_mbedtls PRIVATE ${TCR_THIRD_PARTY}/mbedtls/library)
target_compile_definitions(tcr_mbedtls PUBLIC MBEDTLS_USER_CONFIG_FILE="mbedtls_user_config.h")
if(NOT MSVC)
  target_compile_options(tcr_mbedtls PRIVATE -w)
endif()
if(WIN32)
  target_link_libraries(tcr_mbedtls PUBLIC bcrypt)
endif()

add_library(tcr_flags INTERFACE)
target_compile_options(tcr_flags INTERFACE
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
  -ffp-contract=off
  -ffunction-sections -fdata-sections
  -Wall -Wextra -Werror)

set(TCR_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(TCR_RELEASE_DATA ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(TCR_TABLE_NAMES auto_replies.json replies.json dlc.json cars.json npc.json physparts.json rewards.json summit.json
    collectibles.json regions.json replies_schema.json v1_00_replies.json v1_00_cars.json)
set(TCR_TABLES)
if(EXISTS ${TCR_RELEASE_DATA})
  foreach(name IN LISTS TCR_TABLE_NAMES)
    list(APPEND TCR_TABLES ${name} ${TCR_RELEASE_DATA}/${name})
  endforeach()
  set(TCR_LOGO ${TCR_RELEASE_DATA}/avatar_logo.png)
else()
  set(TCR_DATA_121 ${CMAKE_CURRENT_SOURCE_DIR}/../tcreborn/games/v1_21/data CACHE PATH "the 1.21 game tables")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TCR_DATA_121})
  if(EXISTS ${TCR_DATA_121}/replies.json)
    set(TCR_REPLIES_121 ${TCR_DATA_121}/replies.json)
  else()
    file(GLOB TCR_REPLIES_CANDIDATES CONFIGURE_DEPENDS ${TCR_DATA_121}/*_replies.json)
    list(FILTER TCR_REPLIES_CANDIDATES EXCLUDE REGEX "/auto_replies\\.json$")
    list(LENGTH TCR_REPLIES_CANDIDATES TCR_REPLIES_COUNT)
    if(NOT TCR_REPLIES_COUNT EQUAL 1)
      message(FATAL_ERROR "no single reply table in ${TCR_DATA_121}: ${TCR_REPLIES_CANDIDATES}")
    endif()
    list(GET TCR_REPLIES_CANDIDATES 0 TCR_REPLIES_121)
  endif()
  set(TCR_DATA_100 ${CMAKE_CURRENT_SOURCE_DIR}/../tcreborn/games/v1_00/data CACHE PATH "the 1.00 game tables")
  set(TCR_SCHEMA_121 ${CMAKE_CURRENT_SOURCE_DIR}/../tcreborn/games/v1_21/schema CACHE PATH "the 1.21 reply layouts")
  set(TCR_TABLES
    auto_replies.json ${TCR_DATA_121}/auto_replies.json
    replies.json ${TCR_REPLIES_121}
    dlc.json ${TCR_DATA_121}/dlc.json
    cars.json ${TCR_DATA_121}/cars.json
    npc.json ${TCR_DATA_121}/npc.json
    physparts.json ${TCR_DATA_121}/physparts.json
    rewards.json ${TCR_DATA_121}/rewards.json
    summit.json ${TCR_DATA_121}/summit.json
    collectibles.json ${TCR_DATA_121}/collectibles.json
    regions.json ${TCR_DATA_121}/regions.json
    replies_schema.json ${TCR_SCHEMA_121}/replies.json
    v1_00_replies.json ${TCR_DATA_100}/replies.json
    v1_00_cars.json ${TCR_DATA_100}/cars.json)
  set(TCR_LOGO ${CMAKE_CURRENT_SOURCE_DIR}/../tcreborn/data/avatar_logo.png)
endif()
set(TCR_TABLE_FILES)
list(LENGTH TCR_TABLES TCR_TABLES_LENGTH)
math(EXPR TCR_TABLES_LAST "${TCR_TABLES_LENGTH} - 1")
foreach(i RANGE 1 ${TCR_TABLES_LAST} 2)
  list(GET TCR_TABLES ${i} path)
  if(NOT EXISTS ${path})
    message(FATAL_ERROR "missing game table: ${path}")
  endif()
  list(APPEND TCR_TABLE_FILES ${path})
endforeach()
if(NOT EXISTS ${TCR_LOGO})
  message(FATAL_ERROR "missing avatar logo: ${TCR_LOGO}")
endif()

set(TCR_EMBED_LIST ${TCR_GENERATED}/embed_tables.cmake)
set(TCR_EMBED_LIST_TEXT "set(TCR_EMBED_TABLES [==[${TCR_TABLES}]==])\n")
if(EXISTS ${TCR_EMBED_LIST})
  file(READ ${TCR_EMBED_LIST} TCR_EMBED_LIST_OLD)
else()
  set(TCR_EMBED_LIST_OLD "")
endif()
if(NOT TCR_EMBED_LIST_OLD STREQUAL TCR_EMBED_LIST_TEXT)
  file(WRITE ${TCR_EMBED_LIST} "${TCR_EMBED_LIST_TEXT}")
endif()
set(TCR_TABLES_SOURCE ${TCR_GENERATED}/tables.cpp)
add_custom_command(
  OUTPUT ${TCR_TABLES_SOURCE}
  COMMAND ${CMAKE_COMMAND} -DTCR_EMBED_LIST=${TCR_EMBED_LIST} -DTCR_EMBED_OUT=${TCR_TABLES_SOURCE}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake ${TCR_EMBED_LIST} ${TCR_TABLE_FILES}
  COMMENT "Embedding the game tables"
  VERBATIM)
set(TCR_LOGO_SOURCE ${TCR_GENERATED}/core/avatar_logo.inc)
add_custom_command(
  OUTPUT ${TCR_LOGO_SOURCE}
  COMMAND ${CMAKE_COMMAND} -DTCR_EMBED_LOGO=${TCR_LOGO} -DTCR_EMBED_OUT=${TCR_LOGO_SOURCE}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake ${TCR_LOGO}
  COMMENT "Embedding the avatar logo"
  VERBATIM)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION)
  set(TCR_VERSION_FILE ${CMAKE_CURRENT_SOURCE_DIR}/VERSION)
  file(STRINGS ${TCR_VERSION_FILE} TCR_VERSION LIMIT_COUNT 1)
else()
  set(TCR_VERSION_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../tcreborn/version.py)
  file(STRINGS ${TCR_VERSION_FILE} TCR_VERSION REGEX "^VERSION *= *[\"']")
  string(REGEX REPLACE "^VERSION *= *[\"']([^\"']*)[\"'].*$" "\\1" TCR_VERSION "${TCR_VERSION}")
endif()
string(STRIP "${TCR_VERSION}" TCR_VERSION)
if(NOT TCR_VERSION MATCHES "^[0-9A-Za-z.+-]+$")
  message(FATAL_ERROR "no version in ${TCR_VERSION_FILE}: '${TCR_VERSION}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TCR_VERSION_FILE})
file(CONFIGURE OUTPUT ${TCR_GENERATED}/core/version.h CONTENT [==[
// Generated by native/CMakeLists.txt from the version file. Do not edit.
#pragma once

namespace tcr {

inline constexpr const char kVersion[] = "@TCR_VERSION@";

}  // namespace tcr
]==] @ONLY)

file(GLOB_RECURSE TCR_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp)
add_library(tcrcore STATIC ${TCR_CORE_SOURCES} ${TCR_TABLES_SOURCE} ${TCR_LOGO_SOURCE})
target_include_directories(tcrcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include
                                          ${TCR_GENERATED})
target_link_libraries(tcrcore PUBLIC tcr_mbedtls tcr_zlib PRIVATE tcr_flags)

if(WIN32)
  add_library(tcrplatform STATIC src/platform/windows.cpp src/platform/files.cpp)
  target_link_libraries(tcrplatform PUBLIC ws2_32 bcrypt)
else()
  add_library(tcrplatform STATIC src/platform/posix.cpp src/platform/files.cpp)
  find_package(Threads REQUIRED)
  target_link_libraries(tcrplatform PUBLIC Threads::Threads)
endif()
target_include_directories(tcrplatform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tcrplatform PUBLIC tcrcore PRIVATE tcr_flags)

file(GLOB TCR_SERVER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/server/*.cpp)
add_library(tcrserver STATIC ${TCR_SERVER_SOURCES})
target_link_libraries(tcrserver PUBLIC tcrplatform tcr_mbedtls PRIVATE tcr_flags)

add_library(tcrcore_test SHARED src/capi.cpp src/debug_calls.cpp src/debug_peers.cpp src/probe.cpp)
target_link_libraries(tcrcore_test PRIVATE tcrplatform tcr_flags)
target_compile_definitions(tcrcore_test PRIVATE TCR_BUILD_SHARED)
set_target_properties(tcrcore_test PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

file(GLOB TCR_APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp)
list(FILTER TCR_APP_SOURCES EXCLUDE REGEX "/test_hooks\\.cpp$")
add_executable(TheCrewReborn-Server src/main.cpp ${TCR_APP_SOURCES})
target_link_libraries(TheCrewReborn-Server PRIVATE tcrserver tcr_flags)
set_target_properties(TheCrewReborn-Server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if(APPLE)
  target_link_options(TheCrewReborn-Server PRIVATE -Wl,-dead_strip)
else()
  target_link_options(TheCrewReborn-Server PRIVATE -Wl,--gc-sections)
endif()
if(MINGW AND NOT TCR_WINDOWS_RC_AT_LINK)
  target_link_options(TheCrewReborn-Server PRIVATE -Wl,--no-insert-timestamp)
endif()
if(WIN32)
  set(TCR_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/src/windows/TheCrewReborn-Server.manifest)
  configure_file(src/windows/app.rc.in ${CMAKE_BINARY_DIR}/app.rc @ONLY)
  option(TCR_WINDOWS_RC_AT_LINK "Give the resource script to the compiler driver at link time (zig)" OFF)
  if(TCR_WINDOWS_RC_AT_LINK)
    target_link_options(TheCrewReborn-Server PRIVATE app.rc)
    set_property(TARGET TheCrewReborn-Server APPEND PROPERTY LINK_DEPENDS ${TCR_MANIFEST})
  else()
    enable_language(RC)
    target_sources(TheCrewReborn-Server PRIVATE ${CMAKE_BINARY_DIR}/app.rc)
  endif()
endif()

option(TCR_BUILD_TESTS "Build the test programs" ON)
if(TCR_BUILD_TESTS)
  add_executable(tcr_engine_echo tests/engine_echo.cpp)
  target_link_libraries(tcr_engine_echo PRIVATE tcrserver tcr_flags)
  set_target_properties(tcr_engine_echo PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  add_executable(TheCrewReborn-Server-test src/main.cpp ${TCR_APP_SOURCES} src/app/test_hooks.cpp)
  target_compile_definitions(TheCrewReborn-Server-test PRIVATE TCR_TEST_HOOKS)
  target_link_libraries(TheCrewReborn-Server-test PRIVATE tcrserver tcr_flags)
  set_target_properties(TheCrewReborn-Server-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  add_executable(tcr_hypot_check tests/hypot_check.cpp)
  target_link_libraries(tcr_hypot_check PRIVATE tcrcore tcr_flags)
  set_target_properties(tcr_hypot_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  add_executable(tcr_config_edit tests/config_edit.cpp)
  target_link_libraries(tcr_config_edit PRIVATE tcrplatform tcr_flags)
  set_target_properties(tcr_config_edit PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

set(TCR_ADMIN_SOURCES)
foreach(page admin_page:kAdminPage admin_login:kAdminLogin)
  string(REPLACE ":" ";" page "${page}")
  list(GET page 0 page_file)
  list(GET page 1 page_array)
  set(page_source ${TCR_GENERATED}/app/${page_file}.inc)
  add_custom_command(
    OUTPUT ${page_source}
    COMMAND ${CMAKE_COMMAND} -DTCR_EMBED_LOGO=${CMAKE_CURRENT_SOURCE_DIR}/src/app/${page_file}.html
            -DTCR_EMBED_NAME=${page_array} -DTCR_EMBED_OUT=${page_source}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake ${CMAKE_CURRENT_SOURCE_DIR}/src/app/${page_file}.html
    COMMENT "Embedding ${page_file}.html"
    VERBATIM)
  list(APPEND TCR_ADMIN_SOURCES ${page_source})
endforeach()
target_sources(TheCrewReborn-Server PRIVATE ${TCR_ADMIN_SOURCES})
if(TARGET TheCrewReborn-Server-test)
  target_sources(TheCrewReborn-Server-test PRIVATE ${TCR_ADMIN_SOURCES})
endif()
if(WIN32)
  target_link_libraries(tcrplatform PUBLIC shell32 user32 ole32)
elseif(APPLE)
  target_link_libraries(tcrplatform PUBLIC "-framework Security")
endif()
