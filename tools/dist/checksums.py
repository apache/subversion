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
#
# Check MD5 and SHA-1 and SHA-2 signatures of files, using 
# md5sums, sha1sums, and/or sha512sums as manifests
# Replaces the 'md5sum', 'sha1sum', and 'sha512sums' commands
# on systems that do not have them, such as Mac OS X or Windows.
#
# Usage: checksums.py [manifest]
#   where "os.path.basename(manifest)" is either "md5sums", "sha1sums",
#   "sha512sums"
#
# Tested with the following Python versions:
#        2.4   2.5   2.6   2.7   3.2


import os
import shutil
import sys

try:
    from hashlib import md5
    from hashlib import sha1
    from hashlib import sha512
except ImportError:
    from md5 import md5
    from sha import sha as sha1


class Digester(object):
    BUFFER_SIZE = 1024*1024

    def __init__(self, factory):
        self.factory = factory
        self.digest_size = factory().digest_size
        self.hashfunc = None

    def reset(self):
        self.hashfunc = self.factory()

    def write(self, data):
        return self.hashfunc.update(data)

    def hexdigest(self):
        return self.hashfunc.hexdigest()


def main(manipath):
    basedir, manifest = os.path.split(manipath)

    if manifest == 'md5sums':
        sink = Digester(md5)
    elif manifest == 'sha1sums':
        sink = Digester(sha1)
    elif manifest == 'sha512sums':
        sink = Digester(sha512)
    else:
        raise ValueError('The name of the digest manifest must be '
                         "'md5sums', 'sha1sums', or 'sha512sums', not '%s'" % manifest)

    # No 'with' statement in Python 2.4 ...
    stream = None
    try:
        stream = open(manipath, 'r')
        for line in stream:
            sink.reset()
            parse_digest(basedir, line.rstrip(), sink)
    finally:
        if stream is not None:
            stream.close()


def parse_digest(basedir, entry, sink):
    length = 2 * sink.digest_size
    expected = entry[:length].lower()
    filename = entry[length + 2:]

    # Still no 'with' statement in Python 2.4 ...
    source = None
    try:
        source = open(os.path.join(basedir, filename), 'rb')
        shutil.copyfileobj(source, sink, sink.BUFFER_SIZE)
        actual = sink.hexdigest().lower()
    finally:
        if source is not None:
            source.close()

    if expected != actual:
        raise ValueError('Mismatch: expected %s, actual %s:  %s'
                         % (expected, actual, filename))
    print('ok: %s  %s' % (actual, filename))


if __name__ == '__main__':
    main(sys.argv[1])
