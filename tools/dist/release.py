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

# Futures (Python 2.5 compatibility)
from __future__ import with_statement

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


try:
    subprocess.check_output
except AttributeError:
    def check_output(cmd):
        proc = subprocess.Popen(['svn', 'list', dist_dev_url],
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        (stdout, stderr) = proc.communicate()
        rc = proc.wait()
        if rc or stderr:
            logging.error('%r failed with stderr %r', cmd, stderr)
            raise subprocess.CalledProcessError(rc, cmd)
        return stdout
    subprocess.check_output = check_output
    del check_output

# Our required / recommended release tool versions by release branch
tool_versions = {
  'trunk' : {
            'autoconf' : '2.69',
            'libtool'  : '2.4.3',
            'swig'     : '3.0.0',
  },
  '1.9' : {
            'autoconf' : '2.69',
            'libtool'  : '2.4.3',
            'swig'     : '3.0.0'
  },
  '1.8' : {
            'autoconf' : '2.69',
            'libtool'  : '2.4.3',
            'swig'     : '2.0.9',
  },
  '1.7' : {
            'autoconf' : '2.68',
            'libtool'  : '2.4.3',
            'swig'     : '2.0.4',
  },
  '1.6' : {
            'autoconf' : '2.64',
            'libtool'  : '1.5.26',
            'swig'     : '1.3.36',
  },
}

# The version that is our current recommended release
recommended_release = '1.8'

# Some constants
repos = 'http://svn.apache.org/repos/asf/subversion'
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

def run_script(verbose, script):
    if verbose:
        stdout = None
        stderr = None
    else:
        stdout = get_nullfile()
        stderr = subprocess.STDOUT

    for l in script.split('\n'):
        subprocess.check_call(l.split(), stdout=stdout, stderr=stderr)

def download_file(url, target):
    response = urllib2.urlopen(url)
    target_file = open(target, 'w')
    target_file.write(response.read())

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
            download_file(self._url, tarball)

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
    def __init__(self, base_dir, use_existing, verbose, autoconf_ver):
        RollDep.__init__(self, base_dir, use_existing, verbose)
        self.label = 'autoconf'
        self._filebase = 'autoconf-' + autoconf_ver
        self._autoconf_ver =  autoconf_ver
        self._url = 'http://ftp.gnu.org/gnu/autoconf/%s.tar.gz' % self._filebase

    def have_usable(self):
        output = self._test_version(['autoconf', '-V'])
        if not output: return False

        version = output[0].split()[-1:][0]
        return version == self._autoconf_ver

    def use_system(self):
        if not self._use_existing: return False
        return self.have_usable()


class LibtoolDep(RollDep):
    def __init__(self, base_dir, use_existing, verbose, libtool_ver):
        RollDep.__init__(self, base_dir, use_existing, verbose)
        self.label = 'libtool'
        self._filebase = 'libtool-' + libtool_ver
        self._libtool_ver = libtool_ver
        self._url = 'http://ftp.gnu.org/gnu/libtool/%s.tar.gz' % self._filebase

    def have_usable(self):
        output = self._test_version(['libtool', '--version'])
        if not output: return False

        return self._libtool_ver in output[0]

    def use_system(self):
        # We unconditionally return False here, to avoid using a borked
        # system libtool (I'm looking at you, Debian).
        return False


class SwigDep(RollDep):
    def __init__(self, base_dir, use_existing, verbose, swig_ver, sf_mirror):
        RollDep.__init__(self, base_dir, use_existing, verbose)
        self.label = 'swig'
        self._filebase = 'swig-' + swig_ver
        self._swig_ver = swig_ver
        self._url = 'http://sourceforge.net/projects/swig/files/swig/%(swig)s/%(swig)s.tar.gz/download?use_mirror=%(sf_mirror)s' % \
            { 'swig' : self._filebase,
              'sf_mirror' : sf_mirror }
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
                           tool_versions[args.version.branch]['autoconf'])
    libtool = LibtoolDep(args.base_dir, args.use_existing, args.verbose,
                         tool_versions[args.version.branch]['libtool'])
    swig = SwigDep(args.base_dir, args.use_existing, args.verbose,
                   tool_versions[args.version.branch]['swig'],
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

def roll_tarballs(args):
    'Create the release artifacts.'

    if args.branch:
        branch = args.branch
    else:
        branch = 'branches/%d.%d.x' % (args.version.major, args.version.minor)

    logging.info('Rolling release %s from branch %s@%d' % (args.version,
                                                           branch, args.revnum))

    # Ensure we've got the appropriate rolling dependencies available
    autoconf = AutoconfDep(args.base_dir, False, args.verbose,
                         tool_versions[args.version.branch]['autoconf'])
    libtool = LibtoolDep(args.base_dir, False, args.verbose,
                         tool_versions[args.version.branch]['libtool'])
    swig = SwigDep(args.base_dir, False, args.verbose,
                   tool_versions[args.version.branch]['swig'], None)

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

    # For now, just delegate to dist.sh to create the actual artifacts
    extra_args = ''
    if args.version.is_prerelease():
        if args.version.pre == 'nightly':
            extra_args = '-nightly'
        else:
            extra_args = '-%s %d' % (args.version.pre, args.version.pre_num)
    # Build Unix last to leave Unix-style svn_version.h for tagging
    logging.info('Buildling Windows tarballs')
    run_script(args.verbose, '%s/dist.sh -v %s -pr %s -r %d -zip %s'
                     % (sys.path[0], args.version.base, branch, args.revnum,
                        extra_args) )
    logging.info('Building UNIX tarballs')
    run_script(args.verbose, '%s/dist.sh -v %s -pr %s -r %d %s'
                     % (sys.path[0], args.version.base, branch, args.revnum,
                        extra_args) )

    # Move the results to the deploy directory
    logging.info('Moving artifacts and calculating checksums')
    for e in extns:
        if args.version.pre == 'nightly':
            filename = 'subversion-nightly.%s' % e
        else:
            filename = 'subversion-%s.%s' % (args.version, e)

        shutil.move(filename, get_deploydir(args.base_dir))
        filename = os.path.join(get_deploydir(args.base_dir), filename)
        m = hashlib.sha1()
        m.update(open(filename, 'r').read())
        open(filename + '.sha1', 'w').write(m.hexdigest())

    shutil.move('svn_version.h.dist',
                get_deploydir(args.base_dir) + '/' + 'svn_version.h.dist'
                + '-' + str(args.version))

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
    svn_cmd = ['svn', 'import', '-m',
               'Add %s candidate release artifacts' % args.version.base,
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

    logging.info('Creating tag for %s' % str(args.version))

    if args.branch:
        branch = secure_repos + '/' + args.branch
    else:
        branch = secure_repos + '/branches/%d.%d.x' % (args.version.major,
                                                       args.version.minor)
    target = get_target(args)

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
    subprocess.check_call(svnmucc_cmd)

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

    filenames = stdout.split('\n')
    tar_gz_archives = []
    for entry in filenames:
      if fnmatch.fnmatch(entry, 'subversion-*.tar.gz'):
        tar_gz_archives.append(entry)

    versions = []
    for archive in tar_gz_archives:
        versions.append(Version(archive))

    svnmucc_cmd = ['svnmucc', '-m', 'Remove old Subversion releases.\n' +
                   'They are still available at ' +
                   'http://archive.apache.org/dist/subversion/']
    if (args.username):
        svnmucc_cmd += ['--username', args.username]
    for k, g in itertools.groupby(sorted(versions),
                                  lambda x: (x.major, x.minor)):
        releases = list(g)
        logging.info("Saving release '%s'", releases[-1])

        for r in releases[:-1]:
            for filename in filenames:
              if fnmatch.fnmatch(filename, 'subversion-%s.*' % r):
                logging.info("Removing '%s'" % filename)
                svnmucc_cmd += ['rm', dist_release_url + '/' + filename]

    # don't redirect stdout/stderr since svnmucc might ask for a password
    subprocess.check_call(svnmucc_cmd)

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
           }

    if args.version.is_prerelease():
        template_filename = 'rc-news.ezt'
    else:
        template_filename = 'stable-news.ezt'

    template = ezt.Template()
    template.parse(get_tmplfile(template_filename).read())
    template.generate(sys.stdout, data)


def get_sha1info(args, replace=False):
    'Return a list of sha1 info for the release'

    target = get_target(args)

    sha1s = glob.glob(os.path.join(target, 'subversion*-%s*.sha1' % args.version))

    class info(object):
        pass

    sha1info = []
    for s in sha1s:
        i = info()
        # strip ".sha1"
        fname = os.path.basename(s)[:-5]
        if replace:
            # replace the version number with the [version] reference
            i.filename = Version.regex.sub('[version]', fname)
        else:
            i.filename = fname
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
        template_filename = 'stable-release-ann.ezt'

    template = ezt.Template(compress_whitespace = False)
    template.parse(get_tmplfile(template_filename).read())
    template.generate(sys.stdout, data)


def write_downloads(args):
    'Output the download section of the website.'
    sha1info = get_sha1info(args, replace=True)

    data = { 'version'              : str(args.version),
             'fileinfo'             : sha1info,
           }

    template = ezt.Template(compress_whitespace = False)
    template.parse(get_tmplfile('download.ezt').read())
    template.generate(sys.stdout, data)


#----------------------------------------------------------------------
# Validate the signatures for a release

key_start = '-----BEGIN PGP SIGNATURE-----'
fp_pattern = re.compile(r'^pub\s+(\w+\/\w+)[^\n]*\n\s+Key\sfingerprint\s=((\s+[0-9A-F]{4}){10})\nuid\s+([^<\(]+)\s')

def get_siginfo(args, quiet=False):
    'Returns a list of signatures for the release.'

    try:
        import gnupg
    except ImportError:
        import _gnupg as gnupg
    gpg = gnupg.GPG()

    target = get_target(args)

    good_sigs = {}
    fingerprints = {}
    output = []

    glob_pattern = os.path.join(target, 'subversion*-%s*.asc' % args.version)
    for filename in glob.glob(glob_pattern):
        text = open(filename).read()
        keys = text.split(key_start)

        if not quiet:
            logging.info("Checking %d sig(s) in %s" % (len(keys[1:]), filename))
        for key in keys[1:]:
            fd, fn = tempfile.mkstemp()
            os.write(fd, key_start + key)
            os.close(fd)
            verified = gpg.verify_file(open(fn, 'rb'), filename[:-4])
            os.unlink(fn)

            if verified.valid:
                good_sigs[verified.key_id[-8:]] = True
            else:
                sys.stderr.write("BAD SIGNATURE for %s\n" % filename)
                if verified.key_id:
                    sys.stderr.write("  key id: %s\n" % verified.key_id)
                sys.exit(1)

    for id in good_sigs.keys():
        gpg = subprocess.Popen(['gpg', '--fingerprint', id],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        rc = gpg.wait()
        gpg_output = gpg.stdout.read()
        if rc:
            print(gpg_output)
            sys.stderr.write("UNABLE TO GET FINGERPRINT FOR %s" % id)
            sys.exit(1)

        gpg_output = "\n".join([ l for l in gpg_output.splitlines()
                                                     if l[0:7] != 'Warning' ])

        fp = fp_pattern.match(gpg_output).groups()
        fingerprints["%s [%s] %s" % (fp[3], fp[0], fp[1])] = fp

    for entry in sorted(fingerprints.keys()):
        fp = fingerprints[entry]
        output.append("   %s [%s] with fingerprint:" % (fp[3], fp[0]))
        output.append("   %s" % fp[1])

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
    subparser.add_argument('revnum', type=int,
                    help='''The revision number to base the release on.''')
    subparser.add_argument('--branch',
                    help='''The branch to base the release on.''')

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
    subparser.add_argument('revnum', type=int,
                    help='''The revision number to base the release on.''')
    subparser.add_argument('--branch',
                    help='''The branch to base the release on.''')
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

    # finally, run the subcommand, and give it the parsed arguments
    args.func(args)


if __name__ == '__main__':
    main()
