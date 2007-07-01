from csvn.core import *
from ctypes import *
import csvn.types as _types
import os

class WC(object):
    """A SVN working copy."""

    def __init__(self, path="", user=None):
        """Open a working copy directory relative to PATH"""

        self.pool = Pool()
        self.iterpool = Pool()
        self.path = path

        self.client = POINTER(svn_client_ctx_t)()
        svn_client_create_context(byref(self.client), self.pool)
        self._as_parameter_ = POINTER(svn_ra_session_t)()

        self.client[0].notify_func2 = \
            svn_wc_notify_func2_t(self._notify_func_wrapper)
        self.client[0].notify_baton2 = cast(id(self), c_void_p)
        self._notify_func = None

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

    def diff(self, path="", diff_options=[], recurse=True,
                ignore_ancestry=True, no_diff_deleted=False,
                ignore_content_type=False, header_encoding="",
                outfile_name=None, errfile_name=None, append=False,
                return_strings=False):
        """Produce svn diff output that describes the difference between
        PATH at base revision and working copy.
        
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
        
        If OUTFILE_NAME is provided, then the diff will be written to
        OUTFILE_NAME. If OUTFILE_NAME does not exist, it will be created.
        If OUTFILE_NAME does exist, it will be truncated unless APPEND is
        True, in which case it will be appended. If OUTFILE_NAME
        is not provided, results will be printed to stdout.
        
        If ERRFILE_NAME is provided, then errors will be written to
        ERRFILE_NAME. If ERRFILE_NAME does not exist, it will be created.
        If ERRFILE_NAME does exist, it will be truncated unless APPEND is
        True, in which case it will be appended. If ERRFILE_NAME
        is not provided, results will be printed to stderr.
        
        If RETURN_STRINGS is True (False by default), a list of strings
        [out,err] will be returned. out holds the diff output, err holds
        the error output. RETURN_STRINGS can only be True when OUTFILE_NAME
        and ERRFILE_NAME are provided, otherwise it will be set to False,
        regardless of input.
        
        In the case that both APPEND and RETURN_STRINGS are True, the entire
        contents of OUTFILE_NAME will be returned in out and the entire
        contents of ERRFILE_NAME will be returned in err."""
                    
        diff_options = self._build_path_list(diff_options)
        
        rev1 = svn_opt_revision_t()
        rev1.kind = svn_opt_revision_base
        
        rev2 = svn_opt_revision_t()
        rev2.kind = svn_opt_revision_working
        
        path = self._build_path(path)
        
        if (not outfile_name) or (not errfile_name):
            # return_strings can only be True if output is not going to
            # stdout and stderr.
            return_strings = False
        
        outfile = pointer(apr_file_t())
        
        if outfile_name:
            if append:
                svn_io_file_open(byref(outfile), outfile_name,
                            APR_WRITE | APR_READ | APR_CREATE | APR_APPEND,
                            0644, self.iterpool)
            else:
                svn_io_file_open(byref(outfile), outfile_name,
                            APR_WRITE | APR_READ | APR_CREATE | APR_TRUNCATE,
                            0644, self.iterpool)
        else:
            # Default: Output to stdout
            apr_file_open_stdout(byref(outfile), self.iterpool)
            
        errfile = pointer(apr_file_t())
        
        if errfile_name:
            if append:
                svn_io_file_open(byref(errfile), errfile_name,
                    APR_WRITE | APR_READ | APR_CREATE | APR_APPEND, 0644,
                    self.iterpool)
            else:
                svn_io_file_open(byref(errfile), errfile_name,
                    APR_WRITE | APR_READ | APR_CREATE | APR_TRUNCATE, 0644,
                    self.iterpool)
        else:
            # Default: output to stderr
            apr_file_open_stderr(byref(errfile), self.iterpool)
        
        svn_client_diff3(diff_options, path, rev1, path,
                        rev2, recurse, ignore_ancestry, no_diff_deleted,
                        ignore_content_type, header_encoding, outfile,
                        errfile, self.client, self.iterpool)
        
        if return_strings:
            # Case to return strings with the diff contents
            outbuf = svn_stringbuf_create("", self.iterpool)
            errbuf = svn_stringbuf_create("", self.iterpool)
            
            # Make sure read starts at beginning
            svn_io_file_seek(outfile, APR_SET, pointer(c_longlong(0)),
                            self.iterpool)
            svn_io_file_seek(errfile, APR_SET, pointer(c_longlong(0)),
                            self.iterpool)
            
            svn_stringbuf_from_aprfile(byref(outbuf), outfile, self.iterpool)
            svn_stringbuf_from_aprfile(byref(errbuf), errfile, self.iterpool)
            
            svn_io_file_close(outfile, self.iterpool)
            svn_io_file_close(errfile, self.iterpool)
            
            return [outbuf.contents.data, errbuf.contents.data]
        
        svn_io_file_close(outfile, self.iterpool)
        svn_io_file_close(errfile, self.iterpool)
