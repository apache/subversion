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
# FindHttpd.cmake -- CMake module for Httpd library
#

include(GNUInstallDirs)

if(WIN32)
  find_path(HTTPD_INCLUDE_DIR
    NAMES httpd.h
    PATH_SUFFIXES
      include
  )

  find_library(HTTPD_LIBRARY
    NAMES libhttpd
    PATH_SUFFIXES lib
  )

  find_library(MOD_DAV_LIBRARY
    NAMES mod_dav
    PATH_SUFFIXES lib
  )

  if (HTTPD_INCLUDE_DIR AND EXISTS "${HTTPD_INCLUDE_DIR}/ap_release.h")
    file(
      STRINGS "${HTTPD_INCLUDE_DIR}/ap_release.h" VERSION_STRINGS
      REGEX "#define (AP_SERVER_MAJORVERSION_NUMBER|AP_SERVER_MINORVERSION_NUMBER|AP_SERVER_PATCHLEVEL_NUMBER)"
    )

    string(REGEX REPLACE ".*AP_SERVER_MAJORVERSION_NUMBER +([0-9]+).*" "\\1" HTTPD_VERSION_MAJOR ${VERSION_STRINGS})
    string(REGEX REPLACE ".*AP_SERVER_MINORVERSION_NUMBER +([0-9]+).*" "\\1" HTTPD_VERSION_MINOR ${VERSION_STRINGS})
    string(REGEX REPLACE ".*AP_SERVER_PATCHLEVEL_NUMBER +([0-9]+).*" "\\1" HTTPD_VERSION_PATCH ${VERSION_STRINGS})

    set(HTTPD_VERSION "${HTTPD_VERSION_MAJOR}.${HTTPD_VERSION_MINOR}.${HTTPD_VERSION_PATCH}")
  endif()
  set(_httpd_modules_dir "${CMAKE_INSTALL_BINDIR}")

else()
  find_program(APXS_EXECUTABLE
               NAMES apxs2 apxs
               PATH /usr/local/apache2/bin /usr/local/apache/bin /usr/bin /usr/sbin)

  function(_APXS_CONFIG_VAR VARNAME OUTVAR)
    execute_process(COMMAND "${APXS_EXECUTABLE}" -q ${VARNAME}
                    RESULT_VARIABLE _APXS_SUCCESS
                    OUTPUT_VARIABLE _APXS_OUTPUT
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
    set(${OUTVAR} "${_APXS_OUTPUT}" PARENT_SCOPE)
  endfunction()

  if(APXS_EXECUTABLE)
    _APXS_CONFIG_VAR("HTTPD_VERSION" HTTPD_VERSION)
    _APXS_CONFIG_VAR("INCLUDEDIR" HTTPD_INCLUDE_DIR)
    set(_httpd_modules_dir "${CMAKE_INSTALL_LIBEXECDIR}")
    set(HTTPD_FOUND TRUE)
  endif()

endif()

set(HTTPD_MODULES_DIR "${_httpd_modules_dir}"
    CACHE PATH "Install directory for Apache modules")

find_package_handle_standard_args(
  Httpd
  REQUIRED_VARS
    HTTPD_INCLUDE_DIR
    HTTPD_MODULES_DIR
  VERSION_VAR
    HTTPD_VERSION
)

if(HTTPD_FOUND)
  if(NOT TARGET httpd::httpd)
    add_library(httpd::httpd INTERFACE IMPORTED)
    set_target_properties(httpd::httpd PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES ${HTTPD_INCLUDE_DIR}
    )
    if(WIN32)
      set_target_properties(httpd::httpd PROPERTIES
        INTERFACE_LINK_LIBRARIES ${HTTPD_LIBRARY}
      )
    elseif(APPLE)
      target_link_options(httpd::httpd INTERFACE
        "-Wl,-undefined,dynamic_lookup"
      )
    endif()
  endif()

  if(NOT TARGET httpd::mod_dav)
    add_library(httpd::mod_dav INTERFACE IMPORTED)
    set_target_properties(httpd::mod_dav PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES ${HTTPD_INCLUDE_DIR}
    )
    if(WIN32)
      set_target_properties(httpd::mod_dav PROPERTIES
        INTERFACE_LINK_LIBRARIES ${MOD_DAV_LIBRARY}
      )
    elseif(APPLE)
      target_link_options(httpd::mod_dav INTERFACE
        "-Wl,-undefined,dynamic_lookup"
      )
    endif()
  endif()

endif()
