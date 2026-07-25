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
# FindSerf.cmake -- CMake module for Serf library
#

include(GNUInstallDirs)

find_path(Serf_INCLUDE_DIR
  NAMES serf.h
  PATH_SUFFIXES
    include
    "${CMAKE_INSTALL_INCLUDEDIR}"
    include/serf-2
    "${CMAKE_INSTALL_INCLUDEDIR}/serf-2"
    include/serf-1
    "${CMAKE_INSTALL_INCLUDEDIR}/serf-1"
)

mark_as_advanced(
  Serf_INCLUDE_DIR
  Serf_LIBRARY
)

# TODO: Shared Serf

if (Serf_INCLUDE_DIR AND EXISTS ${Serf_INCLUDE_DIR}/serf.h)
  file(
    STRINGS "${Serf_INCLUDE_DIR}/serf.h" VERSION_STRINGS
    REGEX "#define (SERF_MAJOR_VERSION|SERF_MINOR_VERSION|SERF_PATCH_VERSION)"
  )

  string(REGEX REPLACE ".*SERF_MAJOR_VERSION +([0-9]+).*" "\\1" SERF_MAJOR_VERSION ${VERSION_STRINGS})
  string(REGEX REPLACE ".*SERF_MINOR_VERSION +([0-9]+).*" "\\1" SERF_MINOR_VERSION ${VERSION_STRINGS})
  string(REGEX REPLACE ".*SERF_PATCH_VERSION +([0-9]+).*" "\\1" SERF_PATCH_VERSION ${VERSION_STRINGS})
else()
  # Default version to 1.0.0 if not found.
  set(SERF_MAJOR_VERSION 1)
  set(SERF_MINOR_VERSION 0)
  set(SERF_PATCH_VERSION 0)
endif()

set(Serf_VERSION "${SERF_MAJOR_VERSION}.${SERF_MINOR_VERSION}.${SERF_PATCH_VERSION}")

find_library(Serf_LIBRARY
  NAMES "serf-${SERF_MAJOR_VERSION}"
  PATH_SUFFIXES lib "${CMAKE_INSTALL_LIBDIR}"
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
  Serf
  REQUIRED_VARS
    Serf_LIBRARY
    Serf_INCLUDE_DIR
  VERSION_VAR
    Serf_VERSION
)

add_library(Serf::Serf STATIC IMPORTED)

set_target_properties(Serf::Serf PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES ${Serf_INCLUDE_DIR}
  IMPORTED_LOCATION ${Serf_LIBRARY}
)

find_package(OpenSSL REQUIRED)
find_package(APR REQUIRED)
find_package(ZLIB REQUIRED)

target_link_libraries(Serf::Serf INTERFACE
  apr::apr
  OpenSSL::SSL
  ZLIB::ZLIB
)

if(APR_VERSION VERSION_LESS 2.0.0)
  find_package(APRUtil REQUIRED)
  target_link_libraries(Serf::Serf INTERFACE apr::aprutil)
endif()

if (WIN32)
  target_link_libraries(Serf::Serf INTERFACE
    crypt32.lib
    rpcrt4.lib
    mswsock.lib
    secur32.lib
    ws2_32.lib
  )
endif()
