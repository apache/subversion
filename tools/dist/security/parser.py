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

"""
Parser for CVE/CAN advisories and patches.
"""

from __future__ import absolute_import


import os
import ast
import base64
import quopri


class Notification(object):
    """
    The complete security notification, containing multiple advisories.
    """

    class __Metadata(object):
        """
        The metadata for one advisory, with the following fields:
            tracking_id - the CVE/CAN number
            title       - a short description of the issue
            culprit     - server, client or both
            advisory    - an Advisory object
            patches     - a list of Patch objects, sorted in descending
                          order by the base version
        """

        CULPRIT_SERVER = 'server'
        CULPRIT_CLIENT = 'client'

        __culprits = ((CULPRIT_SERVER, CULPRIT_CLIENT,
                      (CULPRIT_SERVER, CULPRIT_CLIENT),
                      (CULPRIT_CLIENT, CULPRIT_SERVER)))

        def __init__(self, basedir, tracking_id,
                     title, culprit, advisory, patches):
            if culprit not in self.__culprits:
                raise ValueError('Culprit should be one of: '
                                 + ', '.join(repr(x) for x in self.__culprits))
            self.tracking_id = tracking_id
            self.title = title
            self.culprit = frozenset(tuple(culprit))
            self.advisory = Advisory(os.path.join(basedir, advisory))
            self.patches = []
            for base_version, patchfile in patches.items():
                patch = Patch(base_version, os.path.join(basedir, patchfile))
                self.patches.append(patch)
            self.patches.sort(reverse=True,
                              key=lambda x: tuple(
                                  int(q) for q in x.base_version.split('.')))

    def __init__(self, rootdir, *tracking_ids):
        """
        Create the security notification for all TRACKING_IDS.
        The advisories and patches for each tracking ID must be
        in the appropreiately named subdirectory of ROOTDIR.
        """

        self.__advisories = []
        for tid in tracking_ids:
            self.__advisories.append(self.__parse_advisory(rootdir, tid))

    def __iter__(self):
        return self.__advisories.__iter__()

    def __parse_advisory(self, rootdir, tracking_id):
        basedir = os.path.join(rootdir, tracking_id)
        with open(os.path.join(basedir, 'metadata'), 'rt') as md:
            metadata = ast.literal_eval(md.read())

        return self.__Metadata(basedir, tracking_id,
                               metadata['title'],
                               metadata['culprit'],
                               metadata['advisory'],
                               metadata['patches'])


class __Part(object):
    def __init__(self, path):
        self.__text = self.__load_file(path)

    def __load_file(self, path):
        """
        Load a file at PATH into memory as an array of lines.
        if self.TEXTMODE is True, strip whitespace from the end of
        all lines and strip empty lines from the end of the file.
        """

        text = []
        with open(path, 'rb') as src:
            for line in src:
                if self.TEXTMODE:
                    line = line.rstrip() + b'\n'
                text.append(line)

        # Strip trailing empty lines in text mode
        if self.TEXTMODE:
            while len(text) and not text[-1]:
                del text[-1]

        return b''.join(text)

    def get_text(self):
        """
        Return the raw contents.
        """

        return self.__text.decode('UTF-8')

    def get_quoted_printable(self):
        """
        Return contents encoded as quoted-printable.
        """

        return quopri.encodestring(self.__text).decode('ascii')

    BASE64_LINE_LENGTH = 64
    def get_base64(self):
        """
        Return multi-line Base64-encoded contents with the lenght
        of the lines limited to BASE64_LINE_LENGTH.
        """

        text = []
        data = base64.standard_b64encode(self.__text)
        start = 0
        end = self.BASE64_LINE_LENGTH
        while end < len(data):
            text.append(data[start:end] + b'\n')
            start += self.BASE64_LINE_LENGTH
            end += self.BASE64_LINE_LENGTH
        if start < len(data):
            text.append(data[start:] + b'\n')
        return b''.join(text).decode('ascii')


class Advisory(__Part):
    """
    In-memory container for the text of the advisory.
    """

    TEXTMODE = True


class Patch(__Part):
    """
    In-memory container for patches.
    """

    TEXTMODE = False

    def __init__(self, base_version, path):
        super(Patch, self).__init__(path)
        self.base_version = base_version

    def get_quoted_printable(self):
        raise NotImplementedError('Quoted-printable patches? Really?')
