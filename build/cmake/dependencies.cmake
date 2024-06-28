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
# dependencies.cmake -- finds CMake dependecies
#

# Setup modules path

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/build/cmake")

### APR

find_package(APR REQUIRED)
add_library(external-apr ALIAS apr::apr)

### APR-Util

find_package(APRUtil REQUIRED)
add_library(external-aprutil ALIAS apr::aprutil)

### ZLIB

find_package(ZLIB)
add_library(external-zlib ALIAS ZLIB::ZLIB)

### EXPAT

find_package(EXPAT MODULE REQUIRED)
add_library(external-xml ALIAS EXPAT::EXPAT)

### LZ4

option(SVN_USE_INTERNAL_LZ4 "Use internal version of lz4" ON)

if(SVN_USE_INTERNAL_LZ4)
  add_library(external-lz4 STATIC "build/win32/empty.c")
  target_compile_definitions(external-lz4 PUBLIC "SVN_INTERNAL_LZ4")
else()
  find_package(lz4 CONFIG REQUIRED)
  add_library(external-lz4 ALIAS lz4::lz4)
endif()

### UTF8PROC

option(SVN_USE_INTERNAL_UTF8PROC "Use internal version of utf8proc" ON)

if(SVN_USE_INTERNAL_UTF8PROC)
  add_library(external-utf8proc STATIC "build/win32/empty.c")
  target_compile_definitions(external-utf8proc PUBLIC "SVN_INTERNAL_UTF8PROC")
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
  target_include_directories(external-sqlite PUBLIC ${SVN_SQLITE_AMALGAMATION_DIR})
  target_compile_definitions(external-sqlite PUBLIC SVN_SQLITE_INLINE)
else()
  find_package(SQLite3 REQUIRED)
  add_library(external-sqlite ALIAS SQLite::SQLite3)
endif()
