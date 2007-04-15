from ctypes import *
import sys, os
from csvn.core import *
from txn import Txn

class Repos(object):
    def __init__(self, path, username = None, create = False):
        self.pool = Pool()
        self.iterpool = Pool()
        self._as_parameter_ = POINTER(svn_repos_t)()
        self.username = username
        if create:
            svn_repos_create(byref(self._as_parameter_), path,
                             None, None, None, None, self.pool)
        else:
            svn_repos_open(byref(self._as_parameter_), path, self.pool)
        self.fs = _fs(self)

    def latest_revnum(self):
        """Get the latest revision in the repository"""
        return self.fs.latest_revnum()

    def check_path(self, path, rev = None):
        """Check whether the given PATH exists in the specified REV. If REV
           is not specified, look at the latest revision.

        If the path is ...
          ... absent, then we return svn_node_node.
          ... a regular file, then we return svn_node_file.
          ... a directory, then we return svn_node_dir
          ... unknown, then we return svn_node_unknowna
        """
        root = self.fs.root(rev=rev, pool=self.iterpool)
        return root.check_path(path)

    def uuid(self):
        """Return a universally-unique ID for this repository"""
        return self.fs.uuid()

    def set_rev_prop(self, rev, name, value, author=None):
        """Set the NAME property to VALUE in the specified
           REV. If AUTHOR is supplied, AUTHOR is the name
           of the user who requested the change."""
        rev = svn_revnum_t(rev)
        svn_repos_fs_change_rev_prop2(self, rev, author, name, value,
                                      svn_repos_authz_func_t(),
                                      None, self.iterpool)
        self.iterpool.clear()

    def txn(self):
        """Open a new transaction for commit to the specified
           repository, assuming that our data is up to date as
           of base_rev. Setup the author and commit message
           revprops."""
        return Txn(self)

    # Private. Produces a delta editor for the commit, so that the Txn
    # class can commit its changes over the RA layer.
    def _get_commit_editor(self, message, commit_callback, commit_baton, pool):
        editor = POINTER(svn_delta_editor_t)()
        editor_baton = c_void_p()
        svn_repos_get_commit_editor4(byref(editor),
            byref(editor_baton), self, None, "", "", self.username, message,
            commit_callback, commit_baton, svn_repos_authz_callback_t(),
            None, pool)
        return (editor, editor_baton)


class _fs_txn(object):
    """This class represents an open transaction to a Subversion repository.
      Using this class, you can create, modify, move, and copy as many files
      as you like. Once you are ready to commit your changes, call the commit()
      method.

      NOTE: This is a private class. It should not be used outside of
            this module. Use the Repos.txn() method instead.

            Currently, this class is unused, but that may change in
            the future when the API for accessing the FS layer via
            Python becomes available.
    """

    def __init__(self, repos, author, message, base_rev=None):
        """Open a new transaction for commit to the specified
           repository, assuming that our data is up to date as
           of base_rev. Setup the author and commit message
           revprops.
        """

        self.repos = repos
        self.fs = repos.fs
        self.pool = Pool()
        self.iterpool = Pool()

        # Default to HEAD
        if base_rev is None:
            base_rev = self.fs.latest_revnum()

        self._as_parameter_ = POINTER(svn_fs_txn_t)()
        svn_repos_fs_begin_txn_for_commit(byref(self._as_parameter_),
          self.repos, svn_revnum_t(base_rev), author, message, self.pool)
        self.txn_root = self.fs.root(txn=self, pool=self.pool,
                                     iterpool=self.iterpool)

    def put(self, filename, contents, create=False):
        """Set the contents of file as specified."""
        if create:
            self.txn_root.make_file(filename)
        self.txn_root.send_file_contents(filename, contents)
        self.iterpool.clear()

    def copy(self, dest_filename, src_filename, src_rev=None):
        """Copy src_filename@src_rev to dest_filename"""

        self.txn_root.copy(dest_filename, src_filename, src_rev)
        self.iterpool.clear()

    def move(self, dest_filename, src_filename, src_rev=None):
        """Move src_filename@src_rev to dest_filename"""
        self.txn_root.delete(src_filename)
        self.txn_root.copy(dest_filename, src_filename, src_rev)
        self.iterpool.clear()
        
    def delete(self, filename):
        """Delete a file from the repository"""
        self.txn_root.delete(filename)
        self.iterpool.clear()
 
    def commit(self):
        """Commit all of your changes to the repository"""
        new_rev = svn_revnum_t()
        svn_repos_fs_commit_txn(None, self.repos, byref(new_rev),
                                self, self.pool)
        return new_rev.value

class _fs(object):
    """NOTE: This is a private class. Don't use it outside of
       this module. Use the Repos class instead.

       This class represents an svn_fs_t object""" 

    def __init__(self, repos):
        self.repos = repos
        self.iterpool = Pool()
        self._as_parameter_ = svn_repos_fs(repos)

    def latest_revnum(self):
        """See Repos.latest_revnum"""
        rev = svn_revnum_t()
        svn_fs_youngest_rev(byref(rev), self, self.iterpool)
        self.iterpool.clear()
        return rev.value

    def uuid(self):
        """See Repos.uuid"""
        uuid_buffer = c_char_p()
        svn_fs_get_uuid(self, byref(uuid_buffer), self.iterpool)
        uuid_str = string_at(uuid_buffer)
        self.iterpool.clear()
        return uuid_str

    def root(self, rev = None, txn = None, pool = None,
             iterpool = None):
        """Create a new svn_fs_root_t object from txn or rev.
           If neither txn nor rev or set, this root object will
           point to the latest revision root.

           The svn_fs_root object itself will be allocated in pool.
           If iterpool is supplied, iterpool will be used for any
           temporary allocations. Otherwise, pool will be used for
           temporary allocations."""
        return _fs_root(self, rev, txn, pool, iterpool)

    def txn(self, message, base_rev=None):
        """Open a new transaction for commit to the specified
           repository, assuming that our data is up to date as
           of base_rev. Setup the author and commit message
           revprops."""
        return _fs_txn(self.repos, message, base_rev)

class _fs_root(object):
    """NOTE: This is a private class. Don't use it outside of
       this module. Use the Repos.txn() method instead.

       This class represents an svn_fs_root_t object""" 

    def __init__(self, fs, rev = None, txn = None, pool = None,
                 iterpool = None):
        """See _fs.root()"""

        assert(pool)

        self.pool = pool
        self.iterpool = iterpool or pool
        self.fs = fs
        self._as_parameter_ = POINTER(svn_fs_root_t)()

        if txn and rev:
            raise Exception("You can't specify both a txn and a rev")

        if txn:
            svn_fs_txn_root(byref(self._as_parameter_), txn, self.pool)
        else:
            if not rev:
                rev = fs.latest_revnum()
            svn_fs_revision_root(byref(self._as_parameter_), fs, rev, self.pool)

    def check_path(self, path):
        """Check whether the specified path exists in this root.
           See Repos.check_path() for details."""

        kind = svn_node_kind_t()
        svn_fs_check_path(byref(kind), self, path, self.iterpool)

        return kind.value

    ## All of the functions below are currently only used by the _fs_txn
    ## class, which has not yet been exposed as a public API.

    def make_file(self, filename):
        """Create a new file with the specified filename, and no contents"""
        assert(svn_fs_is_txn_root(self))

        svn_fs_make_file(self, filename, self.iterpool)

    def send_file_contents(self, filename, contents):
        """Set the contents of the specified file"""
        assert(svn_fs_is_txn_root(self))

        contents_handler = svn_txdelta_window_handler_t()
        contents_baton = c_void_p()

        svn_fs_apply_textdelta(byref(contents_handler),
          byref(contents_baton), self, filename, None, None, self.iterpool)

        # We support either reading a file from disk, or from a string.
        assert isinstance(contents, (str, file))

        if isinstance(contents, str):
            contents = svn_string_create(contents, self.iterpool)
            svn_txdelta_send_string(contents, contents_handler,
                                    contents_baton, self.iterpool)

        elif isinstance(contents, file):

            f = POINTER(apr_file_t)()

            svn_io_file_open(byref(f), contents.name,
                APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                self.iterpool)

            stream = svn_stream_from_aprfile2(f, FALSE, self.iterpool)

            svn_txdelta_send_stream(stream, contents_handler,
                                    contents_baton, None, self.iterpool)

            svn_stream_close(stream)

    def copy(self, dest_filename, src_filename, src_rev=None):
        """Copy src_filename@src_rev to dest_filename"""
        assert(svn_fs_is_txn_root(self))

        rev_root = self.fs.root(rev=src_rev, pool=self.iterpool)
        svn_fs_copy(rev_root, src_filename, self,
                    dest_filename, self.iterpool)

    def delete(self, filename):
        """Delete the specified file"""
        assert(svn_fs_is_txn_root(self))

        svn_fs_delete(self, filename, self.iterpool)

