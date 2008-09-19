#!/usr/bin/env python
#
# svnmerge-migrate-history-remotely.py: Remotely migrate merge history from
#     svnmerge.py's format to Subversion 1.5's format.
#
# ====================================================================
# Copyright (c) 2008 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================

# $HeadURL$
# $Date$
# $Author$
# $Rev$


### THE SVNMERGE HISTORY MIGRATION ALGORITHM EMPLOYED HEREIN
###
### 1. Fetch properties for PATH, looking specifically for
###    'svnmerge-blocked', and 'svnmerge-integrated'.
###
### 2. Convert properties into real mergeinfo.  NOTE: svnmerge-*
###    properties have a slightly different and more flexible syntax.
###
### 3. Combine mergeinfos together.
###
### 4. [non-naive] Subtract natural history of the merge target from
###    its own mergeinfo.
###
### 5. [non-naive] Filter mergeinfo by merge source natural history
###    (so that mergeinfo only reflects real locations).
###
### 6. Convert mergeinfo back into a property value.  If it reflects
###    a change over the previously existing mergeinfo, or there were
###    some svnmerge-* properties to delete, then write the new
###    mergeinfo and delete the svnmerge-* properties.


import sys
import os
import urllib
import getopt
try:
    my_getopt = getopt.gnu_getopt
except AttributeError:
    my_getopt = getopt.getopt
try:
    from svn import client, ra, core
    svn_version = (core.SVN_VER_MAJOR, core.SVN_VER_MINOR, core.SVN_VER_PATCH)
    if svn_version < (1, 5, 0):
        raise ImportError
except ImportError:
    raise Exception, \
          "Subversion Python bindings version 1.5.0 or newer required."

### -------------------------------------------------------------------------

def errput(msg, only_a_warning=False):
    """Output MSG to stderr, possibly decorating it as an error
    instead of just a warning as determined by ONLY_A_WARNING."""

    if not only_a_warning:
        sys.stderr.write("ERROR: ")
    sys.stderr.write(msg + "\n")

def make_optrev(rev):
    """Return an svn_opt_revision_t build from revision specifier REV.
    REV maybe be an integer value or one of the following string
    specifiers: 'HEAD', 'BASE'."""

    try:
        revnum = int(rev)
        kind = core.svn_opt_revision_number
    except ValueError:
        revnum = -1
        if rev == 'HEAD':
            kind = core.svn_opt_revision_head
        elif rev == 'BASE':
            kind = core.svn_opt_revision_base
        else:
            raise Exception("Unsupported revision specified '%s'" % str(rev))
    optrev = core.svn_opt_revision_t()
    optrev.kind = kind
    optrev.value.number = revnum
    return optrev

def svnmerge_prop_to_mergeinfo(svnmerge_prop_val):
    """Parse svnmerge-* property value SVNMERGE_PROP_VAL (which uses
    any whitespace for delimiting sources and stores source paths
    URI-encoded) into Subversion mergeinfo."""

    if svnmerge_prop_val is None:
        return None

    # First we convert the svnmerge prop value into an svn:mergeinfo
    # prop value, then we parse it into mergeinfo.
    sources = svnmerge_prop_val.split()
    svnmerge_prop_val = ''
    for source in sources:
        pieces = source.split(':')
        if len(pieces) != 2:
            continue
        pieces[0] = urllib.unquote(pieces[0])
        svnmerge_prop_val = svnmerge_prop_val + '%s\n' % (':'.join(pieces))
    return core.svn_mergeinfo_parse(svnmerge_prop_val or '')

def mergeinfo_merge(mergeinfo1, mergeinfo2):
    """Like svn.core.svn_mergeinfo_merge(), but preserves None-ness."""

    if mergeinfo1 is None and mergeinfo2 is None:
        return None
    if mergeinfo1 is None:
        return mergeinfo2
    if mergeinfo2 is None:
        return mergeinfo1
    return core.svn_mergeinfo_merge(mergeinfo1, mergeinfo2)

def relative_path_from_urls(root_url, url, leading_slash=False):
    """Return the relative path (not URI-encoded) created by
    subtracting from URL the ROOT_URL.  Add a LEADING_SLASH if
    requested."""
    
    if root_url == url:
        return leading_slash and '/' or ''
    assert url.startswith(root_url)
    if leading_slash:
        return urllib.unquote(url[len(root_url):])
    else:
        return urllib.unquote(url[len(root_url)+1:])

def pretty_print_mergeinfo(mergeinfo, indent=0):
    """Print MERGEINFO hash, one source per line, with a given INDENT."""
    
    mstr = core.svn_mergeinfo_to_string(mergeinfo)
    sys.stdout.write('\n'.join(map(lambda x: indent * ' ' + x,
                                   filter(None, mstr.split('\n')))) + '\n')
    
    
### -------------------------------------------------------------------------
    
class SvnClient:
    """Subversion client operation abstraction."""
    
    def __init__(self, config_dir=None):
        core.svn_config_ensure(config_dir)
        self.ctx = client.ctx_t()
        self.ctx.auth_baton = core.svn_auth_open([
            client.get_simple_provider(),
            client.get_username_provider(),
            client.get_ssl_server_trust_file_provider(),
            client.get_ssl_client_cert_file_provider(),
            client.get_ssl_client_cert_pw_file_provider(),
            ])
        self.ctx.config = core.svn_config_get_config(config_dir)
        if config_dir is not None:
            core.svn_auth_set_parameter(self.ctx.auth_baton,
                                        core.SVN_AUTH_PARAM_CONFIG_DIR,
                                        config_dir)
        self.ra_callbacks = ra.callbacks_t()
        self.ra_callbacks.auth_baton = self.ctx.auth_baton
        self.base_optrev = make_optrev('BASE')

    def get_path_urls(self, path):
        """Return a 2-tuple containing the repository URL associated
        with the versioned working file or directory located at
        PATH and the repository URL for the same."""
    
        infos = []
        def _info_cb(infopath, info, pool, retval=infos):
          infos.append(info)
        client.svn_client_info(path, self.base_optrev, self.base_optrev,
                               _info_cb, 0, self.ctx)
        assert len(infos) == 1
        return infos[0].URL, infos[0].repos_root_URL

    def get_path_revision(self, path):
        """Return the current base revision of versioned file or
        directory PATH."""
        
        infos = []
        def _info_cb(infopath, info, pool, retval=infos):
          infos.append(info)
        client.svn_client_info(path, self.base_optrev, self.base_optrev,
                               _info_cb, 0, self.ctx)
        assert len(infos) == 1
        return infos[0].rev
        
    def get_path_mergeinfo(self, path, root_url=None):
        mergeinfo = client.mergeinfo_get_merged(path, self.base_optrev,
                                                self.ctx)
        if not mergeinfo:
            return mergeinfo
        if not root_url:
            path_url, root_url = self.get_path_urls(path)
            if not root_url:
                ras = self.cc.open_ra_session(url)
                root_url = ra.get_repos_root(ras)
        assert root_url
        new_mergeinfo = {}
        for key, value in mergeinfo.items():
            new_key = relative_path_from_urls(root_url, key, True)
            new_mergeinfo[new_key] = value
        return new_mergeinfo

    def get_path_property(self, path, propname):
        rev = self.base_optrev
        prophash, revnum = client.propget3(propname, path, rev, rev,
                                           core.svn_depth_empty, None,
                                           self.ctx)
        return prophash.get(path, None)

    def set_path_property(self, path, propname, propval):
        client.propset3(propname, propval, path, core.svn_depth_empty,
                        0, core.SVN_INVALID_REVNUM, None, None, self.ctx)

    def get_history_as_mergeinfo(self, ra_session, rel_path, rev,
                                 oldest_rev=core.SVN_INVALID_REVNUM):
        """Return the natural history of REL_PATH in REV, between OLDEST_REV
        and REV, as mergeinfo.  If OLDEST_REV is core.SVN_INVALID_REVNUM,
        all of PATH's history prior to REV will be returned.  REL_PATH is
        relative to the session URL of RA_SESSION.
        (Adapted from Subversion's svn_client__get_history_as_mergeinfo().)"""

        # Fetch the location segments in the history.
        location_segments = []
        def _segment_receiver(segment, pool):
            location_segments.append(segment)
        ra.get_location_segments(ra_session, rel_path, rev, rev,
                                 oldest_rev, _segment_receiver)

        # Transform location segments into merge sources and ranges.
        mergeinfo = {}
        for segment in location_segments:
            if segment.path is None:
                continue
            source_path = '/' + segment.path
            path_ranges = mergeinfo.get(source_path, [])
            range = core.svn_merge_range_t()
            range.start = max(segment.range_start - 1, 0)
            range.end = segment.range_end
            range.inheritable = 1
            path_ranges.append(range)
            mergeinfo[source_path] = path_ranges
        return mergeinfo

    def open_ra_session(self, session_url):
        return ra.open(session_url, self.ra_callbacks, None, self.ctx.config)

### -------------------------------------------------------------------------

class SvnmergeHistoryMigrator:
    """svnmerge.py tracking data conversion class."""
    
    def __init__(self, client_context, verbose=False, naive=False):
        self.cc = client_context
        self.verbose = verbose
        self.naive = naive

    def migrate_path(self, path):
        sys.stdout.write("Searching for merge tracking information...\n")
        
        # Get svnmerge-integrated property for PATH, as Subversion mergeinfo.
        integrated_mergeinfo = svnmerge_prop_to_mergeinfo(
            self.cc.get_path_property(path, 'svnmerge-integrated'))
        if integrated_mergeinfo and self.verbose:
            sys.stdout.write("Found svnmerge-integrated:\n")
            pretty_print_mergeinfo(integrated_mergeinfo, 3)
            
        # Get svnmerge-blocked property for PATH, as Subversion mergeinfo.
        blocked_mergeinfo = svnmerge_prop_to_mergeinfo(
            self.cc.get_path_property(path, 'svnmerge-blocked'))
        if blocked_mergeinfo and self.verbose:
            sys.stdout.write("Found svnmerge-blocked:\n")
            pretty_print_mergeinfo(blocked_mergeinfo, 3)

        # No svnmerge tracking data?  Nothing to do.
        if not (integrated_mergeinfo or blocked_mergeinfo):
            errput("No svnmerge.py tracking data found for '%s'." % (path),
                   True)
            return

        # Fetch Subversion mergeinfo for PATH.  Hopefully there is
        # none, but if there is, we'll assume folks want to keep it.
        orig_mergeinfo = self.cc.get_path_mergeinfo(path)
        if orig_mergeinfo and self.verbose:
            sys.stdout.write("Found Subversion mergeinfo:\n")
            pretty_print_mergeinfo(orig_mergeinfo, 3)

        # Merge all our mergeinfos together.
        new_mergeinfo = mergeinfo_merge(orig_mergeinfo, integrated_mergeinfo)
        new_mergeinfo = mergeinfo_merge(new_mergeinfo, blocked_mergeinfo)

        # Unless we're doing a naive migration (or we've no, or only
        # empty, mergeinfo anyway), start trying to cleanup after
        # svnmerge.py's history-ignorant initialization.
        if not self.naive and new_mergeinfo:
            
            sys.stdout.write("Sanitizing mergeinfo (this can take a "
                             "while)...\n")
            
            # What we need:
            #    - the relative path in the repository for PATH
            #    - repository root URL and an RA session rooted thereat
            #    - the base revision of PATH
            path_url, root_url = self.cc.get_path_urls(path)
            if root_url:
                ras = self.cc.open_ra_session(root_url)
            else:
                ras = self.cc.open_ra_session(path_url)
                root_url = ra.get_repos_root(ras)
                ra.reparent(ras, root_url)
            assert path_url.startswith(root_url)
            rel_path = relative_path_from_urls(root_url, path_url)
            path_rev = self.cc.get_path_revision(path)

            # We begin by subtracting the natural history of the merge
            # target from its own mergeinfo.
            implicit_mergeinfo = \
                self.cc.get_history_as_mergeinfo(ras, rel_path, path_rev)
            if self.verbose:
                sys.stdout.write("   subtracting natural history:\n")
                pretty_print_mergeinfo(implicit_mergeinfo, 6)
            new_mergeinfo = core.svn_mergeinfo_remove(implicit_mergeinfo,
                                                      new_mergeinfo)
            if self.verbose:
                sys.stdout.write("   remaining mergeinfo to be filtered:\n")
                pretty_print_mergeinfo(new_mergeinfo, 6)
                
            # Unfortunately, svnmerge.py tends to initialize using
            # oft-bogus revision ranges like 1-SOMETHING when the
            # merge source didn't even exist in r1.  So if the natural
            # history of a branch begins in some revision other than
            # r1, there's still going to be cruft revisions left in
            # NEW_MERGEINFO after subtracting the natural history.
            # So, we also examine the natural history of the merge
            # sources, and use that as a filter for the explicit
            # mergeinfo we've calculated so far.
            mergeinfo_so_far = new_mergeinfo
            new_mergeinfo = {}
            for source_path, ranges in mergeinfo_so_far.items():

                # If by some chance it is the case that /path:RANGE1
                # and /path:RANGE2 a) represent different lines of
                # history, and b) were combined into
                # /path:RANGE1+RANGE2 (due to the ranges being
                # contiguous), we'll foul this up.  But the chances
                # are preeeeeeeetty slim.
                for range in ranges:
                    try:
                        history = self.cc.get_history_as_mergeinfo(
                            ras, source_path[1:], range.end, range.start + 1)
                        if self.verbose:
                            sys.stdout.write("   new sanitized chunk:\n")
                            pretty_print_mergeinfo(history, 6)
                        new_mergeinfo = mergeinfo_merge(new_mergeinfo, history)
                    except svn.core.SubversionException, e:
                        if not (e.apr_err == svn.core.SVN_ERR_FS_NOT_FOUND
                                or e.apr_err == svn.core.SVN_ERR_FS_NO_SUCH_REVISION):
                            raise

        if self.verbose:
            sys.stdout.write("New converted mergeinfo:\n")
            pretty_print_mergeinfo(new_mergeinfo, 3)

        sys.stdout.write("Locally removing svnmerge properties and setting "
                         "new svn:mergeinfo property.\n")
        self.cc.set_path_property(path, 'svnmerge-integrated', None)
        self.cc.set_path_property(path, 'svnmerge-blocked', None)
        self.cc.set_path_property(path, 'svn:mergeinfo',
                                  core.svn_mergeinfo_to_string(new_mergeinfo))
            

### -------------------------------------------------------------------------

def usage_and_exit(errmsg=None):
    stream = errmsg and sys.stderr or sys.stdout
    stream.write("""Usage: %s [OPTIONS] BRANCH_PATH

Convert svnmerge.py tracking data found on the working copy
BRANCH_PATH into Subversion merge tracking data as a set of local
property modifications.  If BRANCH_PATH already has Subversion merge
tracking data, preserve it during the conversion process.  After this
script runs successfully, you can review and then commit the local
property modifications.  This script will *not* touch the contents of
any files at or under BRANCH_PATH -- it only effects property
modifications, which you can revert rather than commit if you so
desire.

NOTE: BRANCH_PATH need only be a depth-empty checkout of the branch
whose svnmerge.py tracking data you wish to convert.

NOTE: This script requires remote read access to the Subversion
repository whose working copy data you are trying to convert, but
currently does not implement prompting authentication providers.  You
must have valid cached credentials for this script to work.

Options:

   --help (-h, -?)      Show this usage message
   --verbose (-v)       Run in verbose mode
   --naive              Run a naive conversion (faster, but might generate
                           non-ideal results)
""" % (os.path.basename(sys.argv[0])))
    if errmsg:
        stream.write("\nERROR: %s\n" % (errmsg))
    sys.exit(errmsg and 1 or 0)

def main():
    try:
        opts, args = my_getopt(sys.argv[1:], "vh?",
                               ["verbose", "naive-mode", "help"])
    except:
        raise
        usage_and_exit("Unable to process arguments/options.")

    # Process arguments.
    if not args:
        usage_and_exit("No working copy path provided.")
    else:
        branch_path = core.svn_path_canonicalize(args[0])
        
    # Process options.
    verbose = naive_mode = False
    for opt, value in opts:
        if opt == "--help" or opt in ("-h", "-?"):
            usage_and_exit()
        elif opt == "--verbose" or opt == "-v":
            verbose = True
        elif opt == "--naive-mode":
            naive_mode = True
        else:
            usage_and_exit("Unknown option '%s'" % (opt))

    # Do the work.
    shm = SvnmergeHistoryMigrator(SvnClient(), verbose, naive_mode)
    shm.migrate_path(branch_path)

if __name__ == "__main__":
    main()
