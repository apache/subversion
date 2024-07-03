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
# CMakeLists.txt -- configuration file for CMake build system
#

cmake_minimum_required(VERSION 3.12)

project("Subversion")

include("build/cmake/dependencies.cmake")

include("build/cmake/options.cmake")

include("build/cmake/extractor.cmake")

set(SVN_INCLUDE_DIRECTORIES
  "${CMAKE_CURRENT_SOURCE_DIR}/subversion/include"
  "${CMAKE_CURRENT_BINARY_DIR}"
)

if (WIN32)
  add_compile_definitions(
    "alloca=_alloca"
    "WIN32"
  )
endif()

if (NOT EXISTS "${CMAKE_SOURCE_DIR}/build/cmake/targets.cmake")
  message(FATAL_ERROR
    "The 'build/cmake/targets.cmake' file does NOT exist. "
    "Use the following command to generate it:\n"
    "  python gen-make.py -t cmake"
  )
endif()

include("build/cmake/targets.cmake")
