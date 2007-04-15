import csvn.core as svn
from csvn.core import *
import os

class Txn(object):
    def __init__(self, session):
        self.pool = Pool()
        self.iterpool = Pool()
        self.session = session
        self.root = _txn_operation(None, "OPEN", svn_node_dir)
        self.commit_callback = None

    def delete(self, path, base_rev=None):
        """Delete PATH from the repository as of base_rev"""

        path_components = os.path.normpath(path).split("/")
        parent = self.root
        copyfrom_path = None
        total_path = path_components[0]
        for path_component in path_components[1:]:
            parent = parent.open(total_path)
            if parent.copyfrom_path:
                copyfrom_path = parent.copyfrom_path
                base_rev = parent.copyfrom_rev

            total_path = "%s/%s" % (total_path, path_component)
            if copyfrom_path:
                copyfrom_path = "%s/%s" % (copyfrom_path, path_component)

        kind = self.session.check_path(copyfrom_path or total_path, base_rev)

        if kind == svn_node_none:
            if base_rev:
                message = "'%s' not found in rev %d" % (path, base_rev)
            else:
                message = "'%s' not found" % (path)
            raise SubversionException(SVN_ERR_BAD_URL, message)

        parent.open(total_path, "DELETE", kind)

    def mkdir(self, path):
        """Create a directory at PATH""" 

        path_components = os.path.normpath(path).split("/")
        parent = self.root
        total_path = path_components[0]
        for path_component in path_components[1:]:
            parent = parent.open(total_path)
            total_path = "%s/%s" % (total_path, path_component)
        parent.open(total_path, "ADD", svn_node_dir)

    def copy(self, dest_path, src_path, src_rev=None, contents=None):
        assert contents is None or isinstance(contents, (str, file))

        if not src_rev:
            src_rev = self.session.latest_revnum()

        kind = self.session.check_path(src_path, src_rev)
        path_components = os.path.normpath(dest_path).split("/")
        parent = self.root
        total_path = path_components[0]
        for path_component in path_components[1:]:
            parent = parent.open(total_path)
            total_path = "%s/%s" % (total_path, path_component)

        parent.open(total_path, "ADD",
                    kind, copyfrom_path=src_path,
                    copyfrom_rev=src_rev, contents=contents)

    def put(self, path, contents, create=False):
        assert isinstance(contents, (str, file))
        path_components = os.path.normpath(path).split("/")
        parent = self.root
        total_path = path_components[0]
        for path_component in path_components[1:]:
            parent = parent.open(total_path)
            total_path = "%s/%s" % (total_path, path_component)
        parent.open(total_path, create and "ADD" or "OPEN",
                    svn_node_file, contents=contents)

    def commit(self, message, base_rev = None):

        if base_rev is None:
            base_rev = self.session.latest_revnum()

        editor = POINTER(svn_delta_editor_t)()
        editor_baton = c_void_p()

        commit_baton = cast(id(self), c_void_p)

        self.commit_callback = svn_commit_callback2_t(_txn_commit_callback)
        (editor, editor_baton) = self.session._get_commit_editor(message,
            self.commit_callback, commit_baton, self.pool)
        
        child_baton = c_void_p()
        try:
            SVN_ERR(editor[0].open_root(editor_baton, svn_revnum_t(base_rev),
                                        self.pool, byref(child_baton)))
            self.root.replay(editor[0], base_rev, editor_baton)
        except SubversionException:
            try:
                SVN_ERR(editor[0].abort_edit(editor_baton, self.pool))
            except SubversionException:
                pass
            raise

        return self.committed_rev

    def _txn_committed(self, info):
        self.committed_rev = info.revision
        self.committed_date = info.date
        self.committed_author = info.author
        self.post_commit_err = info.post_commit_err

def _txn_commit_callback(info, baton, pool):
    client_txn = cast(baton, py_object).value
    client_txn._txn_committed(info[0])

class _txn_operation(object):
    def __init__(self, path, action, kind, copyfrom_path = None,
                 copyfrom_rev = -1, contents = None):
        self.path = path
        self.action = action
        self.kind = kind
        self.copyfrom_path = copyfrom_path
        self.copyfrom_rev = copyfrom_rev
        self.contents = contents
        self.ops = {}

    def open(self, path, action="OPEN", kind=svn_node_dir,
             copyfrom_path = None, copyfrom_rev = -1, contents = None):
        if self.ops.has_key(path):
            op = self.ops[path]
            if action == "OPEN" and op.kind in (svn_node_dir, svn_node_file):
                return op
            elif action == "ADD" and op.action == "DELETE":
                op.action = "REPLACE"
                return op
            elif (action == "DELETE" and op.action == "OPEN" and
                  kind == svn_node_dir):
                op.action = action
                return op
            else:
                # throw error
                pass
        else:
            self.ops[path] = _txn_operation(path, action, kind,
                                            copyfrom_path = copyfrom_path,
                                            copyfrom_rev = copyfrom_rev,
                                            contents = contents)
            return self.ops[path] 

    def replay(self, editor, base_rev, baton):
        subpool = Pool()
        child_baton = c_void_p()
        file_baton = c_void_p()
        if self.path is None:
            SVN_ERR(editor.open_root(baton, svn_revnum_t(base_rev), subpool,
                                     byref(child_baton)))
        else:
            if self.action == "DELETE" or self.action == "REPLACE":
                SVN_ERR(editor.delete_entry(self.path, base_rev, baton,
                                            subpool))
            elif self.action == "OPEN":
                if self.kind == svn_node_dir:
                    SVN_ERR(editor.open_directory(self.path, baton,
                                svn_revnum_t(base_rev), subpool,
                                byref(child_baton)))
                else:
                    SVN_ERR(editor.open_file(self.path, baton,
                                svn_revnum_t(base_rev), subpool,
                                byref(file_baton)))

            if self.action in ("ADD", "REPLACE"):
                if self.kind == svn_node_dir:
                    revnum = svn_revnum_t(-1)
                    SVN_ERR(editor.add_directory(self.path, baton, None,
                                                 revnum, subpool,
                                                 byref(child_baton)))
                else:
                    SVN_ERR(editor.add_file(self.path, baton,
                                            self.copyfrom_path,
                                            svn_revnum_t(self.copyfrom_rev),
                                            subpool, byref(file_baton)))

            # If there's a source file, and we opened a file to write,
            # write out the contents
            if self.contents and file_baton:
                handler = svn_txdelta_window_handler_t()
                handler_baton = c_void_p()
                f = POINTER(apr_file_t)()
                SVN_ERR(editor.apply_textdelta(file_baton, NULL, subpool,
                        byref(handler), byref(handler_baton)))

                if isinstance(self.contents, file):
                    svn_io_file_open(byref(f), self.contents.name, APR_READ,
                                     APR_OS_DEFAULT, subpool)
                    contents = svn_stream_from_aprfile(f, subpool)
                    svn_txdelta_send_stream(contents, handler, handler_baton,
                                            NULL, subpool)
                    svn_io_file_close(f, subpool)
                else:
                    contents = svn_string_create(self.contents, subpool)
                    svn_txdelta_send_string(contents, handler, handler_baton,
                                            subpool)

            # If we opened a file, we need to close it
            if file_baton:
                SVN_ERR(editor.close_file(file_baton, NULL, subpool))

        if self.kind == svn_node_dir and self.action != "DELETE":
            assert(child_baton)

            # Look at the children
            for op in self.ops.values():
                op.replay(editor, base_rev, child_baton)

            if self.path:
                # Close the directory
                SVN_ERR(editor.close_directory(child_baton, subpool))
            else:
                # Close the editor
                SVN_ERR(editor.close_edit(baton, subpool))

