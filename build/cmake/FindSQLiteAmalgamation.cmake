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
# FindSQLiteAmalgamation.cmake -- CMake module for the SQLite Amalgamation
#

find_path(SQLITE_AMALGAMATION_DIR
  NAMES sqlite3.c
)

mark_as_advanced(SQLITE_AMALGAMATION_DIR)

if (SQLITE_AMALGAMATION_DIR AND EXISTS "${SQLITE_AMALGAMATION_DIR}/sqlite3.c")
  file(STRINGS "${SQLITE_AMALGAMATION_DIR}/sqlite3.c" _ver_line
       REGEX "^#define SQLITE_VERSION  *\"[0-9]+\\.[0-9]+\\.[0-9]+\""
       LIMIT_COUNT 1)
  string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+"
         SQLite3_VERSION "${_ver_line}")
  unset(_ver_line)
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
  SQLiteAmalgamation
  REQUIRED_VARS
    SQLITE_AMALGAMATION_DIR
  VERSION_VAR
    SQLite3_VERSION
)

if(SQLiteAmalgamation_FOUND AND NOT TARGET SQLite::SQLite3Amalgamation)
  add_library(SQLite::SQLite3Amalgamation IMPORTED INTERFACE)

  target_include_directories(SQLite::SQLite3Amalgamation INTERFACE ${SQLITE_AMALGAMATION_DIR})

  # TODO: maybe drop SVN_SQLITE_INLINE out of this module?
  target_compile_definitions(SQLite::SQLite3Amalgamation INTERFACE SVN_SQLITE_INLINE)
endif()
