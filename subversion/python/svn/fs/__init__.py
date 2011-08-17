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

import os, uuid, stat, shutil, tempfile, datetime
import ConfigParser

import svn
import svn.hash
import svn.prop
import svn._cache
import svn.err

# Some constants
CONFIG_PRE_1_4_COMPATIBLE   = "pre-1.4-compatible"
CONFIG_PRE_1_5_COMPATIBLE   = "pre-1.5-compatible"
CONFIG_PRE_1_6_COMPATIBLE   = "pre-1.6-compatible"

PATH_FORMAT                 = "format"          # Contains format number
PATH_UUID                   = "uuid"            # Contains UUID
PATH_CURRENT                = "current"         # Youngest revision
PATH_LOCK_FILE              = "write-lock"      # Revision lock file
PATH_REVS_DIR               = "revs"            # Directory of revisions
PATH_REVPROPS_DIR           = "revprops"        # Directory of revprops
PATH_TXNS_DIR               = "transactions"    # Directory of transactions
PATH_TXN_PROTOS_DIR         = "txn-protorevs"   # Directory of proto-revs
PATH_TXN_CURRENT            = "txn-current"     # File with next txn key */
PATH_TXN_CURRENT_LOCK       = "txn-current-lock" # Lock for txn-current */
PATH_MIN_UNPACKED_REV       = "min-unpacked-rev" # Oldest revision which
                                                 # has not been packed.
PATH_CONFIG                 = "fsfs.conf"       # Configuration

FORMAT_NUMBER                       = 4

MIN_TXN_CURRENT_FORMAT              = 3
MIN_LAYOUT_FORMAT_OPTION_FORMAT     = 3
MIN_PROTOREVS_DIR_FORMAT            = 3
MIN_NO_GLOBAL_IDS_FORMAT            = 3
MIN_PACKED_FORMAT                   = 4
MIN_REP_SHARING_FORMAT              = 4
PACKED_REVPROP_SQLITE_DEV_FORMAT    = 5

_DEFAULT_MAX_FILES_PER_DIR = 1000
_TIMESTAMP_FORMAT = '%Y-%m-%dT%H:%M:%S.%fZ'

# Names of sections and options in fsfs.conf.
CONFIG_SECTION_CACHES               = "caches"
CONFIG_OPTION_FAIL_STOP             = "fail-stop"
CONFIG_SECTION_REP_SHARING          = "rep-sharing"
CONFIG_OPTION_ENABLE_REP_SHARING    = "enable-rep-sharing"

_CONFIG_DEFAULTS = {
   CONFIG_OPTION_ENABLE_REP_SHARING : 'True',
  }

_DEFAULT_CONFIG_CONTENTS = \
'''### This file controls the configuration of the FSFS filesystem.

[%s]
### These options name memcached servers used to cache internal FSFS
### data.  See http://www.danga.com/memcached/ for more information on
### memcached.  To use memcached with FSFS, run one or more memcached
### servers, and specify each of them as an option like so:
# first-server = 127.0.0.1:11211
# remote-memcached = mymemcached.corp.example.com:11212
### The option name is ignored; the value is of the form HOST:PORT.
### memcached servers can be shared between multiple repositories;
### however, if you do this, you *must* ensure that repositories have
### distinct UUIDs and paths, or else cached data from one repository
### might be used by another accidentally.  Note also that memcached has
### no authentication for reads or writes, so you must ensure that your
### memcached servers are only accessible by trusted users.

[%s]
### When a cache-related error occurs, normally Subversion ignores it
### and continues, logging an error if the server is appropriately
### configured (and ignoring it with file:// access).  To make
### Subversion never ignore cache errors, uncomment this line.
# %s = true

[%s]
### To conserve space, the filesystem can optionally avoid storing
### duplicate representations.  This comes at a slight cost in
### performance, as maintaining a database of shared representations can
### increase commit times.  The space savings are dependent upon the size
### of the repository, the number of objects it contains and the amount of
### duplication between them, usually a function of the branching and
### merging process.
###
### The following parameter enables rep-sharing in the repository.  It can
### be switched on and off at will, but for best space-saving results
### should be enabled consistently over the life of the repository.
### 'svnadmin verify' will check the rep-cache regardless of this setting.
### rep-sharing is enabled by default.
# %s = true
''' % (svn._cache.CONFIG_CATEGORY_MEMCACHED_SERVERS, CONFIG_SECTION_CACHES,
       CONFIG_OPTION_FAIL_STOP, CONFIG_SECTION_REP_SHARING,
       CONFIG_OPTION_ENABLE_REP_SHARING)


def _read_format(path):
    try:
        with open(path, 'rb') as f:
            format = int(f.readline())
            max_files_per_dir = 0
            for l in f:
                l = l.split()
                if format > MIN_LAYOUT_FORMAT_OPTION_FORMAT \
                        and l[0] == 'layout':
                    if l[1] == 'linear':
                        max_files_per_dir = 0
                    elif l[1] == 'sharded':
                        max_files_per_dir = int(l[2])
        return (format, max_files_per_dir)
    except IOError:
        # Treat an absent format file as format 1.
        return (1, 0)


def _write_format(path, format, max_files_per_dir, overwrite=True):
    assert 1 <= format and format <= FORMAT_NUMBER

    if format >= MIN_LAYOUT_FORMAT_OPTION_FORMAT:
        if max_files_per_dir:
            contents = "%d\nlayout sharded %d\n" % (format, max_files_per_dir)
        else:
            contents = "%d\nlayout linear\n" % (format, max_files_per_dir)
    else:
        format = "%d\n" % format

    if not overwrite:
        with open(path, 'wb') as f:
            f.write(contents)
    else:
        tempf = tempfile.NamedTemporaryFile(dir=os.path.dirname(path),
                                            delete=False)
        tempf.write(contents)
        tempf.close()
        os.rename(tempf.name, path)

    # And set the perms to make it read only
    os.chmod(path, stat.S_IREAD)


def _check_format(format):
    '''Return the error svn.err.FS_UNSUPPORTED_FORMAT if FS's format
    number is not the same as a format number supported by this
    Subversion.'''

    # Blacklist.  These formats may be either younger or older than
    # SVN_FS_FS__FORMAT_NUMBER, but we don't support them.
    if format is PACKED_REVPROP_SQLITE_DEV_FORMAT:
        raise svn.SubversionException(svn.err.FS_UNSUPPORTED_FORMAT,
                                      "Found format '%d', only created by " +
                                      "unreleased dev builds; see " +
                                      "http://subversion.apache.org" +
                                      "/docs/release-notes/1.7#revprop-packing"
                                       % format)

    #  We support all formats from 1-current simultaneously
    if 1 <= format <= FORMAT_NUMBER:
        return

    raise svn.SubversionException(svn.err.FS_UNSUPPORTED_FORMAT,
                "Expected FS format between '1' and '%d'; found format '%d'" %
                (FORMAT_NUMBER, format))


class FS(object):
    def __path_rev_shard(self, rev):
        assert self.max_files_per_dir
        return os.path.join(self.path, PATH_REVS_DIR,
                            '%d' % (rev // self.max_files_per_dir) )

    def __path_revprops_shard(self, rev):
        assert self.max_files_per_dir
        return os.path.join(self.path, PATH_REVPROPS_DIR,
                            '%d' % (rev // self.max_files_per_dir) )

    def __path_revprops(self, rev):
        if self.max_files_per_dir:
            return os.path.join(self.__path_revprops_shard(rev), str(rev))
        else:
            return os.path.join(self.path, PATH_REVPROPS_DIR, str(rev))
            
    def __path_rev_absolute(self, rev):
        if self.format < MIN_PACKED_FORMAT or not self.__is_packed_rev(rev):
            return self.__path_rev(rev)
        else:
            return self.__path_rev_packed(rev, "pack")

    def __path_rev(self, rev):
        assert not self.__is_packed_rev(rev)
        if self.max_files_per_dir:
            return os.path.join(self.__path_rev_shard(rev), str(rev))
        else:
            return os.path.join(self.path, PATH_REVS_DIR, str(rev))

    def __is_packed_rev(self, rev):
        return rev < self.__min_unpacked_rev

    def __read_current(self):
        with open(self.__path_current, 'rb') as f:
            return f.readline().split()

    def _get_youngest(self):
        try:
            return int(self.__read_current()[0])
        except:
            ### This is a little bug-for-bug compat.  See svnadmin tests 13
            ### about how atol() converts 'fish' to '0'.
            return 0

    def youngest_rev(self):
        self._youngest_rev_cache = self._get_youngest()
        return self._youngest_rev_cache

    def set_uuid(self, uuid_in = None):
        '''Set the UUID for the filesystem.  If UUID_IN is not given, generate
           a new one a la RFC 4122.'''

        if not uuid_in:
            uuid_in = uuid.uuid1()
        self.uuid = uuid_in

        with open(self.__path_uuid, 'wb') as f:
            f.write(str(self.uuid) + '\n')

        # We use the permissions of the 'current' file, because the 'uuid'
        # file does not exist during repository creation.
        shutil.copymode(self.__path_current, self.__path_uuid)


    def _write_config(self):
        with open(self.__path_config, 'w') as f:
            f.write(_DEFAULT_CONFIG_CONTENTS)

    def _read_config(self):
        self._config = ConfigParser.RawConfigParser(_CONFIG_DEFAULTS)
        ### We add this section manually below to get the tests running (the
        ### tests clobber the default config file).  There may be less-hacky
        ### ways of ensuring we don't error when attempting to read
        ### non-existent config values.
        self._config.add_section(CONFIG_SECTION_REP_SHARING)
        self._config.read(self.__path_config)

        if format >= MIN_REP_SHARING_FORMAT:
            self._rep_sharing_allowed = self._config.getboolean(
                                               CONFIG_SECTION_REP_SHARING,
                                               CONFIG_OPTION_ENABLE_REP_SHARING)
        else:
            self._rep_sharing_allowed = False


    def _ensure_revision_exists(self, rev):
        if not svn.is_valid_revnum(rev):
            raise svn.SubversionException(svn.err.FS_NO_SUCH_REVISION,
                                    "Invalid revision number '%ld'" % rev)

        # Did the revision exist the last time we checked the current file?
        if rev <= self._youngest_rev_cache:
            return

        # Check again
        self._youngest_rev_cache = self._get_youngest()
        if rev <= self._youngest_rev_cache:
            return

        raise svn.SubversionException(svn.err.FS_NO_SUCH_REVISION,
                                      "No such revision %ld" % rev)

    def _set_revision_proplist(self, rev, props):
        self._ensure_revision_exists(rev)

        final_path = self.__path_revprops(rev)
        tempf = tempfile.NamedTemporaryFile(dir=os.path.dirname(final_path),
                                            delete=False)
        with tempf:
            tempf.write(svn.hash.encode(props, svn.hash.TERMINATOR))

        shutil.copystat(self.__path_rev_absolute(rev), tempf.name)
        os.rename(tempf.name, final_path)


    def __update_min_unpacked_rev(self):
        assert self.format >= MIN_PACKED_FORMAT
        with open(self.__path_min_unpacked_rev, 'rb') as f:
            self.__min_unpacked_rev = int(f.readline())


    def __write_revision_zero(self):
        'Write out the zeroth revision for filesystem FS.'
        path_revision_zero = self.__path_rev(0)

        with open(path_revision_zero, 'wb') as f:
            f.write("PLAIN\nEND\nENDREP\n" +
                    "id: 0.0.r0/17\n" +
                    "type: dir\n" +
                    "count: 0\n" +
                    "text: 0 0 4 4 " +
                    "2d2977d1c96f487abe4a1e202dd03b4e\n" +
                    "cpath: /\n" +
                    "\n\n17 107\n")

        # Set the file read-only
        os.chmod(path_revision_zero, stat.S_IREAD | stat.S_IRGRP | stat.S_IROTH)

        now = datetime.datetime.utcnow()
        props = { svn.prop.REVISION_DATE : now.strftime(_TIMESTAMP_FORMAT) }
        self._set_revision_proplist(0, props)


    def _create_fs(self, config):
        'Create a new Subversion filesystem'
        self._youngest_rev_cache = 0
        self.__min_unpacked_rev = 0

        # See if compatibility with older versions was explicitly requested.
        if CONFIG_PRE_1_4_COMPATIBLE in config:
            self.format = 1
        elif CONFIG_PRE_1_5_COMPATIBLE in config:
            self.format = 2
        elif CONFIG_PRE_1_6_COMPATIBLE in config:
            self.format = 3
        else:
            self.format = FORMAT_NUMBER

        # Override the default linear layout if this is a new-enough format.
        if self.format >= MIN_LAYOUT_FORMAT_OPTION_FORMAT:
            self.max_files_per_dir = _DEFAULT_MAX_FILES_PER_DIR
        else:
            self.max_files_per_dir = 0

        # Create the revision data and revprops directories.
        if self.max_files_per_dir:
            os.makedirs(self.__path_rev_shard(0))
            os.makedirs(self.__path_revprops_shard(0))
        else:
            os.makedirs(os.path.join(self.path, PATH_REVS_DIR))
            os.makedirs(os.path.join(self.path, PATH_REVPROPS_DIR))

        # Create the transaction directory.
        os.makedirs(os.path.join(self.path, PATH_TXNS_DIR))

        # Create the protorevs directory.
        if self.format >= MIN_PROTOREVS_DIR_FORMAT:
            os.makedirs(os.path.join(self.path, PATH_TXN_PROTOS_DIR))

        # Create the 'current' file.
        if self.format >= MIN_NO_GLOBAL_IDS_FORMAT:
            current_contents = "0\n"
        else:
            current_contents = "0 1 1\n"
        with open(self.__path_current, 'wb') as f:
            f.write(current_contents)

        with open(self.__path_lock, 'wb') as f:
            f.write('')

        # Create the min unpacked rev file.
        if self.format >= MIN_PACKED_FORMAT:
            with open(self.__path_min_unpacked_rev, 'wb') as f:
                f.write('0\n')

        self.set_uuid()
        self.__write_revision_zero()

        self._write_config()
        self._read_config()

        if self.format >= MIN_TXN_CURRENT_FORMAT:
            with open(self.__path_txn_current, 'wb') as f:
                f.write('0\n')
            with open(self.__path_txn_current_lock, 'wb') as f:
                f.write('')

        # This filesystem is ready.  Stamp it with a format number.
        _write_format(self.__path_format, self.format, self.max_files_per_dir,
                      False)


    def _open_fs(self):
        'Open an existing Subvesion filesystem'
        with open(self.__path_uuid, 'rb') as f:
            self.uuid = uuid.UUID(f.readline().rstrip())

        # Read the FS format number.
        (self.format, self.max_files_per_dir) = _read_format(self.__path_format)
        _check_format(self.format)

        # Read the min unpacked revision.
        if self.format >= MIN_PACKED_FORMAT:
            self.__update_min_unpacked_rev()

        self._youngest_rev_cache = self._get_youngest()

        # Read the configuration file
        self._read_config()


    def __setup_paths(self):
        self.__path_uuid = os.path.join(self.path, PATH_UUID)
        self.__path_current = os.path.join(self.path, PATH_CURRENT)
        self.__path_format = os.path.join(self.path, PATH_FORMAT)
        self.__path_min_unpacked_rev = os.path.join(self.path,
                                                    PATH_MIN_UNPACKED_REV)
        self.__path_lock = os.path.join(self.path, PATH_LOCK_FILE)
        self.__path_config = os.path.join(self.path, PATH_CONFIG)
        self.__path_txn_current = os.path.join(self.path, PATH_TXN_CURRENT)
        self.__path_txn_current_lock = os.path.join(self.path,
                                                    PATH_TXN_CURRENT_LOCK)


    def __init__(self, path, create=False, config=None):
        self.path = path
        self.__setup_paths()

        if not config:
            config = {}

        if create:
            self._create_fs(config)
        else:
            self._open_fs()



# A few helper functions for C callers
def _create_fs(path, config=None):
    return FS(path, create=True, config=config)

def _open_fs(path):
    return FS(path)
