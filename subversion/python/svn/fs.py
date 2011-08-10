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

import os, uuid, shutil


# Some constants
PATH_UUID                   = "uuid"            # Contains UUID
PATH_CURRENT                = "current"         # Youngest revision


class FS(object):
    def set_uuid(self, uuid_in = None):
        '''Set the UUID for the filesystem.  If UUID_IN is not given, generate
           a new one a la RFC 4122.'''

        if not uuid_in:
            uuid_in = uuid.uuid1()
        self.uuid = uuid_in

        f = open(self.__path_uuid, 'wb')
        f.write(str(self.uuid) + '\n')
        f.close()

        # We use the permissions of the 'current' file, because the 'uuid'
        # file does not exist during repository creation.
        shutil.copymode(self.__path_current, self.__path_uuid)


    def _create_fs(self):
        'Create a new Subversion filesystem'


    def _open_fs(self):
        'Open an existing Subvesion filesystem'
        f = open(self.__path_uuid, 'rb')
        self.uuid = uuid.UUID(f.readline().rstrip())
        f.close()


    def __setup_paths(self):
        self.__path_uuid = os.path.join(self.path, PATH_UUID)
        self.__path_current = os.path.join(self.path, PATH_CURRENT)


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

def _set_uuid(fs, uuid):
    fs.set_uuid(uuid)
