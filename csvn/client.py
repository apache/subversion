import csvn.core as svn
from csvn.core import *
from txn import Txn
import os

class User(object):

    def __init__(self, username=None, password=None):
        """Create a user object which represents a user
           with the specified username and password."""

        self._username = username
        self._password = password
        self.pool = Pool()

    def username(self):
        """Return the current username.

           By default, this function just returns the username
           which was supplied in the constructor, but subclasses
           may behave differently."""
        return self._username

    def password(self):
        """Return the current password.

           By default, this function just returns the password
           which was supplied in the constructor, but subclasses
           may behave differently."""
        return self._password

    def allow_access(self, requested_access, path):
        """Check whether the current user has the REQUESTED_ACCESS
           to PATH.

           If PATH is None, this function should check if the
           REQUESTED_ACCESS is granted for at least one path
           in the repository.

           REQUESTED_ACCESS is an integer which may consist of
           any combination of the following fields:
              svn_authz_read:      The path can be read
              svn_authz_write:     The path can be altered
              svn_authz_recursive: The other access credentials
                                   are recursive.

           By default, this function always returns True, but
           subclasses may behave differently.

           This function is used by the "Repository" class to check
           permissions (see repos.py).

           FIXME: This function isn't currently used, because we
           haven't implemented higher-level authz yet.
        """

        return True

    def setup_auth_baton(self, auth_baton):

        # Setup the auth baton using the default options from the
        # command-line client
        svn_cmdline_setup_auth_baton(auth_baton, TRUE,
            self._username, self._password, NULL, TRUE, NULL,
            svn_cancel_func_t(), NULL, self.pool)


class ClientURI(object):
    """A URI to an object in a Subversion repository, stored internally in
       encoded format.

       When you supply URIs to a RemoteClient, or a transaction"""

    def __init__(self, uri, encoded=True):
        """Create a ClientURI object from a URI. If encoded=True, the
           input string may be URI-encoded."""
        pool = Pool()
        if not encoded:
            uri = svn_path_uri_encode(uri, pool)
        self._as_parameter_ = str(svn_path_canonicalize(uri, pool))

    def join(self, uri):
        """Join this URI and the specified relative URI,
           adding a slash if necessary."""
        pool = Pool()
        return ClientURI(svn_path_join(self, uri, pool))

    def dirname(self):
        """Get the parent directory of this URI"""
        pool = Pool()
        return ClientURI(svn_path_dirname(self, pool))

    def relative_path(self, uri, encoded=True):
        """Convert the supplied URI to a decoded path, relative to me."""
        pool = Pool()
        if not encoded:
            uri = svn_path_uri_encode(uri, pool)
        child_path = svn_path_is_child(self, uri, pool) or uri
        return str(svn_path_uri_decode(child_path, pool))

    def longest_ancestor(self, uri):
        """Get the longest ancestor of this URI and another URI"""
        pool = Pool()
        return ClientURI(svn_path_get_longest_ancestor(self, uri, pool))

    def __str__(self):
        """Return the URI as a string"""
        return self._as_parameter_

class ClientSession(object):

    def __init__(self, url, user=None):
        """Open a new session to URL with the specified USER.
           USER must be an object that implements the 'User' interface."""

        self.pool = Pool()
        self.iterpool = Pool()
        self.url = ClientURI(url)
        self.user = user

        self.client = POINTER(svn_client_ctx_t)()
        svn_client_create_context(byref(self.client), self.pool)

        self.user.setup_auth_baton(pointer(self.client.contents.auth_baton))

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

        If ENCODED is True, the path may be URI-encoded.
        """

        path = self._relative_path(path, encoded)
        if rev is None:
            rev = self.latest_revnum()
        kind = svn_node_kind_t()
        svn_ra_check_path(self, path, svn_revnum_t(rev), byref(kind),
                          self.iterpool)
        self.iterpool.clear()
        return kind.value

    def list(self, path, rev = SVN_INVALID_REVNUM, fields = SVN_DIRENT_ALL):
        """List the contents of the specified directory PATH@REV. This 
           function returns a dictionary, which maps entry names to
           directory entries (svn_dirent_t objects).

           If REV is not specified, we look at the latest revision of the
           repository.

           FIELDS controls what portions of the svn_dirent_t object are
           filled in. To have them completely filled in, just pass in
           SVN_DIRENT_ALL (which is the default); otherwise, pass the
           bitwise OR of all the SVN_DIRENT_ fields you would like to
           have returned to you.
        """
        dirents = Hash(POINTER(svn_dirent_t), None)
        svn_ra_get_dir2(self, dirents.byref(), NULL, NULL, path,
                        rev, fields, dirents.pool)
        return dirents

    def cat(self, buffer, path, rev = SVN_INVALID_REVNUM):
        """Get PATH@REV and save it to BUFFER. BUFFER must be a Python file
           or a StringIO object.

           If REV is not specified, we look at the latest revision of the
           repository."""
        stream = Stream(buffer)
        svn_ra_get_file(self, path, rev, stream, NULL, NULL, stream.pool)

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

class LocalClient(object):
    """A client which accesses the repository directly. This class
       may allow you to perform some administrative actions which
       cannot be performed remotely (e.g. create repositories,
       dump repositories, etc.)

       Unlike ClientSession, the functions in this class do not
       accept URIs, and instead only accept local filesystem
       paths.

       By default, this class does not perform any checks to verify
       permissions, assuming that the specified user has full
       administrative access to the repository. To teach this class
       to enforce an authz policy, you must subclass User
       and implement the allow_access function.
    """

    def __init__(self, path, create=False, user=None):
        """Open the repository at PATH. If create is True,
           create a new repository.

           If specified, user must be a User instance.
        """
        self.pool = Pool()
        self.iterpool = Pool()
        self._as_parameter_ = POINTER(svn_repos_t)()
        self.user = user
        if create:
            svn_repos_create(byref(self._as_parameter_), path,
                             None, None, None, None, self.pool)
        else:
            svn_repos_open(byref(self._as_parameter_), path, self.pool)
        self.fs = _fs(self)

    def latest_revnum(self):
        """Get the latest revision in the repository"""
        return self.fs.latest_revnum()

    def check_path(self, path, rev = None, encoded=False):
        """Check whether the given PATH exists in the specified REV. If REV
           is not specified, look at the latest revision.

        If the path is ...
          ... absent, then we return svn_node_node.
          ... a regular file, then we return svn_node_file.
          ... a directory, then we return svn_node_dir
          ... unknown, then we return svn_node_unknowna
        """
        assert(not encoded)
        root = self.fs.root(rev=rev, pool=self.iterpool)
        return root.check_path(path)

    def uuid(self):
        """Return a universally-unique ID for this repository"""
        return self.fs.uuid()

    def set_rev_prop(self, rev, name, value):
        """Set the NAME property to VALUE in the specified
           REV."""
        rev = svn_revnum_t(rev)
        svn_repos_fs_change_rev_prop2(self, rev, author, name, value,
                                      svn_repos_authz_func_t(),
                                      None, self.iterpool)
        self.iterpool.clear()

    def txn(self):
        """Open up a new transaction, so that you can commit a change
           to the repository"""
        assert(self.user is not None,
               "If you would like to commit changes to the repository, "
               "you must supply a user object when you initialize "
               "the repository object")
        return Txn(self)

    # Private. Produces a delta editor for the commit, so that the Txn
    # class can commit its changes over the RA layer.
    def _get_commit_editor(self, message, commit_callback, commit_baton, pool):
        editor = POINTER(svn_delta_editor_t)()
        editor_baton = c_void_p()
        svn_repos_get_commit_editor4(byref(editor),
            byref(editor_baton), self, None, "", "",
            self.user.username(), message,
            commit_callback, commit_baton, svn_repos_authz_callback_t(),
            None, pool)
        return (editor, editor_baton)

    def _relative_path(self, path):
        return path

    # Private. Convert a repository-relative copyfrom path into a proper
    # copyfrom URI
    def _abs_copyfrom_path(self, path):
        return path

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
        uuid_buffer = String()
        svn_fs_get_uuid(self, byref(uuid_buffer), self.iterpool)
        uuid_str = str(uuid_buffer)
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
