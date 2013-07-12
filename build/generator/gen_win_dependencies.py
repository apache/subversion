#
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
#
#
# gen_win_dependencies.py 
#
#   base class for generating windows projects, containing the
#   dependency locator code shared between the test runner and
#   the make file generators
#

import os
try:
  # Python >=2.5
  from hashlib import md5 as hashlib_md5
except ImportError:
  # Python <2.5
  from md5 import md5 as hashlib_md5
import sys
import fnmatch
import re
import subprocess
import glob
import string
import generator.swig.header_wrappers
import generator.swig.checkout_swig_header
import generator.swig.external_runtime

if sys.version_info[0] >= 3:
  # Python >=3.0
  from io import StringIO
else:
  # Python <3.0
  try:
    from cStringIO import StringIO
  except ImportError:
    from StringIO import StringIO

import gen_base
import ezt

class SVNCommonLibrary:

  def __init__(self, name, include_dir, lib_dir, lib_name, version=None,
               debug_lib_dir=None, debug_lib_name=None, dll_dir=None,
               dll_name=None, debug_dll_dir=None, debug_dll_name=None):
    self.name = name
    self.include_dir = include_dir
    self.lib_dir = lib_dir
    self.lib_name = lib_name
    self.version = version
    self.dll_dir = dll_dir
    self.dll_name = dll_name

    if debug_lib_dir:
      self.debug_lib_dir = debug_lib_dir
    else:
      self.debug_lib_dir = lib_dir
      
    if debug_lib_name:
      self.debug_lib_name = debug_lib_name
    else:
      self.debug_lib_name = lib_name
      
    if debug_dll_dir:
      self.debug_dll_dir = debug_dll_dir
    else:
      self.debug_dll_dir = dll_dir
      
    if debug_dll_name:
      self.debug_dll_name = debug_dll_name
    else:
      self.debug_dll_name = dll_name

class GenDependenciesBase(gen_base.GeneratorBase):
  """This intermediate base class exists to be instantiated by win-tests.py,
  in order to obtain information from build.conf and library paths without
  actually doing any generation."""
  _extension_map = {
    ('exe', 'target'): '.exe',
    ('exe', 'object'): '.obj',
    ('lib', 'target'): '.dll',
    ('lib', 'object'): '.obj',
    ('pyd', 'target'): '.pyd',
    ('pyd', 'object'): '.obj',
    }

  _libraries = {}     # Dict of SVNCommonLibrary instances of found libraries

  def parse_options(self, options):
    self.apr_path = 'apr'
    self.apr_util_path = 'apr-util'
    self.apr_iconv_path = 'apr-iconv'
    self.serf_path = None
    self.serf_lib = None
    self.bdb_path = 'db4-win32'
    self.httpd_path = None
    self.libintl_path = None
    self.zlib_path = 'zlib'
    self.openssl_path = None
    self.jdk_path = None
    self.junit_path = None
    self.swig_path = None
    self.vs_version = '2002'
    self.sln_version = '7.00'
    self.vcproj_version = '7.00'
    self.vcproj_extension = '.vcproj'
    self.sqlite_path = 'sqlite-amalgamation'
    self.skip_sections = { 'mod_dav_svn': None,
                           'mod_authz_svn': None,
                           'mod_dontdothat' : None,
                           'libsvn_auth_kwallet': None,
                           'libsvn_auth_gnome_keyring': None }

    # Instrumentation options
    self.disable_shared = None
    self.static_apr = None
    self.static_openssl = None
    self.instrument_apr_pools = None
    self.instrument_purify_quantify = None
    self.configure_apr_util = None
    self.sasl_path = None

    # NLS options
    self.enable_nls = None

    # ML (assembler) is disabled by default; use --enable-ml to detect
    self.enable_ml = None

    for opt, val in options:
      if opt == '--with-berkeley-db':
        self.bdb_path = val
      elif opt == '--with-apr':
        self.apr_path = val
      elif opt == '--with-apr-util':
        self.apr_util_path = val
      elif opt == '--with-apr-iconv':
        self.apr_iconv_path = val
      elif opt == '--with-serf':
        self.serf_path = val
      elif opt == '--with-httpd':
        self.httpd_path = val
        del self.skip_sections['mod_dav_svn']
        del self.skip_sections['mod_authz_svn']
        del self.skip_sections['mod_dontdothat']
      elif opt == '--with-libintl':
        self.libintl_path = val
        self.enable_nls = 1
      elif opt == '--with-jdk':
        self.jdk_path = val
      elif opt == '--with-junit':
        self.junit_path = val
      elif opt == '--with-zlib':
        self.zlib_path = val
      elif opt == '--with-swig':
        self.swig_path = val
      elif opt == '--with-sqlite':
        self.sqlite_path = val
      elif opt == '--with-sasl':
        self.sasl_path = val
      elif opt == '--with-openssl':
        self.openssl_path = val
      elif opt == '--enable-purify':
        self.instrument_purify_quantify = 1
        self.instrument_apr_pools = 1
      elif opt == '--enable-quantify':
        self.instrument_purify_quantify = 1
      elif opt == '--enable-pool-debug':
        self.instrument_apr_pools = 1
      elif opt == '--enable-nls':
        self.enable_nls = 1
      elif opt == '--enable-bdb-in-apr-util':
        self.configure_apr_util = 1
      elif opt == '--enable-ml':
        self.enable_ml = 1
      elif opt == '--disable-shared':
        self.disable_shared = 1
      elif opt == '--with-static-apr':
        self.static_apr = 1
      elif opt == '--with-static-openssl':
        self.static_openssl = 1
      elif opt == '--vsnet-version':
        if val == '2002' or re.match('7(\.\d+)?$', val):
          self.vs_version = '2002'
          self.sln_version = '7.00'
          self.vcproj_version = '7.00'
          self.vcproj_extension = '.vcproj'
        elif val == '2003' or re.match('8(\.\d+)?$', val):
          self.vs_version = '2003'
          self.sln_version = '8.00'
          self.vcproj_version = '7.10'
          self.vcproj_extension = '.vcproj'
        elif val == '2005' or re.match('9(\.\d+)?$', val):
          self.vs_version = '2005'
          self.sln_version = '9.00'
          self.vcproj_version = '8.00'
          self.vcproj_extension = '.vcproj'
        elif val == '2008' or re.match('10(\.\d+)?$', val):
          self.vs_version = '2008'
          self.sln_version = '10.00'
          self.vcproj_version = '9.00'
          self.vcproj_extension = '.vcproj'
        elif val == '2010':
          self.vs_version = '2010'
          self.sln_version = '11.00'
          self.vcproj_version = '10.0'
          self.vcproj_extension = '.vcxproj'
        elif val == '2012' or val == '11':
          self.vs_version = '2012'
          self.sln_version = '12.00'
          self.vcproj_version = '11.0'
          self.vcproj_extension = '.vcxproj'
        elif re.match('^1\d+$', val):
          self.vsversion = val
          self.sln_version = '12.00'
          self.vcproj_version = val + '.0'
          self.vcproj_extension = '.vcxproj'
        else:
          print('WARNING: Unknown VS.NET version "%s",'
                 ' assuming "%s"\n' % (val, '7.00'))


  def __init__(self, fname, verfname, options, find_libs=True):

    # parse (and save) the options that were passed to us
    self.parse_options(options)

    # Initialize parent
    gen_base.GeneratorBase.__init__(self, fname, verfname, options)

    # These files will be excluded from the build when they're not
    # explicitly listed as project sources.
    self._excluded_from_build = frozenset(self.private_includes
                                          + self.private_built_includes)

    if find_libs:
      self.find_libraries(False)
      
  def find_libraries(self, show_warnings):
  
    self._find_apr()
    self._find_apr_util_and_expat()
    # Find Berkeley DB
    self._find_bdb(show_warnings)
    
    if show_warnings:
      # Find the right Ruby include and libraries dirs and
      # library name to link SWIG bindings with
      self._find_ruby()

      # Find the right Perl library name to link SWIG bindings with
      self._find_perl()

      # Find the right Python include and libraries dirs for SWIG bindings
      self._find_python()

      # Find the installed SWIG version to adjust swig options
      self._find_swig()

      # Find the installed Java Development Kit
      self._find_jdk()

      # Find Sqlite
      self._find_sqlite()

      # Look for ZLib and ML
      if self.zlib_path:
        self._find_zlib()
        self._find_ml()

      # Find serf and its dependencies
      if self.serf_path:
        self._find_serf()
    
    if show_warnings:
      for lib in self._libraries.values():
        print('Found %s %s' % (lib.name, lib.version))
    
  def _find_apr(self):
    "Find the APR library and version"

    minimal_apr_version = (0, 9, 0)
    
    if not self.apr_path:
      sys.stderr.write("ERROR: Use '--with-apr' option to configure APR " + \
                       "location.\n")
      sys.exit(1)                       

    inc_path = os.path.join(self.apr_path, 'include')
    version_file_path = os.path.join(inc_path, 'apr_version.h')

    if not os.path.exists(version_file_path):
      sys.stderr.write("ERROR: '%s' not found.\n" % version_file_path)
      sys.stderr.write("Use '--with-apr' option to configure APR location.\n")
      sys.exit(1)

    txt = open(version_file_path).read()

    vermatch = re.search(r'^\s*#define\s+APR_MAJOR_VERSION\s+(\d+)', txt, re.M)
    major = int(vermatch.group(1))

    vermatch = re.search(r'^\s*#define\s+APR_MINOR_VERSION\s+(\d+)', txt, re.M)
    minor = int(vermatch.group(1))

    vermatch = re.search(r'^\s*#define\s+APR_PATCH_VERSION\s+(\d+)', txt, re.M)
    patch = int(vermatch.group(1))

    version = (major, minor, patch)
    self.apr_version = apr_version = '%d.%d.%d' % version

    if version < minimal_apr_version:
      sys.stderr.write("ERROR: apr %s or higher is required "
                       "(%s found)\n" % (
                          '.'.join(str(v) for v in minimal_apr_version),
                          self.apr_version))
      sys.exit(1)

    suffix = ''
    if major > 0:
        suffix = '-%d' % major

    if self.static_apr:
      lib_name = 'apr%s.lib' % suffix
      lib_dir = os.path.join(self.apr_path, 'LibR')
      dll_dir = None
      debug_dll_dir = None
      
      if not os.path.isdir(lib_dir) and \
         os.path.isfile(os.path.join(self.apr_path, 'lib', lib_name)):
        # Installed APR instead of APR-Source
        lib_dir = os.path.join(self.apr_path, 'lib')
        debug_lib_dir = None
      else:
        debug_lib_dir = os.path.join(self.apr_path, 'LibD')
    else:
      lib_name = 'libapr%s.lib' % suffix
      lib_dir = os.path.join(self.apr_path, 'Release')
      
      if not os.path.isdir(lib_dir) and \
         os.path.isfile(os.path.join(self.apr_path, 'lib', lib_name)):
        # Installed APR instead of APR-Source
        lib_dir = os.path.join(self.apr_path, 'lib')
        debug_lib_dir = lib_dir
      else:
        debug_lib_dir = os.path.join(self.apr_path, 'Debug')
        
      dll_name = 'libapr%s.dll' % suffix
      if os.path.isfile(os.path.join(lib_dir, dll_name)):
        dll_dir = lib_dir
        debug_dll_dir = debug_lib_dir
      else:
        dll_dir = lib_dir
        debug_dll_dir = debug_lib_dir
      
    self._libraries['apr'] = SVNCommonLibrary('apr', inc_path, lib_dir, lib_name,
                                              apr_version,
                                              debug_lib_dir=debug_lib_dir,
                                              dll_dir=dll_dir,
                                              dll_name=dll_name,
                                              debug_dll_dir=debug_dll_dir)

  def _find_apr_util_and_expat(self):
    "Find the APR-util library and version"

    minimal_aprutil_version = (0, 9, 0)
    
    inc_path = os.path.join(self.apr_util_path, 'include')
    version_file_path = os.path.join(inc_path, 'apu_version.h')

    if not os.path.exists(version_file_path):
      sys.stderr.write("ERROR: '%s' not found.\n" % version_file_path);
      sys.stderr.write("Use '--with-apr-util' option to configure APR-Util location.\n");
      sys.exit(1)

    txt = open(version_file_path).read()

    vermatch = re.search(r'^\s*#define\s+APU_MAJOR_VERSION\s+(\d+)', txt, re.M)
    major = int(vermatch.group(1))

    vermatch = re.search(r'^\s*#define\s+APU_MINOR_VERSION\s+(\d+)', txt, re.M)
    minor = int(vermatch.group(1))

    vermatch = re.search(r'^\s*#define\s+APU_PATCH_VERSION\s+(\d+)', txt, re.M)
    patch = int(vermatch.group(1))

    version = (major, minor, patch)
    self.aprutil_version = aprutil_version = '%d.%d.%d' % version

    suffix = ''
    if major > 0:
        suffix = '-%d' % major

    if self.static_apr:
      lib_name = 'aprutil%s.lib' % suffix
      lib_dir = os.path.join(self.aprutil_path, 'LibR')
      dll_dir = None
      debug_dll_dir = None
      
      if not os.path.isdir(lib_dir) and \
         os.path.isfile(os.path.join(self.apr_util_path, 'lib', lib_name)):
        # Installed APR-Util instead of APR-Util-Source
        lib_dir = os.path.join(self.apr_util_path, 'lib')
        debug_lib_dir = None
      else:
        debug_lib_dir = os.path.join(self.apr_util_path, 'LibD')
    else:
      lib_name = 'libaprutil%s.lib' % suffix
      lib_dir = os.path.join(self.apr_util_path, 'Release')
      
      if not os.path.isdir(lib_dir) and \
         os.path.isfile(os.path.join(self.apr_util_path, 'lib', lib_name)):
        # Installed APR-Util instead of APR-Util-Source
        lib_dir = os.path.join(apr_util_path, 'lib')
        debug_lib_dir = lib_dir
      else:
        debug_lib_dir = os.path.join(self.apr_util_path, 'Debug')
        
      dll_name = 'libaprutil%s.dll' % suffix
      if os.path.isfile(os.path.join(lib_dir, dll_name)):
        dll_dir = lib_dir
        debug_dll_dir = debug_lib_dir
      else:
        dll_dir = lib_dir
        debug_dll_dir = debug_lib_dir
      
    self._libraries['aprutil'] = SVNCommonLibrary('apr-util', inc_path, lib_dir,
                                                   lib_name,
                                                   aprutil_version,
                                                   debug_lib_dir=debug_lib_dir,
                                                   dll_dir=dll_dir,
                                                   dll_name=dll_name,
                                                   debug_dll_dir=debug_dll_dir)

    # And now find expat
    # If we have apr-util as a source location, it is in a subdir.
    # If we have an install package it is in the lib subdir
    if os.path.exists(os.path.join(self.apr_util_path, 'xml/expat')):
      inc_path = os.path.join(self.apr_util_path, 'xml/expat/lib')
      lib_dir = os.path.join(self.apr_util_path, 'xml/expat/lib/LibR')
      debug_lib_dir = os.path.join(self.apr_util_path, 'xml/expat/lib/LibD')
    else:
      inc_path = os.path.join(self.apr_util_path, 'include')
      lib_dir = os.path.join(self.apr_util_path, 'lib')
      debug_lib_dir = None
      
    version_file_path = os.path.join(inc_path, 'expat.h')

    if not os.path.exists(version_file_path):
      sys.stderr.write("ERROR: '%s' not found.\n" % version_file_path);
      sys.stderr.write("Use '--with-apr-util' option to configure APR-Util's XML location.\n");
      sys.exit(1)
      
    txt = open(version_file_path).read()

    vermatch = re.search(r'^\s*#define\s+XML_MAJOR_VERSION\s+(\d+)', txt, re.M)
    major = int(vermatch.group(1))

    vermatch = re.search(r'^\s*#define\s+XML_MINOR_VERSION\s+(\d+)', txt, re.M)
    minor = int(vermatch.group(1))

    vermatch = re.search(r'^\s*#define\s+XML_MICRO_VERSION\s+(\d+)', txt, re.M)
    patch = int(vermatch.group(1))

    version = (major, minor, patch)
    xml_version = '%d.%d.%d' % version

    self._libraries['xml'] = SVNCommonLibrary('expat', inc_path, lib_dir,
                                               'xml.lib', xml_version,
                                               debug_lib_dir = debug_lib_dir)

  def _find_bdb(self, show_warnings):
    "Find the Berkeley DB library and version"

    # Default to not found
    self.bdb_lib = None

    inc_path = os.path.join(self.bdb_path, 'include')
    db_h_path = os.path.join(inc_path, 'db.h')

    if not self.bdb_path or not os.path.isfile(db_h_path):
      if show_warnings and self.bdb_path:
        print('WARNING: \'%s\' not found' % (db_h_path,))
        print("Use '--with-berkeley-db' to configure BDB location.\n");
      return

    # Obtain bdb version from db.h
    txt = open(db_h_path).read()

    maj_match = re.search(r'DB_VERSION_MAJOR\s+(\d+)', txt)
    min_match = re.search(r'DB_VERSION_MINOR\s+(\d+)', txt)
    patch_match = re.search(r'DB_VERSION_PATCH\s+(\d+)', txt)

    if maj_match and min_match and patch_match:
      ver = (int(maj_match.group(1)),
             int(min_match.group(1)),
             int(patch_match.group(1)))
    else:
      return

    version = '%d.%d.%d' % ver
    versuffix = '%d%d' % (ver[0], ver[1])

    # Before adding "60" to this list, see build/ac-macros/berkeley-db.m4.
    if versuffix not in (
            '50', '51', '52', '53',
            '40', '41', '42', '43', '44', '45', '46', '47', '48',
       ):
      return

    lib_dir = os.path.join(self.bdb_path, 'lib')
    lib_name = 'libdb%s.lib' % (versuffix,)

    if not os.path.exists(os.path.join(lib_dir, lib_name)):
      return

    # Do we have a debug version?
    debug_lib_name = 'libdb%sd.lib' % (versuffix,)
    if not os.path.isfile(os.path.join(lib_dir, debug_lib_name)):
      debug_lib_name = None

    dll_dir = os.path.join(self.bdb_path, 'bin')

    # Are there binaries we should copy for testing?
    dll_name = os.path.splitext(lib_name)[0] + '.dll'
    if not os.path.isfile(os.path.join(dll_dir, dll_name)):
      dll_name = None

    if debug_lib_name:
      debug_dll_name = os.path.splitext(debug_lib_name)[0] + '.dll'
      if not os.path.isfile(os.path.join(dll_dir, debug_dll_name)):
        debug_dll_name = None
    else:
      debug_dll_name = None

    self._libraries['db'] = SVNCommonLibrary('db', inc_path, lib_dir, lib_name,
                                              version,
                                              debug_lib_name=debug_lib_name,
                                              dll_dir=dll_dir,
                                              dll_name=dll_name,
                                              debug_dll_name=debug_dll_name)

    # For compatibility with old code
    self.bdb_lib = self._libraries['db'].lib_name

  def _find_perl(self):
    "Find the right perl library name to link swig bindings with"
    self.perl_includes = []
    self.perl_libdir = None
    fp = os.popen('perl -MConfig -e ' + escape_shell_arg(
                  'print "$Config{PERL_REVISION}$Config{PERL_VERSION}"'), 'r')
    try:
      line = fp.readline()
      if line:
        msg = 'Found installed perl version number.'
        self.perl_lib = 'perl' + line.rstrip() + '.lib'
      else:
        msg = 'Could not detect perl version.'
        self.perl_lib = 'perl56.lib'
      print('%s\n  Perl bindings will be linked with %s\n'
             % (msg, self.perl_lib))
    finally:
      fp.close()

    fp = os.popen('perl -MConfig -e ' + escape_shell_arg(
                  'print $Config{archlib}'), 'r')
    try:
      line = fp.readline()
      if line:
        self.perl_libdir = os.path.join(line, 'CORE')
        self.perl_includes = [os.path.join(line, 'CORE')]
    finally:
      fp.close()

  def _find_ruby(self):
    "Find the right Ruby library name to link swig bindings with"
    self.ruby_includes = []
    self.ruby_libdir = None
    self.ruby_version = None
    self.ruby_major_version = None
    self.ruby_minor_version = None
    # Pass -W0 to stifle the "-e:1: Use RbConfig instead of obsolete
    # and deprecated Config." warning if we are using Ruby 1.9.
    proc = os.popen('ruby -rrbconfig -W0 -e ' + escape_shell_arg(
                    "puts Config::CONFIG['ruby_version'];"
                    "puts Config::CONFIG['LIBRUBY'];"
                    "puts Config::CONFIG['archdir'];"
                    "puts Config::CONFIG['libdir'];"), 'r')
    try:
      rubyver = proc.readline()[:-1]
      if rubyver:
        self.ruby_version = rubyver
        self.ruby_major_version = string.atoi(self.ruby_version[0])
        self.ruby_minor_version = string.atoi(self.ruby_version[2])
        libruby = proc.readline()[:-1]
        if libruby:
          msg = 'Found installed ruby %s' % rubyver
          self.ruby_lib = libruby
          self.ruby_includes.append(proc.readline()[:-1])
          self.ruby_libdir = proc.readline()[:-1]
      else:
        msg = 'Could not detect Ruby version, assuming 1.8.'
        self.ruby_version = "1.8"
        self.ruby_major_version = 1
        self.ruby_minor_version = 8
        self.ruby_lib = 'msvcrt-ruby18.lib'
      print('%s\n  Ruby bindings will be linked with %s\n'
             % (msg, self.ruby_lib))
    finally:
      proc.close()

  def _find_python(self):
    "Find the appropriate options for creating SWIG-based Python modules"
    self.python_includes = []
    self.python_libdir = ""
    try:
      from distutils import sysconfig
      inc = sysconfig.get_python_inc()
      plat = sysconfig.get_python_inc(plat_specific=1)
      self.python_includes.append(inc)
      if inc != plat:
        self.python_includes.append(plat)
      self.python_libdir = self.apath(sysconfig.PREFIX, "libs")
    except ImportError:
      pass

  def _find_jdk(self):
    if not self.jdk_path:
      jdk_ver = None
      try:
        try:
          # Python >=3.0
          import winreg
        except ImportError:
          # Python <3.0
          import _winreg as winreg
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                           r"SOFTWARE\JavaSoft\Java Development Kit")
        # Find the newest JDK version.
        num_values = winreg.QueryInfoKey(key)[1]
        for i in range(num_values):
          (name, value, key_type) = winreg.EnumValue(key, i)
          if name == "CurrentVersion":
            jdk_ver = value
            break

        # Find the JDK path.
        if jdk_ver is not None:
          key = winreg.OpenKey(key, jdk_ver)
          num_values = winreg.QueryInfoKey(key)[1]
          for i in range(num_values):
            (name, value, key_type) = winreg.EnumValue(key, i)
            if name == "JavaHome":
              self.jdk_path = value
              break
        winreg.CloseKey(key)
      except (ImportError, EnvironmentError):
        pass
      if self.jdk_path:
        print("Found JDK version %s in %s\n" % (jdk_ver, self.jdk_path))
    else:
      print("Using JDK in %s\n" % (self.jdk_path))

  def _find_swig(self):
    # Require 1.3.24. If not found, assume 1.3.25.
    default_version = '1.3.25'
    minimum_version = '1.3.24'
    vernum = 103025
    minimum_vernum = 103024
    libdir = ''

    if self.swig_path is not None:
      self.swig_exe = os.path.abspath(os.path.join(self.swig_path, 'swig'))
    else:
      self.swig_exe = 'swig'

    try:
      outfp = subprocess.Popen([self.swig_exe, '-version'], stdout=subprocess.PIPE, universal_newlines=True).stdout
      txt = outfp.read()
      if txt:
        vermatch = re.compile(r'^SWIG\ Version\ (\d+)\.(\d+)\.(\d+)$', re.M) \
                   .search(txt)
      else:
        vermatch = None

      if vermatch:
        version = tuple(map(int, vermatch.groups()))
        # build/ac-macros/swig.m4 explains the next incantation
        vernum = int('%d%02d%03d' % version)
        print('Found installed SWIG version %d.%d.%d\n' % version)
        if vernum < minimum_vernum:
          print('WARNING: Subversion requires version %s\n'
                 % minimum_version)

        libdir = self._find_swig_libdir()
      else:
        print('Could not find installed SWIG,'
               ' assuming version %s\n' % default_version)
        self.swig_libdir = ''
      outfp.close()
    except OSError:
      print('Could not find installed SWIG,'
             ' assuming version %s\n' % default_version)
      self.swig_libdir = ''

    self.swig_vernum = vernum
    self.swig_libdir = libdir

  def _find_swig_libdir(self):
    fp = os.popen(self.swig_exe + ' -swiglib', 'r')
    try:
      libdir = fp.readline().rstrip()
      if libdir:
        print('Using SWIG library directory %s\n' % libdir)
        return libdir
      else:
        print('WARNING: could not find SWIG library directory\n')
    finally:
      fp.close()
    return ''

  def _find_ml(self):
    "Check if the ML assembler is in the path"
    if not self.enable_ml:
      self.have_ml = 0
      return
    fp = os.popen('ml /help', 'r')
    try:
      line = fp.readline()
      if line:
        msg = 'Found ML, ZLib build will use ASM sources'
        self.have_ml = 1
      else:
        msg = 'Could not find ML, ZLib build will not use ASM sources'
        self.have_ml = 0
      print('%s\n' % (msg,))
    finally:
      fp.close()

  def _get_serf_version(self):
    "Retrieves the serf version from serf.h"

    # shouldn't be called unless serf is there
    assert self.serf_path and os.path.exists(self.serf_path)

    self.serf_ver_maj = None
    self.serf_ver_min = None
    self.serf_ver_patch = None

    # serf.h should be present
    if not os.path.exists(os.path.join(self.serf_path, 'serf.h')):
      return None, None, None

    txt = open(os.path.join(self.serf_path, 'serf.h')).read()

    maj_match = re.search(r'SERF_MAJOR_VERSION\s+(\d+)', txt)
    min_match = re.search(r'SERF_MINOR_VERSION\s+(\d+)', txt)
    patch_match = re.search(r'SERF_PATCH_VERSION\s+(\d+)', txt)
    if maj_match:
      self.serf_ver_maj = int(maj_match.group(1))
    if min_match:
      self.serf_ver_min = int(min_match.group(1))
    if patch_match:
      self.serf_ver_patch = int(patch_match.group(1))

    return self.serf_ver_maj, self.serf_ver_min, self.serf_ver_patch

  def _find_serf(self):
    "Check if serf and its dependencies are available"

    minimal_serf_version = (1, 2, 1)
    self.serf_lib = None
    if self.serf_path and os.path.exists(self.serf_path):
      if self.openssl_path and os.path.exists(self.openssl_path):
        self.serf_lib = 'serf'
        version = self._get_serf_version()
        if None in version:
          msg = 'Unknown serf version found; but, will try to build ' \
                'ra_serf.'
        else:
          self.serf_ver = '.'.join(str(v) for v in version)
          if version < minimal_serf_version:
            self.serf_lib = None
            msg = 'Found serf %s, but >= %s is required. ra_serf will not be built.\n' % \
                  (self.serf_ver, '.'.join(str(v) for v in minimal_serf_version))
          else:
            msg = 'Found serf %s' % self.serf_ver
        print(msg)
      else:
        print('openssl not found, ra_serf will not be built\n')
    else:
      print('serf not found, ra_serf will not be built\n')

  def _find_sqlite(self):
    "Find the Sqlite library and version"

    minimal_sqlite_version = (3, 7, 12)

    header_file = os.path.join(self.sqlite_path, 'inc', 'sqlite3.h')

    # First check for compiled version of SQLite.
    if os.path.exists(header_file):
      # Compiled SQLite seems found, check for sqlite3.lib file.
      lib_file = os.path.join(self.sqlite_path, 'lib', 'sqlite3.lib')
      if not os.path.exists(lib_file):
        sys.stderr.write("ERROR: '%s' not found.\n" % lib_file)
        sys.stderr.write("Use '--with-sqlite' option to configure sqlite location.\n");
        sys.exit(1)
      self.sqlite_inline = False
    else:
      # Compiled SQLite not found. Try amalgamation version.
      amalg_file = os.path.join(self.sqlite_path, 'sqlite3.c')
      if not os.path.exists(amalg_file):
        sys.stderr.write("ERROR: SQLite not found in '%s' directory.\n" % self.sqlite_path)
        sys.stderr.write("Use '--with-sqlite' option to configure sqlite location.\n");
        sys.exit(1)
      header_file = os.path.join(self.sqlite_path, 'sqlite3.h')
      self.sqlite_inline = True

    fp = open(header_file)
    txt = fp.read()
    fp.close()
    vermatch = re.search(r'^\s*#define\s+SQLITE_VERSION\s+"(\d+)\.(\d+)\.(\d+)(?:\.(\d))?"', txt, re.M)

    version = vermatch.groups()

    # Sqlite doesn't add patch numbers for their ordinary releases
    if not version[3]:
      version = version[0:3]

    version = tuple(map(int, version))

    self.sqlite_version = '.'.join(str(v) for v in version)

    if version < minimal_sqlite_version:
      sys.stderr.write("ERROR: sqlite %s or higher is required "
                       "(%s found)\n" % (
                          '.'.join(str(v) for v in minimal_sqlite_version),
                          self.sqlite_version))
      sys.exit(1)
    else:
      print('Found SQLite %s' % self.sqlite_version)

  def _find_zlib(self):
    "Find the ZLib library and version"

    if not self.zlib_path:
      self.zlib_version = '1'
      return

    header_file = os.path.join(self.zlib_path, 'zlib.h')

    if not os.path.exists(header_file):
      self.zlib_version = '1'
      return

    fp = open(header_file)
    txt = fp.read()
    fp.close()
    vermatch = re.search(r'^\s*#define\s+ZLIB_VERSION\s+"(\d+)\.(\d+)\.(\d+)(?:\.\d)?"', txt, re.M)

    version = tuple(map(int, vermatch.groups()))

    self.zlib_version = '%d.%d.%d' % version

    print('Found ZLib %s' % self.zlib_version)

# ============================================================================
# This is a cut-down and modified version of code from:
#   subversion/subversion/bindings/swig/python/svn/core.py
#
if sys.platform == "win32":
  _escape_shell_arg_re = re.compile(r'(\\+)(\"|$)')

  def escape_shell_arg(arg):
    # The (very strange) parsing rules used by the C runtime library are
    # described at:
    # http://msdn.microsoft.com/library/en-us/vclang/html/_pluslang_Parsing_C.2b2b_.Command.2d.Line_Arguments.asp

    # double up slashes, but only if they are followed by a quote character
    arg = re.sub(_escape_shell_arg_re, r'\1\1\2', arg)

    # surround by quotes and escape quotes inside
    arg = '"' + arg.replace('"', '"^""') + '"'
    return arg

else:
  def escape_shell_arg(str):
    return "'" + str.replace("'", "'\\''") + "'"
