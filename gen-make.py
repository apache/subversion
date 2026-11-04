#!/usr/bin/env python
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
# gen-make.py -- generate makefiles for building Subversion
#


import os
import traceback
import sys

import getopt
try:
  my_getopt = getopt.gnu_getopt
except AttributeError:
  my_getopt = getopt.getopt
try:
  # Python >=3.0
  import configparser
except ImportError:
  # Python <3.0
  import ConfigParser as configparser

# for the generator modules
sys.path.insert(0, os.path.join('build', 'generator'))

# for getversion
sys.path.insert(1, 'build')

gen_modules = {
  'cmake' : ('gen_cmake', 'CMake build system'),
  'make' : ('gen_make', 'Makefiles for POSIX systems'),
  }

def main(fname, gentype, verfname=None,
         skip_depends=0, other_options=None):
  if verfname is None:
    verfname = os.path.join('subversion', 'include', 'svn_version.h')

  gen_module = __import__(gen_modules[gentype][0])

  generator = gen_module.Generator(fname, verfname, other_options)

  if not skip_depends:
    generator.compute_hdr_deps()

  generator.write()
  generator.write_sqlite_headers()
  generator.write_errno_table()
  generator.write_config_keys()

  if ('--debug', '') in other_options:
    for dep_type, target_dict in generator.graph.deps.items():
      sorted_targets = sorted(target_dict.keys(), key=str)
      for target in sorted_targets:
        print(dep_type + ": " + _objinfo(target))
        for source in target_dict[target]:
          print("  " + _objinfo(source))
      print("=" * 72)
    gen_keys = sorted(generator.__dict__.keys())
    for name in gen_keys:
      value = generator.__dict__[name]
      if isinstance(value, list):
        print(name + ": ")
        for i in value:
          print("  " + _objinfo(i))
        print("=" * 72)


def _objinfo(o):
  if isinstance(o, str):
    return repr(o)
  else:
    t = o.__class__.__name__
    n = getattr(o, 'name', '-')
    f = getattr(o, 'filename', '-')
    return "%s: %s %s" % (t,n,f)


def _usage_exit(err=None):
  "print ERR (if any), print usage, then exit the script"
  if err:
    print("ERROR: %s\n" % (err))
  print("USAGE:  gen-make.py [options...] [conf-file]")
  print("  -s        skip dependency generation")
  print("  --debug   print lots of stuff only developers care about")
  print("  --release release mode")
  print("  --reload  reuse all options from the previous invocation")
  print("            of the script, except -s, -t, --debug and --reload")
  print("  -t TYPE   use the TYPE generator; can be one of:")
  items = sorted(gen_modules.items())
  for name, (module, desc) in items:
    print('            %-12s  %s' % (name, desc))
  print("")
  print("            The default generator type is 'make'")
  print("")
  print("  Makefile-specific options:")
  print("")
  print("  --assume-shared-libs")
  print("           omit dependencies on libraries, on the assumption that")
  print("           shared libraries will be built, so that it is unnecessary")
  print("           to relink executables when the libraries that they depend")
  print("           on change.  This is an option for developers who want to")
  print("           increase the speed of frequent rebuilds.")
  print("           *** Do not use unless you understand the consequences. ***")
  print("")
  print("  UNIX-specific options:")
  print("")
  print("  --installed-libs")
  print("           Comma-separated list of Subversion libraries to find")
  print("           pre-installed instead of building (probably only")
  print("           useful for packagers)")
  sys.exit(1)


class Options:
  def __init__(self):
    self.list = []
    self.dict = {}

  def add(self, opt, val, overwrite=True):
    if opt in self.dict:
      if overwrite:
        self.list[self.dict[opt]] = (opt, val)
    else:
      self.dict[opt] = len(self.list)
      self.list.append((opt, val))

if __name__ == '__main__':
  try:
    opts, args = my_getopt(sys.argv[1:], 'st:D:',
                           ['debug',
                            'release',
                            'reload',
                            'assume-shared-libs',
                            'installed-libs=',
                            ])
    if len(args) > 1:
      _usage_exit("Too many arguments")
  except getopt.GetoptError:
    typ, val, tb = sys.exc_info()
    msg = ''.join(traceback.format_exception_only(typ, val))
    _usage_exit(msg)

  conf = 'build.conf'
  skip = 0
  gentype = 'make'
  rest = Options()

  if args:
    conf = args[0]

  # First merge options with previously saved to gen-make.opts if --reload
  # options used
  for opt, val in opts:
    if opt == '--reload':
      prev_conf = configparser.ConfigParser()
      prev_conf.read('gen-make.opts')
      for opt, val in prev_conf.items('options'):
        if opt != '--debug':
          rest.add(opt, val)
      del prev_conf
    elif opt == '--with-neon' or opt == '--without-neon':
      # Provide a warning that we ignored these arguments
      print("Ignoring no longer supported argument '%s'" % opt)
    else:
      rest.add(opt, val)

  # Parse options list
  for opt, val in rest.list:
    if opt == '-s':
      skip = 1
    elif opt == '-t':
      gentype = val

  # Remember all options so that --reload and other scripts can use them
  opt_conf = open('gen-make.opts', 'w')
  opt_conf.write('[options]\n')
  for opt, val in rest.list:
    opt_conf.write(opt + ' = ' + val + '\n')
  opt_conf.close()

  if gentype not in gen_modules.keys():
    _usage_exit("Unknown module type '%s'" % (gentype))

  main(conf, gentype, skip_depends=skip, other_options=rest.list)


### End of file.
