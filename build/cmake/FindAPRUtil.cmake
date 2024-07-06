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
# FindAPRUtil.cmake -- CMake module for APR-Util library
#

find_path(APRUTIL_INCLUDE_DIR
  NAMES apu.h
  PATH_SUFFIXES
    include
    include/apr-1 # Not yet in apr
)

find_library(APRUTIL_LIBRARY_SHARED
  NAMES libaprutil-1
  PATH_SUFFIXES lib
)

find_library(APRUTIL_LIBRARY_STATIC
  NAMES aprutil-1
  PATH_SUFFIXES lib
)

find_file(APRUTIL_DLL
  NAMES libaprutil-1.dll
  PATH_SUFFIXES bin
)

if(APRUTIL_LIBRARY_SHARED)
  set(APRUTIL_LIBRARY ${APRUTIL_LIBRARY_SHARED})
elseif(APRUTIL_LIBRARY_STATIC)
  set(APRUTIL_LIBRARY ${APRUTIL_LIBRARY_STATIC})
endif()

file(
  STRINGS "${APRUTIL_INCLUDE_DIR}/apu_version.h" VERSION_STRINGS
  REGEX "#define (APU_MAJOR_VERSION|APU_MINOR_VERSION|APU_PATCH_VERSION)"
)

string(REGEX REPLACE ".*APU_MAJOR_VERSION +([0-9]+).*" "\\1" APU_MAJOR_VERSION ${VERSION_STRINGS})
string(REGEX REPLACE ".*APU_MINOR_VERSION +([0-9]+).*" "\\1" APU_MINOR_VERSION ${VERSION_STRINGS})
string(REGEX REPLACE ".*APU_PATCH_VERSION +([0-9]+).*" "\\1" APU_PATCH_VERSION ${VERSION_STRINGS})

set(APRUTIL_VERSION "${APU_MAJOR_VERSION}.${APU_MINOR_VERSION}.${APU_PATCH_VERSION}")

include(FindPackageHandleStandardArgs)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(
  APRUtil
  REQUIRED_VARS
    APRUTIL_LIBRARY
    APRUTIL_INCLUDE_DIR
  VERSION_VAR
    APRUTIL_VERSION
)

if(APRUtil_FOUND AND NOT TARGET apr::aprutil)
  if (APRUTIL_LIBRARY_SHARED)
    add_library(apr::aprutil SHARED IMPORTED)
    target_compile_definitions(apr::aprutil INTERFACE "APU_DECLARE_IMPORT")
    set_target_properties(apr::aprutil PROPERTIES
      IMPORTED_LOCATION ${APRUTIL_DLL}
      IMPORTED_IMPLIB ${APRUTIL_LIBRARY}
    )
  else()
    add_library(apr::aprutil STATIC IMPORTED)
    target_compile_definitions(apr::aprutil INTERFACE "APU_DECLARE_STATIC")
    set_target_properties(apr::aprutil PROPERTIES
      IMPORTED_LOCATION ${APRUTIL_LIBRARY}
    )
  endif()

  target_include_directories(apr::aprutil INTERFACE ${APRUTIL_INCLUDE_DIR})
endif()
