#!/usr/bin/env python3
"""\
Script to store password in plaintext in ~/.subversion/auth/svn.simple/

Useful in case Subversion is compiled without support for writing
passwords in plaintext.

Only use this script if the security implications are understood
and it is acceptable by your organization to store passwords in plaintext.

See http://subversion-staging.apache.org/faq.html#plaintext-passwords
"""

# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
# ====================================================================

import os
import sys

TERMINATOR = b"END\n"

PARSERDESCR = """\
Store plaintext password in ~/.subversion/auth/svn.simple/

Existing passwords and authentication realms can be inspected by:

    svn auth [--show-passwords]

The authentication realm can also be found using:

    svn info URL
"""

def _read_one_datum(fd, letter):
    """\
    Read a 'K <length>\\n<key>\\n' or 'V <length>\\n<value>\\n' block from
    an svn_hash_write2()-format FD.

    LETTER identifies the first letter, as a bytes object.
    """
    assert letter in {b'K', b'V'}

    # Read the letter and the space
    readletter = fd.read(1)
    if readletter != letter or fd.read(1) != b' ':
        raise ValueError('Hash file format error: Expected {} got {}'.format(letter, readletter))

    # Read the length and the newline
    line = fd.readline()
    if line[-1:] != b'\n':
        raise ValueError('Hash file format error: Expected trailing \\n')
    expected_length = int(line[:-1])

    # Read the datum and its newline
    datum = fd.read(expected_length)
    if len(datum) != expected_length:
        raise ValueError('Hash file format error: Expected length {} got {}'.format(expected_length, len(datum)))
    if fd.read(1) != b'\n':
        raise ValueError('Hash file format error: Extra data after reading {} bytes, expected \\n')

    return datum

def svn_hash_read(fd):
    """\
    Read an svn_hash_write2()-formatted file from FD, terminated by "END".

    Return a dict mapping bytes to bytes.
    """
    assert 'b' in fd.mode
    assert TERMINATOR[0] not in {b'K', b'V'}

    ret = {}
    while True:
        if fd.peek(1)[0] == TERMINATOR[0]:
            if fd.readline() != TERMINATOR:
                raise ValueError('Hash file format error: Expected file terminator {}'.format(TERMINATOR))
            if fd.peek(1):
                raise ValueError('Hash file format error: Extra content after file terminator')
            return ret

        key = _read_one_datum(fd, b'K')
        value = _read_one_datum(fd, b'V')
        ret[key] = value

def outputHash(fd, hash):
    """\
    Write a dictionary HASH to an open file descriptor FD in the
    svn_hash_write2()-format, terminated by "END\\n".

    The keys and values must have datatype 'bytes' and strings must be
    encoded using utf-8.
    """
    assert 'b' in fd.mode

    for key, val in dict.items():
        fd.write(b'K ' + bytes(str(len(key)), 'utf-8') + b'\n')
        fd.write(key + b'\n')
        fd.write(b'V ' + bytes(str(len(val)), 'utf-8') + b'\n')
        fd.write(val + b'\n')
    fd.write(TERMINATOR)

def writeHashFile(filename, hash):
    """\
    Write the dict HASH to a file named FILENAME in svn_hash_write2()
    format.
    """
    tmpFilename = filename + '.tmp'
    try:
        with open(tmpFilename, 'xb') as fd:
            outputHash(fd, dict)
            os.rename(tmpFilename, filename)
    except FileExistsError:
        print('{}: File {!r} already exist. Is the script already running?'
              .format(os.path.basename(__file__), tmpFilename),
              file=sys.stderr)
    except:
        os.remove(tmpFilename)
        raise

def main():
    # These imports are only being used by main
    import argparse
    import getpass
    import hashlib

    # Parse arguments
    parser = argparse.ArgumentParser(
        description=PARSERDESCR,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('realm', help='Server authentication real')
    parser.add_argument('-u', '--user', help='Set username')
    args = parser.parse_args()

    # The file name is the md5encoding of the realm
    m = hashlib.md5()
    m.update(args.realm.encode('utf-8'))
    authfileName = os.path.join(os.path.expanduser('~/.subversion/auth/svn.simple/'), m.hexdigest())

    # If the authfile doesn't already exist, verify that a username has been provided
    # or else prompt for it
    existingFile = os.path.exists(authfileName)
    if not existingFile and args.user is None:
        args.user = input("Enter username for realm {}: ".format(args.realm))
        if args.user == '':
            parser.exit(1, 'Username required.\n')

    # Prompt for password
    password = getpass.getpass("Enter password for realm {}: ".format(args.realm))

    # In an existing file, we add/replace password/username/passtype
    if existingFile:
        hash = svn_hash_read(open(authfileName, 'rb'))
        if args.user is not None:
            hash[b'username'] = args.user.encode('utf-8')
        hash[b'password'] = password.encode('utf-8')
        hash[b'passtype'] = b'simple'

    # For a new file, set realmstring, username, password and passtype
    else:
        hash = {
            b'svn:realmstring': args.realm.encode('utf-8'),
            b'username': args.user.encode('utf-8'),
            b'passtype': b'simple',
            b'password': password.encode('utf-8'),
        }

    del password

    # Write out the resulting file
    writeHashFile(authfileName, hash)


if __name__ == '__main__':
    main()

