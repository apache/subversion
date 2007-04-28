import csvn.core as svn
from csvn.core import *
from txn import Txn
import os

class ClientURI(object):

    def __init__(self, uri, encoded=True):
        """Create a ClientURI object from an absolute URI. If encoded=True, the
           input string may be URI-encoded."""
        pool = Pool()
        if not encoded:
            uri = svn_path_uri_encode(uri, pool)
        self._as_parameter_ = svn_path_canonicalize(uri, pool)

    def join(self, uri):
        """Convert the specified absolute uri to a decoded path,
           relative to me."""
        return ClientURI(svn_path_join(self, uri, Pool()))

    def dirname(self):
        """Get the parent directory of this URI"""
        return ClientURI(svn_path_dirname(self, Pool()))

    def relative_path(self, uri, encoded=True):
        """Convert the supplied URI to a decoded path, relative to me."""
        pool = Pool()
        if not encoded:
            uri = svn_path_uri_encode(uri, pool)
        child_path = svn_path_is_child(self, uri, pool) or uri
        return svn_path_uri_decode(child_path, pool)

    def longest_ancestor(self, uri):
        """Get the longest ancestor of this URI and another URI"""
        return ClientURI(svn_path_get_longest_ancestor(self, uri, Pool()))

    def __str__(self):
        return self._as_parameter_

class ClientSession(object):

    def __init__(self, url, username=None, password=None):
        """Open a new session to URL with the specified USERNAME and
           PASSWORD"""

        self.pool = Pool()
        self.iterpool = Pool()
        self.url = ClientURI(url)

        self.client = POINTER(svn_client_ctx_t)()
        svn_client_create_context(byref(self.client), self.pool)

        self.cancel_func = svn_cancel_func_t()
        svn_cmdline_setup_auth_baton(
            byref(self.client.contents.auth_baton), FALSE, username,
            password, NULL, FALSE, NULL, self.cancel_func, NULL, self.pool)

        self._as_parameter_ = POINTER(svn_ra_session_t)()
        svn_client_open_ra_session(byref(self._as_parameter_), url,
                                   self.client, self.pool)

    def txn(self):
        """Create a transaction"""
        return Txn(self)

    def latest_revnum(self):
        """Get the latest revision number in the repository"""
        revnum = svn_revnum_t()
        svn_ra_get_latest_revnum(self, byref(revnum), self.iterpool)
        self.iterpool.clear()
        return revnum.value

    def check_path(self, path, rev = None, encoded=True):
        """Check the status of PATH@REV. If REV is not specified,
           look at the latest revision in the repository.

        If the path is ...
          ... absent, then we return svn_node_node.
          ... a regular file, then we return svn_node_file.
          ... a directory, then we return svn_node_dir
          ... unknown, then we return svn_node_unknown
        """

        path = self._relative_path(path, encoded)
        if rev is None:
            rev = self.latest_revnum()
        kind = svn_node_kind_t()
        svn_ra_check_path(self, path, svn_revnum_t(rev), byref(kind),
                          self.iterpool)
        self.iterpool.clear()
        return kind.value

    # Private. Produces a delta editor for the commit, so that the Txn
    # class can commit its changes over the RA layer.
    def _get_commit_editor(self, message, commit_callback, commit_baton, pool):
        editor = POINTER(svn_delta_editor_t)()
        editor_baton = c_void_p()
        svn_ra_get_commit_editor2(self, byref(editor),
            byref(editor_baton), message, commit_callback,
            commit_baton, NULL, FALSE, pool)
        return (editor, editor_baton)

    # Private. Convert a URI to a repository-relative path
    def _relative_path(self, path, encoded=True):
        return self.url.relative_path(path, encoded)

    # Private. Convert a repository-relative copyfrom path into a proper
    # copyfrom URI
    def _abs_copyfrom_path(self, path):
        return self.url.join(ClientURI(path, False))

