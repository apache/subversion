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
#   This script is intended to simplify creating Subversion releases, by
#   automating as much as is possible.  It works well with our Apache
#   infrastructure, and should make rolling, posting, and announcing
#   releases dirt simple.
#
#   This script may be run on a number of platforms, but it is intended to
#   be run on people.apache.org.  As such, it may have dependencies (such
#   as Python version) which may not be common, but are guaranteed to be
#   available on people.apache.org.


# Stuff we need
import os
import sys
import shutil
import urllib2
import tarfile
import logging
import subprocess
import argparse       # standard in Python 2.7


# Our required / recommended versions
autoconf_ver = '2.68'
libtool_ver = '2.4'
swig_ver = '2.0.4'


#----------------------------------------------------------------------
# Utility functions

def get_prefix(base_dir):
    return os.path.join(base_dir, 'prefix')

def get_tempdir(base_dir):
    return os.path.join(base_dir, 'tempdir')

def get_nullfile():
    # This is certainly not cross platform
    return open('/dev/null', 'w')

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

def cleanup(base_dir, args):
    'Remove generated files and folders.'

    shutil.rmtree(get_prefix(base_dir), True)
    shutil.rmtree(get_tempdir(base_dir), True)


#----------------------------------------------------------------------
# Creating and environment to roll the release

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

        logging.info('Building ' + self._label)
        os.chdir(os.path.join(tempdir, self._filebase))
        run_script(self._verbose,
                   '''./configure --prefix=%s %s
                      make
                      make install''' % (get_prefix(self._base_dir),
                                         self._extra_configure_flags))

        os.chdir(cwd)


class AutoconfDep(RollDep):
    def __init__(self, base_dir, use_existing, verbose):
        RollDep.__init__(self, base_dir, use_existing, verbose)
        self._label = 'autoconf'
        self._filebase = 'autoconf-' + autoconf_ver
        self._url = 'http://ftp.gnu.org/gnu/autoconf/%s.tar.gz' % self._filebase

    def use_system(self):
        if not self._use_existing: return False

        output = self._test_version(['autoconf', '-V'])
        if not output: return False

        version = output[0].split()[-1:][0]
        return version == autoconf_ver


class LibtoolDep(RollDep):
    def __init__(self, base_dir, use_existing, verbose):
        RollDep.__init__(self, base_dir, use_existing, verbose)
        self._label = 'libtool'
        self._filebase = 'libtool-' + libtool_ver
        self._url = 'http://ftp.gnu.org/gnu/libtool/%s.tar.gz' % self._filebase

    def use_system(self):
        if not self._use_existing: return False

        output = self._test_version(['libtool', '--version'])
        if not output: return False

        version = output[0].split()[-1:][0]
        return version == libtool_ver


class SwigDep(RollDep):
    def __init__(self, base_dir, use_existing, verbose, sf_mirror):
        RollDep.__init__(self, base_dir, use_existing, verbose)
        self._label = 'swig'
        self._filebase = 'swig-' + swig_ver
        self._url = 'http://sourceforge.net/projects/swig/files/swig/%(swig)s/%(swig)s.tar.gz/download?use_mirror=%(sf_mirror)s' % \
            { 'swig' : self._filebase,
              'sf_mirror' : sf_mirror }
        self._extra_configure_flags = '--without-pcre'

    def use_system(self):
        if not self._use_existing: return False

        output = self._test_version(['swig', '-version'])
        if not output: return False

        version = output[1].split()[-1:][0]
        return version == swig_ver


def build_env(base_dir, args):
    'Download prerequisites for a release and prepare the environment.'
    logging.info('Creating release environment')

    try:
        os.mkdir(get_prefix(base_dir))
        os.mkdir(get_tempdir(base_dir))
    except OSError:
        if not args.use_existing:
            raise

    autoconf = AutoconfDep(base_dir, args.use_existing, args.verbose)
    libtool = LibtoolDep(base_dir, args.use_existing, args.verbose)
    swig = SwigDep(base_dir, args.use_existing, args.verbose, args.sf_mirror)

    # Check to see if the system versions of the stuff we're downloading will
    # suffice

    if autoconf.use_system():
        logging.info('Using system autoconf-' + autoconf_ver)
    else:
        autoconf.build()

    if libtool.use_system():
        logging.info('Using system libtool-' + libtool_ver)
    else:
        libtool.build()

    if swig.use_system():
        logging.info('Using system swig-' + swig_ver)
    else:
        swig.build()


def roll_tarballs(base_dir, args):
    'Create the release artifacts.'


def announce(base_dir, args):
    'Write the release announcement.'


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
    subparser.add_argument('--sf-mirror', default='softlayer',
                    help='''The mirror to use for downloading files from
                            SourceForge.  If in the EU, you may want to use
                            'kent' for this value.''')
    subparser.add_argument('--use-existing', action='store_true', default=False,
                    help='''Attempt to use existing build dependencies before
                            downloading and building a private set.''')

    # A meta-target
    subparser = subparsers.add_parser('clean',
                    help='''The same as the '--clean' switch, but as a
                            separate subcommand.''')
    subparser.set_defaults(func=cleanup)

    # Parse the arguments
    args = parser.parse_args()

    # first, process any global operations
    if args.clean:
        cleanup(args.base_dir, args)

    # Set up logging
    logger = logging.getLogger()
    if args.verbose:
        logger.setLevel(logging.DEBUG)
    else:
        logger.setLevel(logging.INFO)

    sys.path.append(os.path.join(get_prefix(args.base_dir), 'bin'))

    # finally, run the subcommand, and give it the parsed arguments
    args.func(args.base_dir, args)


if __name__ == '__main__':
    main()
