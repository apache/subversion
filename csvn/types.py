from csvn.core import *

class SvnDate(str):

    def as_apr_time_t(self):
        """Return this date to an apr_time_t object"""
        pool = Pool()
        when = apr_time_t()
        svn_time_from_cstring(byref(when), self, pool)
        return when

    def as_human_string(self):
        """Return this date to a human-readable date"""
        pool = Pool()
        return str(svn_time_to_human_cstring(self.as_apr_time_t(), pool))

class LogEntry(object):
    """REVISION, AUTHOR, DATE, and MESSAGE are straightforward, and
       contain what you expect. DATE is an SvnDate object.

       If no information about the paths changed in this revision is
       available, CHANGED_PATHS will be None. Otherwise, CHANGED_PATHS
       will contain a dictionary which maps every path committed
       in REVISION to svn_log_changed_path_t pointers."""

    __slots__ = ['changed_paths', 'revision',
                 'author', 'date', 'message']

