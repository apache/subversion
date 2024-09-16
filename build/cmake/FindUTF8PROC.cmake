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
# FindUTF8PROC.cmake -- Find the UTF-8 processing library
#

find_path(UTF8PROC_INCLUDE_DIR
  NAMES utf8proc.h
  PATH_SUFFIXES
    include
)

find_library(UTF8PROC_LIBRARY
  NAMES utf8proc
  PATH_SUFFIXES lib
)

mark_as_advanced(
  UTF8PROC_INCLUDE_DIR
  UTF8PROC_LIBRARY
)

if(UTF8PROC_INCLUDE_DIR AND EXISTS ${UTF8PROC_INCLUDE_DIR}/utf8proc.h)
  file(
    STRINGS "${UTF8PROC_INCLUDE_DIR}/utf8proc.h" VERSION_STRINGS
    REGEX "#define (UTF8PROC_VERSION_MAJOR|UTF8PROC_VERSION_MINOR|UTF8PROC_VERSION_PATCH)"
  )

  string(REGEX REPLACE ".*UTF8PROC_VERSION_MAJOR +([0-9]+).*" "\\1" UTF8PROC_VERSION_MAJOR ${VERSION_STRINGS})
  string(REGEX REPLACE ".*UTF8PROC_VERSION_MINOR +([0-9]+).*" "\\1" UTF8PROC_VERSION_MINOR ${VERSION_STRINGS})
  string(REGEX REPLACE ".*UTF8PROC_VERSION_PATCH +([0-9]+).*" "\\1" UTF8PROC_VERSION_PATCH ${VERSION_STRINGS})

  set(UTF8PROC_VERSION "${UTF8PROC_VERSION_MAJOR}.${UTF8PROC_VERSION_MINOR}.${UTF8PROC_VERSION_PATCH}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  UTF8PROC
  REQUIRED_VARS
    UTF8PROC_LIBRARY
    UTF8PROC_INCLUDE_DIR
  VERSION_VAR
    UTF8PROC_VERSION
)

if(UTF8PROC_FOUND)
  add_library(UTF8PROC::UTF8PROC IMPORTED STATIC)
  set_target_properties(UTF8PROC::UTF8PROC PROPERTIES
    IMPORTED_LOCATION ${UTF8PROC_LIBRARY}
    INTERFACE_INCLUDE_DIRECTORIES ${UTF8PROC_INCLUDE_DIR}
  )
endif()
