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

include(FindPackageHandleStandardArgs)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(
  APR
  REQUIRED_VARS
    APR_LIBRARY
    APR_INCLUDE_DIR
)

if(APR_FOUND)
  if (NOT TARGET apr::apr)
    if (APR_LIBRARY_SHARED)
      add_library(apr::apr SHARED IMPORTED)
      target_compile_definitions(apr::apr INTERFACE "APR_DECLARE_IMPORT")
      set_target_properties(apr::apr PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${APR_INCLUDE_DIR}
        IMPORTED_LOCATION ${APR_DLL}
        IMPORTED_IMPLIB ${APR_LIBRARY}
      )
    else()
      add_library(apr::apr STATIC IMPORTED)
      target_compile_definitions(apr::apr INTERFACE "APR_DECLARE_STATIC")
      set_target_properties(apr::apr PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${APR_INCLUDE_DIR}
        IMPORTED_LOCATION ${APR_LIBRARY}
      )
    endif()

    if (WIN32)
      target_link_libraries(apr::apr INTERFACE ws2_32 rpcrt4)
    endif()
  endif()
endif()
