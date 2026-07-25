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
# FindSASL.cmake -- Find the SASL library
#

find_path(SASL_INCLUDE_DIR
  NAMES sasl.h
  PATH_SUFFIXES
    include
    include/sasl
)

find_library(SASL_LIBRARY
  NAMES sasl2
  PATH_SUFFIXES lib
)

mark_as_advanced(
  SASL_INCLUDE_DIR
  SASL_LIBRARY
)

if (SASL_INCLUDE_DIR AND EXISTS ${SASL_INCLUDE_DIR}/sasl.h)
  file(
    STRINGS "${SASL_INCLUDE_DIR}/sasl.h" VERSION_STRINGS
    REGEX "#define (SASL_VERSION_MAJOR|SASL_VERSION_MINOR|SASL_VERSION_STEP)"
  )

  string(REGEX REPLACE ".*SASL_VERSION_MAJOR +([0-9]+).*" "\\1" SASL_VERSION_MAJOR ${VERSION_STRINGS})
  string(REGEX REPLACE ".*SASL_VERSION_MINOR +([0-9]+).*" "\\1" SASL_VERSION_MINOR ${VERSION_STRINGS})
  string(REGEX REPLACE ".*SASL_VERSION_STEP +([0-9]+).*" "\\1" SASL_VERSION_STEP ${VERSION_STRINGS})
else()
  # Default version to 1.0.0 if not found.
  set(SASL_VERSION_MAJOR 1)
  set(SASL_VERSION_MINOR 0)
  set(SASL_VERSION_STEP 0)
endif()

set(SASL_VERSION "${SASL_VERSION_MAJOR}.${SASL_VERSION_MINOR}.${SASL_VERSION_STEP}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  SASL
  REQUIRED_VARS
    SASL_LIBRARY
    SASL_INCLUDE_DIR
  VERSION_VAR
    SASL_VERSION
)

if (SASL_FOUND)
  add_library(SASL::SASL IMPORTED STATIC)
  set_target_properties(SASL::SASL PROPERTIES
    IMPORTED_LOCATION ${SASL_LIBRARY}
    INTERFACE_INCLUDE_DIRECTORIES ${SASL_INCLUDE_DIR}
  )
endif()
