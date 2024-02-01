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
try:
  from urllib.request import urlopen  # Python 3
except:
  from urllib2 import urlopen  # Python 2
import hashlib
import tarfile
import logging
import datetime
import tempfile
import operator
import itertools
import subprocess
import argparse       # standard in Python 2.7
import io
import yaml

import backport.status

# Find ezt, using Subversion's copy, if there isn't one on the system.
try:
    import ezt
except ImportError:
    ezt_path = os.path.dirname(os.path.dirname(os.path.abspath(sys.path[0])))
    ezt_path = os.path.join(ezt_path, 'build', 'generator')
    sys.path.append(ezt_path)

    import ezt
    sys.path.remove(ezt_path)


def get_dist_metadata_file_path():
    return os.path.join(os.path.abspath(sys.path[0]), 'release-lines.yaml')

# Read the dist metadata (about release lines)
with open(get_dist_metadata_file_path(), 'r') as stream:
    dist_metadata = yaml.safe_load(stream)

# Our required / recommended release tool versions by release branch
tool_versions = dist_metadata['tool_versions']

# The version that is our current recommended release
recommended_release = dist_metadata['recommended_release']
# For clean-dist, a whitelist of artifacts to keep, by version.
supported_release_lines = frozenset(dist_metadata['supported_release_lines'])
# Long-Term Support (LTS) versions
lts_release_lines = frozenset(dist_metadata['lts_release_lines'])

# Some constants
svn_repos = os.getenv('SVN_RELEASE_SVN_REPOS',
                      'https://svn.apache.org/repos/asf/subversion')
dist_repos = os.getenv('SVN_RELEASE_DIST_REPOS',
                       'https://dist.apache.org/repos/dist')
dist_dev_url = dist_repos + '/dev/subversion'
dist_release_url = dist_repos + '/release/subversion'
dist_archive_url = 'https://archive.apache.org/dist/subversion'
buildbot_repos = os.getenv('SVN_RELEASE_BUILDBOT_REPOS',
                           'https://svn.apache.org/repos/infra/infrastructure/buildbot/aegis/buildmaster')
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

    def get_ver_tags(self, revnum):
        # These get substituted into svn_version.h
        ver_tag = ''
        ver_numtag = ''
        if self.pre == 'alpha':
            ver_tag = '" (Alpha %d)"' % self.pre_num
            ver_numtag = '"-alpha%d"' % self.pre_num
        elif self.pre == 'beta':
            ver_tag = '" (Beta %d)"' % self.pre_num
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

def get_target(args):
    "Return the location of the artifacts"
    if args.target:
        return args.target
    else:
        return os.path.join(args.base_dir, 'deploy')

def get_branch_path(args):
    if not args.branch:
        try:
            args.branch = 'branches/%d.%d.x' % (args.version.major, args.version.minor)
        except AttributeError:
            raise RuntimeError("Please specify the branch using the release version label argument (for certain subcommands) or the '--branch' global option")

    return args.branch.rstrip('/')  # canonicalize for later comparisons

def get_tmpldir():
    return os.path.join(os.path.abspath(sys.path[0]), 'templates')

def get_tmplfile(filename):
    try:
        return open(os.path.join(get_tmpldir(), filename))
    except IOError:
        # Hmm, we had a problem with the local version, let's try the repo
        return urlopen(svn_repos + '/trunk/tools/dist/templates/' + filename)

def get_nullfile():
    return open(os.path.devnull, 'w')

def run_command(cmd, verbose=True, hide_stderr=False, dry_run=False):
    if verbose:
        print("+ " + ' '.join(cmd))
    stderr = None
    if verbose:
        stdout = None
    else:
        stdout = get_nullfile()
        if hide_stderr:
            stderr = get_nullfile()

    if not dry_run:
        subprocess.check_call(cmd, stdout=stdout, stderr=stderr)
    else:
        print('  ## dry-run; not executed')

def run_script(verbose, script, hide_stderr=False):
    for l in script.split('\n'):
        run_command(l.split(), verbose, hide_stderr)

def download_file(url, target, checksum):
    """Download the file at URL to the local path TARGET.
    If CHECKSUM is a string, verify the checksum of the downloaded
    file and raise RuntimeError if it does not match.  If CHECKSUM
    is None, do not verify the downloaded file.
    """
    assert checksum is None or isinstance(checksum, str)

    response = urlopen(url)
    target_file = open(target, 'w+b')
    target_file.write(response.read())
    target_file.seek(0)
    m = hashlib.sha256()
    m.update(target_file.read())
    target_file.close()
    checksum2 = m.hexdigest()
    if checksum is not None and checksum != checksum2:
        raise RuntimeError("Checksum mismatch for '%s': "\
                           "downloaded: '%s'; expected: '%s'" % \
                           (target, checksum, checksum2))

def run_svn(cmd, verbose=True, dry_run=False, username=None):
    if (username):
        cmd[:0] = ['--username', username]
    run_command(['svn'] + cmd, verbose=verbose, dry_run=dry_run)

def run_svnmucc(cmd, verbose=True, dry_run=False, username=None):
    if (username):
        cmd[:0] = ['--username', username]
    run_command(['svnmucc'] + cmd, verbose=verbose, dry_run=dry_run)

#----------------------------------------------------------------------
def is_lts(version):
    return version.branch in lts_release_lines

def is_recommended(version):
    return version.branch == recommended_release

def get_download_anchor(version):
    if version.is_prerelease():
        return 'pre-releases'
    else:
        if is_recommended(version):
            return 'recommended-release'
        else:
            return 'supported-releases'

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
    shutil.rmtree(get_target(args), True)


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
                                stderr=subprocess.STDOUT,
                                universal_newlines=True)
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
# Create a new minor release branch

def get_trunk_wc_path(base_dir, path=None):
    trunk_wc_path = os.path.join(get_tempdir(base_dir), 'svn-trunk')
    if path is None: return trunk_wc_path
    return os.path.join(trunk_wc_path, path)

def get_buildbot_wc_path(base_dir, path=None):
    buildbot_wc_path = os.path.join(get_tempdir(base_dir), 'svn-buildmaster')
    if path is None: return buildbot_wc_path
    return os.path.join(buildbot_wc_path, path)

def get_trunk_url(revnum=None):
    return svn_repos + '/trunk' + '@' + (str(revnum) if revnum else '')

def get_branch_url(ver):
    return svn_repos + '/branches/' + ver.branch + '.x'

def get_tag_url(ver):
    return svn_repos + '/tags/' + ver.base

def edit_file(path, pattern, replacement):
    print("Editing '%s'" % (path,))
    print("  pattern='%s'" % (pattern,))
    print("  replace='%s'" % (replacement,))
    old_text = open(path, 'r').read()
    new_text = re.sub(pattern, replacement, old_text)
    assert new_text != old_text
    open(path, 'w').write(new_text)

def edit_changes_file(path, newtext):
    """Insert NEWTEXT in the 'CHANGES' file found at PATH,
       just before the first line that starts with 'Version '.
    """
    print("Prepending to '%s'" % (path,))
    print("  text='%s'" % (newtext,))
    lines = open(path, 'r').readlines()
    for i, line in enumerate(lines):
      if line.startswith('Version '):
        with open(path, 'w') as newfile:
          newfile.writelines(lines[:i])
          newfile.write(newtext)
          newfile.writelines(lines[i:])
        break

#----------------------------------------------------------------------
def make_release_branch(args):
    ver = args.version
    run_svn(['copy',
             get_trunk_url(args.revnum),
             get_branch_url(ver),
             '-m', 'Create the ' + ver.branch + '.x release branch.'],
            dry_run=args.dry_run)

#----------------------------------------------------------------------
def update_minor_ver_in_trunk(args):
    """Change the minor version in trunk to the next (future) minor version.
    """
    ver = args.version
    trunk_wc = get_trunk_wc_path(args.base_dir)
    run_svn(['checkout',
             get_trunk_url(args.revnum),
             trunk_wc])

    prev_ver = Version('1.%d.0' % (ver.minor - 1,))
    next_ver = Version('1.%d.0' % (ver.minor + 1,))
    relpaths = []

    relpath = 'subversion/include/svn_version.h'
    relpaths.append(relpath)
    edit_file(get_trunk_wc_path(args.base_dir, relpath),
              r'(#define SVN_VER_MINOR *)%s' % (ver.minor,),
              r'\g<1>%s' % (next_ver.minor,))

    relpath = 'subversion/tests/cmdline/svntest/main.py'
    relpaths.append(relpath)
    edit_file(get_trunk_wc_path(args.base_dir, relpath),
              r'(SVN_VER_MINOR = )%s' % (ver.minor,),
              r'\g<1>%s' % (next_ver.minor,))

    relpath = 'subversion/bindings/javahl/src/org/apache/subversion/javahl/NativeResources.java'
    relpaths.append(relpath)
    try:
        # since r1817921 (just after branching 1.10)
        edit_file(get_trunk_wc_path(args.base_dir, relpath),
                  r'SVN_VER_MINOR = %s;' % (ver.minor,),
                  r'SVN_VER_MINOR = %s;' % (next_ver.minor,))
    except:
        # before r1817921: two separate places
        edit_file(get_trunk_wc_path(args.base_dir, relpath),
                  r'version.isAtLeast\(1, %s, 0\)' % (ver.minor,),
                  r'version.isAtLeast\(1, %s, 0\)' % (next_ver.minor,))
        edit_file(get_trunk_wc_path(args.base_dir, relpath),
                  r'1.%s.0, but' % (ver.minor,),
                  r'1.%s.0, but' % (next_ver.minor,))

    relpath = 'CHANGES'
    relpaths.append(relpath)
    # insert at beginning of CHANGES file
    edit_changes_file(get_trunk_wc_path(args.base_dir, relpath),
                 'Version ' + next_ver.base + '\n'
                 + '(?? ??? 20XX, from /branches/' + next_ver.branch + '.x)\n'
                 + get_tag_url(next_ver) + '\n'
                 + '\n')

    log_msg = '''\
Increment the trunk version number to %s, and introduce a new CHANGES
section, following the creation of the %s.x release branch.

* subversion/include/svn_version.h,
  subversion/bindings/javahl/src/org/apache/subversion/javahl/NativeResources.java,
  subversion/tests/cmdline/svntest/main.py
    (SVN_VER_MINOR): Increment to %s.

* CHANGES: New section for %s.0.
''' % (next_ver.branch, ver.branch, next_ver.minor, next_ver.branch)
    commit_paths = [get_trunk_wc_path(args.base_dir, p) for p in relpaths]
    run_svn(['commit'] + commit_paths + ['-m', log_msg],
            dry_run=args.dry_run)

#----------------------------------------------------------------------
def create_status_file_on_branch(args):
    ver = args.version
    branch_wc = get_workdir(args.base_dir)
    branch_url = get_branch_url(ver)
    run_svn(['checkout', branch_url, branch_wc, '--depth=immediates'])

    status_local_path = os.path.join(branch_wc, 'STATUS')
    template_filename = 'STATUS.ezt'
    data = { 'major-minor'          : ver.branch,
             'major-minor-patch'    : ver.base,
           }

    template = ezt.Template(compress_whitespace=False)
    template.parse(get_tmplfile(template_filename).read())

    with open(status_local_path, 'wx') as g:
        template.generate(g, data)
    run_svn(['add', status_local_path])
    run_svn(['commit', status_local_path,
             '-m', '* branches/' + ver.branch + '.x/STATUS: New file.'],
            dry_run=args.dry_run)

#----------------------------------------------------------------------
def update_backport_bot(args):
    ver = args.version
    print("""\

*** MANUAL STEP REQUIRED ***

  Ask someone with appropriate access to add the %s.x branch
  to the backport merge bot.  See
  https://subversion.apache.org/docs/community-guide/releasing.html#backport-merge-bot

***

""" % (ver.branch,))

#----------------------------------------------------------------------
def update_buildbot_config(args):
    """Add the new branch to the list of branches monitored by the buildbot
       master.
    """
    ver = args.version
    buildbot_wc = get_buildbot_wc_path(args.base_dir)
    run_svn(['checkout', buildbot_repos, buildbot_wc])

    prev_ver = Version('1.%d.0' % (ver.minor - 1,))
    next_ver = Version('1.%d.0' % (ver.minor + 1,))

    relpath = 'master1/projects/subversion.conf'
    edit_file(get_buildbot_wc_path(args.base_dir, relpath),
              r'(MINOR_LINES=\[.*%s)(\])' % (prev_ver.minor,),
              r'\1, %s\2' % (ver.minor,))

    log_msg = '''\
Subversion: start monitoring the %s branch.
''' % (ver.branch)
    commit_paths = [get_buildbot_wc_path(args.base_dir, relpath)]
    run_svn(['commit'] + commit_paths + ['-m', log_msg],
            dry_run=args.dry_run)

#----------------------------------------------------------------------
def create_release_branch(args):
    make_release_branch(args)
    update_minor_ver_in_trunk(args)
    create_status_file_on_branch(args)
    update_backport_bot(args)
    update_buildbot_config(args)


#----------------------------------------------------------------------
def write_release_notes(args):

    # Create a skeleton release notes file from template

    template_filename = \
        'release-notes-lts.ezt' if is_lts(args.version) else 'release-notes.ezt'

    prev_ver = Version('%d.%d.0' % (args.version.major, args.version.minor - 1))
    data = { 'major-minor'          : args.version.branch,
             'previous-major-minor' : prev_ver.branch,
           }

    template = ezt.Template(compress_whitespace=False)
    template.parse(get_tmplfile(template_filename).read())

    if args.edit_html_file:
        with open(args.edit_html_file, 'w') as g:
            template.generate(g, data)
    else:
        template.generate(sys.stdout, data)

    # Add an "in progress" entry in the release notes index
    #
    index_file = os.path.normpath(args.edit_html_file + '/../index.html')
    marker = '<ul id="release-notes-list">\n'
    new_item = '<li><a href="%s.html">Subversion %s</a> – <i>in progress</i></li>\n' % (args.version.branch, args.version.branch)
    edit_file(index_file,
              re.escape(marker),
              (marker + new_item).replace('\\', r'\\'))

#----------------------------------------------------------------------
# Create release artifacts

def compare_changes(repos, branch, revision):
    mergeinfo_cmd = ['svn', 'mergeinfo', '--show-revs=eligible',
                     repos + '/trunk/CHANGES',
                     repos + '/' + branch + '/' + 'CHANGES']
    stdout = subprocess.check_output(mergeinfo_cmd, universal_newlines=True)
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
        stdout = subprocess.check_output(cat_cmd, universal_newlines=True)
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

    branch = get_branch_path(args)

    logging.info('Rolling release %s from branch %s@%d' % (args.version,
                                                           branch, args.revnum))

    check_copyright_year(svn_repos, branch, args.revnum)

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
        compare_changes(svn_repos, branch, args.revnum)

    # Ensure the output directory doesn't already exist
    if os.path.exists(get_target(args)):
        raise RuntimeError('output directory \'%s\' already exists'
                                            % get_target(args))

    os.mkdir(get_target(args))

    logging.info('Preparing working copy source')
    shutil.rmtree(get_workdir(args.base_dir), True)
    run_svn(['checkout',
             svn_repos + '/' + branch + '@' + str(args.revnum),
             get_workdir(args.base_dir)],
            verbose=args.verbose)

    # Exclude stuff we don't want in the tarball, it will not be present
    # in the exported tree.
    exclude = ['contrib', 'notes']
    if branch != 'trunk':
        exclude += ['STATUS']
        if args.version.minor < 7:
            exclude += ['packages', 'www']
    cwd = os.getcwd()
    os.chdir(get_workdir(args.base_dir))
    run_svn(['update', '--set-depth=exclude'] + exclude,
            verbose=args.verbose)
    os.chdir(cwd)

    if args.patches:
        # Assume patches are independent and can be applied in any
        # order, no need to sort.
        majmin = '%d.%d' % (args.version.major, args.version.minor)
        for name in os.listdir(args.patches):
            if name.find(majmin) != -1 and name.endswith('patch'):
                logging.info('Applying patch %s' % name)
                run_svn(['patch',
                         os.path.join(args.patches, name),
                         get_workdir(args.base_dir)],
                        verbose=args.verbose)

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
            eol_style = "--native-eol=CRLF"
        else:
            eol_style = "--native-eol=LF"
        run_svn(['export',
                 eol_style, get_workdir(args.base_dir), exportdir],
                verbose=args.verbose)

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

    def clean_pycache():
        for root, dirs, files in os.walk(exportdir):
            for dname in dirs:
                if dname == '__pycache__':
                    shutil.rmtree(os.path.join(root, dname))

    logging.info('Building Windows tarballs')
    export(windows=True)
    os.chdir(exportdir)
    transform_sql()
    # Can't use the po-update.sh in the Windows export since it has CRLF
    # line endings and won't run, so use the one in the working copy.
    run_script(args.verbose,
               '%s/tools/po/po-update.sh pot' % get_workdir(args.base_dir))
    clean_pycache()  # as with clean_autom4te, is this pointless on Windows?
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
    clean_pycache()  # without this, tarballs contain empty __pycache__ dirs
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
    # important, because it makes the gzip encoding reproducible by
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
        shutil.move(filepath, get_target(args))
        filepath = os.path.join(get_target(args), filename)
        if args.version < Version("1.11.0-alpha1"):
            # 1.10 and earlier generate *.sha1 files for compatibility reasons.
            # They are deprecated, however, so we don't publicly link them in
            # the announcements any more.
            m = hashlib.sha1()
            m.update(open(filepath, 'rb').read())
            open(filepath + '.sha1', 'w').write(m.hexdigest())
        m = hashlib.sha512()
        m.update(open(filepath, 'rb').read())
        open(filepath + '.sha512', 'w').write(m.hexdigest())

    # Nightlies do not get tagged so do not need the header
    if args.version.pre != 'nightly':
        shutil.copy(os.path.join(get_workdir(args.base_dir),
                                 'subversion', 'include', 'svn_version.h'),
                    os.path.join(get_target(args),
                                 'svn_version.h.dist-%s'
                                   % (str(args.version),)))

        # Download and "tag" the KEYS file (in case a signing key is removed
        # from a committer's LDAP profile down the road)
        basename = 'subversion-%s.KEYS' % (str(args.version),)
        filepath = os.path.join(get_tempdir(args.base_dir), basename)
        # The following code require release.py to be executed within a
        # complete wc, not a shallow wc as indicated in HACKING as one option.
        # We /could/ download COMMITTERS from /trunk if it doesn't exist...
        subprocess.check_call([os.path.dirname(__file__) + '/make-keys.sh',
                               '-c', os.path.dirname(__file__) + '/../../COMMITTERS',
                               '-o', filepath])
        shutil.move(filepath, get_target(args))

    # And we're done!

#----------------------------------------------------------------------
# Sign the candidate release artifacts

def sign_candidates(args):
    'Sign candidate artifacts in the dist development directory.'

    def sign_file(filename):
        asc_file = open(filename + '.asc', 'a')
        logging.info("Signing %s" % filename)
        if args.userid:
            proc = subprocess.check_call(['gpg', '-ba', '-u', args.userid,
                                         '-o', '-', filename], stdout=asc_file)
        else:
            proc = subprocess.check_call(['gpg', '-ba', '-o', '-', filename],
                                         stdout=asc_file)
        asc_file.close()

    target = get_target(args)

    for e in extns:
        filename = os.path.join(target, 'subversion-%s.%s' % (args.version, e))
        sign_file(filename)
        if args.version.major == 1 and args.version.minor <= 6:
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
    svn_cmd = ['import', '-m',
               'Add Subversion %s candidate release artifacts' % ver,
               '--auto-props', '--config-option',
               'config:auto-props:*.asc=svn:eol-style=native;svn:mime-type=text/plain',
               target, dist_dev_url]
    run_svn(svn_cmd, verbose=args.verbose, username=args.username)

#----------------------------------------------------------------------
# Create tag
# Bump versions on branch

def create_tag_only(args):
    'Create tag in the repository'

    target = get_target(args)

    logging.info('Creating tag for %s' % str(args.version))

    branch_url = svn_repos + '/' + get_branch_path(args)

    tag = svn_repos + '/tags/' + str(args.version)

    svnmucc_cmd = ['-m', 'Tagging release ' + str(args.version)]
    svnmucc_cmd += ['cp', str(args.revnum), branch_url, tag]
    svnmucc_cmd += ['put', os.path.join(target, 'svn_version.h.dist' + '-' +
                                        str(args.version)),
                    tag + '/subversion/include/svn_version.h']

    # don't redirect stdout/stderr since svnmucc might ask for a password
    try:
        run_svnmucc(svnmucc_cmd, verbose=args.verbose, username=args.username)
    except subprocess.CalledProcessError:
        if args.version.is_prerelease():
            logging.error("Do you need to pass --branch=trunk?")
        raise

def bump_versions_on_branch(args):
    'Bump version numbers on branch'

    logging.info('Bumping version numbers on the branch')

    branch_url = svn_repos + '/' + get_branch_path(args)

    def replace_in_place(fd, startofline, flat, spare):
        """In file object FD, replace FLAT with SPARE in the first line
        starting with regex STARTOFLINE."""

        pattern = r'^(%s)%s' % (startofline, re.escape(flat))
        repl =    r'\g<1>%s' % (spare,)
        fd.seek(0, os.SEEK_SET)
        lines = fd.readlines()
        for i, line in enumerate(lines):
            replacement = re.sub(pattern, repl, line)
            if replacement != line:
                lines[i] = replacement
                break
        else:
            raise RuntimeError("Could not replace r'%s' with r'%s' in '%s'"
                               % (pattern, repl, fd.url))

        fd.seek(0, os.SEEK_SET)
        fd.writelines(lines)
        fd.truncate() # for current callers, new value is never shorter.

    new_version = Version('%d.%d.%d' %
                          (args.version.major, args.version.minor,
                           args.version.patch + 1))

    HEAD = subprocess.check_output(['svn', 'info', '--show-item=revision',
                                    '--', branch_url],
                                   universal_newlines=True).strip()
    HEAD = int(HEAD)
    def file_object_for(relpath):
        fd = tempfile.NamedTemporaryFile(mode='w+', encoding='UTF-8')
        url = branch_url + '/' + relpath
        fd.url = url
        subprocess.check_call(['svn', 'cat', '%s@%d' % (url, HEAD)],
                              stdout=fd)
        return fd

    svn_version_h = file_object_for('subversion/include/svn_version.h')
    replace_in_place(svn_version_h, '#define SVN_VER_PATCH  *',
                     str(args.version.patch), str(new_version.patch))

    STATUS = file_object_for('STATUS')
    replace_in_place(STATUS, 'Status of ',
                     str(args.version), str(new_version))

    svn_version_h.seek(0, os.SEEK_SET)
    STATUS.seek(0, os.SEEK_SET)
    run_svnmucc(['-r', str(HEAD),
                 '-m', 'Post-release housekeeping: '
                       'bump the %s branch to %s.'
                 % (branch_url.split('/')[-1], str(new_version)),
                 'put', svn_version_h.name, svn_version_h.url,
                 'put', STATUS.name, STATUS.url,
                ],
                verbose=args.verbose, username=args.username)
    del svn_version_h
    del STATUS

def create_tag_and_bump_versions(args):
    '''Create tag in the repository and, if not a prerelease version,
       bump version numbers on the branch'''

    create_tag_only(args)

    if not args.version.is_prerelease():
        bump_versions_on_branch(args)

#----------------------------------------------------------------------
# Clean dist

def clean_dist(args):
    '''Clean the distribution directory of release artifacts of
    no-longer-supported minor lines.'''

    stdout = subprocess.check_output(['svn', 'list', dist_release_url],
                                     universal_newlines=True)

    def minor(version):
        """Return the minor release line of the parameter, which must be
        a Version object."""
        return (version.major, version.minor)

    filenames = stdout.split('\n')
    filenames = [x for x in filenames if x.startswith('subversion-')]
    versions = set(map(Version, filenames))
    to_keep = set()
    # TODO: When we release 1.A.0 GA we'll have to manually remove 1.(A-2).* artifacts.
    for line_to_keep in [minor(Version(x + ".0")) for x in supported_release_lines]:
        candidates = list(
            x for x in versions
            if minor(x) == line_to_keep
        )
        if candidates:
            to_keep.add(max(candidates))
    for i in sorted(to_keep):
        logging.info("Saving release '%s'", i)

    svnmucc_cmd = ['-m', 'Remove old Subversion releases.\n' +
                   'They are still available at ' + dist_archive_url]
    for filename in filenames:
        if Version(filename) not in to_keep:
            logging.info("Removing %r", filename)
            svnmucc_cmd += ['rm', dist_release_url + '/' + filename]

    # don't redirect stdout/stderr since svnmucc might ask for a password
    if 'rm' in svnmucc_cmd:
        run_svnmucc(svnmucc_cmd, verbose=args.verbose, username=args.username)
    else:
        logging.info("Nothing to remove")

#----------------------------------------------------------------------
# Move to dist

def move_to_dist(args):
    'Move candidate artifacts to the distribution directory.'

    stdout = subprocess.check_output(['svn', 'list', dist_dev_url],
                                     universal_newlines=True)

    filenames = []
    for entry in stdout.split('\n'):
      if fnmatch.fnmatch(entry, 'subversion-%s.*' % str(args.version)):
        filenames.append(entry)
    svnmucc_cmd = ['-m',
                   'Publish Subversion-%s.' % str(args.version)]
    svnmucc_cmd += ['rm', dist_dev_url + '/' + 'svn_version.h.dist'
                          + '-' + str(args.version)]
    for filename in filenames:
        svnmucc_cmd += ['mv', dist_dev_url + '/' + filename,
                        dist_release_url + '/' + filename]

    # don't redirect stdout/stderr since svnmucc might ask for a password
    logging.info('Moving release artifacts to %s' % dist_release_url)
    run_svnmucc(svnmucc_cmd, verbose=args.verbose, username=args.username)

#----------------------------------------------------------------------
# Write announcements

def write_news(args):
    'Write text for the Subversion website.'
    if args.news_release_date:
        release_date = datetime.datetime.strptime(args.news_release_date, '%Y-%m-%d')
    else:
        release_date = datetime.date.today()
    data = { 'date' : release_date.strftime('%Y%m%d'),
             'date_pres' : release_date.strftime('%Y-%m-%d'),
             'major-minor' : args.version.branch,
             'version' : str(args.version),
             'version_base' : args.version.base,
             'anchor': get_download_anchor(args.version),
             'is_recommended': ezt_bool(is_recommended(args.version)),
             'announcement_url': args.announcement_url,
           }

    if args.version.is_prerelease():
        template_filename = 'rc-news.ezt'
    else:
        template_filename = 'stable-news.ezt'

    template = ezt.Template()
    template.parse(get_tmplfile(template_filename).read())

    # Insert the output into an existing file if requested, else print it
    if args.edit_html_file:
        tmp_name = args.edit_html_file + '.tmp'
        with open(args.edit_html_file, 'r') as f, open(tmp_name, 'w') as g:
            inserted = False
            for line in f:
                if not inserted and line.startswith('<div class="h3" id="news-'):
                    template.generate(g, data)
                    g.write('\n')
                    inserted = True
                g.write(line)
        os.remove(args.edit_html_file)
        os.rename(tmp_name, args.edit_html_file)
    else:
        template.generate(sys.stdout, data)


def get_fileinfo(args):
    'Return a list of file info (filenames) for the release tarballs'

    target = get_target(args)

    files = glob.glob(os.path.join(target, 'subversion*-%s.*.asc' % args.version))
    files.sort()

    class info(object):
        pass

    fileinfo = []
    for f in files:
        i = info()
        # strip ".asc"
        i.filename = os.path.basename(f)[:-4]
        fileinfo.append(i)

    return fileinfo


def write_announcement(args):
    'Write the release announcement.'
    siginfo = get_siginfo(args, True)
    if not siginfo:
      raise RuntimeError("No signatures found for %s at %s" % (args.version, args.target))

    data = { 'version'              : str(args.version),
             'siginfo'              : "\n".join(siginfo) + "\n",
             'major-minor'          : args.version.branch,
             'major-minor-patch'    : args.version.base,
             'anchor'               : get_download_anchor(args.version),
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
    fileinfo = get_fileinfo(args)

    data = { 'version'              : str(args.version),
             'fileinfo'             : fileinfo,
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
    # The values are callables that produce gpg2-like key length and type
    # indications, e.g., "rsa4096" for a 4096-bit RSA key.
    1:  lambda keylen, _: 'rsa' + str(keylen),  # RSA
    3:  lambda keylen, _: 'rsa' + str(keylen),  # RSA Sign Only
    17: lambda keylen, _: 'dsa' + str(keylen),  # DSA
    # This index is not registered with IANA but is used by gpg2
    22: lambda _, parts: parts[16],             # EdDSA
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

    good_sigs = {}
    fingerprints = {}
    output = []

    for fileinfo in get_fileinfo(args):
        filename = os.path.join(get_target(args), fileinfo.filename + '.asc')
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

            fd, fn = tempfile.mkstemp(text=True)
            with os.fdopen(fd, 'w') as key_file:
              key_file.write(key_start + key)
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
            universal_newlines=True,
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
                length_and_type = formatter(keylen, parts) + '/' + long_key_id
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
    with tempfile.NamedTemporaryFile() as keysfile:
      subprocess.check_call([
          os.path.dirname(__file__) + '/make-keys.sh',
          '-c', os.path.dirname(__file__) + '/../../COMMITTERS',
          '-o', keysfile.name,
      ])
      subprocess.check_call(['gpg', '--import', keysfile.name])

def add_to_changes_dict(changes_dict, audience, section, change, revision):
    # Normalize arguments
    if audience:
        audience = audience.upper()
    if section:
        section = section.lower()
    change = change.strip()

    if not audience in changes_dict:
        changes_dict[audience] = dict()
    if not section in changes_dict[audience]:
        changes_dict[audience][section] = dict()

    changes = changes_dict[audience][section]
    if change in changes:
        changes[change].add(revision)
    else:
        changes[change] = set([revision])

def print_section(changes_dict, audience, section, title, mandatory=False):
    if audience in changes_dict:
        audience_changes = changes_dict[audience]
        if mandatory or (section in audience_changes):
            if title:
                print('  - %s:' % title)
        if section in audience_changes:
            print_changes(audience_changes[section])
        elif mandatory:
            print('    (none)')

def print_changes(changes):
    # Print in alphabetical order, so entries with the same prefix are together
    for change in sorted(changes):
        revs = changes[change]
        rev_string = 'r' + str(min(revs)) + (' et al' if len(revs) > 1 else '')
        print('    * %s (%s)' % (change, rev_string))

def write_changelog(args):
    'Write changelog, parsed from commit messages'
    # Changelog lines are lines with the following format:
    #   '['[audience[:section]]']' <message>
    # or:
    #   <message> '['[audience[:section]]']'
    # where audience = U (User-visible) or D (Developer-visible)
    #       section = general|major|minor|client|server|clientserver|other|api|bindings
    #                 (section is optional and is treated case-insensitively)
    #       message = the actual text for CHANGES
    #
    # This means the "changes label" can be used as prefix or suffix, and it
    # can also be left empty (which results in an uncategorized changes entry),
    # if the committer isn't sure where the changelog entry belongs.
    #
    # Putting [skip], [ignore], [c:skip] or [c:ignore] somewhere in the
    # log message means this commit must be ignored for Changelog processing
    # (ignored even with the --include-unlabeled-summaries option).
    #
    # If there is no changes label anywhere in the commit message, and the
    # --include-unlabeled-summaries option is used, we'll consider the summary
    # line of the commit message (= first line except if it starts with a *)
    # as an uncategorized changes entry, except if it contains "status",
    # "changes", "post-release housekeeping" or "follow-up".
    #
    # Examples:
    #   [U:major] Better interactive conflict resolution for tree conflicts
    #   ra_serf: Adjustments for serf versions with HTTP/2 support [U:minor]
    #   [U] Fix 'svn diff URL@REV WC' wrongly looks up URL@HEAD (issue #4597)
    #   Fix bug with canonicalizing Window-specific drive-relative URL []
    #   New svn_ra_list() API function [D:api]
    #   [D:bindings] JavaHL: Allow access to constructors of a couple JavaHL classes

    branch_url = svn_repos + '/' + get_branch_path(args)
    previous = svn_repos + '/' + args.previous
    include_unlabeled = args.include_unlabeled
    separator_line = ('-' * 72) + '\n'

    mergeinfo = subprocess.check_output(['svn', 'mergeinfo', '--show-revs',
                    'eligible', '--log', branch_url, previous],
                                        universal_newlines=True)
    log_messages_dict = {
        # This is a dictionary mapping revision numbers to their respective
        # log messages.  The expression in the "key:" part of the dict
        # comprehension extracts the revision number, as integer, from the
        # 'svn log' output.
        int(log_message.splitlines()[0].split()[0][1:]): log_message
        # The [1:-1] ignores the empty first and last element of the split().
        for log_message in mergeinfo.split(separator_line)[1:-1]
    }
    mergeinfo = mergeinfo.splitlines()

    separator_pattern = re.compile('^-{72}$')
    revline_pattern = re.compile(r'^r(\d+) \| [^|]+ \| [^|]+ \| \d+ lines?$')
    changes_prefix_pattern = re.compile(r'^\[(U|D)?:?([^\]]+)?\](.+)$')
    changes_suffix_pattern = re.compile(r'^(.+)\[(U|D)?:?([^\]]+)?\]$')
    # TODO: push this into backport.status as a library function
    auto_merge_pattern = \
        re.compile(r'^Merge (r\d+,? |the r\d+ group |the \S+ branch:)')

    changes_dict = dict()  # audience -> (section -> (change -> set(revision)))
    revision = -1
    got_firstline = False
    unlabeled_summary = None
    changes_ignore = False
    audience = None
    section = None
    message = None

    for line in mergeinfo:
        if separator_pattern.match(line):
            # New revision section. Reset variables.
            # If there's an unlabeled summary from a previous section, and
            # include_unlabeled is True, put it into uncategorized_changes.
            if include_unlabeled and unlabeled_summary and not changes_ignore:
                if auto_merge_pattern.match(unlabeled_summary):
                    # 1. Parse revision numbers from the first line
                    merged_revisions = [
                        int(x) for x in
                        re.compile(r'(?<=\br)\d+\b').findall(unlabeled_summary)
                    ]
                    # TODO pass each revnum in MERGED_REVISIONS through this
                    #      logic, in order to extract CHANGES_PREFIX_PATTERN
                    #      and CHANGES_SUFFIX_PATTERN lines from the trunk log
                    #      message.

                    # 2. Parse the STATUS entry
                    this_log_message = log_messages_dict[revision]
                    status_paragraph = this_log_message.split('\n\n')[2]
                    logsummary = \
                        backport.status.StatusEntry(status_paragraph).logsummary
                    add_to_changes_dict(changes_dict, None, None,
                                        ' '.join(logsummary), revision)
                else:
                    add_to_changes_dict(changes_dict, None, None,
                                        unlabeled_summary, revision)
            revision = -1
            got_firstline = False
            unlabeled_summary = None
            changes_ignore = False
            audience = None
            section = None
            message = None
            continue

        revmatch = revline_pattern.match(line)
        if revmatch and (revision == -1):
            # A revision line: get the revision number
            revision = int(revmatch.group(1))
            logging.debug('Changelog processing revision r%d' % revision)
            continue

        if line.strip() == '':
            # Skip empty / whitespace lines
            continue

        if not got_firstline:
            got_firstline = True
            if (not re.search(r'status|changes|post-release housekeeping|follow-up|^\*',
                              line, re.IGNORECASE)
                    and not changes_prefix_pattern.match(line)
                    and not changes_suffix_pattern.match(line)):
                unlabeled_summary = line

        if re.search(r'\[(c:)?(skip|ignore)\]', line, re.IGNORECASE):
            changes_ignore = True

        prefix_match = changes_prefix_pattern.match(line)
        if prefix_match:
            audience = prefix_match.group(1)
            section = prefix_match.group(2)
            message = prefix_match.group(3)
            add_to_changes_dict(changes_dict, audience, section, message, revision)

        suffix_match = changes_suffix_pattern.match(line)
        if suffix_match:
            message = suffix_match.group(1)
            audience = suffix_match.group(2)
            section = suffix_match.group(3)
            add_to_changes_dict(changes_dict, audience, section, message, revision)

    # Output the sorted changelog entries
    # 1) Uncategorized changes
    print_section(changes_dict, None, None, None)
    print()
    # 2) User-visible changes
    print(' User-visible changes:')
    print_section(changes_dict, 'U', None, None)
    print_section(changes_dict, 'U', 'general', 'General')
    print_section(changes_dict, 'U', 'major', 'Major new features')
    print_section(changes_dict, 'U', 'minor', 'Minor new features and improvements')
    print_section(changes_dict, 'U', 'client', 'Client-side bugfixes', mandatory=True)
    print_section(changes_dict, 'U', 'server', 'Server-side bugfixes', mandatory=True)
    print_section(changes_dict, 'U', 'clientserver', 'Client-side and server-side bugfixes')
    print_section(changes_dict, 'U', 'other', 'Other tool improvements and bugfixes')
    print_section(changes_dict, 'U', 'bindings', 'Bindings bugfixes', mandatory=True)
    print()
    # 3) Developer-visible changes
    print(' Developer-visible changes:')
    print_section(changes_dict, 'D', None, None)
    print_section(changes_dict, 'D', 'general', 'General', mandatory=True)
    print_section(changes_dict, 'D', 'api', 'API changes', mandatory=True)
    print_section(changes_dict, 'D', 'bindings', 'Bindings')

#----------------------------------------------------------------------
# Main entry point for argument parsing and handling

def main():
    'Parse arguments, and drive the appropriate subcommand.'

    # Setup our main parser
    parser = argparse.ArgumentParser(
                            description='Create an Apache Subversion release.')
    parser.add_argument('--clean', action='store_true', default=False,
                   help='''Remove any directories previously created by %(prog)s,
                           including the 'prefix' dir, the 'temp' dir, and the
                           default or specified target dir.''')
    parser.add_argument('--verbose', action='store_true', default=False,
                   help='Increase output verbosity')
    parser.add_argument('--base-dir', default=os.getcwd(),
                   help='''The directory in which to create needed files and
                           folders.  The default is the current working
                           directory.''')
    parser.add_argument('--target',
                   help='''The full path to the directory containing
                           release artifacts. Default: <BASE_DIR>/deploy''')
    parser.add_argument('--branch',
                   help='''The branch to base the release on,
                           as a path relative to ^/subversion/.
                           Default: 'branches/MAJOR.MINOR.x'.''')
    parser.add_argument('--username',
                   help='Username for committing to ' + svn_repos +
                        ' or ' + dist_repos + '.')
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

    # Setup the parser for the create-release-branch subcommand
    subparser = subparsers.add_parser('create-release-branch',
                    help='''Create a minor release branch: branch from trunk,
                            update version numbers on trunk, create status
                            file on branch, update backport bot,
                            update buildbot config.''')
    subparser.set_defaults(func=create_release_branch)
    subparser.add_argument('version', type=Version,
                    help='''A version number to indicate the branch, such as
                            '1.7.0' (the '.0' is required).''')
    subparser.add_argument('revnum', type=lambda arg: int(arg.lstrip('r')),
                           nargs='?', default=None,
                    help='''The trunk revision number to base the branch on.
                            Default is HEAD.''')
    subparser.add_argument('--dry-run', action='store_true', default=False,
                   help='Avoid committing any changes to repositories.')

    # Setup the parser for the create-release-branch subcommand
    subparser = subparsers.add_parser('write-release-notes',
                    help='''Write a template release-notes file.''')
    subparser.set_defaults(func=write_release_notes)
    subparser.add_argument('version', type=Version,
                    help='''A version number to indicate the branch, such as
                            '1.7.0' (the '.0' is required).''')
    subparser.add_argument('revnum', type=lambda arg: int(arg.lstrip('r')),
                           nargs='?', default=None,
                    help='''The trunk revision number to base the branch on.
                            Default is HEAD.''')
    subparser.add_argument('--edit-html-file',
                    help='''Write the template release-notes to this file,
                            and update 'index.html' in the same directory.''')
    subparser.add_argument('--dry-run', action='store_true', default=False,
                   help='Avoid committing any changes to repositories.')

    # Setup the parser for the roll subcommand
    subparser = subparsers.add_parser('roll',
                    help='''Create the release artifacts.''')
    subparser.set_defaults(func=roll_tarballs)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')
    subparser.add_argument('revnum', type=lambda arg: int(arg.lstrip('r')),
                    help='''The revision number to base the release on.''')
    subparser.add_argument('--patches',
                    help='''The path to the directory containing patches.''')

    # Setup the parser for the sign-candidates subcommand
    subparser = subparsers.add_parser('sign-candidates',
                    help='''Sign the release artifacts.''')
    subparser.set_defaults(func=sign_candidates)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')
    subparser.add_argument('--userid',
                    help='''The (optional) USER-ID specifying the key to be
                            used for signing, such as '110B1C95' (Key-ID). If
                            omitted, uses the default key.''')

    # Setup the parser for the post-candidates subcommand
    subparser = subparsers.add_parser('post-candidates',
                    help='''Commit candidates to the release development area
                            of the dist.apache.org repository.''')
    subparser.set_defaults(func=post_candidates)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')

    # Setup the parser for the create-tag subcommand
    subparser = subparsers.add_parser('create-tag',
                    help='''Create the release tag and, if not a prerelease
                            version, bump version numbers on the branch.''')
    subparser.set_defaults(func=create_tag_and_bump_versions)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')
    subparser.add_argument('revnum', type=lambda arg: int(arg.lstrip('r')),
                    help='''The revision number to base the release on.''')

    # Setup the parser for the bump-versions-on-branch subcommand
    subparser = subparsers.add_parser('bump-versions-on-branch',
                    help='''Bump version numbers on branch.''')
    subparser.set_defaults(func=bump_versions_on_branch)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')
    subparser.add_argument('revnum', type=lambda arg: int(arg.lstrip('r')),
                    help='''The revision number to base the release on.''')

    # The clean-dist subcommand
    subparser = subparsers.add_parser('clean-dist',
                    help=clean_dist.__doc__.split('\n\n')[0])
    subparser.set_defaults(func=clean_dist)
    subparser.add_argument('--dist-dir',
                    help='''The directory to clean.''')

    # The move-to-dist subcommand
    subparser = subparsers.add_parser('move-to-dist',
                    help='''Move candidates and signatures from the temporary
                            release dev location to the permanent distribution
                            directory.''')
    subparser.set_defaults(func=move_to_dist)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')

    # The write-news subcommand
    subparser = subparsers.add_parser('write-news',
                    help='''Output to stdout template text for use in the news
                            section of the Subversion website.''')
    subparser.set_defaults(func=write_news)
    subparser.add_argument('--announcement-url',
                    help='''The URL to the archived announcement email.''')
    subparser.add_argument('--news-release-date',
                    help='''The release date for the news, as YYYY-MM-DD.
                            Default: today.''')
    subparser.add_argument('--edit-html-file',
                    help='''Insert the text into this file
                            news.html, index.html).''')
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
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')

    # write-downloads
    subparser = subparsers.add_parser('write-downloads',
                    help='''Output to stdout template text for the download
                            table for subversion.apache.org''')
    subparser.set_defaults(func=write_downloads)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')

    # check-sigs
    subparser = subparsers.add_parser('check-sigs',
                    help='''Output to stdout the signatures collected for this
                            release''')
    subparser.set_defaults(func=check_sigs)
    subparser.add_argument('version', type=Version,
                    help='''The release label, such as '1.7.0-alpha1'.''')

    # get-keys
    subparser = subparsers.add_parser('get-keys',
                    help='''Import committers' public keys to ~/.gpg/''')
    subparser.set_defaults(func=get_keys)

    # A meta-target
    subparser = subparsers.add_parser('clean',
                    help='''The same as the '--clean' switch, but as a
                            separate subcommand.''')
    subparser.set_defaults(func=cleanup)

    # write-changelog
    subparser = subparsers.add_parser('write-changelog',
                    help='''Output to stdout changelog entries parsed from
                            commit messages, optionally labeled with a category
                            like [U:client], [D:api], [U], ...''')
    subparser.set_defaults(func=write_changelog)
    subparser.add_argument('previous',
                    help='''The "previous" branch or tag, relative to
                            ^/subversion/, to compare "branch" against.''')
    subparser.add_argument('--include-unlabeled-summaries',
                    dest='include_unlabeled',
                    action='store_true', default=False,
                    help='''Include summary lines that do not have a changes
                            label, unless an explicit [c:skip] or [c:ignore]
                            is part of the commit message (except if the
                            summary line contains 'STATUS', 'CHANGES',
                            'Post-release housekeeping', 'Follow-up' or starts
                            with '*').''')

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
    try:
      args.func(args)
    except AttributeError:
      parser.print_help()


if __name__ == '__main__':
    main()
