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
# options.cmake -- options and confiurations for CMake
#

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/subversion/svn_private_config.hc"
  "${CMAKE_CURRENT_BINARY_DIR}/svn_private_config.h"
)

option(SVN_BUILD_RA_LOCAL "Build Subversion Local Repository Access Library" ON)
if (SVN_BUILD_RA_LOCAL)
  add_compile_definitions("SVN_LIBSVN_RA_LINKS_RA_LOCAL")
endif()

# TODO:
# option(SVN_BUILD_RA_SERF "Build Subversion HTTP/WebDAV Protocol Repository Access Library" OFF)
# if (SVN_BUILD_RA_SERF)
#   add_compile_definitions("SVN_LIBSVN_RA_LINKS_RA_SERF")
# endif()

option(SVN_BUILD_RA_SVN "Build Subversion SVN Protocol Repository Access Library" ON)
if (SVN_BUILD_RA_SVN)
  add_compile_definitions("SVN_LIBSVN_RA_LINKS_RA_SVN")
endif()

option(SVN_BUILD_FS_FS "Build Subversion FSFS Repository Filesystem Library" ON)
if (SVN_BUILD_FS_FS)
  add_compile_definitions("SVN_LIBSVN_FS_LINKS_FS_FS")
endif()

option(SVN_BUILD_FS_X "Build Subversion FSX Repository Filesystem Library" ON)
if (SVN_BUILD_FS_X)
  add_compile_definitions("SVN_LIBSVN_FS_LINKS_FS_X")
endif()

option(SVN_BUILD_PROGRAMS "Build Subversion programs (such as svn.exe)" ON)
option(SVN_BUILD_TOOLS "Build Subversion tools" OFF)
option(SVN_BUILD_TEST "Build Subversion test-suite" OFF)

option(BUILD_SHARED_LIBS "Build using shared libraries" ON)

option(SVN_BUILD_SHARED_FS "Build shared FS modules" ${BUILD_SHARED_LIBS})
if(SVN_BUILD_SHARED_FS)
  set(SVN_FS_BUILD_TYPE SHARED)
else()
  set(SVN_FS_BUILD_TYPE STATIC)
endif()

option(SVN_BUILD_SHARED_RA "Build shared RA modules" OFF)
if(SVN_BUILD_SHARED_RA)
  set(SVN_RA_BUILD_TYPE SHARED)
else()
  set(SVN_RA_BUILD_TYPE STATIC)
endif()

if(SVN_BUILD_SHARED_RA)
  message(FATAL_ERROR "SVN_BUILD_SHARED_RA not yet supported")
endif()
