#!/usr/bin/env python
# python: coding=utf-8
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


# About this script:
#   This script is intended to simplify creating Subversion releases for
#   any of the supported release lines of Subversion.
#   It works well with our Apache infrastructure, and should make rolling,
#   posting, and announcing releases dirt simple.
#
#   This script may be run on a number of platforms, but it is intended to
#   be run on people.apache.org.  As such, it may have dependencies (such
#   as Python version) which may not be common, but are guaranteed to be
#   available on people.apache.org.

# It'd be kind of nice to use the Subversion python bindings in this script,
# but people.apache.org doesn't currently have them installed

# Stuff we need
import os
import re
import sys
import glob
import fnmatch
import shutil
import urllib2
import hashlib
import tarfile
import logging
import datetime
import tempfile
import operator
import itertools
import subprocess
import argparse       # standard in Python 2.7

# Find ezt, using Subversion's copy, if there isn't one on the system.
try:
    import ezt
except ImportError:
    ezt_path = os.path.dirname(os.path.dirname(os.path.abspath(sys.path[0])))
    ezt_path = os.path.join(ezt_path, 'build', 'generator')
    sys.path.append(ezt_path)

    import ezt
    sys.path.remove(ezt_path)


# Our required / recommended release tool versions by release branch
tool_versions = {
  'trunk' : {
            'autoconf' : ['2.69',
            '954bd69b391edc12d6a4a51a2dd1476543da5c6bbf05a95b59dc0dd6fd4c2969'],
            'libtool'  : ['2.4.6',
            'e3bd4d5d3d025a36c21dd6af7ea818a2afcd4dfc1ea5a17b39d7854bcd0c06e3'],
            'swig'     : ['3.0.10',
            '2939aae39dec06095462f1b95ce1c958ac80d07b926e48871046d17c0094f44c'],
  },
  '1.10' : {
            'autoconf' : ['2.69',
            '954bd69b391edc12d6a4a51a2dd1476543da5c6bbf05a95b59dc0dd6fd4c2969'],
            'libtool'  : ['2.4.6',
            'e3bd4d5d3d025a36c21dd6af7ea818a2afcd4dfc1ea5a17b39d7854bcd0c06e3'],
            'swig'     : ['3.0.10',
            '2939aae39dec06095462f1b95ce1c958ac80d07b926e48871046d17c0094f44c'],
  },
  '1.9' : {
            'autoconf' : ['2.69',
            '954bd69b391edc12d6a4a51a2dd1476543da5c6bbf05a95b59dc0dd6fd4c2969'],
            'libtool'  : ['2.4.6',
            'e3bd4d5d3d025a36c21dd6af7ea818a2afcd4dfc1ea5a17b39d7854bcd0c06e3'],
            'swig'     : ['2.0.12',
            '65e13f22a60cecd7279c59882ff8ebe1ffe34078e85c602821a541817a4317f7'],
  },
  '1.8' : {
            'autoconf' : ['2.69',
            '954bd69b391edc12d6a4a51a2dd1476543da5c6bbf05a95b59dc0dd6fd4c2969'],
            'libtool'  : ['2.4.3',
            '36b4881c1843d7585de9c66c4c3d9a067ed3a3f792bc670beba21f5a4960acdf'],
            'swig'     : ['2.0.9',
            '586954000d297fafd7e91d1ad31089cc7e249f658889d11a44605d3662569539'],
  },
}

# The version that is our current recommended release
# ### TODO: derive this from svn_version.h; see ../../build/getversion.py
recommended_release = '1.9'

# Some constants
repos = 'https://svn.apache.org/repos/asf/subversion'
secure_repos = 'https://svn.apache.org/repos/asf/subversion'
dist_repos = 'https://dist.apache.org/repos/dist'
dist_dev_url = dist_repos + '/dev/subversion'
dist_release_url = dist_repos + '/release/subversion'
KEYS = 'https://people.apache.org/keys/group/subversion.asc'
extns = ['zip', 'tar.gz', 'tar.bz2']


#----------------------------------------------------------------------
# Utility functions

class Version(object):
    regex = re.compile(r'(\d+).(\d+).(\d+)(?:-(?:(rc|alpha|beta)(\d+)))?')

    def __init__(self, ver_str):
        # Special case the 'trunk-nightly' version
        if ver_str == 'trunk-nightly':
            self.major = None
            self.minor = None
            self.patch = None
            self.pre = 'nightly'
            self.pre_num = None
            self.base = 'nightly'
            self.branch = 'trunk'
            return

        match = self.regex.search(ver_str)

        if not match:
            raise RuntimeError("Bad version string '%s'" % ver_str)

        self.major = int(match.group(1))
        self.minor = int(match.group(2))
        self.patch = int(match.group(3))

        if match.group(4):
            self.pre = match.group(4)
            self.pre_num = int(match.group(5))
        else:
            self.pre = None
            self.pre_num = None

        self.base = '%d.%d.%d' % (self.major, self.minor, self.patch)
        self.branch = '%d.%d' % (self.major, self.minor)

    def is_prerelease(self):
        return self.pre != None

    def is_recommended(self):
        return self.branch == recommended_release

    def get_download_anchor(self):
        if self.is_prerelease():
            return 'pre-releases'
        else:
            if self.is_recommended():
                return 'recommended-release'
            else:
                return 'supported-releases'

    def get_ver_tags(self, revnum):
        # These get substituted into svn_version.h
        ver_tag = ''
        ver_numtag = ''
        if self.pre == 'alpha':
            ver_tag = '" (Alpha %d)"' % self.pre_num
            ver_numtag = '"-alpha%d"' % self.pre_num
        elif self.pre == 'beta':
            ver_tag = '" (Beta %d)"' % args.version.pre_num
            ver_numtag = '"-beta%d"' % self.pre_num
        elif self.pre == 'rc':
            ver_tag = '" (Release Candidate %d)"' % self.pre_num
            ver_numtag = '"-rc%d"' % self.pre_num
        elif self.pre == 'nightly':
            ver_tag = '" (Nightly Build r%d)"' % revnum
            ver_numtag = '"-nightly-r%d"' % revnum
        else:
            ver_tag = '" (r%d)"' % revnum 
            ver_numtag = '""'
        return (ver_tag, ver_numtag)

    def __serialize(self):
        return (self.major, self.minor, self.patch, self.pre, self.pre_num)

    def __eq__(self, that):
        return self.__serialize() == that.__serialize()

    def __ne__(self, that):
        return self.__serialize() != that.__serialize()

    def __hash__(self):
        return hash(self.__serialize())

    def __lt__(self, that):
        if self.major < that.major: return True
        if self.major > that.major: return False

        if self.minor < that.minor: return True
        if self.minor > that.minor: return False

        if self.patch < that.patch: return True
        if self.patch > that.patch: return False

        if not self.pre and not that.pre: return False
        if not self.pre and that.pre: return False
        if self.pre and not that.pre: return True

        # We are both pre-releases
        if self.pre != that.pre:
            return self.pre < that.pre
        else:
            return self.pre_num < that.pre_num

    def __str__(self):
        "Return an SVN_VER_NUMBER-formatted string, or 'nightly'."
        if self.pre:
            if self.pre == 'nightly':
                return 'nightly'
            else:
                extra = '-%s%d' % (self.pre, self.pre_num)
        else:
            extra = ''

        return self.base + extra

    def __repr__(self):

        return "Version(%s)" % repr(str(self))

def get_prefix(base_dir):
    return os.path.join(base_dir, 'prefix')

def get_tempdir(base_dir):
    return os.path.join(base_dir, 'tempdir')

def get_workdir(base_dir):
    return os.path.join(get_tempdir(base_dir), 'working')

# The name of this directory is also used to name the tarball and for
# the root of paths within the tarball, e.g. subversion-1.9.5 or
# subversion-nightly-r1800000
def get_exportdir(base_dir, version, revnum):
    if version.pre != 'nightly':
        return os.path.join(get_tempdir(base_dir), 'subversion-'+str(version))
    return os.path.join(get_tempdir(base_dir),
                        'subversion-%s-r%d' % (version, revnum))

def get_deploydir(base_dir):
    return os.path.join(base_dir, 'deploy')

def get_target(args):
    "Return the location of the artifacts"
    if args.target:
        return args.target
    else:
        return get_deploydir(args.base_dir)

def get_tmpldir():
    return os.path.join(os.path.abspath(sys.path[0]), 'templates')

def get_tmplfile(filename):
    try:
        return open(os.path.join(get_tmpldir(), filename))
    except IOError:
        # Hmm, we had a problem with the local version, let's try the repo
        return urllib2.urlopen(repos + '/trunk/tools/dist/templates/' + filename)

def get_nullfile():
    return open(os.path.devnull, 'w')

def run_script(verbose, script, hide_stderr=False):
    stderr = None
    if verbose:
        stdout = None
    else:
        stdout = get_nullfile()
        if hide_stderr:
            stderr = get_nullfile()

    for l in script.split('\n'):
        subprocess.check_call(l.split(), stdout=stdout, stderr=stderr)

def download_file(url, target, checksum):
    response = urllib2.urlopen(url)
    target_file = open(target, 'w+')
    target_file.write(response.read())
    target_file.seek(0)
    m = hashlib.sha256()
    m.update(target_file.read())
    target_file.close()
    checksum2 = m.hexdigest()
    if checksum != checksum2:
        raise RuntimeError("Checksum mismatch for '%s': "\
                           "downloaded: '%s'; expected: '%s'" % \
                           (target, checksum, checksum2))

#----------------------------------------------------------------------
# ezt helpers

# In ezt, «[if-any foo]» is true when «data['foo'] == False»,
# hence, provide this constant for readability.
ezt_False = ""

# And this constant for symmetry.
ezt_True = True

# And this for convenience.
def ezt_bool(boolean_value):
    return ezt_True if boolean_value else ezt_False

#----------------------------------------------------------------------
# Cleaning up the environment

def cleanup(args):
    'Remove generated files and folders.'
    logging.info('Cleaning')

    shutil.rmtree(get_prefix(args.base_dir), True)
    shutil.rmtree(get_tempdir(args.base_dir), True)
    shutil.rmtree(get_deploydir(args.base_dir), True)


#----------------------------------------------------------------------
# Creating an environment to roll the release

class RollDep(object):
    'The super class for each of the build dependencies.'
    def __init__(self, base_dir, use_existing, verbose):
        self._base_dir = base_dir
        self._use_existing = use_existing
        self._verbose = verbose

    def _test_version(self, cmd):
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        (stdout, stderr) = proc.communicate()
        rc = proc.wait()
        if rc: return ''

        return stdout.split('\n')

    def build(self):
        if not hasattr(self, '_extra_configure_flags'):
            self._extra_configure_flags = ''
        cwd = os.getcwd()
        tempdir = get_tempdir(self._base_dir)
        tarball = os.path.join(tempdir, self._filebase + '.tar.gz')

        if os.path.exists(tarball):
            if not self._use_existing:
                raise RuntimeError('autoconf tarball "%s" already exists'
                                                                    % tarball)
            logging.info('Using existing %s.tar.gz' % self._filebase)
        else:
            logging.info('Fetching %s' % self._filebase)
            download_file(self._url, tarball, self._checksum)

        # Extract tarball
        tarfile.open(tarball).extractall(tempdir)

        logging.info('Building ' + self.label)
        os.chdir(os.path.join(tempdir, self._filebase))
        run_script(self._verbose,
                   '''./configure --prefix=%s %s
                      make
                      make install''' % (get_prefix(self._base_dir),
                                         self._extra_configure_flags))

        os.chdir(cwd)


class AutoconfDep(RollDep):
    def __init__(self, base_dir, use_existing, verbose, autoconf_ver, checksum):
        RollDep.__init__(self, base_dir, use_existing, verbose)
        self.label = 'autoconf'
        self._filebase = 'autoconf-' + autoconf_ver
        self._autoconf_ver =  autoconf_ver
        self._url = 'https://ftp.gnu.org/gnu/autoconf/%s.tar.gz' % self._filebase
        self._checksum = checksum

    def have_usable(self):
        output = self._test_version(['autoconf', '-V'])
        if not output: return False

        version = output[0].split()[-1:][0]
        return version == self._autoconf_ver

    def use_system(self):
        if not self._use_existing: return False
        return self.have_usable()


class LibtoolDep(RollDep):
    def __init__(self, base_dir, use_existing, verbose, libtool_ver, checksum):
        RollDep.__init__(self, base_dir, use_existing, verbose)
        self.label = 'libtool'
        self._filebase = 'libtool-' + libtool_ver
        self._libtool_ver = libtool_ver
        self._url = 'https://ftp.gnu.org/gnu/libtool/%s.tar.gz' % self._filebase
        self._checksum = checksum

    def have_usable(self):
        output = self._test_version(['libtool', '--version'])
        if not output: return False

        return self._libtool_ver in output[0]

    def use_system(self):
        # We unconditionally return False here, to avoid using a borked
        # system libtool (I'm looking at you, Debian).
        return False

    def build(self):
        RollDep.build(self)
        # autogen.sh looks for glibtoolize before libtoolize
        bin_dir = os.path.join(get_prefix(self._base_dir), "bin")
        os.symlink("libtoolize", os.path.join(bin_dir, "glibtoolize"))
        os.symlink("libtool", os.path.join(bin_dir, "glibtool"))


class SwigDep(RollDep):
    def __init__(self, base_dir, use_existing, verbose, swig_ver, checksum,
        sf_mirror):
        RollDep.__init__(self, base_dir, use_existing, verbose)
        self.label = 'swig'
        self._filebase = 'swig-' + swig_ver
        self._swig_ver = swig_ver
        self._url = 'https://sourceforge.net/projects/swig/files/swig/%(swig)s/%(swig)s.tar.gz/download?use_mirror=%(sf_mirror)s' % \
            { 'swig' : self._filebase,
              'sf_mirror' : sf_mirror }
        self._checksum = checksum
        self._extra_configure_flags = '--without-pcre'

    def have_usable(self):
        output = self._test_version(['swig', '-version'])
        if not output: return False

        version = output[1].split()[-1:][0]
        return version == self._swig_ver

    def use_system(self):
        if not self._use_existing: return False
        return self.have_usable()


def build_env(args):
    'Download prerequisites for a release and prepare the environment.'
    logging.info('Creating release environment')

    try:
        os.mkdir(get_prefix(args.base_dir))
        os.mkdir(get_tempdir(args.base_dir))
    except OSError:
        if not args.use_existing:
            raise

    autoconf = AutoconfDep(args.base_dir, args.use_existing, args.verbose,
                           tool_versions[args.version.branch]['autoconf'][0],
                           tool_versions[args.version.branch]['autoconf'][1])
    libtool = LibtoolDep(args.base_dir, args.use_existing, args.verbose,
                         tool_versions[args.version.branch]['libtool'][0],
                         tool_versions[args.version.branch]['libtool'][1])
    swig = SwigDep(args.base_dir, args.use_existing, args.verbose,
                   tool_versions[args.version.branch]['swig'][0],
                   tool_versions[args.version.branch]['swig'][1],
                   args.sf_mirror)

    # iterate over our rolling deps, and build them if needed
    for dep in [autoconf, libtool, swig]:
        if dep.use_system():
            logging.info('Using system %s' % dep.label)
        else:
            dep.build()


#----------------------------------------------------------------------
# Create release artifacts

def compare_changes(repos, branch, revision):
    mergeinfo_cmd = ['svn', 'mergeinfo', '--show-revs=eligible',
                     repos + '/trunk/CHANGES',
                     repos + '/' + branch + '/' + 'CHANGES']
    stdout = subprocess.check_output(mergeinfo_cmd)
    if stdout:
      # Treat this as a warning since we are now putting entries for future
      # minor releases in CHANGES on trunk.
      logging.warning('CHANGES has unmerged revisions: %s' %
                      stdout.replace("\n", " "))


_current_year = str(datetime.datetime.now().year)
_copyright_re = re.compile(r'Copyright (?:\(C\) )?(?P<year>[0-9]+)'
                           r' The Apache Software Foundation',
                           re.MULTILINE)

def check_copyright_year(repos, branch, revision):
    def check_file(branch_relpath):
        file_url = (repos + '/' + branch + '/'
                    + branch_relpath + '@' + str(revision))
        cat_cmd = ['svn', 'cat', file_url]
        stdout = subprocess.check_output(cat_cmd)
        m = _copyright_re.search(stdout)
        if m:
            year = m.group('year')
        else:
            year = None
        if year != _current_year:
            logging.warning('Copyright year in ' + branch_relpath
                            + ' is not the current year')
    check_file('NOTICE')
    check_file('subversion/libsvn_subr/version.c')

def replace_lines(path, actions):
    with open(path, 'r') as old_content:
        lines = old_content.readlines()
    with open(path, 'w') as new_content:
        for line in lines:
            for start, pattern, repl in actions:
                if line.startswith(start):
                    line = re.sub(pattern, repl, line)
            new_content.write(line)

def roll_tarballs(args):
    'Create the release artifacts.'

    if not args.branch:
        args.branch = 'branches/%d.%d.x' % (args.version.major, args.version.minor)

    branch = args.branch # shorthand
    branch = branch.rstrip('/') # canonicalize for later comparisons

    logging.info('Rolling release %s from branch %s@%d' % (args.version,
                                                           branch, args.revnum))

    check_copyright_year(repos, args.branch, args.revnum)

    # Ensure we've got the appropriate rolling dependencies available
    autoconf = AutoconfDep(args.base_dir, False, args.verbose,
                         tool_versions[args.version.branch]['autoconf'][0],
                         tool_versions[args.version.branch]['autoconf'][1])
    libtool = LibtoolDep(args.base_dir, False, args.verbose,
                         tool_versions[args.version.branch]['libtool'][0],
                         tool_versions[args.version.branch]['libtool'][1])
    swig = SwigDep(args.base_dir, False, args.verbose,
                   tool_versions[args.version.branch]['swig'][0],
                   tool_versions[args.version.branch]['swig'][1], None)

    for dep in [autoconf, libtool, swig]:
        if not dep.have_usable():
           raise RuntimeError('Cannot find usable %s' % dep.label)

    if branch != 'trunk':
        # Make sure CHANGES is sync'd.
        compare_changes(repos, branch, args.revnum)

    # Ensure the output directory doesn't already exist
    if os.path.exists(get_deploydir(args.base_dir)):
        raise RuntimeError('output directory \'%s\' already exists'
                                            % get_deploydir(args.base_dir))

    os.mkdir(get_deploydir(args.base_dir))

    logging.info('Preparing working copy source')
    shutil.rmtree(get_workdir(args.base_dir), True)
    run_script(args.verbose, 'svn checkout %s %s'
               % (repos + '/' + branch + '@' + str(args.revnum),
                  get_workdir(args.base_dir)))

    # Exclude stuff we don't want in the tarball, it will not be present
    # in the exported tree.
    exclude = ['contrib', 'notes']
    if branch != 'trunk':
        exclude += ['STATUS']
        if args.version.minor < 7:
            exclude += ['packages', 'www']
    cwd = os.getcwd()
    os.chdir(get_workdir(args.base_dir))
    run_script(args.verbose,
               'svn update --set-depth exclude %s' % " ".join(exclude))
    os.chdir(cwd)

    if args.patches:
        # Assume patches are independent and can be applied in any
        # order, no need to sort.
        majmin = '%d.%d' % (args.version.major, args.version.minor)
        for name in os.listdir(args.patches):
            if name.find(majmin) != -1 and name.endswith('patch'):
                logging.info('Applying patch %s' % name)
                run_script(args.verbose,
                           '''svn patch %s %s'''
                           % (os.path.join(args.patches, name),
                              get_workdir(args.base_dir)))

    # Massage the new version number into svn_version.h.
    ver_tag, ver_numtag = args.version.get_ver_tags(args.revnum)
    replacements = [('#define SVN_VER_TAG',
                     '".*"', ver_tag),
                    ('#define SVN_VER_NUMTAG',
                     '".*"', ver_numtag),
                    ('#define SVN_VER_REVISION',
                     '[0-9][0-9]*', str(args.revnum))]
    if args.version.pre != 'nightly':
        # SVN_VER_PATCH might change for security releases, e.g., when
        # releasing 1.9.7 from the magic revision of 1.9.6.
        #
        # ### Would SVN_VER_MAJOR / SVN_VER_MINOR ever change?
        # ### Note that SVN_VER_MINOR is duplicated in some places, see
        # ### <https://subversion.apache.org/docs/community-guide/releasing.html#release-branches>
        replacements += [('#define SVN_VER_MAJOR',
                          '[0-9][0-9]*', str(args.version.major)),
                         ('#define SVN_VER_MINOR',
                          '[0-9][0-9]*', str(args.version.minor)),
                         ('#define SVN_VER_PATCH',
                          '[0-9][0-9]*', str(args.version.patch))]
    replace_lines(os.path.join(get_workdir(args.base_dir),
                               'subversion', 'include', 'svn_version.h'),
                  replacements)

    # Basename for export and tarballs, e.g. subversion-1.9.5 or
    # subversion-nightly-r1800000
    exportdir = get_exportdir(args.base_dir, args.version, args.revnum)
    basename = os.path.basename(exportdir)

    def export(windows):
        shutil.rmtree(exportdir, True)
        if windows:
            eol_style = "--native-eol CRLF"
        else:
            eol_style = "--native-eol LF"
        run_script(args.verbose, "svn export %s %s %s"
                   % (eol_style, get_workdir(args.base_dir), exportdir))

    def transform_sql():
        for root, dirs, files in os.walk(exportdir):
            for fname in files:
                if fname.endswith('.sql'):
                    run_script(args.verbose,
                               'python build/transform_sql.py %s/%s %s/%s'
                               % (root, fname, root, fname[:-4] + '.h'))

    def clean_autom4te():
        for root, dirs, files in os.walk(get_workdir(args.base_dir)):
            for dname in dirs:
                if dname.startswith('autom4te') and dname.endswith('.cache'):
                    shutil.rmtree(os.path.join(root, dname))

    logging.info('Building Windows tarballs')
    export(windows=True)
    os.chdir(exportdir)
    transform_sql()
    # Can't use the po-update.sh in the Windows export since it has CRLF
    # line endings and won't run, so use the one in the working copy.
    run_script(args.verbose,
               '%s/tools/po/po-update.sh pot' % get_workdir(args.base_dir))
    os.chdir(cwd)
    clean_autom4te() # dist.sh does it but pointless on Windows?
    os.chdir(get_tempdir(args.base_dir))
    run_script(args.verbose,
               'zip -q -r %s %s' % (basename + '.zip', basename))
    os.chdir(cwd)

    logging.info('Building Unix tarballs')
    export(windows=False)
    os.chdir(exportdir)
    transform_sql()
    run_script(args.verbose,
               '''tools/po/po-update.sh pot
                  ./autogen.sh --release''',
               hide_stderr=True) # SWIG is noisy
    os.chdir(cwd)
    clean_autom4te() # dist.sh does it but probably pointless

    # Do not use tar, it's probably GNU tar which produces tar files
    # that are not compliant with POSIX.1 when including filenames
    # longer than 100 chars.  Platforms without a tar that understands
    # the GNU tar extension will not be able to extract the resulting
    # tar file.  Use pax to produce POSIX.1 tar files.
    #
    # Use the gzip -n flag - this prevents it from storing the
    # original name of the .tar file, and far more importantly, the
    # mtime of the .tar file, in the produced .tar.gz file. This is
    # important, because it makes the gzip encoding reproducable by
    # anyone else who has an similar version of gzip, and also uses
    # "gzip -9n". This means that committers who want to GPG-sign both
    # the .tar.gz and the .tar.bz2 can download the .tar.bz2 (which is
    # smaller), and locally generate an exact duplicate of the
    # official .tar.gz file. This metadata is data on the temporary
    # uncompressed tarball itself, not any of its contents, so there
    # will be no effect on end-users.
    os.chdir(get_tempdir(args.base_dir))
    run_script(args.verbose,
               '''pax -x ustar -w -f %s %s
                  bzip2 -9fk %s
                  gzip -9nf %s'''
               % (basename + '.tar', basename,
                  basename + '.tar',
                  basename + '.tar'))
    os.chdir(cwd)

    # Move the results to the deploy directory
    logging.info('Moving artifacts and calculating checksums')
    for e in extns:
        filename = basename + '.' + e
        filepath = os.path.join(get_tempdir(args.base_dir), filename)
        shutil.move(filepath, get_deploydir(args.base_dir))
        filepath = os.path.join(get_deploydir(args.base_dir), filename)
        m = hashlib.sha1()
        m.update(open(filepath, 'r').read())
        open(filepath + '.sha1', 'w').write(m.hexdigest())
        m = hashlib.sha512()
        m.update(open(filepath, 'r').read())
        open(filepath + '.sha512', 'w').write(m.hexdigest())

    # Nightlies do not get tagged so do not need the header
    if args.version.pre != 'nightly':
        shutil.copy(os.path.join(get_workdir(args.base_dir),
                                 'subversion', 'include', 'svn_version.h'),
                    os.path.join(get_deploydir(args.base_dir),
                                 'svn_version.h.dist-%s' % str(args.version)))

    # And we're done!

#----------------------------------------------------------------------
# Sign the candidate release artifacts

def sign_candidates(args):
    'Sign candidate artifacts in the dist development directory.'

    def sign_file(filename):
        asc_file = open(filename + '.asc', 'a')
        logging.info("Signing %s" % filename)
        proc = subprocess.check_call(['gpg', '-ba', '-o', '-', filename],
                                     stdout=asc_file)
        asc_file.close()

    target = get_target(args)

    for e in extns:
        filename = os.path.join(target, 'subversion-%s.%s' % (args.version, e))
        sign_file(filename)
        if args.version.major >= 1 and args.version.minor <= 6:
            filename = os.path.join(target,
                                   'subversion-deps-%s.%s' % (args.version, e))
            sign_file(filename)


#----------------------------------------------------------------------
# Post the candidate release artifacts

def post_candidates(args):
    'Post candidate artifacts to the dist development directory.'

    target = get_target(args)

    logging.info('Importing tarballs to %s' % dist_dev_url)
    ver = str(args.version)
    svn_cmd = ['svn', 'import', '-m',
               'Add Subversion %s candidate release artifacts' % ver,
               '--auto-props', '--config-option',
               'config:auto-props:*.asc=svn:eol-style=native;svn:mime-type=text/plain',
               target, dist_dev_url]
    if (args.username):
        svn_cmd += ['--username', args.username]
    subprocess.check_call(svn_cmd)

#----------------------------------------------------------------------
# Create tag

def create_tag(args):
    'Create tag in the repository'

    target = get_target(args)

    logging.info('Creating tag for %s' % str(args.version))

    if not args.branch:
        args.branch = 'branches/%d.%d.x' % (args.version.major, args.version.minor)

    branch = secure_repos + '/' + args.branch.rstrip('/')

    tag = secure_repos + '/tags/' + str(args.version)

    svnmucc_cmd = ['svnmucc', '-m',
                   'Tagging release ' + str(args.version)]
    if (args.username):
        svnmucc_cmd += ['--username', args.username]
    svnmucc_cmd += ['cp', str(args.revnum), branch, tag]
    svnmucc_cmd += ['put', os.path.join(target, 'svn_version.h.dist' + '-' +
                                        str(args.version)),
                    tag + '/subversion/include/svn_version.h']

    # don't redirect stdout/stderr since svnmucc might ask for a password
    try:
        subprocess.check_call(svnmucc_cmd)
    except subprocess.CalledProcessError:
        if args.version.is_prerelease():
            logging.error("Do you need to pass --branch=trunk?")
        raise

    if not args.version.is_prerelease():
        logging.info('Bumping revisions on the branch')
        def replace_in_place(fd, startofline, flat, spare):
            """In file object FD, replace FLAT with SPARE in the first line
            starting with STARTOFLINE."""

            fd.seek(0, os.SEEK_SET)
            lines = fd.readlines()
            for i, line in enumerate(lines):
                if line.startswith(startofline):
                    lines[i] = line.replace(flat, spare)
                    break
            else:
                raise RuntimeError('Definition of %r not found' % startofline)

            fd.seek(0, os.SEEK_SET)
            fd.writelines(lines)
            fd.truncate() # for current callers, new value is never shorter.

        new_version = Version('%d.%d.%d' %
                              (args.version.major, args.version.minor,
                               args.version.patch + 1))

        def file_object_for(relpath):
            fd = tempfile.NamedTemporaryFile()
            url = branch + '/' + relpath
            fd.url = url
            subprocess.check_call(['svn', 'cat', '%s@%d' % (url, args.revnum)],
                                  stdout=fd)
            return fd

        svn_version_h = file_object_for('subversion/include/svn_version.h')
        replace_in_place(svn_version_h, '#define SVN_VER_PATCH ',
                         str(args.version.patch), str(new_version.patch))

        STATUS = file_object_for('STATUS')
        replace_in_place(STATUS, 'Status of ',
                         str(args.version), str(new_version))

        svn_version_h.seek(0, os.SEEK_SET)
        STATUS.seek(0, os.SEEK_SET)
        subprocess.check_call(['svnmucc', '-r', str(args.revnum),
                               '-m', 'Post-release housekeeping: '
                                     'bump the %s branch to %s.'
                               % (branch.split('/')[-1], str(new_version)),
                               'put', svn_version_h.name, svn_version_h.url,
                               'put', STATUS.name, STATUS.url,
                              ])
        del svn_version_h
        del STATUS

#----------------------------------------------------------------------
# Clean dist

def clean_dist(args):
    'Clean the distribution directory of all but the most recent artifacts.'

    stdout = subprocess.check_output(['svn', 'list', dist_release_url])

    def minor(version):
        """Return the minor release line of the parameter, which must be
        a Version object."""
        return (version.major, version.minor)

    filenames = stdout.split('\n')
    filenames = filter(lambda x: x.startswith('subversion-'), filenames)
    versions = set(map(Version, filenames))
    minor_lines = set(map(minor, versions))
    to_keep = set()
    # Keep 3 minor lines: 1.10.0-alpha3, 1.9.7, 1.8.19.
    # TODO: When we release 1.A.0 GA we'll have to manually remove 1.(A-2).* artifacts.
    for recent_line in sorted(minor_lines, reverse=True)[:3]:
        to_keep.add(max(
            x for x in versions
            if minor(x) == recent_line
        ))
    for i in sorted(to_keep):
        logging.info("Saving release '%s'", i)

    svnmucc_cmd = ['svnmucc', '-m', 'Remove old Subversion releases.\n' +
                   'They are still available at ' +
                   'https://archive.apache.org/dist/subversion/']
    if (args.username):
        svnmucc_cmd += ['--username', args.username]
    for filename in filenames:
        if Version(filename) not in to_keep:
            logging.info("Removing %r", filename)
            svnmucc_cmd += ['rm', dist_release_url + '/' + filename]

    # don't redirect stdout/stderr since svnmucc might ask for a password
    if 'rm' in svnmucc_cmd:
        subprocess.check_call(svnmucc_cmd)
    else:
        logging.info("Nothing to remove")

#----------------------------------------------------------------------
# Move to dist

def move_to_dist(args):
    'Move candidate artifacts to the distribution directory.'

    stdout = subprocess.check_output(['svn', 'list', dist_dev_url])

    filenames = []
    for entry in stdout.split('\n'):
      if fnmatch.fnmatch(entry, 'subversion-%s.*' % str(args.version)):
        filenames.append(entry)
    svnmucc_cmd = ['svnmucc', '-m',
                   'Publish Subversion-%s.' % str(args.version)]
    if (args.username):
        svnmucc_cmd += ['--username', args.username]
    svnmucc_cmd += ['rm', dist_dev_url + '/' + 'svn_version.h.dist'
                          + '-' + str(args.version)]
    for filename in filenames:
        svnmucc_cmd += ['mv', dist_dev_url + '/' + filename,
                        dist_release_url + '/' + filename]

    # don't redirect stdout/stderr since svnmucc might ask for a password
    logging.info('Moving release artifacts to %s' % dist_release_url)
    subprocess.check_call(svnmucc_cmd)

#----------------------------------------------------------------------
# Write announcements

def write_news(args):
    'Write text for the Subversion website.'
    data = { 'date' : datetime.date.today().strftime('%Y%m%d'),
             'date_pres' : datetime.date.today().strftime('%Y-%m-%d'),
             'major-minor' : args.version.branch,
             'version' : str(args.version),
             'version_base' : args.version.base,
             'anchor': args.version.get_download_anchor(),
             'is_recommended': ezt_bool(args.version.is_recommended()),
           }

    if args.version.is_prerelease():
        template_filename = 'rc-news.ezt'
    else:
        template_filename = 'stable-news.ezt'

    template = ezt.Template()
    template.parse(get_tmplfile(template_filename).read())
    template.generate(sys.stdout, data)


def get_sha1info(args):
    'Return a list of sha1 info for the release'

    target = get_target(args)

    sha1s = glob.glob(os.path.join(target, 'subversion*-%s*.sha1' % args.version))

    class info(object):
        pass

    sha1info = []
    for s in sha1s:
        i = info()
        # strip ".sha1"
        i.filename = os.path.basename(s)[:-5]
        i.sha1 = open(s, 'r').read()
        sha1info.append(i)

    return sha1info


def write_announcement(args):
    'Write the release announcement.'
    sha1info = get_sha1info(args)
    siginfo = "\n".join(get_siginfo(args, True)) + "\n"

    data = { 'version'              : str(args.version),
             'sha1info'             : sha1info,
             'siginfo'              : siginfo,
             'major-minor'          : args.version.branch,
             'major-minor-patch'    : args.version.base,
             'anchor'               : args.version.get_download_anchor(),
           }

    if args.version.is_prerelease():
        template_filename = 'rc-release-ann.ezt'
    else:
        data['dot-zero'] = ezt_bool(args.version.patch == 0)
        # TODO: instead of requiring the RM to remember to pass --security,
        #   read the private repository where CVE announcements are staged,
        #   parse the json file that identifies which versions are affected,
        #   and accordingly automagically set data['security'].
        data['security'] = ezt_bool(args.security)
        template_filename = 'stable-release-ann.ezt'

        # The template text assumes these two are mutually exclusive.
        # If you ever find a reason to make a x.y.0 release with a security
        # bug, just comment this out and update the template before sending.
        assert not (data['dot-zero'] and data['security'])

    template = ezt.Template(compress_whitespace = False)
    template.parse(get_tmplfile(template_filename).read())
    template.generate(sys.stdout, data)


def write_downloads(args):
    'Output the download section of the website.'
    sha1info = get_sha1info(args)

    data = { 'version'              : str(args.version),
             'fileinfo'             : sha1info,
           }

    template = ezt.Template(compress_whitespace = False)
    template.parse(get_tmplfile('download.ezt').read())
    template.generate(sys.stdout, data)


#----------------------------------------------------------------------
# Validate the signatures for a release

key_start = '-----BEGIN PGP SIGNATURE-----'
key_end = '-----END PGP SIGNATURE-----'

PUBLIC_KEY_ALGORITHMS = {
    # These values are taken from the RFC's registry at:
    # https://www.iana.org/assignments/pgp-parameters/pgp-parameters.xhtml#pgp-parameters-12
    #
    # The values are callables that produce gpg1-like key length and type
    # indications, e.g., "4096R" for a 4096-bit RSA key.
    1: (lambda keylen: str(keylen) + 'R'), # RSA
}

def _make_human_readable_fingerprint(fingerprint):
    return re.compile(r'(....)' * 10).sub(r'\1 \2 \3 \4 \5  \6 \7 \8 \9 \10',
                                          fingerprint)

def get_siginfo(args, quiet=False):
    'Returns a list of signatures for the release.'

    try:
        import gnupg
    except ImportError:
        import security._gnupg as gnupg
    gpg = gnupg.GPG()

    target = get_target(args)

    good_sigs = {}
    fingerprints = {}
    output = []

    glob_pattern = os.path.join(target, 'subversion*-%s*.asc' % args.version)
    for filename in glob.glob(glob_pattern):
        text = open(filename).read()
        keys = text.split(key_start)

        # Check the keys file syntax. We've been bitten in the past
        # with syntax errors in the key delimiters that GPG didn't
        # catch for us, but the ASF key checker tool did.
        if keys[0]:
            sys.stderr.write("SYNTAX ERROR: %s does not start with '%s'\n"
                             % (filename, key_start))
            sys.exit(1)
        keys = keys[1:]

        if not quiet:
            logging.info("Checking %d sig(s) in %s" % (len(keys), filename))

        n = 0
        for key in keys:
            n += 1
            if not key.rstrip().endswith(key_end):
                sys.stderr.write("SYNTAX ERROR: Key %d in %s"
                                 " does not end with '%s'\n"
                                 % (n, filename, key_end))
                sys.exit(1)

            fd, fn = tempfile.mkstemp()
            os.write(fd, key_start + key)
            os.close(fd)
            verified = gpg.verify_file(open(fn, 'rb'), filename[:-4])
            os.unlink(fn)

            if verified.valid:
                good_sigs[verified.fingerprint] = True
            else:
                sys.stderr.write("BAD SIGNATURE: Key %d in %s\n"
                                 % (n, filename))
                if verified.key_id:
                    sys.stderr.write("  key id: %s\n" % verified.key_id)
                sys.exit(1)

    for id in good_sigs.keys():
        # Most potential signers have public short keyid (32-bit) collisions in
        # the https://evil32.com/ set, which has been uploaded to the
        # keyservers, so generate the long keyid (see use of LONG_KEY_ID below).
        #
        # TODO: in the future it'd be nice to use the 'gnupg' module here.
        gpg_output = subprocess.check_output(
            ['gpg', '--fixed-list-mode', '--with-colons', '--fingerprint', id],
            stderr=subprocess.STDOUT,
        )
        gpg_output = gpg_output.splitlines()

        # This code was added in r934990, but there was no comment (nor log
        # message text) explaining its purpose.  I've commented it out since
        # ignoring arbitrary warnings in a verification codepath is Bad.  If
        # you run into warnings on your machine, feel free to uncomment it,
        # but when you do so please make it match specific warnings only.
        #
        #gpg_output = "\n".join([ l for l in gpg_output.splitlines()
        #                                             if l[0:7] != 'Warning' ])

        # Parse gpg's output.  This happens to work for both gpg1 and gpg2,
        # even though their outputs are slightly different.
        #
        # See http://git.gnupg.org/cgi-bin/gitweb.cgi?p=gnupg.git;a=blob_plain;f=doc/DETAILS
        for line in gpg_output:
            parts = line.split(':')
            if parts[0] == 'pub':
                keylen = int(parts[2])
                keytype = int(parts[3])
                formatter = PUBLIC_KEY_ALGORITHMS[keytype]
                long_key_id = parts[4]
                length_and_type = formatter(keylen) + '/' + long_key_id
                del keylen, keytype, formatter, long_key_id
                break
        else:
            raise RuntimeError("Failed to determine LONG_KEY_ID")
        for line in gpg_output:
            parts = line.split(':')
            if parts[0] == 'fpr':
                fingerprint = parts[9]
                break
        else:
            raise RuntimeError("Failed to determine FINGERPRINT")
        for line in gpg_output:
            parts = line.split(':')
            if parts[0] == 'uid':
                name = parts[9].split(' <')[0]
                break
        else:
            raise RuntimeError("Failed to determine NAME")

        format_expandos = dict(
            name=name,
            length_and_type=length_and_type,
            fingerprint=_make_human_readable_fingerprint(fingerprint),
        )
        del name, length_and_type, fingerprint
        line = "   {name} [{length_and_type}] with fingerprint:"
        output.append( line.format(**format_expandos) )
        line = "    {fingerprint}"
        output.append( line.format(**format_expandos) )

    return output

def check_sigs(args):
    'Check the signatures for the release.'

    output = get_siginfo(args)
    for line in output:
        print(line)

def get_keys(args):
    'Import the LDAP-based KEYS file to gpg'
    # We use a tempfile because urlopen() objects don't have a .fileno()
    with tempfile.SpooledTemporaryFile() as fd:
        fd.write(urllib2.urlopen(KEYS).read())
        fd.flush()
        fd.seek(0)
        subprocess.check_call(['gpg', '--import'], stdin=fd)

#----------------------------------------------------------------------
# Main entry point for argument parsing and handling

def main():
    'Parse arguments, and drive the appropriate subcommand.'

    # Setup our main parser
    parser = argparse.ArgumentParser(
                            description='Create an Apache Subversion release.')
    parser.add_argument('--clean', action='store_true', default=False,
                   help='Remove any directories previously created by %(prog)s')
    parser.add_argument('--verbose', action='store_true', default=False,
                   help='Increase output verbosity')
    parser.add_argument('--base-dir', default=os.getcwd(),
                   help='''The directory in which to create needed files and
                           folders.  The default is the current working
                           directory.''')
    subparsers = parser.add_subparsers(title='subcommands')

    # Setup the parser for the build-env subcommand
    subparser = subparsers.add_parser('build-env',
                    help='''Download release prerequisistes, including autoconf,
                            libtool, and swig.''')
    subparser.set_defaults(func=build_env)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')
    subparser.add_argument('--sf-mirror', default='softlayer',
                    help='''The mirror to use for downloading files from
                            SourceForge.  If in the EU, you may want to use
                            'kent' for this value.''')
    subparser.add_argument('--use-existing', action='store_true', default=False,
                    help='''Attempt to use existing build dependencies before
                            downloading and building a private set.''')

    # Setup the parser for the roll subcommand
    subparser = subparsers.add_parser('roll',
                    help='''Create the release artifacts.''')
    subparser.set_defaults(func=roll_tarballs)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')
    subparser.add_argument('revnum', type=lambda arg: int(arg.lstrip('r')),
                    help='''The revision number to base the release on.''')
    subparser.add_argument('--branch',
                    help='''The branch to base the release on,
                            relative to ^/subversion/.''')
    subparser.add_argument('--patches',
                    help='''The path to the directory containing patches.''')

    # Setup the parser for the sign-candidates subcommand
    subparser = subparsers.add_parser('sign-candidates',
                    help='''Sign the release artifacts.''')
    subparser.set_defaults(func=sign_candidates)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')
    subparser.add_argument('--target',
                    help='''The full path to the directory containing
                            release artifacts.''')

    # Setup the parser for the post-candidates subcommand
    subparser = subparsers.add_parser('post-candidates',
                    help='''Commit candidates to the release development area
                            of the dist.apache.org repository.''')
    subparser.set_defaults(func=post_candidates)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')
    subparser.add_argument('--username',
                    help='''Username for ''' + dist_repos + '''.''')
    subparser.add_argument('--target',
                    help='''The full path to the directory containing
                            release artifacts.''')

    # Setup the parser for the create-tag subcommand
    subparser = subparsers.add_parser('create-tag',
                    help='''Create the release tag.''')
    subparser.set_defaults(func=create_tag)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')
    subparser.add_argument('revnum', type=lambda arg: int(arg.lstrip('r')),
                    help='''The revision number to base the release on.''')
    subparser.add_argument('--branch',
                    help='''The branch to base the release on,
                            relative to ^/subversion/.''')
    subparser.add_argument('--username',
                    help='''Username for ''' + secure_repos + '''.''')
    subparser.add_argument('--target',
                    help='''The full path to the directory containing
                            release artifacts.''')

    # The clean-dist subcommand
    subparser = subparsers.add_parser('clean-dist',
                    help='''Clean the distribution directory (and mirrors) of
                            all but the most recent MAJOR.MINOR release.''')
    subparser.set_defaults(func=clean_dist)
    subparser.add_argument('--dist-dir',
                    help='''The directory to clean.''')
    subparser.add_argument('--username',
                    help='''Username for ''' + dist_repos + '''.''')

    # The move-to-dist subcommand
    subparser = subparsers.add_parser('move-to-dist',
                    help='''Move candiates and signatures from the temporary
                            release dev location to the permanent distribution
                            directory.''')
    subparser.set_defaults(func=move_to_dist)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')
    subparser.add_argument('--username',
                    help='''Username for ''' + dist_repos + '''.''')

    # The write-news subcommand
    subparser = subparsers.add_parser('write-news',
                    help='''Output to stdout template text for use in the news
                            section of the Subversion website.''')
    subparser.set_defaults(func=write_news)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')

    # write-announcement
    subparser = subparsers.add_parser('write-announcement',
                    help='''Output to stdout template text for the emailed
                            release announcement.''')
    subparser.set_defaults(func=write_announcement)
    subparser.add_argument('--security', action='store_true', default=False,
                    help='''The release being announced includes security
                            fixes.''')
    subparser.add_argument('--target',
                    help='''The full path to the directory containing
                            release artifacts.''')
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')

    # write-downloads
    subparser = subparsers.add_parser('write-downloads',
                    help='''Output to stdout template text for the download
                            table for subversion.apache.org''')
    subparser.set_defaults(func=write_downloads)
    subparser.add_argument('--target',
                    help='''The full path to the directory containing
                            release artifacts.''')
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')

    # check-sigs
    subparser = subparsers.add_parser('check-sigs',
                    help='''Output to stdout the signatures collected for this
                            release''')
    subparser.set_defaults(func=check_sigs)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')
    subparser.add_argument('--target',
                    help='''The full path to the directory containing
                            release artifacts.''')

    # get-keys
    subparser = subparsers.add_parser('get-keys',
                    help='''Import committers' public keys to ~/.gpg/''')
    subparser.set_defaults(func=get_keys)

    # A meta-target
    subparser = subparsers.add_parser('clean',
                    help='''The same as the '--clean' switch, but as a
                            separate subcommand.''')
    subparser.set_defaults(func=cleanup)

    # Parse the arguments
    args = parser.parse_args()

    # first, process any global operations
    if args.clean:
        cleanup(args)

    # Set up logging
    logger = logging.getLogger()
    if args.verbose:
        logger.setLevel(logging.DEBUG)
    else:
        logger.setLevel(logging.INFO)

    # Fix up our path so we can use our installed versions
    os.environ['PATH'] = os.path.join(get_prefix(args.base_dir), 'bin') + ':' \
                                                            + os.environ['PATH']

    # Make timestamps in tarballs independent of local timezone
    os.environ['TZ'] = 'UTC'

    # finally, run the subcommand, and give it the parsed arguments
    args.func(args)


if __name__ == '__main__':
    main()
