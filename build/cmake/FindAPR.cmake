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
# FindAPR.cmake -- CMake module for APR library
#

find_path(APR_INCLUDE_DIR
  NAMES apr.h
  PATH_SUFFIXES
    include
    include/apr-1
)

find_library(APR_LIBRARY_SHARED
  NAMES libapr-1
  PATH_SUFFIXES lib
)

find_library(APR_LIBRARY_STATIC
  NAMES apr-1
  PATH_SUFFIXES lib
)

find_file(APR_DLL
  NAMES libapr-1.dll
  PATH_SUFFIXES bin
)

if(APR_LIBRARY_SHARED)
  set(APR_LIBRARY ${APR_LIBRARY_SHARED})
elseif(APR_LIBRARY_STATIC)
  set(APR_LIBRARY ${APR_LIBRARY_STATIC})
endif()

file(
  STRINGS "${APR_INCLUDE_DIR}/apr_version.h" VERSION_STRINGS
  REGEX "#define (APR_MAJOR_VERSION|APR_MINOR_VERSION|APR_PATCH_VERSION)"
)

string(REGEX REPLACE ".*APR_MAJOR_VERSION +([0-9]+).*" "\\1" APR_MAJOR_VERSION ${VERSION_STRINGS})
string(REGEX REPLACE ".*APR_MINOR_VERSION +([0-9]+).*" "\\1" APR_MINOR_VERSION ${VERSION_STRINGS})
string(REGEX REPLACE ".*APR_PATCH_VERSION +([0-9]+).*" "\\1" APR_PATCH_VERSION ${VERSION_STRINGS})

set(APR_VERSION "${APR_MAJOR_VERSION}.${APR_MINOR_VERSION}.${APR_PATCH_VERSION}")

include(FindPackageHandleStandardArgs)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(
  APR
  REQUIRED_VARS
    APR_LIBRARY
    APR_INCLUDE_DIR
  VERSION_VAR
    APR_VERSION
)

if(APR_FOUND AND NOT TARGET apr::apr)
  if (APR_LIBRARY_SHARED)
    add_library(apr::apr SHARED IMPORTED)
    set_target_properties(apr::apr PROPERTIES
      IMPORTED_LOCATION ${APR_DLL}
      IMPORTED_IMPLIB ${APR_LIBRARY}
      INTERFACE_COMPILE_DEFINITIONS "APR_DECLARE_IMPORT"
    )
  else()
    add_library(apr::apr STATIC IMPORTED)
    set_target_properties(apr::apr PROPERTIES
      IMPORTED_LOCATION ${APR_LIBRARY}
      INTERFACE_COMPILE_DEFINITIONS "APR_DECLARE_STATIC"
    )
  endif()

  target_include_directories(apr::apr INTERFACE ${APR_INCLUDE_DIR})

  if (WIN32)
    target_link_libraries(apr::apr INTERFACE ws2_32 rpcrt4)
  endif()
endif()
