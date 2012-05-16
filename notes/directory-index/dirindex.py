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
    def __init__(self, msg, *args, **kwargs):
        opcode = kwargs.pop("action", None)
        if opcode is not None:
            msg = Dirent._opname(opcode) + msg
        super(Error, self).__init__(msg, *args, **kwargs)


class SQL(object):
    """Named index of SQL schema definitions and statements.

    Parses "schema.sql" and creates a class-level attribute for each
    script and statement in that file.
    """

    @classmethod
    def _load_statements(cls):
        import cStringIO
        import pkgutil
        import re

        comment_rx = re.compile(r"\s*--.*$")
        header_rx = re.compile(r"^---(STATEMENT|SCRIPT)"
                               r"\s+(?P<name>[_A-Z]+)$")

        name = None
        content = None

        def record_current_statement():
            if name is not None:
                setattr(cls, name, content.getvalue())

        schema = cStringIO.StringIO(pkgutil.get_data(__name__, "schema.sql"))
        for line in schema:
            line = line.rstrip()
            if not line:
                continue

            header = header_rx.match(line)
            if header:
                record_current_statement()
                name = header.group("name")
                content = cStringIO.StringIO()
                continue

            line = comment_rx.sub("", line)
            if not line:
                continue

            if content is not None:
                content.write(line)
                content.write("\n")
        record_current_statement()
SQL._load_statements()


class SQLobject(object):
    """Base for ORM abstractions."""

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
    """O/R mapping for the "revision" table."""

    __slots__ = ("version", "created", "author", "log")

    def _put(self, cursor):
        if self.created is None:
            now = datetime.datetime.utcnow()
            self.created = now.strftime("%Y-%m-%dT%H:%M:%S.%fZ")
        self._execute(cursor, SQL.INSERT_REVISION_RECORD,
                      [self.version, self.created, self.author, self.log])

    @classmethod
    def _get(cls, cursor, pkey):
        cursor.execute(SQL.GET_REVENT_BY_VERSION, [pkey])
        return cls._from_row(cursor.fetchone())


class Strent(SQLobject):
    """O/R mapping for the "strindex" table."""

    __slots__ = ("strid", "content")

    def _put(self, cursor):
        self._execute(cursor, SQL.INSERT_STRINDEX_RECORD, [self.content])
        self.strid = cursor.lastrowid

    @classmethod
    def _get(cls, cursor, pkey):
        cls._execute(cursor, SQL.GET_STRENT_BY_STRID, [pkey])
        return cls._from_row(cursor.fetchone())

    @classmethod
    def _find(cls, cursor, content):
        cls._execute(cursor, SQL.GET_STRENT_BY_CONTENT, [content])
        return cls._from_row(cursor.fetchone())


class Dirent(SQLobject):
    """O/R mapping for a virtual non-materialized view representing
    a join of the "dirindex" and "strindex" tables."""

    __slots__ = ("rowid", "origin", "pathid", "version",
                 "kind", "opcode", "subtree",
                 "abspath")

    # Kinds
    DIR = "D"
    FILE = "F"

    # Opcodes
    ADD = "A"
    REPLACE = "R"
    MODIFY = "M"
    DELETE = "D"
    RENAME = "N"

    # Opcode names
    __opnames = {ADD: "add",
                 REPLACE: "replace",
                 MODIFY: "modify",
                 DELETE: "delete",
                 RENAME: "rename"}

    @classmethod
    def _opname(cls, opcode):
        return cls.__opnames.get(opcode)

    @property
    def _deleted(self):
        return (self.opcode == self.DELETE)

    def __str__(self):
        return "%d %c%c%c %c %s" % (
            self.version,
            self.subtree and "(" or " ",
            self.opcode,
            self.subtree and ")" or " ",
            self.kind, self.abspath)

    def _put(self, cursor):
        strent = Strent._find(cursor, self.abspath)
        if strent is None:
            strent = Strent(content = self.abspath)
            strent._put(cursor)
        self._execute(cursor, SQL.INSERT_DIRINDEX_RECORD,
                      [self.origin, strent.strid, self.version,
                       self.kind, self.opcode,self.subtree])
        self.rowid = cursor.lastrowid
        self.pathid = strent.strid

    @classmethod
    def _get(cls, cursor, pkey):
        cls._execute(cursor, SQL.GET_DIRENT_BY_ROWID, [pkey])
        return cls._from_row(cursor.fetchone())

    @classmethod
    def _find(cls, cursor, abspath, version):
        cls._execute(cursor,
                     SQL.GET_DIRENT_BY_ABSPATH_AND_VERSION,
                     [abspath, version])
        return cls._from_row(cursor.fetchone())


class Index(object):
    def __init__(self, database):
        self.conn = sqlite3.connect(database, isolation_level = "IMMEDIATE")
        self.conn.row_factory = sqlite3.Row
        self.cursor = self.conn.cursor()
        self.cursor.execute("PRAGMA page_size = 4096")
        self.cursor.execute("PRAGMA temp_store = MEMORY")
        self.cursor.execute("PRAGMA foreign_keys = ON")
        self.cursor.execute("PRAGMA case_sensitive_like = ON")
        self.cursor.execute("PRAGMA encoding = 'UTF-8'")

    @staticmethod
    def normpath(abspath):
        return abspath.rstrip("/")

    @staticmethod
    def subtree_pattern(abspath):
        return (abspath.rstrip("/")
                .replace("#", "##")
                .replace("%", "#%")
                .replace("_", "#_")) + "/%"

    def initialize(self):
        try:
            SQLobject._log("%s", SQL.CREATE_SCHEMA)
            self.cursor.executescript(SQL.CREATE_SCHEMA)
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
            SQL.LOOKUP_ABSPATH_AT_REVISION,
            [abspath, version])
        row = self.cursor.fetchone()
        if row is not None:
            dirent = Dirent._from_row(row)
            if not dirent._deleted:
                return dirent
        return None

    def subtree(self, abspath, version):
        SQLobject._execute(
            self.cursor,
            SQL.LIST_SUBTREE_AT_REVISION,
            [version, self.subtree_pattern(abspath)])
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
            SQL.LIST_DIRENT_SUCCESSORS,
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

    class __Context(object):
        def __init__(self, version, connection):
            self.version = version
            self.conn = connection
            self.cursor = connection.cursor()
            SQLobject._execute(self.cursor, SQL.CREATE_TRANSACTION_CONTEXT)

        def clear(self):
            SQLobject._execute(self.cursor, SQL.REMOVE_TRANSACTION_CONTEXT)

        def __iter__(self):
            SQLobject._execute(self.cursor, SQL.LIST_TRANSACTION_RECORDS)
            for row in self.cursor:
                dirent = Dirent._from_row(row)
                dirent.version = self.version
                yield dirent

        def lookup(self, abspath):
            SQLobject._execute(self.cursor,
                               SQL.GET_TRANSACTION_RECORD,
                               [abspath])
            row = self.cursor.fetchone()
            if row is not None:
                dirent = Dirent._from_row(row)
                dirent.version = self.version
                return dirent
            return None

        def remove(self, abspath, purge=False):
            target = self.lookup(abspath)
            if not target:
                raise Error("txn context: remove nonexistent " + abspath)
            logging.debug("txn context: remove %s", abspath)
            SQLobject._execute(self.cursor,
                               SQL.REMOVE_TRANSACTION_RECORD,
                               [abspath])
            if purge:
                logging.debug("txn context: purge %s/*", abspath)
                SQLobject._execute(self.cursor,
                                   SQL.REMOVE_TRANSACTION_SUBTREE,
                                   [Index.subtree_pattern(abspath)])

        def record(self, dirent, replace=False, purge=False):
            target = self.lookup(dirent.abspath)
            if target is not None:
                if not replace:
                    raise Error("txn context: record existing "
                                + dirent.abspath)
                elif not target.subtree:
                    raise Error("txn context: replace conflict "
                                + dirent.abspath)
                self.remove(target.abspath, purge and target.kind == Dirent.DIR)
            SQLobject._execute(self.cursor,
                               SQL.INSERT_TRANSACTION_RECORD,
                               [dirent.origin, dirent.abspath,
                                dirent.kind, dirent.opcode, dirent.subtree])

    def __enter__(self):
        if self.revent is not None:
            raise Error("revision is read-only")
        self.__context = self.__Context(self.version, self.index.conn)
        SQLobject._execute(self.index.cursor, "BEGIN")
        self.revent = self.index.new_revision(
            self.version, self.__created, self.__author, self.__log)
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        try:
            if exc_type is None:
                for dirent in self.__context:
                    self.index.insert(dirent)
                    logging.debug("insert: %s", dirent)
                self.index.commit()
            else:
                self.index.rollback()
        except:
            self.index.rollback()
            raise
        finally:
            self.__context.clear()
            self.__context = None

    def __record(self, dirent, replace=False, purge=False):
        self.__context.record(dirent, replace, purge)
        logging.debug("record: %s", dirent)

    def __check_writable(self, opcode):
        if self.__context is None:
            raise Error(" requires a transaction", action=opcode)

    def __check_not_root(self, abspath, opcode):
        if abspath.rstrip("/") == "":
            raise Error(" not allowed on /", action=opcode)

    def __find_target(self, abspath, opcode):
        target = self.__context.lookup(abspath)
        if target is not None:
            if not target.subtree:
                raise Error(" overrides explicit " + abspath, action=opcode)
            return target, target.origin
        target = self.index.lookup(abspath, self.version - 1)
        if target is None:
            raise Error(" target does not exist: " + abspath, action=opcode)
        return target, target.rowid

    def lookup(self, abspath):
        try:
            return self.index.lookup(self.index.normpath(abspath),
                                     self.version)
        finally:
            if self.__context is None:
                self.index.rollback()

    def __add(self, opcode, abspath, kind, frompath, fromver):
        origin = None
        if frompath is not None:
            frompath = self.index.normpath(frompath)
            fromver = int(fromver)
            origin = self.index.lookup(frompath, fromver)
            if origin is None:
                raise Error(" source does not exist: " + frompath, action=opcode)
            if origin.kind != kind:
                raise Error(" changes the source object kind", action=opcode)
            origin = origin.rowid
        dirent = Dirent(origin = origin,
                        abspath = abspath,
                        version = self.version,
                        kind = kind,
                        opcode = opcode,
                        subtree = 0)
        self.__record(dirent,
                      replace=(opcode == Dirent.REPLACE),
                      purge=(opcode == Dirent.REPLACE))
        if frompath is not None and dirent.kind == Dirent.DIR:
            prefix = dirent.abspath
            offset = len(frompath)
            for source in list(self.index.subtree(frompath, fromver)):
                abspath = prefix + source.abspath[offset:]
                self.__record(Dirent(origin = source.rowid,
                                     abspath = abspath,
                                     version = self.version,
                                     kind = source.kind,
                                     opcode = opcode,
                                     subtree = 1))

    def add(self, abspath, kind, frompath=None, fromver=None):
        opcode = Dirent.ADD
        abspath = self.index.normpath(abspath)
        self.__check_writable(opcode)
        self.__check_not_root(abspath, opcode)
        return self.__add(opcode, abspath, kind, frompath, fromver)

    def replace(self, abspath, kind, frompath=None, fromver=None):
        opcode = Dirent.REPLACE
        abspath = self.index.normpath(abspath)
        self.__check_writable(opcode)
        self.__check_not_root(abspath, opcode)
        self.__find_target(abspath, opcode)
        return self.__add(opcode, abspath, kind, frompath, fromver)

    def modify(self, abspath):
        opcode = Dirent.MODIFY
        abspath = self.index.normpath(abspath)
        self.__check_writable(opcode)
        target, origin = self.__find_target(abspath, opcode)
        dirent = Dirent(origin = origin,
                        abspath = abspath,
                        version = self.version,
                        kind = target.kind,
                        opcode = opcode,
                        subtree = 0)
        self.__record(dirent, replace=True)

    def delete(self, abspath):
        opcode = Dirent.DELETE
        abspath = self.index.normpath(abspath)
        self.__check_writable(opcode)
        self.__check_not_root(abspath, opcode)
        target, origin = self.__find_target(abspath, opcode)
        dirent = Dirent(origin = origin,
                        abspath = abspath,
                        version = self.version,
                        kind = target.kind,
                        opcode = opcode,
                        subtree = 0)
        self.__record(dirent, replace=True, purge=True)
        if target.version < self.version and dirent.kind == Dirent.DIR:
            for source in self.index.subtree(abspath, self.version - 1):
                self.__record(Dirent(origin = source.rowid,
                                     abspath = source.abspath,
                                     version = self.version,
                                     kind = source.kind,
                                     opcode = opcode,
                                     subtree = 1))


def simpletest(database):
    ix = Index(database)
    ix.initialize()
    with Revision(ix, 1) as rev:
        rev.add(u'/A', Dirent.DIR)
        rev.add(u'/A/B', Dirent.DIR)
        rev.add(u'/A/B/c', Dirent.FILE)
    with Revision(ix, 2) as rev:
        rev.add(u'/A/B/d', Dirent.FILE)
    with Revision(ix, 3) as rev:
        rev.add(u'/X', Dirent.DIR, u'/A', 1)
        rev.add(u'/X/B/d', Dirent.FILE, u'/A/B/d', 2)
    with Revision(ix, 4) as rev:
        # rev.rename(u'/X/B/d', u'/X/B/x')
        rev.delete(u'/X/B/d')
        rev.add(u'/X/B/x', Dirent.FILE, u'/X/B/d', 3)
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
            "", succ._deleted and "x_x" or "-->",
            succ.abspath, succ.version)

    ix.close()

def loggedsimpletest(database):
    import sys
    logging.basicConfig(level=logging.DEBUG, #SQLobject.LOGLEVEL,
                        stream=sys.stderr)
    simpletest(database)
