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


class SQLobject(object):
    __slots__ = ()
    def __init__(self, **kwargs):
        for name, val in kwargs.items():
            setattr(self, name, val)
        for name in self.__slots__:
            if not hasattr(self, name):
                setattr(self, name, None)

    def _put(self, cursor):
        raise NotImplementedError("SQLobject._insert")

    @classmethod
    def _get(self, cursor, pkey):
        raise NotImplementedError("SQLobject._insert")

    @classmethod
    def _from_row(cls, row):
        if row is not None:
            return cls(**dict((col, row[col]) for col in row.keys()))
        return None

    LOGLEVEL = (logging.NOTSET + logging.DEBUG) // 2
    if logging.getLevelName(LOGLEVEL) == 'Level %s' % LOGLEVEL:
        logging.addLevelName(LOGLEVEL, 'SQL')

    @classmethod
    def _log(cls, *args, **kwargs):
        return logging.log(cls.LOGLEVEL, *args, **kwargs)

    @classmethod
    def _execute(cls, cursor, statement, parameters=None):
        if parameters is not None:
            fmt = statement.replace("%", "%%").replace("?", "%r")
            cls._log("EXECUTE: " + fmt, *parameters)
            return cursor.execute(statement, parameters)
        else:
            cls._log("EXECUTE: %s", statement)
            return cursor.execute(statement)


class Revent(SQLobject):
    __slots__ = ("version", "created", "author", "log")

    def _put(self, cursor):
        if self.created is None:
            now = datetime.datetime.utcnow()
            self.created = now.strftime("%Y-%m-%dT%H:%M:%S.%fZ")
        self._execute(cursor,
                      "INSERT INTO revision (version, created, author, log)"
                      " VALUES (?, ?, ?, ?)",
                      [self.version, self.created, self.author, self.log])

    @classmethod
    def _get(cls, cursor, pkey):
        cursor.execute("SELECT * FROM revision WHERE version = ?", [pkey])
        return cls._from_row(cursor.fetchone())


class Pathent(SQLobject):
    __slots__ = ("pathid", "abspath")

    def _put(self, cursor):
        self._execute(cursor,
                      "INSERT INTO pathindex (abspath) VALUES (?)",
                      [self.abspath])
        self.pathid = cursor.lastrowid

    @classmethod
    def _get(cls, cursor, pkey):
        cls._execute(cursor,
                     "SELECT * FROM pathindex WHERE pathid = ?",
                     [pkey])
        return cls._from_row(cursor.fetchone())

    @classmethod
    def _find(cls, cursor, abspath):
        cls._execute(cursor,
                     "SELECT * FROM pathindex WHERE abspath = ?",
                     [abspath])
        return cls._from_row(cursor.fetchone())


class Dirent(SQLobject):
    __slots__ = ("rowid", "pathid", "version", "deleted",
                 "kind", "origin", "copied", "subtree",
                 "abspath")

    def __str__(self):
        return "%3d %c%c %s" % (
            self.version,
            self.deleted and "x" or " ",
            self.kind and "f" or "d",
            self.abspath)

    def _put(self, cursor):
        pathent = Pathent._find(cursor, self.abspath)
        if pathent is None:
            pathent = Pathent(abspath = self.abspath)
            pathent._put(cursor)
        self._execute(cursor,
                      "INSERT INTO dirindex"
                      " (pathid, version, deleted,"
                      " kind, origin, copied, subtree)"
                      " VALUES (?, ?, ?, ?, ?, ?, ?)",
                      [pathent.pathid, self.version, self.deleted,
                       self.kind, self.origin, self.copied, self.subtree])
        self.rowid = cursor.lastrowid
        self.pathid = pathent.pathid

    @classmethod
    def _get(cls, cursor, pkey):
        cls._execute(cursor,
                     "SELECT dirindex.*, pathindex.abspath"
                     " FROM dirindex JOIN pathindex"
                     " ON dirindex.pathid = pathindex.pathid"
                     " WHERE dirindex.rowid = ?", [pkey])
        return cls._from_row(cursor.fetchone())

    @classmethod
    def _find(cls, cursor, abspath, version):
        cls._execute(cursor,
                     "SELECT dirindex.*, pathindex.abspath"
                     " FROM dirindex JOIN pathindex"
                     " ON dirindex.pathid = pathindex.pathid"
                     " WHERE pathindex.abspath = ?"
                     " AND dirindex.version = ?",
                     [abspath, version])
        return cls._from_row(cursor.fetchone())


class Index(object):
    def __init__(self, database):
        self.conn = sqlite3.connect(database, isolation_level = "IMMEDIATE")
        self.conn.row_factory = sqlite3.Row
        self.cursor = self.conn.cursor()
        self.cursor.execute("PRAGMA foreign_keys = ON")
        self.cursor.execute("PRAGMA case_sensitive_like = ON")
        self.cursor.execute("PRAGMA encoding = 'UTF-8'")

    __schema = """
DROP TABLE IF EXISTS dirindex;
DROP TABLE IF EXISTS pathindex;
DROP TABLE IF EXISTS revision;

CREATE TABLE revision (
  version integer NOT NULL PRIMARY KEY,
  created timestamp NOT NULL,
  author  varchar NULL,
  log     varchar NULL
);

CREATE TABLE pathindex (
  pathid  integer NOT NULL PRIMARY KEY,
  abspath varchar NOT NULL UNIQUE
);

CREATE TABLE dirindex (
  rowid   integer NOT NULL PRIMARY KEY,
  pathid  integer NOT NULL REFERENCES pathindex(pathid),
  version integer NOT NULL REFERENCES revision(version),
  deleted boolean NOT NULL,
  kind    integer NOT NULL,
  origin  integer NULL REFERENCES dirindex(rowid),
  copied  boolean NOT NULL,
  subtree boolean NOT NULL
);
CREATE UNIQUE INDEX dirindex_versioned_tree ON dirindex(pathid, version DESC);
CREATE INDEX dirindex_successor_list ON dirindex(origin);
CREATE INDEX dirindex_deleted ON dirindex(deleted);

INSERT INTO revision (version, created, author, log)
  VALUES (0, 'EPOCH', NULL, NULL);
INSERT INTO pathindex (pathid, abspath) VALUES (0, '/');
INSERT INTO dirindex (rowid, pathid, version, deleted,
                      kind, origin, copied, subtree)
  VALUES (0, 0, 0, 0, 0, NULL, 0, 0);
"""

    def initialize(self):
        try:
            SQLobject._log("%s", self.__schema)
            self.cursor.executescript(self.__schema)
            self.commit()
        finally:
            self.rollback()

    def commit(self):
        SQLobject._log("COMMIT")
        return self.conn.commit()

    def rollback(self):
        SQLobject._log("ROLLBACK")
        return self.conn.rollback()

    def close(self):
        self.rollback()
        SQLobject._log("CLOSE")
        return self.conn.close()

    def get_revision(self, version):
        return Revent._get(self.cursor, version)

    def new_revision(self, version, created=None, author=None, log=None):
        revent = Revent(version = version,
                        created = created,
                        author = author,
                        log = log)
        revent._put(self.cursor)
        return revent

    def insert(self, dirent):
        assert isinstance(dirent, Dirent)
        dirent._put(self.cursor)
        return dirent

    def lookup(self, abspath, version):
        SQLobject._execute(
            self.cursor,
            "SELECT dirindex.*, pathindex.abspath FROM dirindex"
            " JOIN pathindex ON dirindex.pathid = pathindex.pathid"
            " WHERE pathindex.abspath = ? AND dirindex.version <= ?"
            " ORDER BY pathindex.abspath ASC, dirindex.version DESC"
            " LIMIT 1",
            [abspath, version])
        row = self.cursor.fetchone()
        if row is not None and not row["deleted"]:
            return Dirent._from_row(row)
        return None

    def subtree(self, abspath, version):
        pattern = (abspath.rstrip("/")
                   .replace("#", "##")
                   .replace("%", "#%")
                   .replace("_", "#_")) + "/%"
        SQLobject._execute(
            self.cursor,
            "SELECT dirindex.*, pathindex.abspath FROM dirindex"
            " JOIN pathindex ON dirindex.pathid = pathindex.pathid"
            " JOIN (SELECT pathid, MAX(version) AS maxver FROM dirindex"
            " WHERE version <= ? GROUP BY pathid) AS filtered"
            " ON dirindex.pathid == filtered.pathid"
            " AND dirindex.version == filtered.maxver"
            " WHERE pathindex.abspath LIKE ? ESCAPE '#'"
            " AND NOT dirindex.deleted"
            " ORDER BY pathindex.abspath ASC",
            [version, pattern])
        for row in self.cursor:
            yield Dirent._from_row(row)

    def predecessor(self, dirent):
        assert isinstance(dirent, Dirent)
        if dirent.origin is None:
            return None
        return Dirent._get(self.cursor, dirent.origin)

    def successors(self, dirent):
        assert isinstance(dirent, Dirent)
        SQLobject._execute(
            self.cursor,
            "SELECT dirindex.*, pathindex.abspath FROM dirindex"
            " JOIN pathindex ON dirindex.pathid = pathindex.pathid"
            " WHERE dirindex.origin = ?"
            " ORDER BY pathindex.abspath ASC, dirindex.version ASC",
            [dirent.rowid])
        for row in self.cursor:
            yield Dirent._from_row(row)


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
        SQLobject._log("BEGIN")
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

    def __record(self, dirent, action):
        self.__context[dirent.abspath] = dirent
        if dirent.subtree:
            action = "(%s)" % action
        else:
            action = " %s " % action
        logging.debug(" %-9s %s", action, dirent)

    def __check_writable(self, action):
        if self.__context is None:
            raise Error(action + " requires a transaction")

    def __check_not_root(self, abspath, action):
        if abspath.rstrip("/") == "":
            raise Error(action + " not allowed on /")

    def __find_target(self, abspath, action):
        target = self.__context.get(abspath)
        if target is not None and not target.subtree:
            raise Error(action + " overrides explicit " + abspath)
        if target is None:
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
        dirent = Dirent(abspath = abspath,
                        version = self.version,
                        deleted = 0,
                        kind = kind,
                        origin = origin,
                        copied = int(origin is not None),
                        subtree = 0)
        self.__record(dirent, action)
        if frompath is not None:
            offset = len(frompath.rstrip("/"))
            prefix = abspath.rstrip("/")
            for source in self.index.subtree(frompath, fromver):
                dirent = Dirent(rowid = source.rowid,
                                abspath = prefix + source.abspath[offset:],
                                version = self.version,
                                deleted = 0,
                                kind = source.kind,
                                origin = source.rowid,
                                copied = 1,
                                subtree = 1)
                self.__record(dirent, action)

    def add(self, abspath, kind, frompath=None, fromver=None):
        action = "add"
        self.__check_writable(action)
        self.__check_not_root(abspath, action)
        return self.__add(action, abspath, kind, frompath, fromver)

    def replace(self, abspath, kind, frompath=None, fromver=None):
        action = "replace"
        self.__check_writable(action)
        self.__check_not_root(abspath, action)
        self.__find_target(abspath, action)
        return self.__add(action, abspath, kind, frompath, fromver)

    def modify(self, abspath):
        action = "modify"
        self.__check_writable(action)
        target = self.__find_target(abspath, action)
        dirent = Dirent(abspath = abspath,
                        version = self.version,
                        deleted = 0,
                        kind = target.kind,
                        origin = target.rowid,
                        copied = 0,
                        subtree = 0)
        self.__record(dirent, action)

    def delete(self, abspath):
        action = "replace"
        self.__check_writable(action)
        self.__check_not_root(abspath, action)
        target = self.__find_target(abspath, action)
        dirent = Dirent(abspath = abspath,
                        version = self.version,
                        deleted = 1,
                        kind = target.kind,
                        origin = target.rowid,
                        copied = 0,
                        subtree = 0)
        self.__record(dirent, action)
        for source in self.index.subtree(abspath, self.version - 1):
            dirent = Dirent(rowid = source.rowid,
                            abspath = source.abspath,
                            version = self.version,
                            deleted = 1,
                            kind = source.kind,
                            origin = source.rowid,
                            copied = 0,
                            subtree = 1)
            self.__record(dirent, action)


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
    logging.basicConfig(level=SQLobject.LOGLEVEL, stream=sys.stderr)
    simpletest(database)
