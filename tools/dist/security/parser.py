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
import re
import ast
import base64
import quopri


class Notification(object):
    """
    The complete security notification, containing multiple advisories.
    """

    class Metadata(object):
        """
        The metadata for one advisory, with the following properties:
            tracking_id - the CVE/CAN number
            title       - a short description of the issue
            culprit     - server, client or both
            advisory    - a Text object with the text of the advisory
            patches     - a list of Patch objects, sorted in descending
                          order by the base version
        """

        CULPRIT_SERVER = 'server'
        CULPRIT_CLIENT = 'client'

        __CULPRITS = ((CULPRIT_SERVER, CULPRIT_CLIENT,
                      (CULPRIT_SERVER, CULPRIT_CLIENT),
                      (CULPRIT_CLIENT, CULPRIT_SERVER)))

        def __init__(self, basedir, tracking_id,
                     title, culprit, advisory, patches):
            if culprit not in self.__CULPRITS:
                raise ValueError('Culprit should be one of: '
                                 + ', '.join(repr(x) for x in self.__CULPRITS))
            if not isinstance(culprit, tuple):
                culprit = (culprit,)

            self.__tracking_id = tracking_id
            self.__title = title
            self.__culprit = frozenset(culprit)
            self.__advisory = Text(os.path.join(basedir, advisory))
            self.__patches = []
            for base_version, patchfile in patches.items():
                patch = Patch(base_version, os.path.join(basedir, patchfile))
                self.__patches.append(patch)
            self.__patches.sort(reverse=True, key=lambda x: x.base_version_key)

        @property
        def tracking_id(self):
            return self.__tracking_id

        @property
        def title(self):
            return self.__title

        @property
        def culprit(self):
            return self.__culprit

        @property
        def advisory(self):
            return self.__advisory

        @property
        def patches(self):
            return tuple(self.__patches)


    def __init__(self, rootdir, *tracking_ids):
        """
        Create the security notification for all TRACKING_IDS.
        The advisories and patches for each tracking ID must be
        in the appropreiately named subdirectory of ROOTDIR.

        The notification text assumes that RELEASE_VERSIONS will
        be published on RELEASE_DATE and that the tarballs are
        available in DIST_REVISION of the dist repository.
        """

        assert(len(tracking_ids) > 0)
        self.__advisories = []
        for tid in tracking_ids:
            self.__advisories.append(self.__parse_advisory(rootdir, tid))

    def __iter__(self):
        return self.__advisories.__iter__()

    def __len__(self):
        return len(self.__advisories)

    def __parse_advisory(self, rootdir, tracking_id):
        """
        Parse a single advisory named TRACKING_ID in ROOTDIR.
        """

        basedir = os.path.join(rootdir, tracking_id)
        with open(os.path.join(basedir, 'metadata'), 'rt') as md:
            metadata = ast.literal_eval(md.read())

        return self.Metadata(basedir, tracking_id,
                             metadata['title'],
                             metadata['culprit'],
                             metadata['advisory'],
                             metadata['patches'])

    def base_version_keys(self):
        """
        Return the set of base-version keys of all the patches.
        """

        base_version_keys = set()
        for metadata in self:
            for patch in metadata.patches:
                base_version_keys.add(patch.base_version_key)
        return base_version_keys


class __Part(object):
    def __init__(self, path, text=None):
        """
        Create a text object with contents from the file at PATH.
        If self.TEXTMODE is True, strip whitespace from the end of
        all lines and strip empty lines from the end of the file.

        Alternatively, if PATH is None, set the contents to TEXT,
        which must be convertible to bytes.
        """

        assert (path is None) is not (text is None)
        if path:
            self.__text = self.__load_file(path)
        else:
            self.__text = bytes(text)

    def __load_file(self, path):
        with open(path, 'rb') as src:
            if not self.TEXTMODE:
                return src.read()

            text = []
            for line in src:
                text.append(line.rstrip() + b'\n')

            # Strip trailing empty lines in text mode
            while len(text) and not text[-1]:
                del text[-1]
            return b''.join(text)

    @property
    def text(self):
        """
        Return the raw contents.
        """

        return self.__text.decode('UTF-8')

    @property
    def quoted_printable(self):
        """
        Return contents encoded as quoted-printable.
        """

        return quopri.encodestring(self.__text).decode('ascii')

    BASE64_LINE_LENGTH = 64

    @property
    def base64(self):
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


class Text(__Part):
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
        self.__base_version = base_version
        self.__base_version_key = self.split_version(base_version)

    @property
    def base_version(self):
        return self.__base_version

    @property
    def base_version_key(self):
        return self.__base_version_key

    @property
    def quoted_printable(self):
        raise NotImplementedError('Quoted-printable patches? Really?')


    __SPLIT_VERSION_RX = re.compile(r'^(\d+)(?:\.(\d+))?(?:\.(\d+))?(.+)?$')

    @classmethod
    def split_version(cls, version):
        """
        Splits a version number in the form n.n.n-tag into a tuple
        of its components.
        """
        def splitv(version):
            for s in cls.__SPLIT_VERSION_RX.match(version).groups():
                if s is None:
                    continue
                try:
                    n = int(s)
                except ValueError:
                    n = s
                yield n
        return tuple(splitv(version))

    @classmethod
    def join_version(cls, version_tuple):
        """
        Joins a version number tuple returned by Patch.split_version
        into a string.
        """

        def joinv(version_tuple):
            prev = None
            for n in version_tuple:
                if isinstance(n, int) and prev is not None:
                    yield '.'
                prev = n
                yield str(n)
        return ''.join(joinv(version_tuple))
