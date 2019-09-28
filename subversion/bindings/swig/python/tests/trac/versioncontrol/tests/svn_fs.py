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
# Copyright (C) 2005 Edgewall Software
# Copyright (C) 2005 Christopher Lenz <cmlenz@gmx.de>
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    the documentation and/or other materials provided with the
#    distribution.
# 3. The name of the author may not be used to endorse or promote
#    products derived from this software without specific prior written
#    permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS
# OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
# IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
# IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os.path
import stat
import shutil
import sys
import tempfile
import unittest
from io import BytesIO

if sys.version_info[0] >= 3:
  # Python >=3.0
  from urllib.request import pathname2url
else:
  # Python <3.0
  from urllib import pathname2url

from svn import core, repos

from trac.test import TestSetup
from trac.versioncontrol import Changeset, Node
from trac.versioncontrol.svn_fs import SubversionRepository

temp_path = tempfile.mktemp("-trac-svnrepos")
REPOS_PATH = core.svn_dirent_internal_style(temp_path.encode('UTF-8'))
REPOS_URL = pathname2url(temp_path).encode('UTF-8')
del temp_path

if REPOS_URL.startswith(b"///"):
  # Don't add extra slashes if they're already present.
  # (This is important for Windows compatibility).
  REPOS_URL = b"file:" + REPOS_URL
else:
  # If the URL simply starts with '/', we need to add two
  # extra slashes to make it a valid 'file://' URL
  REPOS_URL = b"file://" + REPOS_URL

REPOS_URL = core.svn_uri_canonicalize(REPOS_URL)

class SubversionRepositoryTestSetup(TestSetup):

    def setUp(self):
        dump_path = os.path.join(os.path.split(__file__)[0], 'svnrepos.dump')
        with open(dump_path, 'rb') as dumpfile:
            # Remove the trac-svnrepos directory, so that we can
            # ensure a fresh start.
            self.tearDown()

            r = repos.svn_repos_create(REPOS_PATH, b'', b'', None, None)
            repos.svn_repos_load_fs2(r, dumpfile, BytesIO(),
                                    repos.svn_repos_load_uuid_ignore, b'',
                                    0, 0, None)

    def tearDown(self):
        if os.path.exists(REPOS_PATH):
            repos.delete(REPOS_PATH)


class SubversionRepositoryTestCase(unittest.TestCase):

    def setUp(self):
        self.repos = SubversionRepository(REPOS_PATH, None)

    def tearDown(self):
        self.repos = None

    def test_rev_navigation(self):
        self.assertEqual(0, self.repos.oldest_rev)
        self.assertEqual(None, self.repos.previous_rev(0))
        self.assertEqual(0, self.repos.previous_rev(1))
        self.assertEqual(12, self.repos.youngest_rev)
        self.assertEqual(6, self.repos.next_rev(5))
        self.assertEqual(7, self.repos.next_rev(6))
        # ...
        self.assertEqual(None, self.repos.next_rev(12))

    def test_get_node(self):
        node = self.repos.get_node(b'/trunk')
        self.assertEqual(b'trunk', node.name)
        self.assertEqual(b'/trunk', node.path)
        self.assertEqual(Node.DIRECTORY, node.kind)
        self.assertEqual(6, node.rev)
        self.assertEqual(1112381806, node.last_modified)
        node = self.repos.get_node(b'/trunk/README.txt')
        self.assertEqual(b'README.txt', node.name)
        self.assertEqual(b'/trunk/README.txt', node.path)
        self.assertEqual(Node.FILE, node.kind)
        self.assertEqual(3, node.rev)
        self.assertEqual(1112361898, node.last_modified)

    def test_get_node_specific_rev(self):
        node = self.repos.get_node(b'/trunk', 1)
        self.assertEqual(b'trunk', node.name)
        self.assertEqual(b'/trunk', node.path)
        self.assertEqual(Node.DIRECTORY, node.kind)
        self.assertEqual(1, node.rev)
        self.assertEqual(1112349652, node.last_modified)
        node = self.repos.get_node(b'/trunk/README.txt', 2)
        self.assertEqual(b'README.txt', node.name)
        self.assertEqual(b'/trunk/README.txt', node.path)
        self.assertEqual(Node.FILE, node.kind)
        self.assertEqual(2, node.rev)
        self.assertEqual(1112361138, node.last_modified)

    def test_get_dir_entries(self):
        node = self.repos.get_node(b'/trunk')
        entries = node.get_entries()
        self.assertSequenceEqual(sorted([entry.name for entry in entries]),
                                 sorted([b'README2.txt',
                                  b'dir1',
                                  b'README.txt']))

    def test_get_file_entries(self):
        node = self.repos.get_node(b'/trunk/README.txt')
        entries = node.get_entries()
        self.assertSequenceEqual([entry.name for entry in entries],
                                 [])

    def test_get_dir_content(self):
        node = self.repos.get_node(b'/trunk')
        self.assertEqual(None, node.content_length)
        self.assertEqual(None, node.content_type)
        self.assertEqual(None, node.get_content())

    def test_get_file_content(self):
        node = self.repos.get_node(b'/trunk/README.txt')
        self.assertEqual(8, node.content_length)
        self.assertEqual(b'text/plain', node.content_type)
        self.assertEqual(b'A test.\n', node.get_content().read())

    def test_get_dir_properties(self):
        f = self.repos.get_node(b'/trunk')
        props = f.get_properties()
        self.assertEqual(0, len(props))

    def test_get_file_properties(self):
        f = self.repos.get_node(b'/trunk/README.txt')
        props = f.get_properties()
        self.assertEqual(b'native', props[b'svn:eol-style'])
        self.assertEqual(b'text/plain', props[b'svn:mime-type'])

    # Revision Log / node history

    def test_get_node_history(self):
        node = self.repos.get_node(b'/trunk/README2.txt')
        history = node.get_history()
        self.assertSequenceEqual([x for x in history],
                                 [(b'trunk/README2.txt', 6, b'copy'),
                                  (b'trunk/README.txt', 3, b'edit'),
                                  (b'trunk/README.txt', 2, b'add')])

    def test_get_node_history_follow_copy(self):
        node = self.repos.get_node(b'/tags/v1/README.txt')
        history = node.get_history()
        self.assertSequenceEqual([x for x in history],
                                 [(b'tags/v1/README.txt', 7, b'copy'),
                                  (b'trunk/README.txt', 3, b'edit'),
                                  (b'trunk/README.txt', 2, b'add')])

    # Revision Log / path history

    def test_get_path_history(self):
        history = self.repos.get_path_history(b'/trunk/README2.txt', None)
        self.assertSequenceEqual([x for x in history],
                                 [(b'trunk/README2.txt', 6, b'copy'),
                                  (b'trunk/README.txt', 3, b'unknown')])

    def test_get_path_history_copied_file(self):
        history = self.repos.get_path_history(b'/tags/v1/README.txt', None)
        self.assertSequenceEqual([x for x in history],
                                 [(b'tags/v1/README.txt', 7, b'copy'),
                                  (b'trunk/README.txt', 3, b'unknown')])

    def test_get_path_history_copied_dir(self):
        history = self.repos.get_path_history(b'/branches/v1x', None)
        self.assertSequenceEqual([x for x in history],
                                 [(b'branches/v1x', 12, b'copy'),
                                  (b'tags/v1.1', 10, b'unknown'),
                                  (b'branches/v1x', 11, b'delete'),
                                  (b'branches/v1x', 9, b'edit'),
                                  (b'branches/v1x', 8, b'copy'),
                                  (b'tags/v1', 7, b'unknown')])

    # Diffs

    def _cmp_diff(self, expected, got):
        if expected[0]:
            old = self.repos.get_node(*expected[0])
            self.assertEqual((old.path, old.rev), (got[0].path, got[0].rev))
        if expected[1]:
            new = self.repos.get_node(*expected[1])
            self.assertEqual((new.path, new.rev), (got[1].path, got[1].rev))
        self.assertEqual(expected[2], (got[2], got[3]))

    def test_diff_file_different_revs(self):
        diffs = self.repos.get_deltas(b'trunk/README.txt', 2, b'trunk/README.txt', 3)
        self._cmp_diff(((b'trunk/README.txt', 2),
                        (b'trunk/README.txt', 3),
                        (Node.FILE, Changeset.EDIT)), next(diffs))
        self.assertRaises(StopIteration, lambda: next(diffs))

    def test_diff_file_different_files(self):
        diffs = self.repos.get_deltas(b'branches/v1x/README.txt', 12,
                                      b'branches/v1x/README2.txt', 12)
        self._cmp_diff(((b'branches/v1x/README.txt', 12),
                        (b'branches/v1x/README2.txt', 12),
                        (Node.FILE, Changeset.EDIT)), next(diffs))
        self.assertRaises(StopIteration, lambda: next(diffs))

    def test_diff_file_no_change(self):
        diffs = self.repos.get_deltas(b'trunk/README.txt', 7,
                                      b'tags/v1/README.txt', 7)
        self.assertRaises(StopIteration, lambda: next(diffs))

    def test_diff_dir_different_revs(self):
        diffs = self.repos.get_deltas(b'trunk', 4, b'trunk', 8)
        expected = [
          (None, (b'trunk/README2.txt', 6),
           (Node.FILE, Changeset.ADD)),
          (None, (b'trunk/dir1/dir2', 8),
           (Node.DIRECTORY, Changeset.ADD)),
          (None, (b'trunk/dir1/dir3', 8),
           (Node.DIRECTORY, Changeset.ADD)),
          ((b'trunk/dir2', 4), None,
           (Node.DIRECTORY, Changeset.DELETE)),
          ((b'trunk/dir3', 4), None,
           (Node.DIRECTORY, Changeset.DELETE)),
        ]
        actual = [next(diffs) for i in range(5)]
        actual = sorted(actual,
                        key=lambda diff: ((diff[0] or diff[1]).path,
                                          (diff[0] or diff[1]).rev))
        self.assertEqual(len(expected), len(actual))
        for e,a in zip(expected, actual):
          self._cmp_diff(e,a)
        self.assertRaises(StopIteration, lambda: next(diffs))

    def test_diff_dir_different_dirs(self):
        diffs = self.repos.get_deltas(b'trunk', 1, b'branches/v1x', 12)
        expected = [
          (None, (b'branches/v1x/README.txt', 12),
           (Node.FILE, Changeset.ADD)),
          (None, (b'branches/v1x/README2.txt', 12),
           (Node.FILE, Changeset.ADD)),
          (None, (b'branches/v1x/dir1', 12),
           (Node.DIRECTORY, Changeset.ADD)),
          (None, (b'branches/v1x/dir1/dir2', 12),
           (Node.DIRECTORY, Changeset.ADD)),
          (None, (b'branches/v1x/dir1/dir3', 12),
           (Node.DIRECTORY, Changeset.ADD)),
        ]
        actual = [next(diffs) for i in range(5)]
        actual = sorted(actual, key=lambda diff: (diff[1].path, diff[1].rev))
        # for e,a in zip(expected, actual):
        #   t.write("%r\n" % (e,))
        #   t.write("%r\n" % ((None, (a[1].path, a[1].rev), (a[2], a[3])),) )
        #   t.write('\n')
        self.assertEqual(len(expected), len(actual))
        for e,a in zip(expected, actual):
          self._cmp_diff(e,a)
        self.assertRaises(StopIteration, lambda: next(diffs))

    def test_diff_dir_no_change(self):
        diffs = self.repos.get_deltas(b'trunk', 7,
                                      b'tags/v1', 7)
        self.assertRaises(StopIteration, lambda: next(diffs))

    # Changesets

    def test_changeset_repos_creation(self):
        chgset = self.repos.get_changeset(0)
        self.assertEqual(0, chgset.rev)
        self.assertEqual(None, chgset.message)
        self.assertEqual(None, chgset.author)
        self.assertEqual(1112349461, chgset.date)
        self.assertRaises(StopIteration, lambda: next(chgset.get_changes()))

    def test_changeset_added_dirs(self):
        chgset = self.repos.get_changeset(1)
        self.assertEqual(1, chgset.rev)
        self.assertEqual(b'Initial directory layout.', chgset.message)
        self.assertEqual(b'john', chgset.author)
        self.assertEqual(1112349652, chgset.date)

        changes = chgset.get_changes()
        self.assertSequenceEqual(sorted([x for x in changes]),
          sorted([(b'trunk', Node.DIRECTORY, Changeset.ADD, None, -1),
                  (b'branches', Node.DIRECTORY, Changeset.ADD, None, -1),
                  (b'tags', Node.DIRECTORY, Changeset.ADD, None, -1)]))

    def test_changeset_file_edit(self):
        chgset = self.repos.get_changeset(3)
        self.assertEqual(3, chgset.rev)
        self.assertEqual(b'Fixed README.\n', chgset.message)
        self.assertEqual(b'kate', chgset.author)
        self.assertEqual(1112361898, chgset.date)

        changes = chgset.get_changes()
        self.assertSequenceEqual(sorted([x for x in changes]),
                                 sorted([(b'trunk/README.txt', Node.FILE, Changeset.EDIT,
                                   b'trunk/README.txt', 2)]))

    def test_changeset_dir_moves(self):
        chgset = self.repos.get_changeset(5)
        self.assertEqual(5, chgset.rev)
        self.assertEqual(b'Moved directories.', chgset.message)
        self.assertEqual(b'kate', chgset.author)
        self.assertEqual(1112372739, chgset.date)

        changes = chgset.get_changes()
        self.assertSequenceEqual(sorted([x for x in changes]),
          sorted([
            (b'trunk/dir1/dir2', Node.DIRECTORY, Changeset.MOVE, b'trunk/dir2', 4),
            (b'trunk/dir1/dir3', Node.DIRECTORY, Changeset.MOVE, b'trunk/dir3', 4)]))

    def test_changeset_file_copy(self):
        chgset = self.repos.get_changeset(6)
        self.assertEqual(6, chgset.rev)
        self.assertEqual(b'More things to read', chgset.message)
        self.assertEqual(b'john', chgset.author)
        self.assertEqual(1112381806, chgset.date)

        changes = chgset.get_changes()
        self.assertSequenceEqual(sorted([x for x in changes]),
          sorted([(b'trunk/README2.txt', Node.FILE, Changeset.COPY,
                   b'trunk/README.txt', 3)]))


def suite():
    loader = unittest.TestLoader()
    loader.suiteClass = SubversionRepositoryTestSetup
    return loader.loadTestsFromTestCase(SubversionRepositoryTestCase)

if __name__ == '__main__':
    runner = unittest.TextTestRunner()
    runner.run(suite())
