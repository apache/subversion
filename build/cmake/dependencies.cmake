# Setup modules path

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/build/cmake")

### APR

find_package(APR REQUIRED)
add_library(external-apr ALIAS apr::apr)

### APR-Util

find_package(APRUtil REQUIRED)
add_library(external-aprutil ALIAS apr::aprutil)

### ZLIB

find_package(ZLIB)
add_library(external-zlib ALIAS ZLIB::ZLIB)

### EXPAT

find_package(EXPAT MODULE REQUIRED)
add_library(external-xml ALIAS EXPAT::EXPAT)

### LZ4

option(SVN_USE_INTERNAL_LZ4 "Use internal version of lz4" ON)

if(SVN_USE_INTERNAL_LZ4)
  add_library(external-lz4 STATIC "build/win32/empty.c")
  target_compile_definitions(external-lz4 PUBLIC "SVN_INTERNAL_LZ4")
else()
  find_package(lz4 CONFIG REQUIRED)
  add_library(external-lz4 ALIAS lz4::lz4)
endif()

### UTF8PROC

option(SVN_USE_INTERNAL_UTF8PROC "Use internal version of utf8proc" ON)

if(SVN_USE_INTERNAL_UTF8PROC)
  add_library(external-utf8proc STATIC "build/win32/empty.c")
  target_compile_definitions(external-utf8proc PUBLIC "SVN_INTERNAL_UTF8PROC")
else()
  message(FATAL_ERROR "TODO:")
  # find_package(utf8proc CONFIG REQUIRED)
  # add_library(external-utf8proc ALIAS utf8proc)
endif()

### SQLite3

option(SVN_SQLITE_USE_AMALGAMATION "Use sqlite amalgamation" ON)
set(SVN_SQLITE_AMALGAMATION_ROOT "${CMAKE_SOURCE_DIR}/sqlite-amalgamation"
  CACHE STRING "Directory with sqlite amalgamation"
)

if(SVN_SQLITE_USE_AMALGAMATION)
  add_library(external-sqlite STATIC "build/win32/empty.c")
  find_path(SVN_SQLITE_AMALGAMATION_DIR
    NAMES sqlite3.c
    PATHS ${SVN_SQLITE_AMALGAMATION_ROOT}
  )
  target_include_directories(external-sqlite PUBLIC ${SVN_SQLITE_AMALGAMATION_DIR})
  target_compile_definitions(external-sqlite PUBLIC SVN_SQLITE_INLINE)
else()
  find_package(SQLite3 REQUIRED)
  add_library(external-sqlite ALIAS SQLite::SQLite3)
endif()
