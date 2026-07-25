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
# FindMAGIC.cmake -- Find the libmagic library
#

find_path(MAGIC_INCLUDE_DIR
  NAMES magic.h
  PATH_SUFFIXES
    include
)

find_library(MAGIC_LIBRARY
  NAMES magic
  PATH_SUFFIXES lib
)

mark_as_advanced(
  MAGIC_INCLUDE_DIR
  MAGIC_LIBRARY
)

if (MAGIC_INCLUDE_DIR AND EXISTS ${MAGIC_INCLUDE_DIR}/magic.h)
  file(
    STRINGS "${MAGIC_INCLUDE_DIR}/magic.h" VERSION_STRINGS
    REGEX "#define MAGIC_VERSION"
  )
  string(REGEX REPLACE ".*MAGIC_VERSION[ \t]+([0-9]+).*" "\\1" MAGIC_VERSION ${VERSION_STRINGS})
else()
  set(MAGIC_VERSION 100)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  MAGIC
  REQUIRED_VARS
    MAGIC_LIBRARY
    MAGIC_INCLUDE_DIR
  VERSION_VAR
    MAGIC_VERSION
)

if (MAGIC_FOUND)
  add_library(MAGIC::MAGIC IMPORTED STATIC)
  set_target_properties(MAGIC::MAGIC PROPERTIES
    IMPORTED_LOCATION ${MAGIC_LIBRARY}
    INTERFACE_INCLUDE_DIRECTORIES ${MAGIC_INCLUDE_DIR}
  )
endif()
