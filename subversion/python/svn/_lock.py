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

import os, errno

import svn
import svn.err


# The system-dependent locking bits
if os.name == 'posix':
    import fcntl

    def _lock_file(f, exclusive, blocking):
        flags = 0
        if not blocking:
            flags |= fcntl.LOCK_NB
        if exclusive:
            flags |= fcntl.LOCK_EX
        else:
            flags |= fcntl.LOCK_SH

        fcntl.lockf(f, flags)

    def _unlock_file(f):
        fcntl.lockf(f, fcntl.LOCK_UN)

elif os.name == 'nt':
    # Need to implement os-level locking for Windows
    raise svn.SubversionException(0, "Locking not supported")
else:
    raise svn.SubversionException(0, "Locking not supported")



class Lock(object):
    def __init__(self, path, exclusive=True, blocking=True):
        self._path = path
        self._exclusive = exclusive
        self._blocking = blocking
        self._is_locked = False

    def acquire(self):
        ###  * Should we add a retry loop of some kind?
        self._fd = os.open(self._path, os.O_CREAT | os.O_RDWR)
        _lock_file(self._fd, self._exclusive, self._blocking)
        self._is_locked = True

    def release(self):
        if self._is_locked:
            _unlock_file(self._fd)
            os.close(self._fd)
            del self._fd
            self._is_locked = False

    def is_locked(self):
        return self._is_locked

    def __enter__(self):
        if not self._is_locked:
            self.acquire()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self._is_locked:
            self.release()

    def __del__(self):
        'Make sure we release the lock if/when it get garbage collected.'
        self.release()
