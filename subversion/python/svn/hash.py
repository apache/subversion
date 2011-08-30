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

TERMINATOR = "END"

import svn

def encode(h, terminator):
    output = []
    for k in sorted(h.keys()):
        output.append('K %d\n%s\nV %s\n%s\n' % (len(k), k, len(h[k]), h[k]))

    if terminator:
        output.append('%s\n' % terminator)

    return ''.join(output)


def read(f, terminator):
    'Return a hash as read from the file-like object F.'

    h = {}
    while True:
        line = f.readline().rstrip()

        if line == terminator:
            break

        if line[0:2] == 'K ':
            # Read length and data into a buffer.
            keylen = int(line[2:])
            keybuf = f.read(keylen)

            # Suck up extra newline after key data
            c = f.read(1)
            if c != '\n':
                raise svn.SubversionException(svn.err.MALFORMED_FILE,
                                              "Serialized hash malformed")

            # Read a val length line
            line = f.readline().rstrip()
            if line[0:2] == 'V ':
                vallen = int(line[2:])
                valbuf = f.read(vallen)

                # Suck up extra newline after val data
                c = f.read(1)
                if c != '\n':
                    raise svn.SubversionException(svn.err.MALFORMED_FILE,
                                                  "Serialized hash malformed")

                h[keybuf] = valbuf
            else:
                raise svn.SubversionException(svn.err.MALFORMED_FILE,
                                              "Serialized hash malformed")

        else:
            print line
            raise svn.SubversionException(svn.err.MALFORMED_FILE,
                                          "Serialized hash malformed")

    return h
