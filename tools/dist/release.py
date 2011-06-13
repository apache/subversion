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
import subprocess
import argparse       # standard in Python 2.7


def get_prefix(base_dir):
    return os.path.join(base_dir, 'prefix')

def get_tempdir(base_dir):
    return os.path.join(base_dir, 'tempdir')

def run_script(script):
    lines = script.split('\n')
    for l in lines:
        subprocess.check_call(l.split())


def cleanup(base_dir, args):
    'Remove generated files and folders.'

    shutil.rmtree(get_prefix(base_dir), True)
    shutil.rmtree(get_tempdir(base_dir), True)


def build_env(base_dir, args):
    'Download prerequisites for a release and prepare the environment.'

    # Versions of our environment
    params = { 'autoconf'           : 'autoconf-2.68',
               'libtool'            : 'libtool-2.4',
               'swig'               : 'swig-2.0.4',
               'sf_mirror'          : args.sf_mirror,
             }

    prefix = get_prefix(base_dir)
    if os.path.exists(prefix):
        raise RuntimeError("Directory exists: '%s'" % prefix)
    os.mkdir(prefix)

    tempdir = get_tempdir(base_dir)
    if os.path.exists(tempdir):
        raise RuntimeError("Directory exists: '%s'" % tempdir)
    os.mkdir(tempdir)

    objects = { 'autoconf' : 'http://ftp.gnu.org/gnu/autoconf/%(autoconf)s.tar.gz',
                'libtool' : 'http://ftp.gnu.org/gnu/libtool/%(libtool)s.tar.gz',
                'swig' : 'http://sourceforge.net/projects/swig/files/swig/%(swig)s/%(swig)s.tar.gz/download?use_mirror=%(sf_mirror)s'
              }

    # Grab each of the prerequisite tarballs
    for key, value in objects.items():
        url = value % params
        response = urllib2.urlopen(url)
        target = open(os.path.join(get_tempdir(base_dir), key + '.tar.gz'), 'w')
        target.write(response.read())
        target.close()

        target = tarfile.open(os.path.join(get_tempdir(base_dir),
                                           key + '.tar.gz'))
        target.extractall(get_tempdir(base_dir))

    cwd = os.getcwd()

    # build autoconf
    os.chdir(os.path.join(get_tempdir(base_dir), params['autoconf']))
    run_script('''./configure --prefix=%s
                  make
                  make install''' % get_prefix(base_dir))

    # build libtool
    os.chdir(os.path.join(get_tempdir(base_dir), params['libtool']))
    run_script('''./configure --prefix=%s
                  make
                  make install''' % get_prefix(base_dir))

    # build swig
    os.chdir(os.path.join(get_tempdir(base_dir), params['swig']))
    run_script('''./configure --prefix=%s --without-pcre
                  make
                  make install''' % get_prefix(base_dir))

    os.chdir(cwd)


def roll_tarballs(base_dir, args):
    'Create the release artifacts.'


def announce(base_dir, args):
    'Write the release announcement.'


def main():
    'Parse arguments, and drive the appropriate subcommand.'

    # Setup our main parser
    parser = argparse.ArgumentParser(
                            description='Create an Apache Subversion release.')
    parser.add_argument('--clean', action='store_true', default=False,
                   help='Remove any directories previously created by %(prog)s')
    parser.add_argument('--base-dir', default=os.getcwd(),
                   help='''The directory in which to create needed files and
                           folders.  The default is the current working
                           directory.''')
    subparsers = parser.add_subparsers(title='subcommands')

    # Setup the parser for the build-env subcommand
    build_env_parser = subparsers.add_parser('build-env',
                    help='''Download release prerequisistes, including autoconf,
                            libtool, and swig.''')
    build_env_parser.set_defaults(func=build_env)
    build_env_parser.add_argument('--sf-mirror', default='softlayer',
                    help='''The mirror to use for downloading files from
                            SourceForge.  If in the EU, you may want to use
                            'kent' for this value.''')

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

    sys.path.append(os.path.join(get_prefix(args.base_dir), 'bin'))

    # finally, run the subcommand, and give it the parsed arguments
    args.func(args.base_dir, args)


if __name__ == '__main__':
    main()
