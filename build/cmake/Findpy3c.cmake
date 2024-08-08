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
# Findpy3c.cmake -- CMake module for py3c library
#

find_path(PY3C_INCLUDE_DIR
  NAMES py3c.h
  PATH_SUFFIXES
    "include/py3c"
)

mark_as_advanced(
  PY3C_INCLUDE_DIR
)

include(FindPackageHandleStandardArgs)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(
  py3c
  REQUIRED_VARS
    PY3C_INCLUDE_DIR
)

# TODO: Is it okay to put py3c into 'Python' namespace?
if(py3c_FOUND AND NOT TARGET Python::py3c)
  add_library(Python::py3c IMPORTED INTERFACE)
  set_target_properties(Python::py3c PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${PY3C_INCLUDE_DIR}
  )
endif()
