import csvn.core as svn
from csvn.core import *
from txn import Txn
import os

class ClientSession(object):

    def __init__(self, url, username=None, password=None):
        """Open a new session to URL with the specified USERNAME and
           PASSWORD"""

        self.pool = Pool()
        self.iterpool = Pool()
        self.url = url

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

    def check_path(self, path, rev = None):
        """Check the status of PATH@REV. If REV is not specified,
           look at the latest revision in the repository.

        If the path is ...
          ... absent, then we return svn_node_node.
          ... a regular file, then we return svn_node_file.
          ... a directory, then we return svn_node_dir
          ... unknown, then we return svn_node_unknown
        """

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

    # Private. Convert a repository-relative copyfrom path into a proper
    # copyfrom URI
    def _abs_copyfrom_path(self, path):
        return "%s/%s" % (self.url.rstrip("/"), path)

