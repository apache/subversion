#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# CMakeLists.txt -- configuration file for CMake build system
#

function(read_version path var major minor patch)
  get_filename_component(path ${path} ABSOLUTE)

  if (EXISTS ${path})
    file(
      STRINGS ${path} VERSION_STRINGS
      REGEX "#define (${major}|${minor}|${patch})"
    )

    string(REGEX REPLACE ".*${major} +([0-9]+).*" "\\1" VER_MAJOR ${VERSION_STRINGS})
    string(REGEX REPLACE ".*${minor} +([0-9]+).*" "\\1" VER_MINOR ${VERSION_STRINGS})
    string(REGEX REPLACE ".*${patch} +([0-9]+).*" "\\1" VER_PATCH ${VERSION_STRINGS})

    set(${var} "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}" PARENT_SCOPE)
  endif()
endfunction()

cmake_minimum_required(VERSION 3.12)

# CMP0092: MSVC warning flags are not in CMAKE_<LANG>_FLAGS by default.
if(POLICY CMP0092)
  cmake_policy(SET CMP0092 NEW)
endif()

read_version(
  "subversion/include/svn_version.h" SVN_VERSION
  SVN_VER_MAJOR SVN_VER_MINOR SVN_VER_PATCH
)

project("Subversion"
  VERSION "${SVN_VERSION}"
)

string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}-${CMAKE_HOST_SYSTEM_NAME}" SVN_BUILD_HOST)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}" SVN_BUILD_TARGET)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/subversion/svn_private_config.hc"
  "${CMAKE_CURRENT_BINARY_DIR}/svn_private_config.h"
)

option(SVN_BUILD_RA_LOCAL "Build Subversion Local Repository Access Library" ON)
if (SVN_BUILD_RA_LOCAL)
  add_compile_definitions("SVN_LIBSVN_RA_LINKS_RA_LOCAL")
endif()

# TODO:
# option(SVN_BUILD_RA_SERF "Build Subversion HTTP/WebDAV Protocol Repository Access Library" OFF)
# if (SVN_BUILD_RA_SERF)
#   add_compile_definitions("SVN_LIBSVN_RA_LINKS_RA_SERF")
# endif()

option(SVN_BUILD_RA_SVN "Build Subversion SVN Protocol Repository Access Library" ON)
if (SVN_BUILD_RA_SVN)
  add_compile_definitions("SVN_LIBSVN_RA_LINKS_RA_SVN")
endif()

option(SVN_BUILD_FS_FS "Build Subversion FSFS Repository Filesystem Library" ON)
if (SVN_BUILD_FS_FS)
  add_compile_definitions("SVN_LIBSVN_FS_LINKS_FS_FS")
endif()

option(SVN_BUILD_FS_X "Build Subversion FSX Repository Filesystem Library" ON)
if (SVN_BUILD_FS_X)
  add_compile_definitions("SVN_LIBSVN_FS_LINKS_FS_X")
endif()

option(SVN_DEBUG "Enables specific features for developer builds" OFF)
if (SVN_DEBUG)
  add_compile_definitions("SVN_DEBUG")
endif()

option(SVN_BUILD_PROGRAMS "Build Subversion programs (such as svn.exe)" ON)
option(SVN_BUILD_TOOLS "Build Subversion tools" OFF)
option(SVN_BUILD_TEST "Build Subversion test-suite" OFF)

if (SVN_BUILD_TEST)
  enable_testing()
endif()

option(BUILD_SHARED_LIBS "Build using shared libraries" ON)

option(SVN_BUILD_SHARED_FS "Build shared FS modules" ${BUILD_SHARED_LIBS})
if(SVN_BUILD_SHARED_FS)
  set(SVN_FS_BUILD_TYPE SHARED)
else()
  set(SVN_FS_BUILD_TYPE STATIC)
endif()

option(SVN_BUILD_SHARED_RA "Build shared RA modules" OFF)
if(SVN_BUILD_SHARED_RA)
  set(SVN_RA_BUILD_TYPE SHARED)
else()
  set(SVN_RA_BUILD_TYPE STATIC)
endif()

if(SVN_BUILD_SHARED_RA)
  message(FATAL_ERROR "SVN_BUILD_SHARED_RA not yet supported")
endif()

# Setup modules path

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/build/cmake")

### APR

find_package(APR REQUIRED)
add_library(external-apr ALIAS apr::apr)

### APR-Util

find_package(APRUtil REQUIRED)
add_library(external-aprutil ALIAS apr::aprutil)

### ZLIB

find_package(ZLIB REQUIRED)
add_library(external-zlib ALIAS ZLIB::ZLIB)

### EXPAT

find_package(expat CONFIG REQUIRED)
add_library(external-xml ALIAS expat::expat)

### LZ4

option(SVN_USE_INTERNAL_LZ4 "Use internal version of lz4" ON)

if(SVN_USE_INTERNAL_LZ4)
  add_library(external-lz4 STATIC "build/win32/empty.c")
  target_compile_definitions(external-lz4 PUBLIC "SVN_INTERNAL_LZ4")

  read_version(
    "subversion/libsvn_subr/lz4/lz4internal.h" lz4_VERSION
    LZ4_VERSION_MAJOR LZ4_VERSION_MINOR LZ4_VERSION_RELEASE
  )
else()
  find_package(lz4 CONFIG REQUIRED)
  add_library(external-lz4 ALIAS lz4::lz4)
endif()

### UTF8PROC

option(SVN_USE_INTERNAL_UTF8PROC "Use internal version of utf8proc" ON)

if(SVN_USE_INTERNAL_UTF8PROC)
  add_library(external-utf8proc STATIC "build/win32/empty.c")
  target_compile_definitions(external-utf8proc PUBLIC "SVN_INTERNAL_UTF8PROC")

  read_version(
    "subversion/libsvn_subr/utf8proc/utf8proc_internal.h" UTF8PROC_VERSION
    UTF8PROC_VERSION_MAJOR UTF8PROC_VERSION_MINOR UTF8PROC_VERSION_PATCH
  )
else()
  message(FATAL_ERROR "TODO:")
  # find_package(utf8proc CONFIG REQUIRED)
  # add_library(external-utf8proc ALIAS utf8proc)
endif()

### SQLite3

option(SVN_SQLITE_USE_AMALGAMATION "Use sqlite amalgamation" ON)
set(SVN_SQLITE_AMALGAMATION_ROOT "${CMAKE_SOURCE_DIR}/sqlite-amalgamation"
  CACHE STRING "Directory with sqlite amalgamation"
)

if(SVN_SQLITE_USE_AMALGAMATION)
  add_library(external-sqlite STATIC "build/win32/empty.c")
  find_path(SVN_SQLITE_AMALGAMATION_DIR
    NAMES sqlite3.c
    PATHS ${SVN_SQLITE_AMALGAMATION_ROOT}
  )

  mark_as_advanced(SVN_SQLITE_AMALGAMATION_DIR)

  target_include_directories(external-sqlite PUBLIC ${SVN_SQLITE_AMALGAMATION_DIR})
  target_compile_definitions(external-sqlite PUBLIC SVN_SQLITE_INLINE)

  if (SVN_SQLITE_AMALGAMATION_DIR)
    file(STRINGS ${SVN_SQLITE_AMALGAMATION_DIR}/sqlite3.c _ver_line
         REGEX "^#define SQLITE_VERSION  *\"[0-9]+\\.[0-9]+\\.[0-9]+\""
         LIMIT_COUNT 1)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+"
            SQLite3_VERSION "${_ver_line}")
    unset(_ver_line)
  endif()
else()
  find_package(SQLite3 REQUIRED)
  add_library(external-sqlite ALIAS SQLite::SQLite3)
endif()

find_package(Python COMPONENTS Interpreter REQUIRED)

function(target_exports target_name)
  if (WIN32)
    set(def_file_path "${CMAKE_BINARY_DIR}/${target_name}.def")

    add_custom_command(
      WORKING_DIRECTORY
        "${CMAKE_SOURCE_DIR}"
      COMMAND
        "${Python_EXECUTABLE}"
      ARGS
        "build/generator/extractor.py"
        ${ARGN}
        ">${def_file_path}"
      OUTPUT
        "${def_file_path}"
      DEPENDS
        "build/generator/extractor.py"
        ${ARGN}
    )

    target_sources("${target_name}" PRIVATE "${def_file_path}")
  endif()
endfunction()

include_directories("${CMAKE_CURRENT_BINARY_DIR}")

install(
  DIRECTORY "subversion/include/"
  DESTINATION "include/subversion-1"
)

if (WIN32)
  add_compile_definitions(
    "alloca=_alloca"
    "WIN32"
  )
endif()

if (MSVC)
  # Setup warning level
  add_compile_options(/W4)

  # Disable warning
  add_compile_options(/wd4100)
  add_compile_options(/wd4127)
  add_compile_options(/wd4206)
  add_compile_options(/wd4512)
  add_compile_options(/wd4701)
  add_compile_options(/wd4706)
  add_compile_options(/wd4800)

  # Treat some criticial warnings as error
  add_compile_options(/we4002)
  add_compile_options(/we4003)
  add_compile_options(/we4013)
  add_compile_options(/we4020)
  add_compile_options(/we4022)
  add_compile_options(/we4024)
  add_compile_options(/we4028)
  add_compile_options(/we4029)
  add_compile_options(/we4030)
  add_compile_options(/we4031)
  add_compile_options(/we4033)
  add_compile_options(/we4047)
  add_compile_options(/we4089)
  add_compile_options(/we4113)
  add_compile_options(/we4133)
  add_compile_options(/we4204)
  add_compile_options(/we4700)
  add_compile_options(/we4715)
  add_compile_options(/we4789)
endif()

if (NOT EXISTS "${CMAKE_SOURCE_DIR}/build/cmake/targets.cmake")
  message(FATAL_ERROR
    "The 'build/cmake/targets.cmake' file does NOT exist. "
    "Use the following command to generate it:\n"
    "  python gen-make.py -t cmake"
  )
endif()

include("build/cmake/targets.cmake")

option(SVN_BUILD_SVNXX "Enable compilation of the C++ bindings (requires C++)" OFF)

if (SVN_BUILD_SVNXX)
  target_include_directories(libsvnxx PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/subversion/bindings/cxx/include"
  )

  install(
    DIRECTORY "subversion/bindings/cxx/include/"
    DESTINATION "include/subversion-1"
  )
endif()

message(STATUS "Configuration summary:")
message(STATUS "  Version ......................... : ${SVN_VERSION}")
message(STATUS "  Build type ...................... : ${CMAKE_BUILD_TYPE}")
message(STATUS "    Build shared libraries ........ : ${BUILD_SHARED_LIBS}")
message(STATUS "    Build shared FS Modues ........ : ${SVN_BUILD_SHARED_FS}")
message(STATUS "    Build shared RA Modues ........ : ${SVN_BUILD_SHARED_RA}")
message(STATUS "  Optional modules and targets:")
message(STATUS "    Build FS FS ................... : ${SVN_BUILD_FS_FS}")
message(STATUS "    Build FS X .................... : ${SVN_BUILD_FS_X}")
message(STATUS "    Build RA LOCAL ................ : ${SVN_BUILD_RA_LOCAL}")
message(STATUS "    Build RA SVN .................. : ${SVN_BUILD_RA_SVN}")
message(STATUS "    Build RA SERF ................. : NOT IMPLEMENTED, SERF v1.2.3 (TODO:)")
message(STATUS "    Build Apache Modules .......... : NOT IMPLEMENTED, HTTPD v2.4.68 (TODO:)")
message(STATUS "    Build programs ................ : ${SVN_BUILD_PROGRAMS}")
message(STATUS "    Build tools ................... : ${SVN_BUILD_PROGRAMS}")
message(STATUS "    Build test suite .............. : ${SVN_BUILD_TEST}")
message(STATUS "  Dependecies:")
message(STATUS "    APR ........................... : ${APR_VERSION}")
message(STATUS "    APR-Util ...................... : ${APRUTIL_VERSION}")
message(STATUS "    EXPAT ......................... : ${expat_VERSION}")
message(STATUS "    ZLIB .......................... : ${ZLIB_VERSION}")
message(STATUS "    LZ4 ........................... : ${LZ4_VERSION}")
message(STATUS "    UTF8PROC ...................... : ${UTF8PROC_VERSION}")
message(STATUS "    SQLITE3 ....................... : ${SQLite3_VERSION}")
message(STATUS "    SERF .......................... : NOT IMPLEMENTED")
message(STATUS "    HTTPD ......................... : NOT IMPLEMENTED")
message(STATUS "    TODO: ......................... : Other dependecies")
message(STATUS "  Bindings:")
message(STATUS "    Build SVNXX ................... : ${SVN_BUILD_SVNXX}")
