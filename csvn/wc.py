from csvn.core import *
from csvn.auth import *
from ctypes import *
import csvn.types as _types
import os, sys

class WC(object):
    """A SVN working copy."""

    def __init__(self, path="", user=User()):
        """Open a working copy directory relative to PATH"""

        self.pool = Pool()
        self.iterpool = Pool()
        self.path = path
        self.user = user

        self.client = POINTER(svn_client_ctx_t)()
        svn_client_create_context(byref(self.client), self.pool)
        self._as_parameter_ = POINTER(svn_ra_session_t)()
        
        self.user.setup_auth_baton(pointer(self.client.contents.auth_baton))

        self.client[0].notify_func2 = \
            svn_wc_notify_func2_t(self._notify_func_wrapper)
        self.client[0].notify_baton2 = cast(id(self), c_void_p)
        self._notify_func = None
        
        self.client[0].cancel_func = \
            svn_cancel_func_t(self._cancel_func_wrapper)
        self.client[0].cancel_baton = cast(id(self), c_void_p)
        self._cancel_func = None
        
        self.client[0].progress_func = \
            svn_ra_progress_notify_func_t(self._progress_func_wrapper)
        self.client[0].progress_baton =  cast(id(self), c_void_p)
        self._progress_func = None
        
        self._status_func = \
            svn_wc_status_func2_t(self._status_wrapper)
        self._status = None
        
        self._info_func = \
            svn_info_receiver_t(self._info_wrapper)
        self._info = None
        
        self._list_func = \
            svn_client_list_func_t(self._list_wrapper)
        self._list = None
        
        self.client[0].log_msg_func2 = \
            svn_client_get_commit_log2_t(self._log_func_wrapper)
        self.client[0].log_msg_baton2 = cast(id(self), c_void_p)
        self._log_func = None

    def copy(self, src, dest, rev = ""):
        """Copy SRC to DEST"""

        opt_rev = svn_opt_revision_t()
        dummy_rev = svn_opt_revision_t()
        svn_opt_parse_revision(byref(opt_rev), byref(dummy_rev),
                               str(rev), self.iterpool)

        svn_client_copy3(NULL,
                         self._build_path(src),
                         byref(opt_rev),
                         self._build_path(dest),
                         self.client, self.iterpool)

        self.iterpool.clear()

    def move(self, src, dest, force = False):
        """Move SRC to DEST"""

        svn_client_move3(NULL,
                         self._build_path(src),
                         self._build_path(dest),
                         force,
                         self.client, self.iterpool)
        self.iterpool.clear()

    def delete(self, paths, force = False):
        """Schedule PATHS to be deleted"""

        svn_client_delete2(NULL, self._build_path_list(paths),
                           force, self.client, self.iterpool)
        self.iterpool.clear()

    def add(self, path, recurse = True, force = False, no_ignore = False):
        """Schedule PATH to be added"""

        svn_client_add3(self._build_path(path),
                        recurse, force, no_ignore, self.client,
                        self.iterpool)
        self.iterpool.clear()

    def resolve(self, path, recurse = True):
        """Resolve a conflict on PATH"""

        svn_client_resolved(path, recurse, self.client, self.iterpool)
        self.iterpool.clear()

    def revert(self, paths, recurse = False):
        """Revert PATHS to the most recent version. If RECURSE is TRUE, the
        revert will recurse through directories."""

        svn_client_revert(self._build_path_list(paths), recurse,
                          self.client, self.iterpool)
        self.iterpool.clear()

    def _build_path_list(self, paths):
        """Build a list of canonicalized WC paths"""
        if isinstance(paths, (str, unicode, String)):
            paths = [paths]
        canonicalized_paths = [self._build_path(path) for path in paths]
        return _types.Array(String, canonicalized_paths)

    def _build_path(self, path):
        """Build a canonicalized path to an item in the WC"""
        return svn_path_canonicalize(os.path.join(self.path, path),
                                     self.iterpool)

    def set_notify_func(self, notify_func):
        """Setup a callback so that you can be notified when paths are
           affected by WC operations. When paths are affected, we will
           call the function with an svn_wc_notify_t object.

           For details on the contents of an svn_wc_notify_t object,
           see the documentation for svn_wc_notify_t."""
        self._notify_func = notify_func

    # A helper function which invokes the user notify function with
    # the appropriate arguments.
    def _notify_func_wrapper(baton, notify, pool):
        self = cast(baton, py_object).value
        if self._notify_func:
            self._notify_func(notify[0])
    _notify_func_wrapper = staticmethod(_notify_func_wrapper)
    
    def set_cancel_func(self, cancel_func):
        """Setup a callback so that you can cancel operations. At
        various times the cancel function will be called, giving
        the option of cancelling the operation."""
        
        self._cancel_func = cancel_func
        
    #Just like _notify_func_wrapper, above.
    def _cancel_func_wrapper(baton):
        self = cast(baton, py_object).value
        if self._cancel_func:
            self._cancel_func()
    _cancel_func_wrapper = staticmethod(_cancel_func_wrapper)
    
    def set_progress_func(self, progress_func):
        """Setup a callback for network progress information. This callback
        should accept two apr_off_t objects, being the number of bytes sent
        and the number of bytes to send.
        
        For details of apr_off_t objects, see the apr_off_t documentation."""
        
        self._progress_func = progress_func
        
    def _progress_func_wrapper(progress, total, baton, pool):
        self = cast(baton, py_object).value
        if self._progress_func:
            self._progress_func(progress, total)
    _progress_func_wrapper = staticmethod(_progress_func_wrapper)

    def diff(self, path1="", revnum1=None, path2=None, revnum2=None,
             diff_options=[], recurse=True, ignore_ancestry=True,
             no_diff_deleted=False, ignore_content_type=False,
             header_encoding="", outfile = sys.stdout, errfile = sys.stderr):
        """Produce svn diff output that describes the difference between
        PATH1 at REVISION1 and PATH2 at REVISION2.
        
        If PATH2 is not given, it defaults to PATH1.
        
        REVISION1 will default to svn_opt_revision_base if it is not given.
        
        REVISION2 will default to svn_opt_revision_working if it is not given.
        
        DIFF_OPTIONS will be passed to the diff process.
        
        If RECURSE is True (True by deafult) then directories will be
        recursed.
        
        If IGNORE_ANCESTRY is True (True by default) then items will not be
        checked for relatedness before being diffed.
        
        If NO_DIFF_DELETED is True (False by default) then deleted items will
        not be included in the diff.
        
        If IGNORE_CONTENT_TYPE is True (False by default) then diffs will be
        generated for binary file types.
        
        Generated headers will be encoded using HEADER_ENCODING ("" by
        default).

        The resulting diff will be sent to OUTFILE, which defaults to
        sys.stdout. Any errors will be printed to ERRFILE, which defaults
        to sys.stderr."""
                    
        diff_options = self._build_path_list(diff_options)
        
        rev1 = svn_opt_revision_t()
        if revnum1:
            rev1.kind = svn_opt_revision_number
            rev1.value.number = revnum1
        else:
            rev1.kind = svn_opt_revision_base
        
        rev2 = svn_opt_revision_t()
        if revnum2:
            rev2.kind = svn_opt_revision_number
            rev2.value.number = revnum2
        else:
            rev2.kind = svn_opt_revision_working
        
        path1 = self._build_path(path1)
        if path2:
            path2 = self._build_path(path2)
        else:
            path2 = path1

        # Create temporary objects for output and errors
        apr_outfile = _types.APRFile(outfile)
        apr_errfile = _types.APRFile(errfile)

        svn_client_diff3(diff_options, path1, rev1, path2, rev2, recurse,
            ignore_ancestry, no_diff_deleted, ignore_content_type,
            header_encoding, apr_outfile, apr_errfile, self.client,
            self.iterpool)

        # Close the APR wrappers
        apr_outfile.close()
        apr_errfile.close()

        self.iterpool.clear()
        
    def cleanup(self, path=""):
        """Recursively cleanup the working copy. Finish any incomplete
        operations and release all locks.
        
        If PATH is not provided, it defaults to the WC root."""
        svn_client_cleanup(self._build_path(path), self.client,
                            self.iterpool)
                            
        self.iterpool.clear()
        
    def export(self, from_path, to_path, overwrite=False,
                ignore_externals=True, recurse=True, eol=NULL):
        """Export FROM_PATH to TO_PATH.
        
        If OVERWRITE is True (False by default), TO_PATH will be overwritten.
        
        If IGNORE_EXTERNALS is True (True by default), externals will be
        excluded during export.
        
        If RECURSE is True (True by default) directories will be recursed.
        Otherwise only the top level, non-directory contents of FROM_PATH
        will be exported.
        
        If EOL is provided, it will be used instead of the standard end of
        line marker for your platform."""
        
        svn_client_export3(pointer(svn_revnum_t()),
                            self._build_path(from_path),
                            self._build_path(to_path), NULL,
                            svn_opt_revision_t(), overwrite,
                            ignore_externals, recurse, eol, self.client,
                            self.iterpool)
                            
        self.iterpool.clear()
        
    def resolved(self, path="", recursive=True):
        """Resolve a conflict on PATH. Marks the conflict as resolved, does
        not change the text of PATH.
        
        If RECURSIVE is True (True by default) then directories will be
        recursed."""
        svn_client_resolved(self._build_path(path), recursive, self.client,
                            self.iterpool)
        self.iterpool.clear()

    def mkdir(self, paths):
        """Create a directory or directories in the working copy. PATHS can
        be either a single path of a list of paths to be created."""
        paths = self._build_path_list(paths)
        
        # The commit info shouldn't matter, this is a method of the WC
        # class, so it isn't intended for remote operations.
        info = pointer(svn_commit_info_t())
        
        svn_client_mkdir2(byref(info), paths, self.client, self.iterpool)
        
        self.iterpool.clear()
        
    def propset(self, propname, propval, target="", recurse=True,
                skip_checks=False):
        """Set PROPNAME to PROPVAL for TARGET.
        
        If RECURSE is True (True by default) and TARGET is a directory, the
        operation will recurse.
        
        If SKIP_CHECKS is True (False by default) no sanity checks will be
        performed on PROPNAME."""
        
        svn_client_propset2(propname,
                            svn_string_create(propval, self.iterpool),
                            self._build_path(target), recurse, skip_checks,
                            self.client, self.iterpool)
                            
        self.iterpool.clear()
        
    def proplist(self, target="", recurse=True):
        """Returns an array of the values of the normal properties of TARGET,
        which defaults to the working copy root. The contents of the array
        are svn_client_proplist_item_t objects.
        
        If RECURSE is True, directories will be recursed."""
        peg_revision = svn_opt_revision_t()
        peg_revision.kind = svn_opt_revision_unspecified
        
        revision = svn_opt_revision_t()
        revision.kind = svn_opt_revision_working
        
        props = _types.Array(svn_client_proplist_item_t)
        
        svn_client_proplist2(byref(props.header), self._build_path(target),
                     byref(peg_revision), byref(revision), recurse,
                     self.client, self.iterpool)
        
        self.iterpool.clear()
                 
        return props
        
    def propget(self, propname, target="", recurse=True):
        """Get the the value of PROPNAME for TARGET. Returns a hash the keys
        of which are file paths and the values are the value of PROPNAME for
        the corresponding file. The values of the hash are c_char_p objects,
        which can be treated much like strings.
        
        If RECURSE is True (True by default) directories will be recursed."""
        peg_revision = svn_opt_revision_t()
        peg_revision.kind = svn_opt_revision_unspecified
        
        revision = svn_opt_revision_t()
        revision.kind = svn_opt_revision_working
        
        props = _types.Hash(c_char_p)
        
        svn_client_propget2(byref(props.hash), propname,
                    self._build_path(target), byref(peg_revision),
                    byref(revision), recurse, self.client, self.pool)
                    
        return props
    
    # Internal method to wrap status callback.
    def _status_wrapper(baton, path, status):
        self = cast(baton, py_object).value
        
        if self._status:
            self._status(path, status.contents)
    _status_wrapper = staticmethod(_status_wrapper)
    
    def set_status_func(self, status):
        """Set a callback function to be used the next time the status method
        is called."""
        self._status = status
    
    def status(self, path="", status=None, recurse=True,
                get_all=False, update=False, no_ignore=False,
                ignore_externals=False):
        """Get the status on PATH using callback to STATUS.
        
        PATH defaults to the working copy root.
        
        If STATUS is not provided, a callback previously registered using
        set_status_func will be used. If a callback has not been set, nothing
        will happen.
        
        If RECURSE is True (True by default) directories will be recursed.
        
        If GET_ALL is True (False by default), all entires will be retrieved,
        otherwise only interesting entries (local modifications and/or
        out-of-date) will be retrieved.
        
        If UPDATE is True (False by default) the repository will be contacted
        to get information about out-of-dateness. A svn_revnum_t will be
        returned to indicate which revision the working copy was compared
        against.
        
        If NO_IGNORE is True (False by default) nothing will be ignored.
        
        If IGNORE_EXTERNALS is True (False by default), externals will be
        ignored."""
        rev = svn_opt_revision_t()
        rev.kind = svn_opt_revision_working
        
        if status:
            self.set_status_func(status)
        
        result_rev = svn_revnum_t()
        svn_client_status2(byref(result_rev), self._build_path(path),
                            byref(rev), self._status_func,
                            cast(id(self), c_void_p), recurse, get_all,
                            update, no_ignore, ignore_externals, self.client,
                            self.iterpool)
                            
        self.iterpool.clear()
                            
        return result_rev
        
    def _info_wrapper(baton, path, info, pool):
        self = cast(baton, py_object).value
        
        if self._info:
            self._info(path, info.contents)
    _info_wrapper = staticmethod(_info_wrapper)
    
    def set_info_func(self, info):
        """Set a callback to be used to process svn_info_t objects during
        calls to the info method.
        
        INFO should accept a path and a svn_info_t object as arguments."""
        self._info = info
        
    def info(self, path="", recurse=True, info_func=None):
        """Get info about PATH (defaults to the WC root), using callbacks to
        INFO_FUNC. INFO_FUNC may be set earlier using set_info_func. If
        INFO_FUNC is not provided, the previously registered callback will be
        used.
        
        If RECURSE is True (True by default), directories will be
        recursed."""
        
        if info_func:
            self.set_info_func(info_func)
        
        svn_client_info(self._build_path(path), NULL, NULL, self._info_func,
                cast(id(self), c_void_p), recurse, self.client,
                self.iterpool)
        
    def checkout(self, url, revnum=None, path=None, recurse=True,
                 ignore_externals=False):
        """Checkout a new working copy from URL@PEG_REV at REVNUM. The new
        working copy will be created at path, which defaults to the working
        copy root.
        
        If RECURSE is True (True by default) the contents of any directories
        that are checked out will also be checked out.
        
        If IGNORE_EXTERNALS is True (False by default) externals will not be
        checked out."""

        rev = svn_opt_revision_t()
        if revnum is not None:
            rev.kind = svn_opt_revision_number
            rev.value.number = revnum
        else:
            rev.kind = svn_opt_revision_head

        peg_rev = svn_opt_revision_t()
        URL = String("")
        svn_opt_parse_path(byref(peg_rev), byref(URL), url, self.iterpool)

        if not path:
            path = self.path
        canon_path = svn_path_canonicalize(path, self.iterpool)

        result_rev = svn_revnum_t()

        svn_client_checkout2(byref(result_rev), URL, canon_path,
                             byref(peg_rev), byref(rev), recurse,
                             ignore_externals, self.client, self.iterpool)
                             
    def set_log_func(self, log_func):
        """Register a callback to get a log message for commit and
        commit-like operations. LOG_FUNC should take an array as an argument,
        which holds the files to be commited. It should return a list of the
        form [LOG, FILE] where LOG is a log message and FILE is the temporary
        file, if one was created instead of a log message. If LOG is None,
        the operation will be canceled and FILE will be treated as the
        temporary file holding the temporary commit message."""
        self._log_func = log_func
        
    def _log_func_wrapper(log_msg, tmp_file, commit_items, baton, pool):
        self = cast(baton, py_object).value
        if self._log_func:
            [log, file] = self._log_func(_types.Array(String, commit_items))
            
            if log:
                log_msg = pointer(c_char_p(log))
            else:
                log_msg = NULL
                
            if file:
                tmp_file = pointer(c_char_p(file))
            else:
                tmp_file = NULL
                
    _log_func_wrapper = staticmethod(_log_func_wrapper)
    
    def commit(self, paths="", recurse=True, keep_locks=False):
        """Commit changes in the working copy. Changes to PATHS will be
        commited. If RECURSE is True (True by default), the contents of
        directories will also be commited.
        
        If KEEP_LOCKS is True (False by default), locks will not be
        relinquished during the commit."""
        
        commit_info = pointer(svn_commit_info_t())
        svn_client_commit3(byref(commit_info), self._build_path_list(paths),
                            recurse, keep_locks, self.client, self.iterpool)
                            
        return commit_info.value
        
    def update(self, paths="", revnum=None, recurse=True,
                ignore_externals=True):
        """Update PATHS to REVNUM. PATHS defaults to the working copy root.
        If no revnum is given, it defaults to HEAD. An array containing the
        revisions to which revnum was resolved.
        
        If RECURSE is True (True by default), contents of directories will
        also be updated.
        
        If IGNORE_EXTERNALS is True (True by default) externals will not be
        updated."""
        
        rev = svn_opt_revision_t()
        if revnum is not None:
            rev.kind = svn_opt_revision_number
            rev.value.number = revnum
        else:
            rev.kind = svn_opt_revision_head
        
        result_revs = _types.Array(svn_revnum_t, svn_revnum_t())
        
        svn_client_update2(byref(result_revs.header),
                self._build_path_list(paths), byref(rev), recurse,
                ignore_externals, self.client, self.iterpool)
                   
        return result_revs
    
    #internal method to wrap ls callbacks
    def _list_wrapper(baton, path, dirent, abs_path, pool):
        self = cast(baton, py_object).value
        if self._list:
            self._list(path, dirent.contents, lock.contents, abs_path)
    _list_wrapper = staticmethod(_list_wrapper)
        
    def set_list_func(self, list_func):
        """Set the callback function for list operations."""
        self._list = list_func
        
    def list(self, path="", recurse=True, fetch_locks=False, list_func=None):
        """List items in the working copy. Fetch information about PATH
        (which defaults to the WC root). If PATH is a directory and RECURSE
        is True (True by default) then this operation will recurse.
        
        If FETCH_LOCKS is True (False by default), locks will be included.
        
        If LIST_FUNC is provided, it will be set as the callback to be used.
        """
        peg = svn_opt_revision_t()
        peg.kind = svn_opt_revision_working
        
        rev = svn_opt_revision_t()
        rev.kind = svn_opt_revision_working
        
        if list_func:
            self.set_list_func(list_func)
        
        svn_client_list(self._build_path(path), byref(peg), byref(rev),
            recurse, SVN_DIRENT_ALL, fetch_locks, self._list_func,
            cast(id(self), c_void_p), self.client, self.iterpool)

    def relocate(self, from_url, to_url, dir="", recurse=True):
         """Modify a working copy directory DIR (defaults to WC root),
         changing any repository URLs that begin with FROM_URL to begin with
         TO_URL instead, recursing into subdirectories if RECURSE is True
         (True by default)."""
         
         svn_client_relocate(self._build_path(dir), from_url, to_url, recurse,
                    self.client, self.iterpool)
                     
         self.iterpool.clear()
         
    def switch(self, path, url, revnum=None, recurse=True):
        """Switch working copy PATH to URL at REVNUM. If REVNUM is not
        provided, it defaults to the head revision.
         
        If RECURSE is True, the operation will recurse."""
        result_rev = svn_revnum_t()
        
        revision = svn_opt_revision_t()
        if revnum:
            revision.kind = svn_opt_revision_number
            revision.value.number = revnum
        else:
            revision.kind = svn_opt_revision_head
        
        svn_client_switch(byref(result_rev),
                  self._build_path(path), url, byref(revision),
                  recurse, self.client, self.pool)
    
    def lock(self, paths, comment=NULL, steal_lock=False):
        """Lock the list of WC targets PATHS. If COMMENT is provided, it is
        used as the comment for the lock. If STEAL_LOCK is True (False by
        default), the lock will be created even if the file is already locked.
        
        Alternately, PATHS may be a list of URL targets. In this case, the
        locks are created directly in the repository."""
        targets = _types.Array(c_char_p, paths)
        svn_client_lock(targets, comment, steal_lock, self.client, self.pool)
        
    def unlock(self, paths, break_lock=False):
        """Unlock the list of WC targets PATHS. If BREAK_LOCK is True (False
        by default), the locks willbe broken in the repository as well.
        
        Alternately, PATHS may be alist of URL targets. In this case, the
        locks are destroyed in the repository."""
        targets = _types.Array(c_char_p, paths)
        svn_client_unlock(targets, break_lock, self.client, self.pool)
        
    def merge(self, source1, revnum1, source2, revnum2, target_wcpath,
                recurse=True, ignore_ancestry=False, force=False,
                dry_run=False, merge_options=[]):
        """Merge changes form SOURCE1@REVNUM1 to SOURCE2@REVNUM2 into the
        working copy path TARGET_WCPATH.
        
        If RECURSE is True (True by default) apply changes recursively.
        
        If IGNORE_ANCESTRY is True (False by default) relatedness will not be
        checked.
        
        If FORCE is False (False by default) and the merge involves deleting
        locally modified files, the merge will fail.
        
        If DRY_RUN is True (False by Default) full notification is provided,
        but no local files are modified.
        
        MERGE_OPTIONS is a list of other options to be passed to the diff
        process."""
        
        revision1 = svn_opt_revision_t()
        revision1.kind = svn_opt_revision_number
        revision1.value.number = revnum1
        
        revision2 = svn_opt_revision_t()
        revision2.kind = svn_opt_revision_number
        revision2.value.number = revnum2
        
        merge_options = _types.Array(c_char_p, merge_options)
        
        svn_client_merge2(source1, byref(revision1), source2, byref(revision2),
            target_wcpath, recurse, ignore_ancestry, force, dry_run,
            merge_options.header, self.client, self.iterpool)
