#!/usr/bin/env python
# python: coding=utf-8
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

import re
import sys


HEADER = """<html>
<head>
<title>CHANGES</title>
<meta http-equiv="Content-Type" content="text/html;charset=utf-8" />
</head>
<body>
<pre>
"""

FOOTER = """</pre>
</body>
</html>
"""


url_rx = re.compile(r'(?:^|(?<=\W))(https?://[\w\d./?&=]+)')
url_sub = r'<a href="\1">\1</a>'

issue_rx = re.compile(r'(?<=\W)(#(\d+))')
issue_sub = r'<a href="https://issues.apache.org/jira/browse/SVN-\2">\1</a>'

revision_rx = re.compile(r'(?:^|(?<=\W))(r\d+)')
revision_sub = r'<a href="https://svn.apache.org/\1">\1</a>'

branchtag_rx = re.compile(r'(?<=\W)(/(?:branches|tags)/[\w\d.]+)')
branchtag_sub = r'<a href="https://svn.apache.org/repos/asf/subversion\1">\1</a>'


def generate(stream):
    sys.stdout.write(HEADER)

    beginning = True
    for n in stream.readlines():
        # Skip initial comments and empty lines in the CHANGES file.
        n = n.rstrip()
        if beginning and (not n or n.startswith('#')):
            continue
        beginning = False

        n = url_rx.sub(url_sub, n)
        n = issue_rx.sub(issue_sub, n)
        n = revision_rx.sub(revision_sub, n)
        n = branchtag_rx.sub(branchtag_sub, n)

        sys.stdout.write(n)
        sys.stdout.write('\n')

    sys.stdout.write(FOOTER)


def generate_from(filenme):
    with open(filenme, 'rt') as stream:
        return generate(stream)


def main():
    if len(sys.argv) < 2 or sys.argv[1] == '-':
        return generate(sys.stdin)
    else:
        return generate_from(sys.argv[1])

if __name__ == '__main__':
    main()
