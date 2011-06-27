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

# It'd be kind of nice to use the Subversion python bindings in this script,
# but people.apache.org doesn't currently have them installed

# Stuff we need
import os
import sys
import shutil
import urllib2
import hashlib
import tarfile
import logging
import datetime
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


# Our required / recommended versions
autoconf_ver = '2.68'
libtool_ver = '2.4'
swig_ver = '2.0.4'

# Some constants
repos = 'http://svn.apache.org/repos/asf/subversion'


#----------------------------------------------------------------------
# Utility functions

def get_prefix(base_dir):
    return os.path.join(base_dir, 'prefix')

def get_tempdir(base_dir):
    return os.path.join(base_dir, 'tempdir')

def get_deploydir(base_dir):
    return os.path.join(base_dir, 'deploy')

def get_tmpldir():
    return os.path.join(os.path.abspath(sys.path[0]), 'templates')

def get_tmplfile(filename):
    try:
        return open(os.path.join(get_tmpldir(), filename))
    except IOError:
        # Hmm, we had a problem with the local version, let's try the repo
        return urllib2.urlopen(repos + '/trunk/tools/dist/templates/' + filename)

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

def split_version(version):
    parts = version.split('-')
    if len(parts) == 1:
        return (version, None)

    return (parts[0], parts[1])

#----------------------------------------------------------------------
# Cleaning up the environment

def cleanup(base_dir, args):
    'Remove generated files and folders.'
    logging.info('Cleaning')

    shutil.rmtree(get_prefix(base_dir), True)
    shutil.rmtree(get_tempdir(base_dir), True)
    shutil.rmtree(get_deploydir(base_dir), True)


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
    def __init__(self, base_dir, use_existing, verbose):
        RollDep.__init__(self, base_dir, use_existing, verbose)
        self.label = 'autoconf'
        self._filebase = 'autoconf-' + autoconf_ver
        self._url = 'http://ftp.gnu.org/gnu/autoconf/%s.tar.gz' % self._filebase

    def have_usable(self):
        output = self._test_version(['autoconf', '-V'])
        if not output: return False

        version = output[0].split()[-1:][0]
        return version == autoconf_ver

    def use_system(self):
        if not self._use_existing: return False
        return self.have_usable()


class LibtoolDep(RollDep):
    def __init__(self, base_dir, use_existing, verbose):
        RollDep.__init__(self, base_dir, use_existing, verbose)
        self.label = 'libtool'
        self._filebase = 'libtool-' + libtool_ver
        self._url = 'http://ftp.gnu.org/gnu/libtool/%s.tar.gz' % self._filebase

    def have_usable(self):
        output = self._test_version(['libtool', '--version'])
        if not output: return False

        version = output[0].split()[-1:][0]
        return version == libtool_ver

    def use_system(self):
        # We unconditionally return False here, to avoid using a borked
        # system libtool (I'm looking at you, Debian).
        return False


class SwigDep(RollDep):
    def __init__(self, base_dir, use_existing, verbose, sf_mirror):
        RollDep.__init__(self, base_dir, use_existing, verbose)
        self.label = 'swig'
        self._filebase = 'swig-' + swig_ver
        self._url = 'http://sourceforge.net/projects/swig/files/swig/%(swig)s/%(swig)s.tar.gz/download?use_mirror=%(sf_mirror)s' % \
            { 'swig' : self._filebase,
              'sf_mirror' : sf_mirror }
        self._extra_configure_flags = '--without-pcre'

    def have_usable(self):
        output = self._test_version(['swig', '-version'])
        if not output: return False

        version = output[1].split()[-1:][0]
        return version == swig_ver

    def use_system(self):
        if not self._use_existing: return False
        return self.have_usable()


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

    # iterate over our rolling deps, and build them if needed
    for dep in [autoconf, libtool, swig]:
        if dep.use_system():
            logging.info('Using system %s' % dep.label)
        else:
            dep.build()


#----------------------------------------------------------------------
# Create release artifacts

def roll_tarballs(base_dir, args):
    'Create the release artifacts.'
    extns = ['zip', 'tar.gz', 'tar.bz2']
    (version_base, version_extra) = split_version(args.version)

    if args.branch:
        branch = args.branch
    else:
        branch = version_base[:-1] + 'x'

    logging.info('Rolling release %s from branch %s@%d' % (args.version,
                                                           branch, args.revnum))

    # Ensure we've got the appropriate rolling dependencies available
    autoconf = AutoconfDep(base_dir, False, args.verbose)
    libtool = LibtoolDep(base_dir, False, args.verbose)
    swig = SwigDep(base_dir, False, args.verbose, None)

    for dep in [autoconf, libtool, swig]:
        if not dep.have_usable():
           raise RuntimeError('Cannot find usable %s' % dep.label)

    # Make sure CHANGES is sync'd
    if branch != 'trunk':
        trunk_CHANGES = '%s/trunk/CHANGES@%d' % (repos, args.revnum)
        branch_CHANGES = '%s/branches/%s/CHANGES@%d' % (repos, branch,
                                                        args.revnum)
        proc = subprocess.Popen(['svn', 'diff', '--summarize', branch_CHANGES,
                                   trunk_CHANGES],
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT)
        (stdout, stderr) = proc.communicate()
        proc.wait()

        if stdout:
            raise RuntimeError('CHANGES not synced between trunk and branch')

    # Create the output directory
    if not os.path.exists(get_deploydir(base_dir)):
        os.mkdir(get_deploydir(base_dir))

    # For now, just delegate to dist.sh to create the actual artifacts
    extra_args = ''
    if version_extra:
        if version_extra.startswith('alpha'):
            extra_args = '-alpha %s' % version_extra[5:]
        elif version_extra.startswith('beta'):
            extra_args = '-beta %s' % version_extra[4:]
        elif version_extra.startswith('rc'):
            extra_args = '-rc %s' % version_extra[2:]
        elif version_extra.startswith('nightly'):
            extra_args = '-nightly'
    logging.info('Building UNIX tarballs')
    run_script(args.verbose, '%s/dist.sh -v %s -pr %s -r %d %s'
                     % (sys.path[0], version_base, branch, args.revnum,
                        extra_args) )
    logging.info('Buildling Windows tarballs')
    run_script(args.verbose, '%s/dist.sh -v %s -pr %s -r %d -zip %s'
                     % (sys.path[0], version_base, branch, args.revnum,
                        extra_args) )

    # Move the results to the deploy directory
    logging.info('Moving artifacts and calculating checksums')
    for e in extns:
        if version_extra and version_extra.startswith('nightly'):
            filename = 'subversion-trunk.%s' % e
        else:
            filename = 'subversion-%s.%s' % (args.version, e)

        shutil.move(filename, get_deploydir(base_dir))
        filename = os.path.join(get_deploydir(base_dir), filename)
        m = hashlib.sha1()
        m.update(open(filename, 'r').read())
        open(filename + '.sha1', 'w').write(m.hexdigest())

    shutil.move('svn_version.h.dist', get_deploydir(base_dir))

    # And we're done!


#----------------------------------------------------------------------
# Post the candidate release artifacts

def post_candidates(base_dir, args):
    'Post the generated tarballs to web-accessible directory.'
    (version_base, version_extra) = split_version(args.version)

    if args.target:
        target = args.target
    else:
        target = os.path.join(os.getenv('HOME'), 'public_html', 'svn',
                              args.version)

    if args.code_name:
        dirname = args.code_name
    else:
        dirname = 'deploy'

    if not os.path.exists(target):
        os.makedirs(target)

    data = { 'version'      : args.version,
             'revnum'       : args.revnum,
             'dirname'      : dirname,
           }

    # Choose the right template text
    if version_extra:
        if version_extra.startswith('nightly'):
            template_filename = 'nightly-candidates.ezt'
        else:
            template_filename = 'rc-candidates.ezt'
    else:
        template_filename = 'stable-candidates.ezt'

    template = ezt.Template()
    template.parse(get_tmplfile(template_filename).read())
    template.generate(open(os.path.join(target, 'index.html'), 'w'), data)

    logging.info('Moving tarballs to %s' % os.path.join(target, dirname))
    if os.path.exists(os.path.join(target, dirname)):
        shutil.rmtree(os.path.join(target, dirname))
    shutil.copytree(get_deploydir(base_dir), os.path.join(target, dirname))


#----------------------------------------------------------------------
# Write announcements

def write_news(base_dir, args):
    'Write text for the Subversion website.'
    (version_base, version_extra) = split_version(args.version)

    data = { 'date' : datetime.date.today().strftime('%Y%m%d'),
             'date_pres' : datetime.date.today().strftime('%Y-%m-%d'),
             'version' : args.version,
             'version_base' : version_base[0:3],
           }

    if version_extra:
        if version_extra.startswith('alpha'):
            template_filename = 'rc-news.ezt'
    else:
        template_filename = 'stable-news.ezt'

    template = ezt.Template()
    template.parse(get_tmplfile(template_filename).read())
    template.generate(sys.stdout, data)


def write_announcement(base_dir, args):
    'Write the release announcement.'
    (version_base, version_extra) = split_version(args.version)

    data = { 'version'      : args.version,
             'sha1info'     : 'foo',
             'siginfo'      : 'bar', 
             'major-minor'  : 'boo',
           }

    if version_extra:
        if version_extra.startswith('alpha'):
            template_filename = 'rc-release-ann.ezt'
    else:
        template_filename = 'stable-release-ann.ezt'

    template = ezt.Template(compress_whitespace = False)
    template.parse(get_tmplfile(template_filename).read())
    template.generate(sys.stdout, data)


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

    # Setup the parser for the roll subcommand
    subparser = subparsers.add_parser('roll',
                    help='''Create the release artifacts.''')
    subparser.set_defaults(func=roll_tarballs)
    subparser.add_argument('version',
                    help='''The release label, such as '1.7.0-alpha1'.''')
    subparser.add_argument('revnum', type=int,
                    help='''The revision number to base the release on.''')
    subparser.add_argument('--branch',
                    help='''The branch to base the release on.''')

    # Setup the parser for the post-candidates subcommand
    subparser = subparsers.add_parser('post-candidates',
                    help='''Build the website to host the candidate tarballs.
                            The default location is somewhere in ~/public_html.
                            ''')
    subparser.set_defaults(func=post_candidates)
    subparser.add_argument('version',
                    help='''The release label, such as '1.7.0-alpha1'.''')
    subparser.add_argument('revnum', type=int,
                    help='''The revision number to base the release on.''')
    subparser.add_argument('--target',
                    help='''The full path to the destination.''')
    subparser.add_argument('--code-name',
                    help='''A whimsical name for the release, used only for
                            naming the download directory.''')

    # The write-news subcommand
    subparser = subparsers.add_parser('write-news',
                    help='''Output to stdout template text for use in the news
                            section of the Subversion website.''')
    subparser.set_defaults(func=write_news)
    subparser.add_argument('version',
                    help='''The release label, such as '1.7.0-alpha1'.''')

    subparser = subparsers.add_parser('write-announcement',
                    help='''Output to stdout template text for the emailed
                            release announcement.''')
    subparser.set_defaults(func=write_announcement)
    subparser.add_argument('version',
                    help='''The release label, such as '1.7.0-alpha1'.''')

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

    # Fix up our path so we can use our installed versions
    os.environ['PATH'] = os.path.join(get_prefix(args.base_dir), 'bin') + ':' \
                                                            + os.environ['PATH']

    # finally, run the subcommand, and give it the parsed arguments
    args.func(args.base_dir, args)


if __name__ == '__main__':
    main()
