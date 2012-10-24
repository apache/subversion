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

from __future__ import division, with_statement

import datetime
import logging
import re
import sqlite3
import unicodedata


class Error(Exception):
    def __init__(self, msg, *args, **kwargs):
        action = kwargs.pop("action", None)
        if action is not None:
            msg = "%s %s" % (NodeRev._opname(action), msg)
        super(Error, self).__init__(msg, *args, **kwargs)


class SQLclass(object):
    """Named index of SQL schema definitions and statements.

    Parses "schema.sql" and creates a class-level attribute for each
    script and statement in that file.
    """

    def __init__(self):
        import cStringIO
        import pkgutil

        comment_rx = re.compile(r"\s*--.*$")
        header_rx = re.compile(r"^---(?P<kind>STATEMENT|SCRIPT)"
                               r"\s+(?P<name>[_A-Z]+)$")

        kind = None
        name = None
        content = None

        def record_current_statement():
            if name is not None:
                if kind == "SCRIPT":
                    self.__record_script(name, content.getvalue())
                else:
                    self.__record_statement(name, content.getvalue())

        schema = cStringIO.StringIO(pkgutil.get_data(__name__, "schema.sql"))
        for line in schema:
            line = line.rstrip()
            if not line:
                continue

            header = header_rx.match(line)
            if header:
                record_current_statement()
                kind = header.group("kind")
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

    class __statement(object):
        __slots__ = ("execute", "query")
        def __init__(self, sql, query):
            self.execute = sql._execute
            self.query = query

        def __call__(self, cursor, **kwargs):
            if len(kwargs):
                return self.execute(cursor, self.query, kwargs)
            return self.execute(cursor, self.query)

    def __record_statement(self, name, statement):
        setattr(self, name, self.__statement(self, statement))

    class __script(object):
        __slots__ = ("execute", "query")
        def __init__(self, sql, query):
            self.execute = sql._executescript
            self.query = query

        def __call__(self, cursor):
            return self.execute(cursor, self.query)

    def __record_script(self, name, script):
        setattr(self, name, self.__script(self, script))

    LOGLEVEL = (logging.NOTSET + logging.DEBUG) // 2
    if logging.getLevelName(LOGLEVEL) == 'Level %s' % LOGLEVEL:
        logging.addLevelName(LOGLEVEL, 'SQL')

    def _log(self, *args, **kwargs):
        return logging.log(self.LOGLEVEL, *args, **kwargs)

    __stmt_param_rx = re.compile(r':([a-z]+)')
    def _execute(self, cursor, statement, parameters=None):
        if parameters is not None:
            fmt = statement.replace("%", "%%")
            fmt = self.__stmt_param_rx.sub(r'%(\1)r', fmt)
            self._log("EXECUTE: " + fmt, parameters)
            return cursor.execute(statement, parameters)
        else:
            self._log("EXECUTE: %s", statement)
            return cursor.execute(statement)

    def _executescript(self, cursor, script):
        self._log("EXECUTE: %s", script)
        return cursor.executescript(script)
SQL = SQLclass()


class SQLobject(dict):
    """Base for ORM abstractions."""

    def __init__(self, **kwargs):
        super(SQLobject, self).__init__(**kwargs)
        for name in self._columns:
            super(SQLobject, self).setdefault(name, None)

    def __getattr__(self, name):
        return self.__getitem__(name)

    def __setattr__(self, name, value):
        return self.__setitem__(name, value)

    def __delattr__(self, name):
        return self.__delitem__(name)

    def _put(self, _cursor):
        self._put_statement(_cursor, **self)
        if self.id is None:
            self.id = _cursor.lastrowid
        else:
            assert self.id == _cursor.lastrowid

    @classmethod
    def _get(self, _cursor, pkey):
        self._get_statement(_cursor, id=pkey)
        return cls._from_row(_cursor.fetchone())

    @classmethod
    def _from_row(cls, row):
        if row is not None:
            return cls(**row)
        return None

    def _clone(self):
        return self.__class__(**self)


class Txn(SQLobject):
    """O/R mapping for the "txn" table."""

    _columns = ("id", "treeid", "revision", "created", "author", "state")
    _put_statement = SQL.TXN_INSERT
    _get_statement = SQL.TXN_GET

    # state
    TRANSIENT = "T"
    PERMANENT = "P"
    DEAD = "D"

    def __init__(self, **kwargs):
        super(Txn, self).__init__(**kwargs)
        if self.state is None:
            self.state = self.TRANSIENT

    def __str__(self):
        return "%d/%d %c %s" % (self.revision, self.treeid,
                                self.state, self.created)

    @property
    def _committed(self):
        return self.state == self.PERMANENT

    @property
    def _uncommitted(self):
        return self.state == self.TRANSIENT

    @property
    def _dead(self):
        return self.state == self.DEAD

    @staticmethod
    def _now():
        now = datetime.datetime.utcnow()
        return (now.strftime("%Y%m%dT%H%M%S.%%03dZ")
                % ((now.microsecond + 500) // 1000))

    def _put(self, cursor):
        if self.created is None:
            self.created = self._now()
        super(Txn, self)._put(cursor)
        if self.treeid is None:
            self.treeid = self.id

    @classmethod
    def _find_newest(cls, cursor):
        SQL.TXN_FIND_NEWEST(cursor)
        return cls._from_row(cursor.fetchone())

    @classmethod
    def _find_by_revision(cls, cursor, revision):
        SQL.TXN_FIND_BY_REVISION(cursor, revision = revision)
        return cls._from_row(cursor.fetchone())

    @classmethod
    def _find_by_revision_timestamp(cls, cursor, revision, created):
        SQL.TXN_FIND_BY_REVISION_AND_TIMESTAMP(
            cursor, revision = revision, created = creted)
        return cls._from_row(cursor.fetchone())

    def _commit(self, cursor, revision):
        assert self._uncommitted
        now = self._now()
        SQL.TXN_COMMIT(cursor, id = self.id,
                       revision = revision, created = now)
        self.revision = revision
        self.created = now
        self.state = self.PERMANENT

    def _abort(self, cursor):
        assert self._uncommitted
        SQL.TXN_ABORT(cursor, id = self.id)
        self.state = self.DEAD

    def _cleanup(self, cursor):
        assert self._dead
        SQL.TXN_CLEANUP(cursor, id = self.id)


class NodeRev(SQLobject):
    """O/R mapping for the noderev/string/nodeview table."""

    _columns = ("id", "treeid", "nodeid", "origin", "parent", "branch",
                "nameid", "name", "denameid", "dename",
                "kind", "opcode", "state")
    _put_statement = SQL.NODEREV_INSERT
    _get_statement = SQL.NODEVIEW_GET

    # kind
    DIR = "D"
    FILE = "F"

    # opcode
    ADD = "A"
    REPLACE = "R"
    MODIFY = "M"
    DELETE = "D"
    RENAME = "N"
    BRANCH = "B"
    LAZY = "L"
    BREPLACE = "X"
    LAZY_BREPLACE = "Z"

    # state
    TRANSIENT = "T"
    PERMANENT = "P"

    def __init__(self, **kwargs):
        super(NodeRev, self).__init__(**kwargs)
        if self.state is None:
            self.state = self.TRANSIENT

    def __str__(self):
        return "%d(%d) %c %s%s" % (self.id, self.treeid, self.opcode,
                                   self.name, self._isdir and '/' or '')

    # Opcode names
    __opnames = {ADD: "add",
                 REPLACE: "replace",
                 MODIFY: "modify",
                 DELETE: "delete",
                 RENAME: "rename",
                 BRANCH: "branch",
                 LAZY: "branch",
                 BREPLACE: "branch/replace",
                 LAZY_BREPLACE: "branch/replace"}

    @classmethod
    def _opname(cls, change):
        return cls.__opnames.get(opcode)

    @property
    def _deleted(self):
        return (self.opcode == self.DELETE)

    @property
    def _lazy(self):
        return (self.opcode in (self.LAZY, self.LAZY_BREPLACE))

    @property
    def _transient(self):
        return (self.state == self.TRANSIENT)

    @property
    def _isdir(self):
        return (self.kind == self.DIR)

    @staticmethod
    def __stringid(cursor, val):
        SQL.STRING_FIND(cursor, val = val)
        row = cursor.fetchone()
        if row is not None:
            return row['id']
        SQL.STRING_INSERT(cursor, val = val)
        return cursor.lastrowid

    def _put(self, cursor):
        if self.nameid is None:
            assert self.name is not None
            self.nameid = self.__stringid(cursor, self.name)
        if self.denameid is None:
            if self.dename == self.name:
                self.denameid = self.nameid
            else:
                assert self.dename is not None
                self.denameid = self.__stringid(cursor, self.dename)
        super(NodeRev, self)._put(cursor)
        if self.nodeid is None:
            self.nodeid = self.id
        if self.branch is None:
            self.branch = self.id

    @classmethod
    def _update_treeid(cls, cursor, new_txn, old_txn):
        SQL.NODEREV_UPDATE_TREEID(cursor,
                                  new_treeid = new_txn.treeid,
                                  old_treeid = old_txn.treeid)

    def _delazify(self, cursor):
        assert self._lazy and self._isdir
        opcode = self.opcode == self.LAZY and self.BRANCH or self.BREPLACE
        SQL.NODEREV_UPDATE_OPCODE(cursor, id = self.id, opcode = opcode)
        self.opcode = opcode

    @classmethod
    def _commit(cls, cursor, txn):
        SQL.NODEREV_COMMIT(cursor, treeid = txn.treeid)

    @classmethod
    def _cleanup(cls, cursor, txn):
        SQL.NODEREV_CLEANUP(cursor, treeid = txn.treeid)

    @classmethod
    def __find(cls, cursor, parent, name, txn):
        if txn.state != txn.PERMANENT:
            if parent is None:
                finder = SQL.NODEVIEW_FIND_TRANSIENT_ROOT
            else:
                finder = SQL.NODEVIEW_FIND_TRANSIENT_BY_NAME
        else:
            if parent is None:
                finder = SQL.NODEVIEW_FIND_ROOT
            else:
                finder = SQL.NODEVIEW_FIND_BY_NAME
        finder(cursor, name = name, parent = parent, treeid = txn.treeid)
        return cls._from_row(cursor.fetchone())

    @classmethod
    def _find(cls, cursor, parent, name, txn):
        return cls.__find(cursor, parent, cls.__normtext(name), txn)

    @classmethod
    def _commonprefix(cls, *args):
        args = [arg.split('/') for arg in args]
        prefix = []
        arglen = min(len(parts) for parts in args)
        while arglen > 0:
            same = set(cls.__normtext(parts[0]) for parts in args)
            if len(same) > 1:
                break
            for parts in args:
                del parts[0]
            prefix.append(same.pop())
            arglen -= 1
        return '/'.join(prefix), ['/'.join(parts) for parts in args]

    @classmethod
    def _lookup(cls, cursor, track, relpath, txn):
        if track is None or track.path is None:
            # Lookup from root
            track = Track()
            parent = cls.__find(cursor, None, "", txn)
            if not relpath:
                track.close(parent)
                return track
            track.append(parent)
        else:
            assert track.found
            track = Track(track)
            if not relpath:
                track.close()
                return track
            parent = track.noderev
        parts = cls.__normtext(relpath).split("/")
        for name in parts[:-1]:
            if not parent._isdir:
                raise Error("ENOTDIR: " + track.path)
            while parent._lazy:
                parent = cls._get(cursor, id = parent.origin)
            node = cls.__find(cursor, parent.branch, name, txn)
            if node is None:
                raise Error("ENODIR: " +  track.path + '/' + name)
            parent = node
            track.append(parent)
        while parent._lazy:
            parent = cls._get(cursor, id = parent.origin)
        track.close(cls.__find(cursor, parent.branch, parts[-1], txn))
        return track

    def _listdir(self, cursor, txn):
        assert self._isdir
        if txn.state != txn.PERMANENT:
            lister = SQL.NODEVIEW_LIST_TRANSIENT_DIRECTORY
        else:
            lister = SQL.NODEVIEW_LIST_DIRECTORY
        lister(cursor, parent = self.id, treeid = txn.treeid)
        for row in cursor:
            yield self._from_row(row)

    def _bubbledown(self, cursor, txn):
        assert txn._uncommitted
        assert self._lazy and self._isdir and self.origin is not None
        originmap = dict()
        origin = self
        while origin._lazy:
            origin = self._get(cursor, id = origin.origin)
        for node in origin._listdir(cursor, txn):
            newnode = node._branch(cursor, self, txn)
            originmap[newnode.origin] == newnode
        self._delazify(cursor)
        return originmap

    def _branch(self, cursor, parent, txn, replaced=False):
        assert txn._uncommitted
        if self._isdir:
            opcode = replaced and self.LAZY_BREPLACE or self.LAZY
        else:
            opcode = replaced and self.BREPLACE or self.BRANCH
        node = self._revise(opcode, txn)
        node.parent = parent.id
        node.branch = None
        node._put(cursor)
        return node

    def _revise(self, opcode, txn):
        assert txn._uncommitted
        noderev = NodeRev._clone(self)
        noderev.treeid = txn.treeid
        noderev.origin = self.id
        noderev.opcode = opcode
        return noderev

    __readonly = frozenset(("name",))
    def __setitem__(self, key, value):
        if key in self.__readonly:
            raise Error("NodeRev.%s is read-only" % key)
        if key == "dename":
            name = self.__normtext(value)
            value = self.__text(value)
            if name != self.name:
                super(NodeRev, self).__setitem__("name", name)
                super(NodeRev, self).__setitem__("nameid", None)
            super(NodeRev, self).__setitem__("denameid", None)
        super(NodeRev, self).__setitem__(key, value)

    def __getitem__(self, key):
        if key == "dename":
            dename = super(NodeRev, self).__getitem__(key)
            if dename is not None:
                return dename
            key = "name"
        return super(NodeRev, self).__getitem__(key)

    @classmethod
    def __text(cls, name):
        if not isinstance(name, unicode):
            return name.decode("UTF-8")
        return name

    @classmethod
    def __normtext(cls, name):
        return unicodedata.normalize('NFC', cls.__text(name))


class Track(object):
    __slots__ = ("nodelist", "noderev", "lazy")
    def __init__(self, other=None):
        if other is None:
            self.nodelist = list()
            self.noderev = None
            self.lazy = None
        else:
            self.nodelist = list(other.nodelist)
            self.noderev = other.noderev
            self.lazy = other.lazy

    def __str__(self):
        return "%c%c %r" % (
            self.found and self.noderev.kind or '-',
            self.lazy is not None and 'L' or '-',
            self.path)

    @property
    def found(self):
        return (self.noderev is not None)

    @property
    def parent(self):
        if self.noderev is not None:
            index = len(self.nodelist) - 2;
        else:
            index = len(self.nodelist) - 1;
        if index >= 0:
            return self.nodelist[index]
        return None

    @property
    def path(self):
        if len(self.nodelist):
            return '/'.join(n.name for n in self.nodelist[1:])
        return None

    @property
    def open(self):
        return not isinstance(self.nodelust, tuple)

    def append(self, noderev):
        if self.lazy is None and noderev._lazy:
            self.lazy = len(self.nodelist)
        self.nodelist.append(noderev)

    def close(self, noderev=None):
        if noderev is not None:
            self.append(noderev)
            self.noderev = noderev
        self.nodelist = tuple(self.nodelist)

    def bubbledown(self, cursor, txn):
        if self.lazy is None:
            return
        closed = not self.open
        if closed:
            self.nodelist = list(self.nodelist)
        tracklen = len(self.nodelist)
        index = self.lazy
        node = self.nodelist[index]
        originmap = node._bubbledown(cursor, txn)
        while index < tracklen:
            node = originmap[self.nodelist[index].id]
            self.nodelist[index] = node
            if node._isdir:
                originmap = node._bubbledown(cursor, txn)
            else:
                originmap = None
            index += 1
        self.lazy = None
        if closed:
            self.close()


class Index(object):
    def __init__(self, database):
        self.conn = sqlite3.connect(database, isolation_level = "DEFERRED")
        self.conn.row_factory = sqlite3.Row
        self.cursor = self.conn.cursor()
        self.cursor.execute("PRAGMA page_size = 4096")
        self.cursor.execute("PRAGMA temp_store = MEMORY")
        self.cursor.execute("PRAGMA foreign_keys = ON")
        self.cursor.execute("PRAGMA case_sensitive_like = ON")
        self.cursor.execute("PRAGMA encoding = 'UTF-8'")

    def initialize(self):
        try:
            SQL.CREATE_SCHEMA(self.cursor)
            SQL._execute(
                self.cursor,
                "UPDATE txn SET created = :created WHERE id = 0",
                {"created": Txn._now()})
            self.commit()
        except:
            self.rollback()
            raise

    def begin(self):
        SQL._execute(self.cursor, "BEGIN")

    def commit(self):
        SQL._log("COMMIT")
        self.conn.commit()

    def rollback(self):
        SQL._log("ROLLBACK")
        self.conn.rollback()

    def close(self):
        self.rollback()
        SQL._log("CLOSE")
        self.conn.close()

    def get_txn(self, revision=None):
        if revision is None:
            return Txn._find_newest(self.cursor)
        return Txn._find_by_revision(self.cursor, revision)

    def new_txn(self, revision, created=None, author=None, base_txn = None):
        assert base_txn is None or base_txn.revision == revision
        txn = Txn(revision = revision, created = created, author = author,
                  treeid = base_txn is not None and base_txn.treeid or None)
        txn._put(self.cursor)
        return txn

    def commit_txn(self, txn, revision):
        txn._commit(self.cursor, revision)
        NodeRev._commit(self.cursor, txn)

    def abort_txn(self, txn):
        txn._abort(self.cursor)
        NodeRev._cleanup(self.cursor, txn)
        txn._cleanup(self.cursor)

    def listdir(self, txn, noderev):
        # FIXME: Query seems OK but no results returned?
        return noderev._listdir(self.conn.cursor(), txn)

    def lookup(self, txn, track=None, relpath=""):
        return NodeRev._lookup(self.cursor, track, relpath, txn)

    def __add(self, txn, track, name, kind, opcode, origintrack=None):
        assert kind in (NodeRev.FILE, NodeRev.DIR)
        assert opcode in (NodeRev.ADD, NodeRev.REPLACE)
        if not txn._uncommitted:
            raise Error("EREADONLY: txn " + str(txn))
        if not track.found:
            raise Error("ENOENT: " +  track.path)
        if not track.noderev._isdir:
            raise Error("ENOTDIR: " +  track.path)

        parent = track.noderev
        oldnode = NodeRev._find(self.cursor, parent.id, name, txn)
        if opcode == NodeRev.ADD and oldnode is not None:
            raise Error("EEXIST: " +  track.path + '/' + name)

        if origintrack is not None:
            # Treat add as copy
            if not origintrack.found:
                raise Error("ENOENT: (origin) " +  origintrack.path)
            origin = origintrack.noderev
            if origin.kind != kind:
                raise Error("ENOTSAME: origin %c -> copy %c"
                            % (origin.kind, kind))
            ### Rename detection heuristics here ...
            rename = False
        else:
            origin = None
            rename = False

        if rename:
            raise NotImplementedError("Rename detection heuristics")

        track = Track(track)
        track.bubbledown(self.cursor, txn)
        if oldnode:
            if parent.id != track.noderev.id:
                # Bubbledown changed the track
                parent = track.noderev
                oldnode = NodeRev._find(self.cursor, parent.id, name, txn)
                assert oldnode is not None
            tombstone = oldnode._revise(oldnode.DELETE, txn)
            tombstone.parent = parent.id
            tombstone._put(self.cursor)
        parent = track.noderev
        if origin is not None:
            newnode = origin._branch(self.cursor, parent.id, txn,
                                     replaced = (oldnode is not None))
        else:
            newnode = NodeRev(treeid = txn.treeid,
                              nodeid = None,
                              branch = None,
                              parent = parent.id,
                              kind = kind,
                              opcode = opcode)
            newnode.dename = name
            newnode._put(self.cursor)
        track.close(newnode)
        return track

    def add(self, txn, track, name, kind, origintrack=None):
        return self.__add(txn, track, name, kind, NodeRev.ADD, origintrack)

    def replace(self, txn, track, name, kind, origintrack=None):
        return self.__add(txn, track, name, kind, NodeRev.REPLACE, origintrack)

#    def modify(self, txn, track):
#        if not txn._uncommitted
#            raise Error("EREADONLY: txn " + str(txn))
#        if not track.found:
#            raise Error("ENOENT: " +  track.path)



class Tree(object):
    def __init__(self, index):
        self.index = index
        self.context = None
        index.rollback()


__greek_tree = {
    'iota': 'file',
    'A': {
        'mu': 'file',
        'B': {
            'lambda': 'file',
            'E': {
                'alpha': 'file',
                'beta': 'file'},
            'F': 'dir'},
        'C': 'dir',
        'D': {
            'G': {
                'pi': 'file',
                'rho': 'file',
                'tau': 'file'},
            'H': {
                'chi': 'file',
                'psi': 'file',
                'omega': 'file'}
            }
        }
    }
def greektree(ix, tx):
    def populate(track, items):
        print 'Populating', track
        for name, kind in items.iteritems():
            if kind == 'file':
                node = ix.add(tx, track, name, NodeRev.FILE)
            else:
                node = ix.add(tx, track, name, NodeRev.DIR)
            print 'Added', node, 'node:', node.noderev
            if isinstance(kind, dict):
                populate(node, kind)

    root = ix.lookup(tx)
    populate(root, __greek_tree)


def simpletest(database):
    ix = Index(database)
    ix.initialize()

    try:
        print "Lookup root"
        tx = ix.get_txn()
        print "transaction:", tx
        root = ix.lookup(tx)
        print "root track:", root
        print "root noderev", root.noderev

        print 'Create greek tree'
        tx = ix.new_txn(0)
        print "transaction:", tx
        greektree(ix, tx)
        ix.commit_txn(tx, 1)
        ix.commit()


        def listdir(noderev, prefix):
            for n in ix.listdir(tx, noderev):
                print prefix, str(n)
                if n._isdir:
                    listdir(n, prefix + "  ")

        print "List contents"
        tx = ix.get_txn()
        print "transaction:", tx
        root = ix.lookup(tx)
        print str(root.noderev)
        listdir(root.noderev, " ")

        print "Lookup iota"
        track = ix.lookup(tx, None, "iota")
        print str(track), str(track.noderev)

        print "Lookup A/D/H/psi"
        track = ix.lookup(tx, None, "A/D/H/psi")
        print str(track), str(track.noderev)
    finally:
        ix.close()

def loggedsimpletest(database):
    import sys
    logging.basicConfig(level=SQL.LOGLEVEL,
                        stream=sys.stderr)
    simpletest(database)
