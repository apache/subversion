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


# Some constants
repos = 'https://svn.apache.org/repos/asf/subversion'
secure_repos = 'https://svn.apache.org/repos/asf/subversion'
buildbot_repos = 'https://svn.apache.org/repos/infra/infrastructure/buildbot/aegis/buildmaster'

# Local working copies
base_dir = None  # set by main()

def get_trunk_wc_path(path=None):
    trunk_wc_path = os.path.join(base_dir, 'svn-trunk')
    if path is None: return trunk_wc_path
    return os.path.join(trunk_wc_path, path)
def get_branch_wc_path(ver, path=None):
    branch_wc_path = os.path.join(base_dir, ver.branch + '.x')
    if path is None: return branch_wc_path
    return os.path.join(branch_wc_path, path)
def get_buildbot_wc_path(path=None):
    buildbot_wc_path = os.path.join(base_dir, 'svn-buildmaster')
    if path is None: return buildbot_wc_path
    return os.path.join(buildbot_wc_path, path)

def get_trunk_url():
    return secure_repos + '/trunk'
def get_branch_url(ver):
    return secure_repos + '/branches/' + ver.branch + '.x'
def get_tag_url(ver):
    return secure_repos + '/tags/' + ver.base
def get_buildbot_url():
    return buildbot_repos

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

#----------------------------------------------------------------------
def run(cmd, dry_run=False):
    print('+ ' + ' '.join(cmd))
    if not dry_run:
        stdout = subprocess.check_output(cmd)
        print(stdout)

def run_svn(cmd, dry_run=False):
    run(['svn'] + cmd, dry_run)

def svn_commit(cmd):
    run_svn(['commit'] + cmd, dry_run=True)

def svn_checkout(*args):
    args = ['checkout'] + list(args) + ['--revision={2017-12-01}']
    run_svn(args)

#----------------------------------------------------------------------
def edit_file(path, pattern, replacement):
    print("Editing '%s'" % (path,))
    print("  pattern='%s'" % (pattern,))
    print("  replace='%s'" % (replacement,))
    old_text = open(path, 'r').read()
    new_text = re.sub(pattern, replacement, old_text)
    assert new_text != old_text
    open(path, 'w').write(new_text)

def prepend_file(path, text):
    print("Prepending to '%s'" % (path,))
    print("  text='%s'" % (text,))
    original = open(path, 'r').read()
    open(path, 'w').write(text + original)

#----------------------------------------------------------------------
def make_release_branch(ver):
    run_svn(['copy', get_trunk_url(), get_branch_url(ver),
             '-m', 'Create the ' + ver.branch + '.x release branch.'],
             dry_run=True)

#----------------------------------------------------------------------
def update_minor_ver_in_trunk(ver):
    """Change the minor version in trunk to the next (future) minor version.
    """
    trunk_wc = get_trunk_wc_path()
    trunk_url = get_trunk_url()
    svn_checkout(trunk_url, trunk_wc)

    prev_ver = Version('1.%d.0' % (ver.minor - 1,))
    next_ver = Version('1.%d.0' % (ver.minor + 1,))
    relpaths = []

    relpath = 'subversion/include/svn_version.h'
    relpaths.append(relpath)
    edit_file(get_trunk_wc_path(relpath),
              r'(#define SVN_VER_MINOR *)%s' % (ver.minor,),
              r'\g<1>%s' % (next_ver.minor,))

    relpath = 'subversion/tests/cmdline/svntest/main.py'
    relpaths.append(relpath)
    edit_file(get_trunk_wc_path(relpath),
              r'(SVN_VER_MINOR = )%s' % (ver.minor,),
              r'\g<1>%s' % (next_ver.minor,))

    relpath = 'subversion/bindings/javahl/src/org/apache/subversion/javahl/NativeResources.java'
    relpaths.append(relpath)
    try:
        # since r1817921 (just after branching 1.10)
        edit_file(get_trunk_wc_path(relpath),
                  r'SVN_VER_MINOR = %s;' % (ver.minor,),
                  r'SVN_VER_MINOR = %s;' % (next_ver.minor,))
    except:
        # before r1817921: two separate places
        edit_file(get_trunk_wc_path(relpath),
                  r'version.isAtLeast\(1, %s, 0\)' % (ver.minor,),
                  r'version.isAtLeast\(1, %s, 0\)' % (next_ver.minor,))
        edit_file(get_trunk_wc_path(relpath),
                  r'1.%s.0, but' % (ver.minor,),
                  r'1.%s.0, but' % (next_ver.minor,))

    relpath = 'CHANGES'
    relpaths.append(relpath)
    # insert at beginning of CHANGES file
    prepend_file(get_trunk_wc_path(relpath),
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
    commit_paths = [get_trunk_wc_path(p) for p in relpaths]
    svn_commit(commit_paths + ['-m', log_msg])

#----------------------------------------------------------------------
def create_status_file_on_branch(ver):
    branch_wc = get_branch_wc_path(ver)
    branch_url = get_branch_url(ver)
    svn_checkout(branch_url, branch_wc, '--depth=immediates')

    status_local_path = os.path.join(branch_wc, 'STATUS')
    text='''\
      * * * * * * * * * * * * * * * * * * * * * * * * * * * *
      *                                                     *
      *  THIS RELEASE STREAM IS OPEN FOR STABILIZATION.     *
      *                                                     *
      * * * * * * * * * * * * * * * * * * * * * * * * * * * *

This file tracks the status of releases in the %s.x line.

See http://subversion.apache.org/docs/community-guide/releasing.html#release-stabilization
for details on how release lines and voting work, what kinds of bugs can
delay a release, etc.

Status of %s:

Candidate changes:
==================


Veto-blocked changes:
=====================


Approved changes:
=================
''' % (ver.branch, ver.base)
    open(status_local_path, 'wx').write(text)
    run_svn(['add', status_local_path])
    svn_commit([status_local_path,
                '-m', '* branches/' + ver.branch + '.x/STATUS: New file.'])

#----------------------------------------------------------------------
def update_backport_bot(ver):
    print("""MANUAL STEP: Fork & edit & pull-request on GitHub:
https://github.com/apache/infrastructure-puppet/blob/deployment/modules/svnqavm_pvm_asf/manifests/init.pp
"Add new %s.x branch to list of backport branches"
""" % (ver.branch,))
    print("""Someone needs to run the 'svn checkout' manually.
The exact checkout command is documented in machines/svn-qavm2/notes.txt
in the private repository (need to use a trunk client and the svn-master.a.o
hostname).
""")

#----------------------------------------------------------------------
def update_buildbot_config(ver):
    """Add the new branch to the list of branches monitored by the buildbot
       master.
    """
    buildbot_wc = get_buildbot_wc_path()
    buildbot_url = get_buildbot_url()
    svn_checkout(buildbot_url, buildbot_wc)

    prev_ver = Version('1.%d.0' % (ver.minor - 1,))
    next_ver = Version('1.%d.0' % (ver.minor + 1,))

    relpath = 'master1/projects/subversion.conf'
    edit_file(get_buildbot_wc_path(relpath),
              r'(MINOR_LINES=\[.*%s)(\])' % (prev_ver.minor,),
              r'\1, %s\2' % (ver.minor,))

    log_msg = '''\
Subversion: start monitoring the %s branch.
''' % (ver.branch)
    commit_paths = [get_buildbot_wc_path(relpath)]
    svn_commit(commit_paths + ['-m', log_msg])

#----------------------------------------------------------------------
def steps(args):
    ver = Version('1.10.0')

    make_release_branch(ver)
    update_minor_ver_in_trunk(ver)
    create_status_file_on_branch(ver)
    update_backport_bot(ver)
    update_buildbot_config(ver)


#----------------------------------------------------------------------
# Main entry point for argument parsing and handling

def main():
    'Parse arguments, and drive the appropriate subcommand.'

    # Setup our main parser
    parser = argparse.ArgumentParser(
                            description='Create an Apache Subversion release branch.')
    parser.add_argument('--verbose', action='store_true', default=False,
                   help='Increase output verbosity')
    parser.add_argument('--base-dir', default=os.getcwd(),
                   help='''The directory in which to create needed files and
                           folders.  The default is the current working
                           directory.''')
    subparsers = parser.add_subparsers(title='subcommands')

    # Setup the parser for the build-env subcommand
    subparser = subparsers.add_parser('steps',
                    help='''Run the release-branch-creation steps.''')
    subparser.set_defaults(func=steps)

    # Parse the arguments
    args = parser.parse_args()

    global base_dir
    base_dir = args.base_dir

    # Set up logging
    logger = logging.getLogger()
    if args.verbose:
        logger.setLevel(logging.DEBUG)
    else:
        logger.setLevel(logging.INFO)

    # Make timestamps in tarballs independent of local timezone
    os.environ['TZ'] = 'UTC'

    # finally, run the subcommand, and give it the parsed arguments
    args.func(args)


if __name__ == '__main__':
    main()
