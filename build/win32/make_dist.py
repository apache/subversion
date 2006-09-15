import os
import sys
import shutil
import getopt
import glob
import traceback
import ConfigParser

# The script directory and the source base directory
_scriptdir = os.path.dirname(sys.argv[0])
_srcdir = os.path.join(_scriptdir, '..', '..')
_distname = None
_distdir = None
_readme = None


_stdout = sys.stdout
_stderr = sys.stderr

_logname = os.path.abspath(os.path.join(_scriptdir, 'make_dist.log'))
_logfile = open(_logname, 'w')
sys.stdout = _logfile
sys.stderr = _logfile

def _exit(code=0):
  if code:
    _stderr.write('make_dist: Exit %d\n' % (code,))
  sys.exit(code)


# Action classes

class MissingMethodImpl:
  pass

class Action:
  def run(self, dir, cfg):
    raise MissingMethodImpl()

  def _expand(self, cfg, value):
    cfg.set('__expand__', '__expand__', value)
    return cfg.get('__expand__', '__expand__')

  def _safe_expand(self, cfg, value):
    try:
      return self._expand(cfg, value)
    except:
      return None

  def _copy_file(self, source, target):
    print 'copy:', source
    print '  to:', target
    shutil.copyfile(source, target)

class File(Action):
  def __init__(self, path, name=None):
    self.path = path
    self.name = name

  def run(self, dir, cfg):
    path = self._expand(cfg, self.path)
    if self.name is None:
      name = os.path.basename(path)
    else:
      name = self.name
    self._copy_file(path, os.path.join(dir, name))

class OptFile(Action):
  def __init__(self, path, name=None):
    self.path = path
    self.name = name

  def run(self, dir, cfg):
    path = self._safe_expand(cfg, self.path)
    if path is None or not os.path.isfile(path):
      print 'make_dist: File not found:', self.path
      return
    if self.name is None:
      name = os.path.basename(path)
    else:
      name = self.name
    self._copy_file(path, os.path.join(dir, name))

class FileGlob(Action):
  def __init__(self, pattern):
    self.pattern = pattern

  def run(self, dir, cfg):
    pattern = self._expand(cfg, self.pattern)
    for source in glob.glob(pattern):
      self._copy_file(source, os.path.join(dir, os.path.basename(source)))

class InstallDocs(Action):
  def __init__(self, config, path):
    self.config = config
    self.path = path

  def run(self, dir, cfg):
    config = self._expand(cfg, self.config)
    pattern = os.path.join(self._expand(cfg, self.path), '*.*')
    print 'make_dist: Generating documentation'
    old_cwd = os.getcwd()
    try:
      os.chdir(_srcdir)
      _system('"%s" "%s"' % (cfg.get('tools', 'doxygen'), config))
      os.chdir(old_cwd)
      FileGlob(pattern).run(dir, cfg)
    except:
      os.chdir(old_cwd)
      raise
    else:
      os.chdir(old_cwd)

class InstallIconv(Action):
  def __init__(self, source, build_mode):
    self.source = source
    self.build_mode = build_mode

  def run(self, dir, cfg):
    source = os.path.abspath(self._expand(cfg, self.source))
    build_mode = self._expand(cfg, self.build_mode)
    print 'make_dist: Installing apr-iconv modules'
    install = ('"%s" -nologo -f Makefile.win install' 
               + ' INSTALL_DIR="%s"' 
               + ' BUILD_MODE=%s BIND_MODE=%s') \
               % (cfg.get('tools', 'nmake'),
                  os.path.abspath(dir),
                  build_mode,
                  'shared')
    old_cwd = os.getcwd()
    try:
      os.chdir(os.path.join(source, 'ccs'))
      _system(install)
      os.chdir(os.path.join(source, 'ces'))
      _system(install)
    except:
      os.chdir(old_cwd)
      raise
    else:
      os.chdir(old_cwd)

class InstallJar(Action):
  def __init__(self, jar, source):
    self.jar = jar
    self.source = source

  def run(self, dir, cfg):
    source = os.path.abspath(self._expand(cfg, self.source))
    jarfile = os.path.abspath(os.path.join(dir, self.jar))
    print 'make_dist: Creating jar', self.jar
    _system('"%s" cvf "%s" -C "%s" .'
            % (cfg.get('tools', 'jar'), jarfile, source))

class InstallMoFiles(Action):
  def __init__(self, source):
    self.source = source

  def run(self, dir, cfg):
    pattern = os.path.join(self._expand(cfg, self.source), '*.mo')
    for mofile in glob.glob(pattern):
      localedir = os.path.join(dir, os.path.basename(mofile)[:-3],
                               'LC_MESSAGES')
      os.makedirs(localedir)
      self._copy_file(mofile, os.path.join(localedir, 'subversion.mo'))

# This is the distribution tree
_disttree = {'': OptFile('%(readme)s', 'README.txt'),

             'bin': (File('%(blddir)s/svn/svn.exe'),
                     File('%(blddir)s/svn/svn.pdb'),
                     File('%(blddir)s/svnsync/svnsync.pdb'),
                     File('%(blddir)s/svnsync/svnsync.exe'),
                     File('%(blddir)s/svnadmin/svnadmin.exe'),
                     File('%(blddir)s/svnadmin/svnadmin.pdb'),
                     File('%(blddir)s/svnlook/svnlook.exe'),
                     File('%(blddir)s/svnlook/svnlook.pdb'),
                     File('%(blddir)s/svndumpfilter/svndumpfilter.exe'),
                     File('%(blddir)s/svndumpfilter/svndumpfilter.pdb'),
                     File('%(blddir)s/svnserve/svnserve.exe'),
                     File('%(blddir)s/svnserve/svnserve.pdb'),
                     File('%(blddir)s/svnversion/svnversion.exe'),
                     File('%(blddir)s/svnversion/svnversion.pdb'),
                     File('%(blddir)s/mod_dav_svn/mod_dav_svn.so'),
                     File('%(blddir)s/mod_dav_svn/mod_dav_svn.pdb'),
                     File('%(blddir)s/mod_authz_svn/mod_authz_svn.so'),
                     File('%(blddir)s/mod_authz_svn/mod_authz_svn.pdb'),
                     File('%(@apr)s/%(aprrel)s/libapr.dll'),
                     File('%(@apr)s/%(aprrel)s/libapr.pdb'),
                     File('%(@apr-iconv)s/%(aprrel)s/libapriconv.dll'),
                     File('%(@apr-iconv)s/%(aprrel)s/libapriconv.pdb'),
                     File('%(@apr-util)s/%(aprrel)s/libaprutil.dll'),
                     File('%(@apr-util)s/%(aprrel)s/libaprutil.pdb'),
                     File('%(@berkeley-db)s/bin/libdb%(bdbver)s.dll'),
                     OptFile('%(@berkeley-db)s/bin/libdb%(bdbver)s.pdb'),
                     OptFile('%(@openssl)s/out32dll/libeay32.dll'),
                     OptFile('%(@openssl)s/out32dll/libeay32.pdb'),
                     OptFile('%(@openssl)s/out32dll/ssleay32.dll'),
                     OptFile('%(@openssl)s/out32dll/ssleay32.pdb'),
                     OptFile('%(@libintl)s/bin/intl3_svn.dll'),
                     OptFile('%(@libintl)s/bin/intl3_svn.pdb'),
                     ),

             'doc': InstallDocs('%(srcdir)s/doc/doxygen.conf',
                                '%(srcdir)s/doc/doxygen/html'),

             'iconv': InstallIconv('%(@apr-iconv)s', '%(aprrel)s'),

             'include': FileGlob('%(svndir)s/include/*.h'),
             'include/apr': FileGlob('%(@apr)s/include/*.h'),
             'include/apr-iconv': FileGlob('%(@apr-iconv)s/include/*.h'),
             'include/apr-util': FileGlob('%(@apr-util)s/include/*.h'),

             'lib': (FileGlob('%(blddir)s/libsvn_*/*.lib'),
                     FileGlob('%(blddir)s/libsvn_*/*.pdb')),
             'lib/apr': File('%(@apr)s/%(aprrel)s/libapr.lib'),
             'lib/apr-iconv': File('%(@apr-iconv)s/%(aprrel)s/libapriconv.lib'),
             'lib/apr-util': (File('%(@apr-util)s/%(aprrel)s/libaprutil.lib'),
                              File('%(@apr-util)s/%(aprxml)s/xml.lib'),
                              File('%(@apr-util)s/%(aprxml)s/xml_src.pdb'),
                              ),
             'lib/neon': (File('%(@neon)s/libneon.lib'),
                          OptFile('%(@zlib)s/zlibstat.lib'),
                          ),

             'perl': None,
             'perl/site': None,
             'perl/site/lib': None,
             'perl/site/lib/SVN': FileGlob('%(bindsrc)s/swig/perl/native/*.pm'),
             'perl/site/lib/auto': None,
             'perl/site/lib/auto/SVN': None,
             # Perl module DLLs defined below

             'python': None,
             'python/libsvn': (FileGlob('%(binddir)s/swig/python/libsvn_swig_py/*.dll'),
                               FileGlob('%(binddir)s/swig/python/libsvn_swig_py/*.pdb'),
                               FileGlob('%(bindsrc)s/swig/python/*.py'),
                               FileGlob('%(binddir)s/swig/python/*.dll'),
                               FileGlob('%(binddir)s/swig/python/*.pdb'),
                               ),
             'python/svn': FileGlob('%(bindsrc)s/swig/python/svn/*.py'),

             'javahl': (FileGlob('%(binddir)s/java/javahl/native/libsvn*.dll'),
                        FileGlob('%(binddir)s/java/javahl/native/libsvn*.pdb'),
                        InstallJar('svnjavahl.jar',
                                   '%(bindsrc)s/java/javahl/classes'),
                        ),

             'share': None,
             'share/locale': InstallMoFiles('%(srcdir)s/%(svnrel)s/mo'),
             }

# Define Perl module DLLs
for module in ('Client', 'Core', 'Delta', 'Fs', 'Ra', 'Repos', 'Wc'):
  _disttree['perl/site/lib/auto/SVN/_' + module] = (
    File('%(binddir)s/swig/perl/native/_' + module + '.dll'),
    File('%(binddir)s/swig/perl/native/_' + module + '.pdb'))

def _system(command):
  def reopen_log():
    global _logfile
    _logfile = open(_logname, 'a')
    sys.stdout = _logfile
    sys.stderr = _logfile
  try:
    _logfile.close()
    sys.stdout = _stdout
    sys.stderr = _stderr
    os.system('"%s >>%s 2>&1"' % (command, _logname))
  except:
    reopen_log()
    raise
  else:
    reopen_log()


def _read_config():
  # Read make_dist.conf first. Fill in the default package locations.
  path_defaults = {'@berkeley-db':
                   os.path.abspath(os.path.join(_srcdir, 'db4-win32')),
                   '@apr':
                   os.path.abspath(os.path.join(_srcdir, 'apr')),
                   '@apr-iconv':
                   os.path.abspath(os.path.join(_srcdir, 'apr-iconv')),
                   '@apr-util':
                   os.path.abspath(os.path.join(_srcdir, 'apr-util')),
                   '@neon':
                   os.path.abspath(os.path.join(_srcdir, 'neon')),
                   }

  cfg = ConfigParser.ConfigParser(path_defaults)
  cfg.readfp(open(os.path.join(_scriptdir, 'make_dist.conf'), 'r'))

  # Read the options config generated by gen-make.py
  optcfg = ConfigParser.ConfigParser()
  optcfg.readfp(open(os.path.join(_srcdir, 'gen-make.opts'), 'r'))

  # Move the runtime options into the DEFAULT section
  for opt in optcfg.options('options'):
    if not opt[:7] == '--with-':
      continue
    optdir = os.path.abspath(os.path.join(_srcdir, optcfg.get('options', opt)))
    if not os.path.isdir(optdir):
      print 'make_dist:', opt, '=', optdir
      print 'make_dist: Target is not a directory'
      _exit(1)
    cfg.set('DEFAULT', '@' + opt[7:], optdir)

  # Also add the global parameters to the defaults
  cfg.set('DEFAULT', 'srcdir', os.path.abspath(_srcdir))
  cfg.set('DEFAULT', 'blddir', os.path.join(_srcdir,
                                            '%(svnrel)s', 'subversion'))
  cfg.set('DEFAULT', 'svndir', os.path.join(_srcdir, 'subversion'))
  cfg.set('DEFAULT', 'binddir', '%(blddir)s/bindings')
  cfg.set('DEFAULT', 'bindsrc', '%(svndir)s/bindings')


  if _distname is not None:
    cfg.set('DEFAULT', 'distname', os.path.abspath(_distname))
  if _distdir is not None:
    cfg.set('DEFAULT', 'distdir', os.path.abspath(_distdir))
  if _readme is not None:
    cfg.set('DEFAULT', 'readme', os.path.abspath(_readme))

  return cfg


def _make_zip(suffix, pathlist, extras):
  zipname = '%s%s.zip' % (_distname, suffix)
  zipcmd = '"%s" -9 -r "%s"' % (cfg.get('tools', 'zip'), zipname)
  for path in pathlist:
    zipcmd = zipcmd + ' "' + _distname + path + '"'
  if extras:
    zipcmd = zipcmd + ' ' + extras
  old_cwd = os.getcwd()
  try:
    os.chdir(_distdir)
    if os.path.exists(zipname):
      os.remove(zipname)
    print 'make_dist: Creating %s' % zipname
    _stdout.write('make_dist: Creating %s\n' % zipname)
    _system(zipcmd)
  except:
    os.chdir(old_cwd)
    raise
  else:
    os.chdir(old_cwd)


def _make_dist(cfg):
  try:
    cfg.add_section('__expand__')
    distdir = os.path.abspath(os.path.join(_distdir, _distname))
    if os.path.isdir(distdir):
      shutil.rmtree(distdir)
    os.makedirs(distdir)

    dirlist = _disttree.keys()
    dirlist.sort()

    for reldir in dirlist:
      dir = os.path.join(distdir, reldir)
      if not os.path.exists(dir):
        print 'make_dist: Creating directory', reldir
        _stdout.write('make_dist: Creating directory %s\n' % reldir)
        os.makedirs(dir)
      action = _disttree[reldir]
      if action is None:
        continue
      if type(action) == type(()):
        for subaction in action:
          subaction.run(dir, cfg)
      else:
        action.run(dir, cfg)

    xpdb = '-x "*.pdb"'
    _make_zip('',        ('/README.txt', '/bin', '/httpd',
                          '/iconv', '/share/locale'), xpdb)
    _make_zip('_dev',    ('/README.txt', '/doc', '/include', '/lib'), xpdb)
    _make_zip('_javahl', ('/README.txt', '/javahl'), xpdb)
    _make_zip('_pdb',    ('',), '-i "*.pdb"')
    _make_zip('_pl',     ('/README.txt', '/perl'), xpdb)
    _make_zip('_py',     ('/README.txt', '/python'), xpdb)

  except:
    traceback.print_exc(None, _stderr)
    _exit(1)


if __name__ == '__main__':
  opts, args = getopt.getopt(sys.argv[1:], '', ['readme='])
  if len(args) != 2 or len(opts) > 1:
    _stderr.write('Usage: make_dist.py [--readme=<file>] <distname> <distdir>\n')
    _exit(2)

  _distname, _distdir = args

  if len(opts) != 0:
    _readme = opts[0][1]

  cfg = _read_config()
  _make_dist(cfg)
