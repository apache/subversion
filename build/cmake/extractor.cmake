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
# extractor.cmake -- Extracts function names from header files and writes
# them to a .def file.
#
# cmake -DEXPORT_HEADER_FILE_PATHS=<...path_to_header_files>
#       -DEXPORT_DEF_FILE_PATH=<path_to_def_file>
#       [-DEXPORT_BLACKLIST=<...symbols_to_ignore>]
#       -P extractor.cmake

separate_arguments(EXPORT_HEADER_FILE_PATHS)
separate_arguments(EXPORT_BLACKLIST)

# see build/generator/extractor.py
set(func_regex "(^|\n)((([A-Za-z0-9_]+|[*]) )+[*]?)?((svn|apr)_[A-Za-z0-9_]+)[ \t\r\n]*\\(")

set(defs)
foreach(file ${EXPORT_HEADER_FILE_PATHS})
  file(READ ${file} contents)
  string(REGEX MATCHALL "${func_regex}" funcs ${contents})

  foreach(func_string ${funcs})
    string(REGEX MATCH "[A-Za-z0-9_]+[ \t\r\n]*\\($" func_name ${func_string})
    string(REGEX REPLACE "[ \t\r\n]*\\($" "" func_name ${func_name})
    list(APPEND defs "${func_name}")
  endforeach()

  get_filename_component(filename ${file} NAME)
  if(${filename} STREQUAL "svn_ctype.h")
    # See libsvn_subr/ctype.c for an explanation why we use CONSTANT and not
    # DATA, even though it causes an LNK4087 warning!
    list(APPEND defs "svn_ctype_table = svn_ctype_table_internal CONSTANT")
  elseif(${filename} STREQUAL "svn_wc_private.h")
    # svn_wc__internal_walk_children() is now internal to libsvn_wc
    # but entries-dump.c still calls it
    list(APPEND defs "svn_wc__internal_walk_children")
  endif()
endforeach()

list(SORT defs)
list(REMOVE_DUPLICATES defs)

set(def_file_content "EXPORTS\n")
foreach(def ${defs})
  list(FIND EXPORT_BLACKLIST "${def}" skip)
  if(skip LESS 0)
    string(APPEND def_file_content "${def}\n")
  endif()
endforeach()

if(EXISTS "${EXPORT_DEF_FILE_PATH}")
  file(READ "${EXPORT_DEF_FILE_PATH}" old_file_content)
else()
  set(old_file_content "NOT_EXISTS")
endif()
if(NOT ${old_file_content} STREQUAL ${def_file_content})
  file(WRITE "${EXPORT_DEF_FILE_PATH}" ${def_file_content})
endif()
