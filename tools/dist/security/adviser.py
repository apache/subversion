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
Generator of textual advisories for subversion.a.o/security
"""

from __future__ import absolute_import

import os
import sys


def __write_advisory(metadata, fd):
    """
    Create a textual representation of the advisory described
    by METADATA and write it to the file descriptor FD.
    """

    fd.write(metadata.advisory.text)
    if not metadata.patches:
        return

    fd.write('\nPatches:'
             '\n========\n')
    for patch in metadata.patches:
        fd.write('\n  Patch for Subversion ' + patch.base_version + ':\n'
                 '[[[\n')
        fd.write(patch.text)
        fd.write(']]]\n')

def generate(notification, target_dir):
    """
    Generate all advisories in NOTIFICATION as text files
    in TARGET_DIR. If TARGET_DIR is None, the advisory texts
    will be written to the standard output.
    """

    for metadata in notification:
        if target_dir is None:
            __write_advisory(metadata, sys.stdout)
            continue

        filename = metadata.tracking_id + '-advisory.txt'
        with open(os.path.join(target_dir, filename), 'wt') as fd:
            __write_advisory(metadata, fd)
