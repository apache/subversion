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
# FindCURSES.cmake -- Find the curses library
#

find_path(CURSES_INCLUDE_DIR
  NAMES curses.h
  PATH_SUFFIXES
    include
)

find_library(CURSES_LIBRARY
  NAMES ncurses pdcurses curses 
  PATH_SUFFIXES lib
)

mark_as_advanced(
  CURSES_INCLUDE_DIR
  CURSES_LIBRARY
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  CURSES
  REQUIRED_VARS
    CURSES_LIBRARY
    CURSES_INCLUDE_DIR
)

if (CURSES_FOUND)
  add_library(CURSES::CURSES IMPORTED STATIC)
  set_target_properties(CURSES::CURSES PROPERTIES
    IMPORTED_LOCATION ${CURSES_LIBRARY}
    INTERFACE_INCLUDE_DIRECTORIES ${CURSES_INCLUDE_DIR}
  )
endif()
