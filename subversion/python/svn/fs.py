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

import svn
import svn.hash
import svn.prop

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
PATH_MIN_UNPACKED_REV       = "min-unpacked-rev" # Oldest revision which
                                                 # has not been packed.

FORMAT_NUMBER                       = 4

MIN_LAYOUT_FORMAT_OPTION_FORMAT     = 3
MIN_PROTOREVS_DIR_FORMAT            = 3
MIN_NO_GLOBAL_IDS_FORMAT            = 3
MIN_PACKED_FORMAT                   = 4

_DEFAULT_MAX_FILES_PER_DIR = 1000
_TIMESTAMP_FORMAT = '%Y-%m-%dT%H:%M:%S.%fZ'


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
        return int(self.__read_current()[0])

    def youngest_rev(self):
        self.__youngest_rev_cache = self._get_youngest()
        return self.__youngest_rev_cache

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


    def _ensure_revision_exists(self, rev):
        if not svn.is_valid_revnum(rev):
            raise svn.SubversionException(svn.err.FS_NO_SUCH_REVISION,
                                    "Invalid revision number '%ld'" % rev)

        # Did the revision exist the last time we checked the current file?
        if rev <= self.__youngest_rev_cache:
            return

        # Check again
        self.__youngest_rev_cache = self._get_youngest()
        if rev <= self.__youngest_rev_cache:
            return

        raise svn.SubversionException(svn.err.FS_NO_SUCH_REVISION,
                                      _("No such revision %ld") % rev)

    def _set_revision_proplist(self, rev, props):
        self._ensure_revision_exists(rev)

        final_path = self.__path_revprops(rev)
        (fd, fn) = tempfile.mkstemp(dir=os.path.dirname(final_path))
        os.write(fd, svn.hash.encode(props, svn.hash.TERMINATOR))
        os.close(fd)
        shutil.copystat(self.__path_rev_absolute(rev), fn)

        os.rename(fn, final_path)


    def __read_format(self):
        try:
            with open(self.__path_format, 'rb') as f:
                self.format = int(f.readline())
                self.max_files_per_dir = 0
                for l in f:
                    l = l.split()
                    if self.format > MIN_LAYOUT_FORMAT_OPTION_FORMAT \
                            and l[0] == 'layout':
                        if l[1] == 'linear':
                            self.max_files_per_dir = 0
                        elif l[1] == 'sharded':
                            self.max_files_per_dir = int(l[2])
        except IOError:
            # Treat an absent format file as format 1.
            self.format = 1
            self.max_files_per_dir = 0


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


    def _create_fs(self):
        'Create a new Subversion filesystem'
        self.__youngest_rev_cache = 0
        self.__min_unpacked_rev = 0

        # See if compatibility with older versions was explicitly requested.
        if CONFIG_PRE_1_4_COMPATIBLE in self._config:
            self.format = 1
        elif CONFIG_PRE_1_5_COMPATIBLE in self._config:
            self.format = 2
        elif CONFIG_PRE_1_6_COMPATIBLE in self._config:
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


    def _open_fs(self):
        'Open an existing Subvesion filesystem'
        with open(self.__path_uuid, 'rb') as f:
            self.uuid = uuid.UUID(f.readline().rstrip())

        self.__read_format()
        if self.format >= MIN_PACKED_FORMAT:
            self.__update_min_unpacked_rev()

        self.__youngest_rev_cache = self._get_youngest()


    def __setup_paths(self):
        self.__path_uuid = os.path.join(self.path, PATH_UUID)
        self.__path_current = os.path.join(self.path, PATH_CURRENT)
        self.__path_format = os.path.join(self.path, PATH_FORMAT)
        self.__path_min_unpacked_rev = os.path.join(self.path,
                                                    PATH_MIN_UNPACKED_REV)
        self.__path_lock = os.path.join(self.path, PATH_LOCK_FILE)


    def __init__(self, path, create=False, config=None):
        self.path = path
        self.__setup_paths()

        if config:
            self._config = config
        else:
            self._config = {}

        if create:
            self._create_fs()
        else:
            self._open_fs()



# A few helper functions for C callers
def _create_fs(path, config=None):
    return FS(path, create=True, config=config)

def _open_fs(path):
    return FS(path)
