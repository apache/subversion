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

# Usage: logimport [options] <database-name> <repoa-url>
# Options:
#   --svn=PATH     Use a non-default svn binary
#   --debug        Enable debug-level logging to logimport.debug.log
#   --sqldebug     Enable SQL-level logging to logimport.sql.log
#
# Converts the history of the repository at <repos-url> into a
# single-tree directory index.


import logging
import subprocess
import sys

try:
    from lxml.etree import iterparse
except ImportError:
    from xml.etree.ElementTree import iterparse

from dirindex import Dirent, Index, Revision


def parse(index, stream):
    kindmap = {"dir": Dirent.DIR, "file": Dirent.FILE}

    version = None
    revcount = 0
    for event, logentry in iterparse(stream):
        if logentry.tag != "logentry":
            continue

        version = int(logentry.get("revision"))

        revcount += 1
        if revcount == 1:
            logging.info("initial: r%d", version)
        else:
            logger = not revcount % 1000 and logging.info or logging.debug
            logger("%d: r%d", revcount, version)

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
                    logging.debug("  %s      %s", action, abspath)
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
    try:
        index = Index(database)
        index.cursor.execute("PRAGMA journal_mode = MEMORY")
        index.cursor.execute("PRAGMA locking_mode = EXCLUSIVE")
        index.cursor.execute("PRAGMA synchronous = OFF")
        index.cursor.execute("PRAGMA cache_size = -100000")
        index.initialize()
        svnlog = subprocess.Popen(
            [svn, "log", "-v", "--xml", "-r1:HEAD", url],
            stdout = subprocess.PIPE)
        parse(index, svnlog.stdout)
        return svnlog.wait()
    except:
        logging.exception("logimport failed")
        try:
            svnlog.wait()
        except:
            pass
        return 2


def main():
    import logging.config
    from optparse import OptionParser
    from dirindex import SQLobject

    parser = OptionParser("Usage: %prog [options] <database-name> <repoa-url>")
    parser.add_option("--svn", action="store", default="svn",
                      help="Use a non-default svn binary", metavar="PATH")
    parser.add_option("--debug", action="store_true", default=False,
                      help="Enable debug-level logging to logimport.debug.log")
    parser.add_option("--sqldebug", action="store_true", default=False,
                      help="Enable SQL-level logging to logimport.debug.log")

    opts, args = parser.parse_args()
    if len(args) != 2:
        parser.error("wrong number of arguments")
    database, url = args

    logconfig = {
        "version": 1,
        "formatters": {
            "console": {"format": "%(levelname)-7s %(message)s"},
            "logfile": {"format": "%(asctime)s %(levelname)-7s %(message)s"}},
        "handlers": {
            "console": {
                "class": "logging.StreamHandler",
                "level": logging.INFO,
                "stream": sys.stderr,
                "formatter": "console"}},
        "root": {
            "level": logging.INFO,
            "handlers": ["console"]}}

    handlers = logconfig["root"]["handlers"]
    if opts.debug:
        logconfig["root"]["level"] = logging.DEBUG
        logconfig["handlers"]["debug"] = {
            "class": "logging.FileHandler",
            "level": logging.DEBUG,
            "mode": "w",
            "filename": "./logimport.debug.log",
            "formatter": "logfile"}
        handlers.append("debug")
    if opts.sqldebug:
        logconfig["root"]["level"] = SQLobject.LOGLEVEL
        logconfig["handlers"]["sqldebug"] = {
            "class": "logging.FileHandler",
            "level": SQLobject.LOGLEVEL,
            "mode": "w",
            "filename": "./logimport.sql.log",
            "formatter": "logfile"}
        handlers.append("sqldebug")

    logging.config.dictConfig(logconfig)
    sys.exit(logimport(database, url, opts.svn))


if __name__ == "__main__":
    main()
