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

function(read_version path var major minor patch)
  get_filename_component(path ${path} ABSOLUTE)

  if (EXISTS ${path})
    file(
      STRINGS ${path} VERSION_STRINGS
      REGEX "#define (${major}|${minor}|${patch})"
    )

    string(REGEX REPLACE ".*${major} +([0-9]+).*" "\\1" VER_MAJOR ${VERSION_STRINGS})
    string(REGEX REPLACE ".*${minor} +([0-9]+).*" "\\1" VER_MINOR ${VERSION_STRINGS})
    string(REGEX REPLACE ".*${patch} +([0-9]+).*" "\\1" VER_PATCH ${VERSION_STRINGS})

    set(${var} "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}" PARENT_SCOPE)
  endif()
endfunction()

cmake_minimum_required(VERSION 3.12)

# CMP0092: MSVC warning flags are not in CMAKE_<LANG>_FLAGS by default.
if(POLICY CMP0092)
  cmake_policy(SET CMP0092 NEW)
endif()

# CMP0078: UseSWIG generates standard target names.
if(POLICY CMP0078)
  cmake_policy(SET CMP0078 NEW)
else()
  # Otherwise, setup this behavior manually.
  set(UseSWIG_TARGET_NAME_PREFERENCE STANDARD)
endif()

# CMP0086: UseSWIG honors SWIG_MODULE_NAME via -module flag.
if(POLICY CMP0086)
  cmake_policy(SET CMP0086 NEW)
endif()

# Enable support for MSVC runtime library selection by abstraction
# if supported by CMake.
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

read_version(
  "subversion/include/svn_version.h" SVN_VERSION
  SVN_VER_MAJOR SVN_VER_MINOR SVN_VER_PATCH
)

project("Subversion"
  VERSION "${SVN_VERSION}"
  LANGUAGES C
)

### Options

include(CMakeDependentOption)

# Build components
option(SVN_ENABLE_SVNXX "Enable compilation of the C++ bindings (requires C++)" OFF)
option(SVN_ENABLE_PROGRAMS "Build Subversion programs (such as svn.exe)" ON)
cmake_dependent_option(SVN_ENABLE_TOOLS "Build Subversion tools" ON "SVN_ENABLE_TESTS" OFF)
option(SVN_ENABLE_TESTS "Build Subversion test-suite" OFF)
option(SVN_TEST_EXPAND "Expand tests; This will slow-down configuration, but you will have an ability to run any subtest" OFF)
option(SVN_TEST_CONFIGURE_FOR_PARALLEL "Configures tests for parallel run execution" OFF)
set(SVN_TEST_COMMAND_ARGUMENTS "" CACHE STRING "Additional command line options to be passed to run_tests.py")
option(SVN_ENABLE_APACHE_MODULES "Build modules for Apache HTTPD" OFF)

option(SVN_ENABLE_SWIG_PERL "Enable Subversion SWIG bindings for Perl" OFF)
option(SVN_ENABLE_SWIG_PYTHON "Enable Subversion SWIG bindings into Python" OFF)
option(SVN_ENABLE_SWIG_RUBY "Enable Subversion SWIG bindings into Ruby" OFF)

# Enable modules and features
option(SVN_ENABLE_RA_LOCAL "Enable Subversion Local Repository Access Library" ON)
option(SVN_ENABLE_RA_SERF "Enable Subversion HTTP/WebDAV Protocol Repository Access Library" ON)
option(SVN_ENABLE_RA_SVN "Enable Subversion SVN Protocol Repository Access Library" ON)
option(SVN_ENABLE_FS_FS "Enable Subversion FSFS Repository Filesystem Library" ON)
option(SVN_ENABLE_FS_X "Enable Subversion FSX Repository Filesystem Library" ON)
option(SVN_ENABLE_FS_BASE "Enable Subversion Filesystem Base Library (NOT IMPLEMENTED and DEPRECATED)" OFF)
option(SVN_ENABLE_NLS "Enable gettext functionality" OFF)
option(SVN_ENABLE_AUTH_KWALLET "Enable KWallet auth library" OFF)
option(SVN_ENABLE_AUTH_GNOME_KEYRING "Enable GNOME Keyring for auth credentials" OFF)
cmake_dependent_option(SVN_ENABLE_AUTH_GPG_AGENT "Enable GPG Agent support" OFF "WIN32" ON)

option(SVN_INSTALL_PRIVATE_H "Install private header files." OFF)

# Configuration
option(BUILD_SHARED_LIBS "Build using shared libraries" ON)
cmake_dependent_option(SVN_BUILD_SHARED_FS "Build shared FS modules" ON "BUILD_SHARED_LIBS" OFF)
cmake_dependent_option(SVN_BUILD_SHARED_RA "Build shared RA modules" ON "BUILD_SHARED_LIBS" OFF)
option(SVN_DEBUG "Enables specific features for developer builds" OFF)
cmake_dependent_option(SVN_USE_WIN32_CRASHHANDLER "Enables WIN32 crash handler." ON "WIN32" OFF)
option(SVN_USE_DSO "Defined if svn should try to load DSOs" OFF)
set(SVN_CHECKSUM_BACKEND "apr" CACHE STRING "Checksum backend to use (possible values are 'apr', 'openssl', or 'bcrypt')" )
set_property(CACHE SVN_CHECKSUM_BACKEND PROPERTY STRINGS "apr" "openssl" "bcrypt")
set(SVN_SOVERSION "0" CACHE STRING "Subversion library ABI version")
mark_as_advanced(SVN_SOVERSION)

# Check whether we have pkg-config executable or not; Silence any messages.
find_package(PkgConfig QUIET)
cmake_dependent_option(SVN_USE_PKG_CONFIG "Use pkg-config for the dependencies" ON "PKG_CONFIG_FOUND" OFF)

# Dependecies
option(SVN_USE_INTERNAL_LZ4 "Use internal version of lz4" ON)
option(SVN_USE_INTERNAL_UTF8PROC "Use internal version of utf8proc" ON)
option(SVN_SQLITE_USE_AMALGAMATION "Use sqlite amalgamation" OFF)
set(SQLiteAmalgamation_ROOT "${CMAKE_SOURCE_DIR}/sqlite-amalgamation" CACHE STRING "Directory with sqlite amalgamation")

# Require C++ compiler
if (SVN_ENABLE_SVNXX OR SVN_ENABLE_AUTH_KWALLET)
  # TODO: Also add SVN_ENABLE_JAVAHL to the conditions when they done.

  enable_language(CXX)
endif()

set(PRIVATE_CONFIG_DEFINITIONS "")

macro(add_private_config_definition comment name value)
  string(APPEND PRIVATE_CONFIG_DEFINITIONS
    "\n"
    "/* ${comment} */\n"
  )

  if("${value}" STREQUAL "")
    string(APPEND PRIVATE_CONFIG_DEFINITIONS
      "#define ${name}\n"
    )
  else()
    string(APPEND PRIVATE_CONFIG_DEFINITIONS
      "#define ${name} ${value}\n"
    )
  endif()
endmacro()

if (SVN_ENABLE_RA_LOCAL)
  add_private_config_definition(
    "Defined if libsvn_ra should link against libsvn_ra_local"
    "SVN_LIBSVN_RA_LINKS_RA_LOCAL" "1"
  )
endif()

if (SVN_ENABLE_RA_SERF)
  add_private_config_definition(
    "Defined if libsvn_ra should link against libsvn_ra_serf"
    "SVN_LIBSVN_RA_LINKS_RA_SERF" "1"
  )
endif()

if (SVN_ENABLE_RA_SVN)
  add_private_config_definition(
    "Defined if libsvn_ra should link against libsvn_ra_svn"
    "SVN_LIBSVN_RA_LINKS_RA_SVN" "1"
  )
endif()

if (SVN_ENABLE_FS_FS)
  add_private_config_definition(
    "Defined if libsvn_fs should link against libsvn_fs_fs"
    "SVN_LIBSVN_FS_LINKS_FS_FS" "1"
  )
endif()

if (SVN_ENABLE_FS_X)
  add_private_config_definition(
    "Defined if libsvn_fs should link against libsvn_fs_x"
    "SVN_LIBSVN_FS_LINKS_FS_X" "1"
  )
endif()

if(SVN_ENABLE_AUTH_KWALLET)
  if(NOT BUILD_SHARED_LIBS)
    message(SEND_ERROR "SVN_ENABLE_AUTH_KWALLET is not supported in static build.")
  endif()

  add_private_config_definition(
    "Defined if KWallet support is enabled"
    "SVN_HAVE_KWALLET" "1"
  )
endif()

if(SVN_ENABLE_AUTH_GPG_AGENT)
  add_private_config_definition(
    "Is GPG Agent support enabled?"
    "SVN_HAVE_GPG_AGENT" "1"
  )
endif()

if (SVN_DEBUG)
  add_compile_definitions("SVN_DEBUG")
endif()

if(SVN_USE_DSO)
  add_private_config_definition(
    "Defined if svn should try to load DSOs"
    "SVN_USE_DSO" "1"
  )
endif()

add_private_config_definition(
  "Shared library file name suffix format"
  "SVN_DSO_SUFFIX_FMT" "\"%d${CMAKE_SHARED_LIBRARY_SUFFIX}\""
)
add_private_config_definition(
  "Subversion library major version"
  "SVN_SOVERSION" "${SVN_SOVERSION}"
)

if (SVN_ENABLE_TESTS)
  enable_testing()
endif()

if(SVN_BUILD_SHARED_FS)
  set(SVN_FS_BUILD_TYPE SHARED)
else()
  set(SVN_FS_BUILD_TYPE STATIC)
endif()

if(SVN_BUILD_SHARED_RA)
  set(SVN_RA_BUILD_TYPE SHARED)
else()
  set(SVN_RA_BUILD_TYPE STATIC)
endif()

if(SVN_ENABLE_FS_BASE)
  message(FATAL_ERROR "SVN_ENABLE_FS_BASE is NOT implemented and deprecated.")
endif()

if(BUILD_SHARED_LIBS OR SVN_BUILD_SHARED_FS OR SVN_BUILD_SHARED_RA)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# Setup modules path

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/build/cmake")

# Require pkg-config if we are going to use it, to show the `Found PkgConfig`
# meassge and insure it has actually been found. (yea, this invokes the module
# for second time, but we are guaranteed to from it to not perform actual look
# for the path due to the cache. It's fine to include the same modules for
# multiple times).
if(SVN_USE_PKG_CONFIG)
  find_package(PkgConfig REQUIRED)
endif()

### APR and APR-Util

if(SVN_USE_PKG_CONFIG)
  pkg_search_module(apr REQUIRED IMPORTED_TARGET apr-2 apr-1)
  add_library(external-apr ALIAS PkgConfig::apr)

  if(APR_VERSION VERSION_LESS 2.0.0)
    # apr-1
    pkg_check_modules(aprutil-1 REQUIRED IMPORTED_TARGET apr-util-1)
    add_library(external-aprutil ALIAS PkgConfig::aprutil-1)
  else()
    # apr-2
    add_library(external-aprutil ALIAS PkgConfig::apr)
  endif()
else()
  find_package(APR REQUIRED)
  add_library(external-apr ALIAS apr::apr)

  if(APR_VERSION VERSION_LESS 2.0.0)
    find_package(APRUtil REQUIRED)
    add_library(external-aprutil ALIAS apr::aprutil)
  else()
    add_library(external-aprutil ALIAS apr::apr)
  endif()
endif()

### ZLIB

if(SVN_USE_PKG_CONFIG)
  pkg_check_modules(zlib REQUIRED IMPORTED_TARGET zlib)
  add_library(external-zlib ALIAS PkgConfig::zlib)
else()
  find_package(ZLIB REQUIRED)
  add_library(external-zlib ALIAS ZLIB::ZLIB)
endif()

### EXPAT

if(SVN_USE_PKG_CONFIG)
  pkg_check_modules(expat REQUIRED IMPORTED_TARGET expat)
  add_library(external-xml ALIAS PkgConfig::expat)
else()
  find_package(EXPAT REQUIRED)
  add_library(external-xml ALIAS EXPAT::EXPAT)
endif()

### LZ4

if(SVN_USE_INTERNAL_LZ4)
  add_library(external-lz4 INTERFACE)
  target_compile_definitions(external-lz4 INTERFACE "SVN_INTERNAL_LZ4")

  read_version(
    "subversion/libsvn_subr/lz4/lz4internal.h" lz4_VERSION
    LZ4_VERSION_MAJOR LZ4_VERSION_MINOR LZ4_VERSION_RELEASE
  )
elseif(SVN_USE_PKG_CONFIG)
  pkg_check_modules(lz4 REQUIRED IMPORTED_TARGET liblz4)
  add_library(external-lz4 ALIAS PkgConfig::lz4)
else()
  find_package(lz4 CONFIG REQUIRED)
  add_library(external-lz4 ALIAS lz4::lz4)
endif()

### UTF8PROC

if(SVN_USE_INTERNAL_UTF8PROC)
  add_library(external-utf8proc INTERFACE)
  target_compile_definitions(external-utf8proc INTERFACE "SVN_INTERNAL_UTF8PROC")

  read_version(
    "subversion/libsvn_subr/utf8proc/utf8proc_internal.h" UTF8PROC_VERSION
    UTF8PROC_VERSION_MAJOR UTF8PROC_VERSION_MINOR UTF8PROC_VERSION_PATCH
  )
elseif(SVN_USE_PKG_CONFIG)
  pkg_check_modules(utf8proc REQUIRED IMPORTED_TARGET libutf8proc)
  add_library(external-utf8proc ALIAS PkgConfig::utf8proc)
else()
  find_package(UTF8PROC REQUIRED)
  add_library(external-utf8proc ALIAS UTF8PROC::UTF8PROC)
endif()

### SQLite3

if(SVN_SQLITE_USE_AMALGAMATION)
  find_package(SQLiteAmalgamation REQUIRED)
  add_library(external-sqlite ALIAS SQLite::SQLite3Amalgamation)
elseif(SVN_USE_PKG_CONFIG)
  pkg_check_modules(sqlite3 REQUIRED IMPORTED_TARGET sqlite3)
  add_library(external-sqlite ALIAS PkgConfig::sqlite3)
else()
  # It should be not required.
  find_package(SQLite3)

  if(SQLite3_FOUND)
    add_library(external-sqlite ALIAS SQLite::SQLite3)
  else()
    find_package(SQLiteAmalgamation REQUIRED)
    add_library(external-sqlite ALIAS SQLite::SQLite3Amalgamation)

    # Is implicitness an actual problem? This warning could be removed
    # if it isn't.
    message(WARNING
      "Implicitly using SQLite amalgamation; enable"
      "SVN_SQLITE_USE_AMALGAMATION option to force it."
    )
  endif()
endif()

### Serf
if (SVN_ENABLE_RA_SERF)
  if(SVN_USE_PKG_CONFIG)
    pkg_search_module(serf IMPORTED_TARGET serf-2 serf-1)

    if(serf_FOUND)
      add_library(external-serf ALIAS PkgConfig::serf)
    endif()
  else()
    find_package(Serf REQUIRED)
    add_library(external-serf ALIAS Serf::Serf)
  endif()
endif()

if (SVN_CHECKSUM_BACKEND STREQUAL "apr")
  add_library(external-checksum-libs INTERFACE)
  add_private_config_definition(
    "Defined if svn should use APR to compute checksums."
    "SVN_CHECKSUM_BACKEND_APR" "1"
  )
elseif (SVN_CHECKSUM_BACKEND STREQUAL "openssl")
  find_package(OpenSSL REQUIRED)
  add_library(external-checksum-libs ALIAS OpenSSL::Crypto)
  add_private_config_definition(
    "Defined if svn should use OpenSSL to compute checksums."
    "SVN_CHECKSUM_BACKEND_OPENSSL" "1"
  )
elseif (SVN_CHECKSUM_BACKEND STREQUAL "bcrypt")
  if (NOT WIN32)
    message(SEND_ERROR "BCrypt checksum backend is only available on Win32 platform.")
  endif()

  add_library(external-checksum-libs INTERFACE)
  target_link_libraries(external-checksum-libs INTERFACE Bcrypt.lib)

  add_private_config_definition(
    "Defined if svn should use BCrypt to compute checksums."
    "SVN_CHECKSUM_BACKEND_BCRYPT" "1"
  )
else()
  message(SEND_ERROR
    "Invalid value of SVN_CHECKSUM_BACKEND: '${SVN_CHECKSUM_BACKEND}'. "
    "Possible options are 'apr' or 'openssl'."
  )
endif()

### Python

if(SVN_ENABLE_SWIG_PYTHON)
  find_package(Python REQUIRED COMPONENTS
      Interpreter
      Development.Embed
  )
elseif(SVN_ENABLE_TESTS)
  find_package(Python REQUIRED COMPONENTS
      Interpreter
  )
endif()

### Httpd

if(SVN_ENABLE_APACHE_MODULES)
  find_package(Httpd REQUIRED)
  add_library(external-libhttpd ALIAS httpd::httpd)
  add_library(external-mod_dav ALIAS httpd::mod_dav)
endif()

### KWallet

if(SVN_ENABLE_AUTH_KWALLET)
  find_package(KF5Wallet REQUIRED)
  find_package(KF5CoreAddons REQUIRED)
  find_package(KF5I18n REQUIRED)
  find_package(DBus1 REQUIRED)
  find_package(Qt5 COMPONENTS Core REQUIRED)

  add_library(external-kwallet INTERFACE)
  target_link_libraries(external-kwallet INTERFACE
    KF5::Wallet
    KF5::CoreAddons
    KF5::I18n
    Qt5::Core
    dbus-1
  )
  target_compile_definitions(external-kwallet INTERFACE SVN_HAVE_KF5)
endif()

### GNOME Keyring

if(SVN_ENABLE_AUTH_GNOME_KEYRING)
  add_library(external-gnome-keyring INTERFACE)

  if(SVN_USE_PKG_CONFIG OR PKG_CONFIG_FOUND)
    pkg_check_modules(libsecret-1 REQUIRED IMPORTED_TARGET libsecret-1)
    target_link_libraries(external-gnome-keyring INTERFACE PkgConfig::libsecret-1)
  else()
    message(ERROR "GNOME Keyring requires pkg-config")
  endif()

  add_private_config_definition(
    "Is libsecret support enabled?"
    "SVN_HAVE_LIBSECRET" "1"
  )
endif()

if(SVN_ENABLE_SWIG_PERL OR SVN_ENABLE_SWIG_PYTHON OR SVN_ENABLE_SWIG_RUBY)
  find_package(SWIG REQUIRED)
  include(${SWIG_USE_FILE})

  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/subversion/bindings/swig/proxy")

  set(SWIG_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/subversion/bindings/swig/include
    ${CMAKE_CURRENT_SOURCE_DIR}/subversion/bindings/swig
    ${CMAKE_CURRENT_SOURCE_DIR}/subversion/include

    ${CMAKE_CURRENT_BINARY_DIR}/subversion/bindings/swig/proxy
    ${CMAKE_CURRENT_BINARY_DIR}
  )

  add_library(external-swig INTERFACE)

  file(GLOB swig_headers_input "${CMAKE_CURRENT_SOURCE_DIR}/subversion/include/*.h")
  set(swig_headers_output)
  foreach(swig_header ${swig_headers_input})
    get_filename_component(filename ${swig_header} NAME_WLE)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/subversion/bindings/swig/proxy/${filename}_h.swg")
    list(APPEND swig_headers_output ${output})

    add_custom_command(
      DEPENDS
        "${swig_header}"
      OUTPUT
        "${output}"
      WORKING_DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}"
      COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/build/generator/swig/header_wrappers.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/build.conf"
        "${SWIG_EXECUTABLE}"
        "${swig_header}"
      COMMAND_EXPAND_LISTS
    )
  endforeach()

  add_custom_target(swig_headers DEPENDS ${swig_headers_output})
  add_dependencies(external-swig swig_headers)
endif()

function(swig_target_external_runtime target lang)
  set(swig_runtime_path "${CMAKE_CURRENT_BINARY_DIR}/subversion/bindings/swig/proxy/swig_${lang}_external_runtime.swg")
  add_custom_command(
    OUTPUT ${swig_runtime_path}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/build/generator/swig/external_runtime.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/build.conf"
      "${SWIG_EXECUTABLE}"
      "${lang}"
    COMMAND_EXPAND_LISTS
  )
  target_sources(${target} INTERFACE ${swig_runtime_path})
endfunction()

if(SVN_ENABLE_SWIG_PYTHON)
  find_package(PY3C REQUIRED)

  add_library(external-python INTERFACE)
  target_link_libraries(external-python INTERFACE
    Python::Python
    Python::py3c
  )
  target_include_directories(external-python INTERFACE
    ${SWIG_INCLUDE_DIRECTORIES}
    ${CMAKE_CURRENT_SOURCE_DIR}/subversion/bindings/swig/python/libsvn_swig_py
  )

  swig_target_external_runtime(external-python python)
endif()

if(SVN_ENABLE_SWIG_PERL)
  find_package(Perl REQUIRED)
  find_package(PerlLibs REQUIRED)

  separate_arguments(PERL_EXTRA_C_FLAGS)

  add_library(external-perl IMPORTED INTERFACE)
  target_include_directories(external-perl INTERFACE
    ${PERL_INCLUDE_PATH}
    ${SWIG_INCLUDE_DIRECTORIES}
    ${CMAKE_CURRENT_SOURCE_DIR}/subversion/bindings/swig/perl/libsvn_swig_perl
  )
  target_compile_definitions(external-perl INTERFACE "__inline__=__inline")
  target_compile_options(external-perl INTERFACE ${PERL_EXTRA_C_FLAGS})
  target_link_libraries(external-perl INTERFACE ${PERL_LIBRARY})

  swig_target_external_runtime(external-perl perl)
endif()

if(SVN_ENABLE_SWIG_RUBY)
  find_package(Ruby REQUIRED)

  add_library(external-ruby IMPORTED INTERFACE)
  target_include_directories(external-ruby INTERFACE
    ${Ruby_INCLUDE_DIRS}
    ${SWIG_INCLUDE_DIRECTORIES}
    ${CMAKE_CURRENT_SOURCE_DIR}/subversion/bindings/swig/ruby/libsvn_swig_ruby
  )
  target_compile_definitions(external-ruby INTERFACE HAVE_RB_ERRINFO)
  target_link_libraries(external-ruby INTERFACE ${Ruby_LIBRARY})

  swig_target_external_runtime(external-ruby ruby)
endif()

function(target_exports target_name)
  if (WIN32)
    set(filter_names
      # svn_config_enumerator_t looks like (to our regex) a
      # function declaration for svn_boolean_t
      "svn_boolean_t"
      # Not available on Windows
      "svn_auth_get_keychain_simple_provider"
      "svn_auth_get_keychain_ssl_client_cert_pw_provider"
      "svn_auth_get_gnome_keyring_simple_provider"
      "svn_auth_get_gnome_keyring_ssl_client_cert_pw_provider"
      "svn_auth_get_kwallet_simple_provider"
      "svn_auth_get_kwallet_ssl_client_cert_pw_provider"
      "svn_auth_gnome_keyring_version"
      "svn_auth_kwallet_version"
      "svn_auth_get_gpg_agent_simple_provider"
      "svn_auth_gpg_agent_version"
      # Unavailable in release mode
      "svn_fs_base__trail_debug"
    )

    set(def_file_path ${CMAKE_BINARY_DIR}/${target_name}.def)
    set(headers)

    foreach(file ${ARGN})
      list(APPEND headers "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
    endforeach()

    add_custom_command(
      OUTPUT "${def_file_path}"
      DEPENDS ${headers}
      COMMAND ${CMAKE_COMMAND}
              "-DEXPORT_HEADER_FILE_PATHS=${headers}"
              "-DEXPORT_DEF_FILE_PATH=${def_file_path}"
              "-DEXPORT_BLACKLIST=${filter_names}"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/build/cmake/extractor.cmake"
      WORKING_DIRECTORY "${SERF_SOURCE_DIR}"
    )

    target_sources("${target_name}" PRIVATE "${def_file_path}")
  endif()
endfunction()

### Checks

include(CheckIncludeFiles)
include(CheckSymbolExists)

macro(autocheck_include_files INCLUDE VARIABLE)
  check_include_files(${INCLUDE} ${VARIABLE})

  if(${VARIABLE})
    add_private_config_definition(
      "Define to 1 if you have the <${INCLUDE}> header file."
      "${VARIABLE}" "1"
    )
  endif()
endmacro()

macro(autocheck_symbol_exists SYMBOL FILE VARIABLE)
  check_symbol_exists(${SYMBOL} ${FILE} ${VARIABLE})

  if(${VARIABLE})
    add_private_config_definition(
      "Define to 1 if you have the `${SYMBOL}' function."
      "${VARIABLE}" "1"
    )
  endif()
endmacro()

autocheck_include_files("elf.h" HAVE_ELF_H)
autocheck_include_files("inttypes.h" HAVE_INTTYPES_H)
autocheck_include_files("stdbool.h" HAVE_STDBOOL_H)
autocheck_include_files("stdint.h" HAVE_STDINT_H)

autocheck_include_files("sys/utsname.h" HAVE_SYS_UTSNAME_H)
if (HAVE_SYS_UTSNAME_H)
  autocheck_symbol_exists("uname" "sys/utsname.h" HAVE_UNAME)
endif()

autocheck_include_files("sys/types.h" HAVE_SYS_TYPES_H)

autocheck_include_files("termios.h" HAVE_TERMIOS_H)
if(HAVE_TERMIOS_H)
  autocheck_symbol_exists("tcgetattr" "termios.h" HAVE_TCGETATTR)
  autocheck_symbol_exists("tcsetattr" "termios.h" HAVE_TCSETATTR)
endif()

autocheck_include_files("unistd.h" HAVE_UNISTD_H)
if (HAVE_UNISTD_H)
  autocheck_symbol_exists("symlink" "unistd.h" HAVE_SYMLINK)
  autocheck_symbol_exists("readlink" "unistd.h" HAVE_READLINK)
  autocheck_symbol_exists("getpid" "unistd.h" HAVE_GETPID)
endif()

set(CMAKE_REQUIRED_LIBRARIES external-zlib)
autocheck_symbol_exists("adler32_z" "zlib.h" SVN_ZLIB_HAS_ADLER32_Z)
unset(CMAKE_REQUIRED_LIBRARIES)

include_directories("${CMAKE_CURRENT_BINARY_DIR}")

file(GLOB public_headers "subversion/include/*.h")
install(FILES ${public_headers} DESTINATION "include/subversion-1")

if(SVN_INSTALL_PRIVATE_H)
  file(GLOB private_headers "subversion/include/private/*.h")
  install(FILES ${private_headers} DESTINATION "include/subversion-1/private")
endif()

if(SVN_ENABLE_SVNXX)
  install(
    DIRECTORY "subversion/bindings/cxx/include/"
    DESTINATION "include/subversion-1"
  )
endif()

if (WIN32)
  add_compile_definitions(
    "alloca=_alloca"
    "WIN32"
  )
endif()

if(APPLE)
  add_compile_definitions("DARWIN")
endif()

if (MSVC)
  # Setup warning level
  add_compile_options(/W4)

  # Disable warning
  add_compile_options(/wd4100)
  add_compile_options(/wd4127)
  add_compile_options(/wd4206)
  add_compile_options(/wd4512)
  add_compile_options(/wd4701)
  add_compile_options(/wd4706)
  add_compile_options(/wd4800)

  # Treat some criticial warnings as error
  add_compile_options(/we4002)
  add_compile_options(/we4003)
  add_compile_options(/we4013)
  add_compile_options(/we4020)
  add_compile_options(/we4022)
  add_compile_options(/we4024)
  add_compile_options(/we4028)
  add_compile_options(/we4029)
  add_compile_options(/we4030)
  add_compile_options(/we4031)
  add_compile_options(/we4033)
  add_compile_options(/we4047)
  add_compile_options(/we4089)
  add_compile_options(/we4113)
  add_compile_options(/we4133)
  add_compile_options(/we4204)
  add_compile_options(/we4700)
  add_compile_options(/we4715)
  add_compile_options(/we4789)

  add_compile_definitions(
    "_CRT_SECURE_NO_DEPRECATE"
    "_CRT_NONSTDC_NO_DEPRECATE"
    "_CRT_SECURE_NO_WARNINGS"
  )
endif()

if (NOT EXISTS "${CMAKE_SOURCE_DIR}/build/cmake/targets.cmake")
  message(FATAL_ERROR
    "The 'build/cmake/targets.cmake' file does NOT exist. "
    "Use the following command to generate it:\n"
    "  python gen-make.py -t cmake"
  )
endif()

add_library(ra-libs INTERFACE)
add_library(fs-libs INTERFACE)

if(SVN_ENABLE_NLS)
  # Note: when installing these dependecies with vcpkg, you will need to
  # install 'gettext' package with 'tools' feature. Use the following command
  # for this: `./vcpkg install gettext[tools]`. This package contains both,
  # Gettext and Intl dependecies.
  find_package(Gettext REQUIRED)
  find_package(Intl REQUIRED)

  # If using CMake of version < 3.20, FindIntl would not define IMPORTED target.
  # https://cmake.org/cmake/help/latest/module/FindIntl.html
  if(NOT TARGET Intl::Intl)
    add_library(Intl::Intl INTERFACE IMPORTED)
    set_target_properties(Intl::Intl PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${Intl_INCLUDE_DIRS}"
      INTERFACE_LINK_LIBRARIES "${Intl_LIBRARIES}"
    )
  endif()

  add_library(external-intl ALIAS Intl::Intl)

  add_private_config_definition(
    "Define to 1 if translation of program messages to the user's native language is requested."
    "ENABLE_NLS" "1"
  )

  if (NOT WIN32)
    add_private_config_definition(
      "Defined to be the path to the installed locale dirs"
      "SVN_LOCALE_DIR" "\"${CMAKE_INSTALL_PREFIX}/share/locale\""
    )
  endif()

  add_custom_target(locale ALL)

  file(GLOB SVN_PO_FILES "subversion/po/*.po")

  foreach(po_file ${SVN_PO_FILES})
    get_filename_component(lang ${po_file} NAME_WLE)
    set(mo_file "${CMAKE_BINARY_DIR}/${lang}.mo")

    add_custom_command(
      DEPENDS
        "${po_file}"
      OUTPUT
        "${mo_file}"
      COMMAND
        "${GETTEXT_MSGFMT_EXECUTABLE}" -c -o ${mo_file} ${po_file}
    )

    target_sources(locale PRIVATE ${mo_file})

    install(
      FILES "${mo_file}"
      DESTINATION "share/locale/${lang}/LC_MESSAGES"
      RENAME "subversion.mo"
    )
  endforeach()
else()
  # Declare empty target for Intl if we don't use it.
  add_library(external-intl INTERFACE)
endif()

# Link all targets with Intl library. The 'external-intl' target is always,
# even if we don't use NLS functionality.
#
# Following the CMake documentation [1], the link_libraries affects only on
# the targets declared later, so it should be here.
#
# [1] https://cmake.org/cmake/help/latest/command/link_libraries.html
#     -- "Link libraries to all targets added later."
link_libraries(external-intl)

# Build shared libraries and theirs implibs with 'lib' prefix, for example
# libsvn_subr-1.[lib|a] and libsvn_subr-1.[dll|so]
set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
set(CMAKE_IMPORT_LIBRARY_PREFIX "lib")

# This tells CMake to build static libraries without any prefix
# (just svn_subr-1.[lib|a])
set(CMAKE_STATIC_LIBRARY_PREFIX "")

include("build/cmake/targets.cmake")

if(SVN_ENABLE_TESTS)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(run_tests_script "${CMAKE_CURRENT_SOURCE_DIR}/build/run_tests.py")
  set(list_tests_script "${CMAKE_CURRENT_SOURCE_DIR}/build/list_tests.py")
  set(test_base_dir "${CMAKE_CURRENT_BINARY_DIR}/Testing")

  # Create the virtual environment for Python tests.
  execute_process(
    COMMAND
      "${Python3_EXECUTABLE}" "${run_tests_script}"
      --create-python-venv "${test_base_dir}"
      ${CMAKE_CURRENT_SOURCE_DIR}
      OUTPUT_VARIABLE command_output
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      RESULT_VARIABLE command_result
  )
  if (command_result)
    message(FATAL_ERROR "run_tests.py --create-python-venv failed.")
  endif()
  string(STRIP "${command_output}" python3_test_executable)

  function(add_py_test name prog)
    if(SVN_TEST_CONFIGURE_FOR_PARALLEL)
      set(test_root "${test_base_dir}/${name}")
    else()
      set(test_root "${test_base_dir}")
    endif()

    file(MAKE_DIRECTORY "${test_root}/subversion/tests/cmdline")

    add_test(
      NAME
        "${name}"
      COMMAND
        "${python3_test_executable}" "${run_tests_script}"
        --bin ${binary_dir}
        --tools-bin ${binary_dir}
        --verbose
        --log-to-stdout
        --set-log-level=WARNING
        ${SVN_TEST_COMMAND_ARGUMENTS}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${test_root}
        "${prog}"
      WORKING_DIRECTORY
        ${test_root}
    )
  endfunction()

  file(GLOB PYTHON_TESTS
     "subversion/tests/cmdline/*_tests.py"
  )

  set(binary_dir $<TARGET_FILE_DIR:svn>)

  if(SVN_TEST_EXPAND)
    execute_process(
      COMMAND
        "${Python3_EXECUTABLE}" "${list_tests_script}"
        ${PYTHON_TESTS}
        OUTPUT_VARIABLE tests_list_output
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE command_result
    )

    if (command_result)
      message(FATAL_ERROR "list_tests.py failed.")
    endif()

    string(REGEX MATCHALL "[^\n\r]+" tests_list "${tests_list_output}")

    foreach(test_case ${tests_list})
      if(test_case MATCHES "(.+)#(.+)")
        set(py_test_abspath "${CMAKE_MATCH_1}")
        set(py_test_num "${CMAKE_MATCH_2}")

        get_filename_component(py_test_name ${py_test_abspath} NAME_WLE)
        file(RELATIVE_PATH py_test_relpath ${CMAKE_CURRENT_SOURCE_DIR} ${py_test_abspath})

        add_py_test(
          "cmdline.${py_test_name}.${py_test_num}"
          "${py_test_relpath}#${py_test_num}"
        )
      endif()
    endforeach()
  else()
    foreach(py_test_abspath ${PYTHON_TESTS})
      # Keep `.py'.
      get_filename_component(py_test_name ${py_test_abspath} NAME_WLE)
      file(RELATIVE_PATH py_test_relpath ${CMAKE_CURRENT_SOURCE_DIR} ${py_test_abspath})

      add_py_test(
        "cmdline.${py_test_name}"
        "${py_test_relpath}"
      )
    endforeach()
  endif()
endif()

if (SVN_ENABLE_SVNXX)
  target_include_directories(libsvnxx PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/subversion/bindings/cxx/include"
  )
endif()

if(SVN_USE_WIN32_CRASHHANDLER)
  target_compile_definitions(libsvn_subr PRIVATE
    "SVN_USE_WIN32_CRASHHANDLER"
    "SVN_WIN32_CRASHREPORT_EMAIL=\"users@subversion.apache.org\""
  )
endif()

if(SVN_ENABLE_SWIG_PYTHON)
  add_custom_command(TARGET libsvn_swig_py POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/subversion/bindings/swig/python/svn"
      "$<TARGET_FILE_DIR:libsvn_swig_py>/PythonPackages/svn"
  )

  add_custom_command(TARGET libsvn_swig_py POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
      "${CMAKE_CURRENT_SOURCE_DIR}/subversion/bindings/swig/python/__init__.py"
      "$<TARGET_FILE_DIR:libsvn_swig_py>/PythonPackages/libsvn/__init__.py"
  )
endif()

string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}-${CMAKE_HOST_SYSTEM_NAME}" SVN_BUILD_HOST)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}" SVN_BUILD_TARGET)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/subversion/svn_private_config.hc"
  "${CMAKE_CURRENT_BINARY_DIR}/svn_private_config.h"
)

message(STATUS "Configuration summary:")
message(STATUS "  Version ......................... : ${SVN_VERSION}")
message(STATUS "  Build type ...................... : ${CMAKE_BUILD_TYPE}")
message(STATUS "    Build shared libraries ........ : ${BUILD_SHARED_LIBS}")
message(STATUS "    Build shared FS Modues ........ : ${SVN_BUILD_SHARED_FS}")
message(STATUS "    Build shared RA Modues ........ : ${SVN_BUILD_SHARED_RA}")
message(STATUS "    Use pkg-config dependencies ... : ${SVN_USE_PKG_CONFIG}")
message(STATUS "    Checksum Backend .............. : ${SVN_CHECKSUM_BACKEND}")
message(STATUS "  FS modules:")
message(STATUS "    Enable FSFS ................... : ${SVN_ENABLE_FS_FS}")
message(STATUS "    Enable FSX .................... : ${SVN_ENABLE_FS_X}")
message(STATUS "  RA modules:")
message(STATUS "    Enable file:// ................ : ${SVN_ENABLE_RA_LOCAL}")
message(STATUS "    Enable svn:// ................. : ${SVN_ENABLE_RA_SVN}")
message(STATUS "    Enable http:// and https://.... : ${SVN_ENABLE_RA_SERF}")
message(STATUS "  Auth providers:")
message(STATUS "    Enable KWallet integration .... : ${SVN_ENABLE_AUTH_KWALLET}")
message(STATUS "    Enable Gnome Keyring .......... : ${SVN_ENABLE_AUTH_GNOME_KEYRING}")
message(STATUS "    Enable GPG Agent support ...... : ${SVN_ENABLE_AUTH_GPG_AGENT}")
message(STATUS "  Optional modules and targets:")
message(STATUS "    Build Apache Modules .......... : ${SVN_ENABLE_APACHE_MODULES}")
message(STATUS "    Build programs ................ : ${SVN_ENABLE_PROGRAMS}")
message(STATUS "    Build tools ................... : ${SVN_ENABLE_TOOLS}")
message(STATUS "    Build test suite .............. : ${SVN_ENABLE_TESTS}")
message(STATUS "  Install:")
message(STATUS "    Install prefix: ............... : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "    Install private headers: ...... : ${SVN_INSTALL_PRIVATE_H}")
message(STATUS "  Bindings:")
message(STATUS "    Build SVNXX ................... : ${SVN_ENABLE_SVNXX}")
message(STATUS "    Build SWIG_PERL ............... : ${SVN_ENABLE_SWIG_PERL}")
message(STATUS "    Build SWIG_PYTHON ............. : ${SVN_ENABLE_SWIG_PYTHON}")
message(STATUS "    Build SWIG_RUBY ............... : ${SVN_ENABLE_SWIG_RUBY}")
