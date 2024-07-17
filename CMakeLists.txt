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
  LANGUAGES C
)

### Options

# Build components
option(SVN_BUILD_SVNXX "Enable compilation of the C++ bindings (requires C++)" OFF)
option(SVN_BUILD_PROGRAMS "Build Subversion programs (such as svn.exe)" ON)
option(SVN_BUILD_TOOLS "Build Subversion tools" OFF)
option(SVN_BUILD_TESTS "Build Subversion test-suite" OFF)

# Enable modules and features
option(SVN_ENABLE_RA_LOCAL "Enable Subversion Local Repository Access Library" ON)
option(SVN_ENABLE_RA_SERF "Enable Subversion HTTP/WebDAV Protocol Repository Access Library" OFF)
option(SVN_ENABLE_RA_SVN "Enable Subversion SVN Protocol Repository Access Library" ON)
option(SVN_ENABLE_FS_FS "Enable Subversion FSFS Repository Filesystem Library" ON)
option(SVN_ENABLE_FS_X "Enable Subversion FSX Repository Filesystem Library" ON)
option(SVN_ENABLE_NLS "Enable gettext functionality" OFF)

# Configuration
option(BUILD_SHARED_LIBS "Build using shared libraries" ON)
option(SVN_BUILD_SHARED_FS "Build shared FS modules" ${BUILD_SHARED_LIBS})
option(SVN_BUILD_SHARED_RA "Build shared RA modules" OFF)
option(SVN_DEBUG "Enables specific features for developer builds" OFF)

# Dependecies
option(SVN_USE_INTERNAL_LZ4 "Use internal version of lz4" ON)
option(SVN_USE_INTERNAL_UTF8PROC "Use internal version of utf8proc" ON)
option(SVN_SQLITE_USE_AMALGAMATION "Use sqlite amalgamation" ON)
set(SQLiteAmalgamation_ROOT "${CMAKE_SOURCE_DIR}/sqlite-amalgamation" CACHE STRING "Directory with sqlite amalgamation")

# Require C++ compiler
if (SVN_BUILD_SVNXX)
  # TODO: Also add SVN_BUILD_AUTH_KWALLET and SVN_BUILD_JAVAHL to the
  # conditions when they done.

  enable_language(CXX)
endif()

string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}-${CMAKE_HOST_SYSTEM_NAME}" SVN_BUILD_HOST)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}" SVN_BUILD_TARGET)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/subversion/svn_private_config.hc"
  "${CMAKE_CURRENT_BINARY_DIR}/svn_private_config.h"
)

if (SVN_ENABLE_RA_LOCAL)
  add_compile_definitions("SVN_LIBSVN_RA_LINKS_RA_LOCAL")
endif()

if (SVN_ENABLE_RA_SERF)
  add_compile_definitions("SVN_LIBSVN_RA_LINKS_RA_SERF")
endif()

if (SVN_ENABLE_RA_SVN)
  add_compile_definitions("SVN_LIBSVN_RA_LINKS_RA_SVN")
endif()

if (SVN_ENABLE_FS_FS)
  add_compile_definitions("SVN_LIBSVN_FS_LINKS_FS_FS")
endif()

if (SVN_ENABLE_FS_X)
  add_compile_definitions("SVN_LIBSVN_FS_LINKS_FS_X")
endif()

if (SVN_DEBUG)
  add_compile_definitions("SVN_DEBUG")
endif()

if (SVN_BUILD_TESTS)
  enable_testing()
endif()

if(SVN_BUILD_SHARED_FS)
  set(SVN_FS_BUILD_TYPE SHARED)
else()
  set(SVN_FS_BUILD_TYPE STATIC)
endif()

if(SVN_BUILD_SHARED_RA)
  set(SVN_RA_BUILD_TYPE SHARED)
else()
  set(SVN_RA_BUILD_TYPE STATIC)
endif()

if(SVN_BUILD_SHARED_RA)
  message(FATAL_ERROR "SVN_BUILD_SHARED_RA not yet supported")
endif()

if(BUILD_SHARED_LIBS OR SVN_BUILD_SHARED_FS OR SVN_BUILD_SHARED_RA)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
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

if(SVN_USE_INTERNAL_LZ4)
  add_library(external-lz4 INTERFACE)
  target_compile_definitions(external-lz4 INTERFACE "SVN_INTERNAL_LZ4")

  read_version(
    "subversion/libsvn_subr/lz4/lz4internal.h" lz4_VERSION
    LZ4_VERSION_MAJOR LZ4_VERSION_MINOR LZ4_VERSION_RELEASE
  )
else()
  find_package(lz4 CONFIG REQUIRED)
  add_library(external-lz4 ALIAS lz4::lz4)
endif()

### UTF8PROC

if(SVN_USE_INTERNAL_UTF8PROC)
  add_library(external-utf8proc INTERFACE)
  target_compile_definitions(external-utf8proc INTERFACE "SVN_INTERNAL_UTF8PROC")

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

if(SVN_SQLITE_USE_AMALGAMATION)
  find_package(SQLiteAmalgamation REQUIRED)
  add_library(external-sqlite ALIAS SQLite::SQLite3Amalgamation)
else()
  find_package(SQLite3 REQUIRED)
  add_library(external-sqlite ALIAS SQLite::SQLite3)
endif()

### Serf
if (SVN_ENABLE_RA_SERF)
  find_package(Serf REQUIRED)
  add_library(external-serf ALIAS Serf::Serf)
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

add_library(ra-libs INTERFACE)
add_library(fs-libs INTERFACE)

if(SVN_ENABLE_NLS)
  # Note: when installing these dependecies with vcpkg, you will need to
  # install 'gettext' package with 'tools' feature. Use the following command
  # for this: `./vcpkg install gettext[tools]`. This package contains both,
  # Gettext and Intl dependecies.
  find_package(Gettext REQUIRED)
  find_package(Intl REQUIRED)

  # If using CMake of version < 3.20, FindIntl would not define IMPORTED target.
  # https://cmake.org/cmake/help/latest/module/FindIntl.html
  if(NOT TARGET Intl::Intl)
    add_library(Intl::Intl INTERFACE IMPORTED)
    set_target_properties(Intl::Intl PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${Intl_INCLUDE_DIRS}"
      INTERFACE_LINK_LIBRARIES "${Intl_LIBRARIES}"
    )
  endif()

  add_library(external-intl ALIAS Intl::Intl)

  add_compile_definitions(ENABLE_NLS)

  add_custom_target(locale ALL)

  file(GLOB SVN_PO_FILES "subversion/po/*.po")

  foreach(po_file ${SVN_PO_FILES})
    get_filename_component(lang ${po_file} NAME_WLE)
    set(mo_file "${CMAKE_BINARY_DIR}/${lang}.mo")

    add_custom_command(
      DEPENDS
        "${po_file}"
      OUTPUT
        "${mo_file}"
      COMMAND
        "${GETTEXT_MSGFMT_EXECUTABLE}" -c -o ${mo_file} ${po_file}
    )

    target_sources(locale PRIVATE ${mo_file})

    install(
      FILES "${mo_file}"
      DESTINATION "share/locale/${lang}/LC_MESSAGES"
      RENAME "subversion.mo"
    )
  endforeach()
else()
  # Declare empty target for Intl if we don't use it.
  add_library(external-intl INTERFACE)
endif()

# Link all targets with Intl library. The 'external-intl' target is always,
# even if we don't use NLS functionality.
#
# Following the CMake documentation [1], the link_libraries affects only on
# the targets declared later, so it should be here.
#
# [1] https://cmake.org/cmake/help/latest/command/link_libraries.html
#     -- "Link libraries to all targets added later."
link_libraries(external-intl)

# Build shared libraries and theirs implibs with 'lib' prefix, for example
# libsvn_subr-1.[lib|a] and libsvn_subr-1.[dll|so]
set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
set(CMAKE_IMPORT_LIBRARY_PREFIX "lib")

# This tells CMake to build static libraries without any prefix
# (just svn_subr-1.[lib|a])
set(CMAKE_STATIC_LIBRARY_PREFIX "")

include("build/cmake/targets.cmake")

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
message(STATUS "    Enable FS FS .................. : ${SVN_ENABLE_FS_FS}")
message(STATUS "    Enable FS X ................... : ${SVN_ENABLE_FS_X}")
message(STATUS "    Enable RA LOCAL ............... : ${SVN_ENABLE_RA_LOCAL}")
message(STATUS "    Enable RA SVN ................. : ${SVN_ENABLE_RA_SVN}")
message(STATUS "    Enable RA SERF ................ : ${SVN_ENABLE_RA_SERF}")
message(STATUS "    Build Apache Modules .......... : NOT IMPLEMENTED, HTTPD v2.4.68 (TODO:)")
message(STATUS "    Build programs ................ : ${SVN_BUILD_PROGRAMS}")
message(STATUS "    Build tools ................... : ${SVN_BUILD_TOOLS}")
message(STATUS "    Build test suite .............. : ${SVN_BUILD_TESTS}")
message(STATUS "  Dependecies:")
message(STATUS "    APR ........................... : ${APR_VERSION}")
message(STATUS "    APR-Util ...................... : ${APRUTIL_VERSION}")
message(STATUS "    EXPAT ......................... : ${expat_VERSION}")
message(STATUS "    ZLIB .......................... : ${ZLIB_VERSION}")
message(STATUS "    LZ4 ........................... : ${lz4_VERSION}")
message(STATUS "    UTF8PROC ...................... : ${UTF8PROC_VERSION}")
message(STATUS "    SQLITE3 ....................... : ${SQLite3_VERSION}")
message(STATUS "    SERF .......................... : ${Serf_VERSION}")
message(STATUS "    HTTPD ......................... : NOT IMPLEMENTED")
message(STATUS "    TODO: ......................... : Other dependecies")
message(STATUS "  Bindings:")
message(STATUS "    Build SVNXX ................... : ${SVN_BUILD_SVNXX}")
