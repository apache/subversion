#!/usr/bin/env python
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

# Usage: logimport <database-name> <repoa-url> [path-to-svn]
#
# Converts the history of the repository at <repos-url> into a
# single-tree directory index.


import logging
import subprocess
import sys
from xml.etree import ElementTree
from dirindex import Index, Revision


def parse(index, stream):
    kindmap = {"dir": 0, "file": 1}

    version = None
    revcount = 0
    for event, logentry in ElementTree.iterparse(stream):
        if logentry.tag != "logentry":
            continue

        version = int(logentry.get("revision"))

        revcount += 1
        if revcount == 1 or not revcount % 1000:
            revlogger = logging.info
        else:
            revlogger = logging.debug
        revlogger("%d: r%d", revcount, version)

        created = logentry.find("date")
        if created is not None:
            created = created.text
        else:
            created = ""

        author = logentry.find("author")
        if author is not None:
            author = author.text

        log = logentry.find("msg")
        if log is not None:
            log = log.text

        with Revision(index, version, created, author, log) as rev:
            actionmap = dict(A = (rev.add, True),
                             R = (rev.replace, True),
                             M = (rev.modify, False),
                             D = (rev.delete, False))

            for path in logentry.findall("paths/path"):
                abspath = path.text
                action = path.get("action")
                handler, newnode = actionmap[action]
                if not newnode:
                    logging.debug("  %-s      %s", action, abspath)
                    handler(abspath)
                    continue

                kindstr = path.get("kind")
                kind = kindmap[kindstr]
                frompath = path.get("copyfrom-path")
                if frompath is not None:
                    fromrev = int(path.get("copyfrom-rev"))
                    logging.debug("  %s %-4s %s <- %s@%d",
                                  action, kindstr, abspath,
                                  frompath, fromrev)
                    handler(abspath, kind, frompath, fromrev)
                else:
                    logging.debug("  %s %-4s %s", action, kindstr, abspath)
                    handler(abspath, kind)
    if version is not None:
        logging.info("final: r%d", version)


def logimport(database, url, svn):
    index = Index(database)
    index.initialize()
    index.cursor.execute("PRAGMA journal_mode = MEMORY")
    index.cursor.execute("PRAGMA locking_mode = EXCLUSIVE")
    index.cursor.execute("PRAGMA synchronous = OFF")
    svnlog = subprocess.Popen(
        [svn, "log", "-v", "--xml", "-r1:HEAD", url],
        stdout = subprocess.PIPE)
    parse(index, svnlog.stdout)
    sys.exit(svnlog.wait())


if __name__ == "__main__":
    database = sys.argv[1]
    url = sys.argv[2]
    if len(sys.argv) > 3:
        svn = sys.argv[3]
    else:
        svn = "svn"
    logging.basicConfig(level=logging.INFO, stream=sys.stderr)
    logimport(database, url, svn)
