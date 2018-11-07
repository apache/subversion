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
#   This script is intended to automate steps in creating a new Subversion
#   minor release.

import os
import re
import sys
import logging
import subprocess
import argparse       # standard in Python 2.7

from release import Version


# Some constants
repos = 'https://svn.apache.org/repos/asf/subversion'
secure_repos = 'https://svn.apache.org/repos/asf/subversion'
buildbot_repos = 'https://svn.apache.org/repos/infra/infrastructure/buildbot/aegis/buildmaster'

# Parameters
dry_run = False

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

def run(cmd, dry_run=False):
    print('+ ' + ' '.join(cmd))
    if not dry_run:
        stdout = subprocess.check_output(cmd)
        print(stdout)
    else:
        print('  ## dry-run; not executed')

def run_svn(cmd, dry_run=False):
    run(['svn'] + cmd, dry_run)

def svn_commit(cmd):
    run_svn(['commit'] + cmd, dry_run=dry_run)

def svn_copy_branch(src, dst, message):
    args = ['copy', src, dst, '-m', message]
    run_svn(args, dry_run=dry_run)

def svn_checkout(url, wc, *args):
    args = ['checkout', url, wc] + list(args)
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
def make_release_branch(ver, revnum):
    svn_copy_branch(get_trunk_url() + '@' + (str(revnum) if revnum else ''),
                    get_branch_url(ver),
                    'Create the ' + ver.branch + '.x release branch.')

#----------------------------------------------------------------------
def update_minor_ver_in_trunk(ver, revnum):
    """Change the minor version in trunk to the next (future) minor version.
    """
    trunk_wc = get_trunk_wc_path()
    trunk_url = get_trunk_url()
    svn_checkout(trunk_url + '@' + (str(revnum) if revnum else ''),
                 trunk_wc)

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
def create_release_branch(args):
    make_release_branch(args.version, args.revnum)
    update_minor_ver_in_trunk(args.version, args.revnum)
    create_status_file_on_branch(args.version)
    update_backport_bot(args.version)
    update_buildbot_config(args.version)


#----------------------------------------------------------------------
# Main entry point for argument parsing and handling

def main():
    'Parse arguments, and drive the appropriate subcommand.'

    # Setup our main parser
    parser = argparse.ArgumentParser(
                            description='Create an Apache Subversion release branch.')
    subparsers = parser.add_subparsers(title='subcommands')

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
    subparser.add_argument('--verbose', action='store_true', default=False,
                   help='Increase output verbosity')
    subparser.add_argument('--base-dir', default=os.getcwd(),
                   help='''The directory in which to create needed files and
                           folders.  The default is the current working
                           directory.''')

    # Parse the arguments
    args = parser.parse_args()

    global base_dir, dry_run
    base_dir = args.base_dir
    dry_run = args.dry_run

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
