#!/usr/bin/env python
#
# gen-make.py -- generate makefiles for building Subversion
#


import os
import sys
import getopt


# for the generator modules
sys.path.insert(0, os.path.join('build', 'generator'))

# for getversion
sys.path.insert(1, 'build')


gen_modules = {
  'make' : ('gen_make', 'Makefiles for POSIX systems'),
  'dsp' : ('gen_msvc_dsp', 'MSVC 6.x project files'),
  'vcproj' : ('gen_vcnet_vcproj', 'VC.Net project files'),
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


def _usage_exit():
  "print usage, exit the script"
  print "USAGE:  gen-make.py [options...] [conf-file]"
  print "  -s       skip dependency generation"
  print "  -t TYPE  use the TYPE generator; can be one of:"
  items = gen_modules.items()
  items.sort()
  for name, (module, desc) in items:
    print '           %-12s  %s' % (name, desc)
  print
  print "           The default generator type is 'make'"
  print
  print "  Windows-specific options:"
  print
  print "  --with-apr=DIR"
  print "           the APR sources are in DIR"
  print
  print "  --with-apr-util=DIR"
  print "           the APR-Util sources are in DIR"
  print
  print "  --with-apr-iconv=DIR"
  print "           the APR-Iconv sources are in DIR"
  print
  print "  --with-berkeley-db=DIR"
  print "           look for Berkley DB headers and libs in"
  print "           DIR"
  print
  print "  --with-neon=DIR"
  print "           the Neon sources are in DIR"
  print
  print "  --with-httpd=DIR"
  print "           the httpd sources and binaries required"
  print "           for building mod_dav_svn are in DIR;"
  print "           implies --with-apr{-util, -iconv}, but"
  print "           you can override them"
  print
  print "  --with-libintl=DIR"
  print "           look for GNU libintl headers and libs in DIR;"
  print "           implies --enable-nls"
  print
  print "  --with-openssl=DIR"
  print "           tell neon to look for OpenSSL headers"
  print "           and libs in DIR"
  print
  print "  --with-zlib=DIR"
  print "           tell neon to look for ZLib headers and"
  print "           libs in DIR"
  print
  print "  --with-junit=PATH"
  print "           look for the junit jar here"
  print "           junit is for testing the java bindings"
  print
  print "  --enable-pool-debug"
  print "           turn on APR pool debugging"
  print
  print "  --enable-purify"
  print "           add support for Purify instrumentation;"
  print "           implies --enable-pool-debug"
  print
  print "  --enable-quantify"
  print "           add support for Quantify instrumentation"
  print
  print "  --enable-nls"
  print "           add support for gettext localization"
  print
  print "  --enable-bdb-in-apr-util"
  print "           configure APR-Util to use Berkeley DB"
  print
  print "  --vsnet-version=VER"
  print "           generate for VS.NET version VER (2002, 2003, or 2005)"
  print "           [only valid in combination with '-t vcproj']"
  sys.exit(0)


class Options:
  def __init__(self):
    self.list = []
    self.dict = {}

  def add(self, opt, val):
    if self.dict.has_key(opt):
      self.list[self.dict[opt]] = (opt, val)
    else:
      self.dict[opt] = len(self.list)
      self.list.append((opt, val))

if __name__ == '__main__':
  try:
    opts, args = getopt.getopt(sys.argv[1:], 'st:',
                               ['with-apr=',
                                'with-apr-util=',
                                'with-apr-iconv=',
                                'with-berkeley-db=',
                                'with-neon=',
                                'with-httpd=',
                                'with-libintl=',
                                'with-openssl=',
                                'with-zlib=',
                                'with-junit=',
                                'enable-pool-debug',
                                'enable-purify',
                                'enable-quantify',
                                'enable-nls',
                                'enable-bdb-in-apr-util',
                                'vsnet-version=',
                                ])
    if len(args) > 1:
      _usage_exit()
  except getopt.GetoptError:
    _usage_exit()

  conf = 'build.conf'
  skip = 0
  gentype = 'make'
  rest = Options()

  if args:
    conf = args[0]

  for opt, val in opts:
    if opt == '-s':
      skip = 1
    elif opt == '-t':
      gentype = val
    else:
      rest.add(opt, val)
      if opt == '--with-httpd':
        rest.add('--with-apr', os.path.join(val, 'srclib', 'apr'))
        rest.add('--with-apr-util', os.path.join(val, 'srclib', 'apr-util'))
        rest.add('--with-apr-iconv', os.path.join(val, 'srclib', 'apr-iconv'))

  # Remember all options so other scripts can use them
  opt_conf = open('gen-make.opts', 'w')
  opt_conf.write('[options]\n')
  for opt, val in rest.list:
    opt_conf.write(opt + ' = ' + val + '\n')
  opt_conf.close()

  if gentype not in gen_modules.keys():
    _usage_exit()

  main(conf, gentype, skip_depends=skip, other_options=rest.list)


### End of file.
