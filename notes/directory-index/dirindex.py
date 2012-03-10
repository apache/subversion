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


import collections
import datetime
import logging
import sqlite3


class Error(Exception):
    pass


class _SQLObjectMixin(object):
    @classmethod
    def _columns(cls):
        return ", ".join(cls._fields)

    @classmethod
    def _qualified_columns(cls):
        return ", ".join("%s.%s" % (cls._table, f) for f in cls._fields)

    @classmethod
    def _vars(cls):
        return ", ".join("?" * len(cls._fields))

    @classmethod
    def _columns_nokey(cls):
        return ", ".join(cls._fields[1:])

    @classmethod
    def _vars_nokey(cls):
        return ", ".join("?" * (len(cls._fields) - 1))

    def _nokey(self):
        return self[1:]


class Revent(
        collections.namedtuple("ReventBase", [
            "version", "created", "author", "log"]),
        _SQLObjectMixin):
    _table = "revision"


class Dirent(
        collections.namedtuple("DirentBase", [
            "rowid", "abspath", "version", "deleted",
            "kind", "origin", "copied", "subtree"]),
        _SQLObjectMixin):
    _table = "dirindex"

    def __str__(self):
        return "%3d %c%c %s" % (
            self.version,
            self.deleted and "x" or " ",
            self.kind and "f" or "d",
            self.abspath)


class Index(object):
    def __init__(self, database):
        self.conn = sqlite3.connect(database, isolation_level = "IMMEDIATE")
        self.cursor = self.conn.cursor()
        self.cursor.execute("PRAGMA foreign_keys = ON")

    def execute(self, statement, parameters=None):
        if parameters is not None:
            fmt = statement.replace("%", "%%").replace("?", "%r")
            logging.log(logging.DEBUG//2, "EXECUTE: " + fmt, *parameters)
            return self.cursor.execute(statement, parameters)
        else:
            logging.log(logging.DEBUG//2, "EXECUTE: %s", statement)
            return self.cursor.execute(statement)

    __initialize = (
        """\
DROP TABLE IF EXISTS dirindex""",
        """\
DROP TABLE IF EXISTS revision""",
        """\
CREATE TABLE revision (
  version integer NOT NULL PRIMARY KEY,
  created timestamp NOT NULL,
  author  varchar NULL,
  log     varchar NULL
)""",
        """\
CREATE TABLE dirindex (
  rowid   integer PRIMARY KEY,
  abspath varchar NOT NULL,
  version integer NOT NULL REFERENCES revision(version),
  deleted boolean NOT NULL,
  kind    integer NOT NULL,
  origin  integer NULL REFERENCES dirindex(rowid),
  copied  boolean NOT NULL,
  subtree boolean NOT NULL
)""",
        """\
CREATE UNIQUE INDEX dirindex_versioned_tree
  ON dirindex(abspath ASC, version DESC)""",
        """\
CREATE INDEX dirindex_successor_list
  ON dirindex(origin)""",
        """\
INSERT INTO revision (version, created, author, log)
  VALUES (0, 'EPOCH', NULL, NULL)""",
        """\
INSERT INTO dirindex (abspath, version, deleted, kind, origin, copied, subtree)
  VALUES ('/', 0, 0, 0, NULL, 0, 0)""")
    def initialize(self):
        try:
            for statement in self.__initialize:
                self.execute(statement)
            self.commit()
        finally:
            self.rollback()

    def commit(self):
        logging.log(logging.DEBUG//2, "COMMIT")
        return self.conn.commit()

    def rollback(self):
        logging.log(logging.DEBUG//2, "ROLLBACK")
        return self.conn.rollback()

    def close(self):
        self.rollback()
        logging.log(logging.DEBUG//2, "CLOSE")
        return self.conn.close()

    __get_revision = """\
SELECT %s FROM revision
WHERE version = ?""" % Revent._qualified_columns()
    def get_revision(self, version):
        self.execute(self.__get_revision, [version])
        row = self.cursor.fetchone()
        if row is not None:
            return Revent._make(row)
        return None

    __new_revision = """\
INSERT INTO revision (%s)
  VALUES (%s)""" % (Revent._columns(), Revent._vars())
    def new_revision(self, version, created=None, author=None, log=None):
        if created is None:
            created = datetime.datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%S.%fZ")
        revent = Revent(version = version,
                        created = created,
                        author = author,
                        log = log)
        self.execute(self.__new_revision, revent)
        return revent

    __insert = """\
INSERT INTO dirindex (%s)
  VALUES (%s)""" % (
    Dirent._columns_nokey(), Dirent._vars_nokey())
    def insert(self, dirent):
        assert isinstance(dirent, Dirent)
        self.execute(self.__insert, dirent._nokey())
        return Dirent(self.cursor.lastrowid, *dirent._nokey())

    __lookup = """\
SELECT %s FROM dirindex
WHERE abspath = ? AND version <= ?
ORDER BY abspath ASC, version DESC
LIMIT 1""" % Dirent._qualified_columns()
    def lookup(self, abspath, version):
        self.execute(self.__lookup, [abspath, version])
        row = self.cursor.fetchone()
        if row is not None:
            dirent = Dirent._make(row)
            if not dirent.deleted:
                return dirent
        return None

    __subtree = """\
SELECT %s FROM dirindex
  JOIN (SELECT abspath, MAX(version) AS maxver FROM dirindex
        WHERE version <= ? GROUP BY abspath) AS filtered
  ON dirindex.abspath == filtered.abspath
     AND dirindex.version == filtered.maxver
WHERE dirindex.abspath LIKE ? ESCAPE '#'
      AND NOT dirindex.deleted
ORDER BY dirindex.abspath ASC""" % Dirent._qualified_columns()
    def subtree(self, abspath, version):
        pattern = (abspath.rstrip("/")
                   .replace("#", "##")
                   .replace("%", "#%")
                   .replace("_", "#_")) + "/%"
        self.execute(self.__subtree, [version, pattern])
        for row in self.cursor:
            yield Dirent._make(row)

    __predecessor = """\
SELECT %s FROM dirindex
WHERE rowid = ?"""  % Dirent._qualified_columns()
    def predecessor(self, dirent):
        assert isinstance(dirent, Dirent)
        if dirent.origin is None:
            return None
        self.execute(self.__predecessor, [dirent.origin])
        return Dirent._make(self.cursor.fetchone())

    __successors = """\
SELECT %s FROM dirindex
where origin = ?
ORDER BY abspath ASC, version ASC""" % Dirent._qualified_columns()
    def successors(self, dirent):
        assert isinstance(dirent, Dirent)
        self.execute(self.__successors, [dirent.rowid])
        for row in self.cursor:
            yield Dirent._make(row)


class Revision(object):
    def __init__(self, index, version,
                 created=None, author=None, log=None):
        self.index = index
        self.version = version
        self.revent = index.get_revision(version)
        self.__created = created
        self.__author = author
        self.__log = log
        self.__context = None
        index.rollback()

    def __enter__(self):
        if self.revent is not None:
            raise Error("revision is read-only")
        logging.log(logging.DEBUG//2, "BEGIN")
        self.revent = self.index.new_revision(
            self.version, self.__created, self.__author, self.__log)
        self.__context = {}
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        try:
            if exc_type is None and len(self.__context):
                for dirent in sorted(self.__context.itervalues()):
                    self.index.insert(dirent)
                self.index.commit()
        except:
            self.index.rollback()
            raise
        finally:
            self.__context = None

    def __check_writable(self, action):
        if self.__context is None:
            raise Error(action + " requires a transaction")

    def __check_not_root(self, abspath, action):
        if abspath.rstrip("/") == "":
            raise Error(action + " not allowed on /")

    def __find_target(self, abspath, action):
        target = self.index.lookup(abspath, self.version - 1)
        if target is None:
            raise Error(action + " target does not exist: " + abspath)
        return target

    def lookup(self, abspath):
        try:
            return self.index.lookup(abspath, self.version)
        finally:
            if self.__context is None:
                self.index.rollback()

    def __add(self, action, abspath, kind, frompath, fromver):
        origin = None
        if frompath is not None:
            origin = self.index.lookup(frompath, fromver)
            if origin is None:
                raise Error(action + " source does not exist: " + frompath)
            if origin.kind != kind:
                raise Error(action + " changes the source object kind")
            origin = origin.rowid
        dirent = Dirent(rowid = None,
                        abspath = abspath,
                        version = self.version,
                        deleted = 0,
                        kind = kind,
                        origin = origin,
                        copied = int(origin is not None),
                        subtree = 0)
        self.__context[dirent.abspath] = dirent
        if frompath is not None:
            offset = len(frompath.rstrip("/"))
            prefix = abspath.rstrip("/")
            for source in self.index.subtree(frompath, fromver):
                dirent = Dirent(rowid = None,
                                abspath = prefix + source.abspath[offset:],
                                version = self.version,
                                deleted = 0,
                                kind = source.kind,
                                origin = source.rowid,
                                copied = 1,
                                subtree = 1)
                self.__context[dirent.abspath] = dirent

    def add(self, abspath, kind, frompath=None, fromver=None):
        self.__check_writable("add")
        self.__check_not_root(abspath, "add")
        return self.__add("add", abspath, kind, frompath, fromver)

    def replace(self, abspath, kind, frompath=None, fromver=None):
        self.__check_writable("replace")
        self.__check_not_root(abspath, "replace")
        self.__find_target(abspath, "replace")
        return self.__add("replace", abspath, kind, frompath, fromver)

    def modify(self, abspath):
        self.__check_writable("modify")
        target = self.__find_target(abspath, "modify")
        dirent = Dirent(rowid = None,
                        abspath = abspath,
                        version = self.version,
                        deleted = 0,
                        kind = target.kind,
                        origin = target.rowid,
                        copied = 0,
                        subtree = 0)
        self.__context[dirent.abspath] = dirent

    def delete(self, abspath):
        self.__check_writable("delete")
        self.__check_not_root(abspath, "delete")
        target = self.__find_target(abspath, "delete")
        dirent = Dirent(rowid = None,
                        abspath = abspath,
                        version = self.version,
                        deleted = 1,
                        kind = target.kind,
                        origin = target.rowid,
                        copied = 0,
                        subtree = 0)
        self.__context[dirent.abspath] = dirent
        for source in self.index.subtree(abspath, self.version - 1):
            dirent = Dirent(rowid = None,
                            abspath = source.abspath,
                            version = self.version,
                            deleted = 1,
                            kind = source.kind,
                            origin = source.rowid,
                            copied = 0,
                            subtree = 1)
            self.__context[dirent.abspath] = dirent


def simpletest(database):
    ix = Index(database)
    ix.initialize()
    with Revision(ix, 1) as rev:
        rev.add(u'/A', 0)
        rev.add(u'/A/B', 0)
        rev.add(u'/A/B/c', 1)
    with Revision(ix, 2) as rev:
        rev.add(u'/A/B/d', 1)
    with Revision(ix, 3) as rev:
        rev.add(u'/X', 0, u'/A', 1)
        rev.add(u'/X/B/d', 1, u'/A/B/d', 2)
    with Revision(ix, 4) as rev:
        rev.delete(u'/X/B/d')
        rev.add(u'/X/B/x', 1, u'/X/B/d', 3)
    with Revision(ix, 5) as rev:
        rev.delete(u'/A')

    for r in (0, 1, 2, 3, 4, 5):
        print "Revision: %d" % r
        for dirent in list(ix.subtree('/', r)):
            origin = ix.predecessor(dirent)
            if origin is None:
                print "   " + str(dirent)
            else:
                print "   %-17s  <- %s" % (dirent, origin)

    dirent = ix.lookup('/A/B/c', 4)
    print "/A/B/c@4 -> %s@%d" % (dirent.abspath, dirent.version)
    for succ in ix.successors(dirent):
        print "%11s %s %s@%d" % (
            "", succ.deleted and "x_x" or "-->",
            succ.abspath, succ.version)

    ix.close()

def loggedsimpletest(database):
    import sys
    logging.basicConfig(level=logging.DEBUG, stream=sys.stderr)
    simpletest(database)
