#!/usr/bin/env python

"""Usage: svn2rss.py [OPTION...] REPOS-PATH

Generate an RSS 2.0 feed file containing commit information for the
Subversion repository located at REPOS-PATH.  Once the maximum number
of items is reached, older elements are removed.  The item title is
the revision number, and the item description contains the author,
date, log messages and changed paths.

Options:

 -h, --help             Show this help message.
 
 -f, --feed-file=PATH   Store the feed in the file located at PATH, which
                        will be created if it does not exist, or overwritten if
                        it does.  If not provided, the script will store the
                        feed in the current working directory, in a file named
                        REPOS_NAME.rss (where REPOS_NAME is the basename
                        of the REPOS_PATH command-line argument). 
 
 -r, --revision=X[:Y]   Subversion revision (or revision range) to generate
                        info for.  If not provided, info for the single
                        youngest revision in the repository will be generated.
 
 -m, --max-items=N      Keep only N items in the feed file.  By default,
                        20 items are kept.
 
 -u, --item-url=URL     Use URL as the basis for generating feed item links.
                        This value is appended with '?rev=REV_NUMBER' to form
                        the actual item links.
 
 -U, --feed-url=URL     Use URL as the global link associated with the feed.

 -P, --svn-path=DIR     Look in DIR for the svnlook binary.  If not provided,
                        svnlook must be on the PATH.
"""

import sys

# Python 2.3 is required by PyRSS2Gen
py_version  = sys.version_info
if sys.version_info[0:2] < (2,3):
    sys.stderr.write("Error: Python 2.3 or higher required.\n")
    sys.exit(1)

# And Py2RSSGen is required by this script
try:
    import PyRSS2Gen
except ImportError:
    print >> sys.stderr, "Error: Required PyRSS2Gen module not found."
    print >> sys.stderr, "PyRSS2Gen can be downloaded from:"
    print >> sys.stderr, "http://www.dalkescientific.com/Python/PyRSS2Gen.html"
    print >> sys.stderr, ""
    sys.exit(1)

import getopt
import os
import popen2
import cPickle as pickle
import datetime

def usage_and_exit(errmsg=None):
    """Print a usage message, plus an ERRMSG (if provided), then exit.
    If ERRMSG is provided, the usage message is printed to stderr and
    the script exits with a non-zero error code.  Otherwise, the usage
    message goes to stdout, and the script exits with a zero
    errorcode."""
    if errmsg is None:
        stream = sys.stdout
    else:
        stream = sys.stderr
    print >> stream, __doc__
    if errmsg:
        print >> stream, "\nError: %s" % (errmsg)
        sys.exit(2)
    sys.exit(0)

def check_url(url, opt):
    """Verify that URL looks like a valid URL or option OPT."""
    if not (url.startswith('https://') \
            or url.startswith('http://') \
            or url.startswith('file://')):
      usage_and_exit("svn2rss.py: Invalid url '%s' is specified for " \
                     "'%s' option" % (url, opt))


class Svn2RSS:
    def __init__(self, svn_path, repos_path, item_url, rss_file, 
                 max_items, feed_url):
        self.repos_path = repos_path
        self.item_url = item_url
        self.rss_file = rss_file
        self.max_items = max_items
        self.feed_url = feed_url
        self.svnlook_cmd = 'svnlook'
        if svn_path is not None:
            self.svnlook_cmd = os.path.join(svn_path, 'svnlook')

        (file, ext) = os.path.splitext(self.rss_file)
        self.pickle_file = file + ".pickle"
        if os.path.exists(self.pickle_file):
            self.rss = pickle.load(open(self.pickle_file, "r"))
        else:
            title = ("%s's Subversion Commits Feed"
                     % (os.path.basename(os.path.abspath(self.repos_path))))
            desc = "The latest Subversion commits"
            self.rss = PyRSS2Gen.RSS2(title = title,
                                      link = self.feed_url,
                                      description = desc,
                                      lastBuildDate = datetime.datetime.now(),
                                      items = [])
        
    def add_revision_item(self, revision):
        rss_item = self._make_rss_item(revision)
        self.rss.items.insert(0, rss_item)
        if len(self.rss.items) > self.max_items:
            del self.rss.items[self.max_items:]

    def write_output(self):
        s = pickle.dumps(self.rss)
        f = open(self.pickle_file, "w")
        f.write(s)
        f.close()

        f = open(self.rss_file, "w")
        self.rss.write_xml(f)
        f.close()

    def _make_rss_item(self, revision):
        """ Generate PyRSS2Gen Item from the commit info """
        revision = str(revision)
        item_title = "Revision " + revision
        item_link = self.item_url and self.item_url + "?rev=" + revision or ""

        cmd = [self.svnlook_cmd, 'info', '-r', revision, self.repos_path]
        child_out, child_in, child_err = popen2.popen3(cmd)
        info_lines = child_out.readlines()
        child_out.close()
        child_in.close()
        child_err.close()
        
        cmd = [self.svnlook_cmd, 'changed', '-r', revision, self.repos_path]
        child_out, child_in, child_err = popen2.popen3(cmd)
        changed_data = child_out.read()
        child_out.close()
        child_in.close()
        child_err.close()

        desc = ("\nAuthor: %sDate: %sRevision: %s\nLog: %sModified: \n%s"
                % (info_lines[0], info_lines[1], revision, info_lines[3],
                   changed_data))
        
        rss_item = PyRSS2Gen.RSSItem(title = item_title,
                                     link = item_link,
                                     description = desc,
                                     guid = PyRSS2Gen.Guid(item_link),
                                     pubDate = datetime.datetime.now())
        return rss_item


def main():
    # Parse the command-line options and arguments.
    try:
        opts, args = getopt.gnu_getopt(sys.argv[1:], "hP:r:u:f:m:U:",
                                       ["help",
                                        "svn-path=",
                                        "revision=",
                                        "item-url=",
                                        "feed-file=",
                                        "max-items=",
                                        "feed-url=",
                                        ])
    except getopt.GetoptError, msg:
        usage_and_exit(msg)

    # Make sure required arguments are present.
    if len(args) != 1:
        usage_and_exit("You must specify a repository path.")
    repos_path = os.path.abspath(args[0])

    # Now deal with the options.
    max_items = 20
    commit_rev = svn_path = None
    item_url = feed_url = ""
    feed_file = os.path.basename(repos_path) + ".rss"
    
    for opt, arg in opts:
        if opt in ("-h", "--help"):
            usage_and_exit()
        elif opt in ("-P", "--svn-path"):
            svn_path = arg
        elif opt in ("-r", "--revision"):
            commit_rev = arg
        elif opt in ("-u", "--item-url"):
            item_url = arg
            check_url(item_url, opt)
        elif opt in ("-f", "--feed-file"):
            feed_file = arg
        elif opt in ("-m", "--max-items"):
            try:
               max_items = int(arg)
            except ValueError, msg:
               usage_and_exit("Invalid value '%s' for --max-items." % (arg))
            if max_items < 1:
               usage_and_exit("Value for --max-items must be a positive integer.")
        elif opt in ("-U", "--feed-url"):
            feed_url = arg
            check_url(feed_url, opt)
    
    if commit_rev is None:
        svnlook_cmd = 'svnlook'
        if svn_path is not None:
            svnlook_cmd = os.path.join(svn_path, 'svnlook')
        child_out, child_in, child_err = popen2.popen3([svnlook_cmd,
                                                        'youngest',
                                                        repos_path])
        cmd_out = child_out.readlines()
        child_out.close()
        child_in.close()
        child_err.close()
        revisions = [int(cmd_out[0])]
    else:
        try:
            rev_range = commit_rev.split(':')
            len_rev_range = len(rev_range)
            if len_rev_range == 1:
                revisions = [int(commit_rev)]
            elif len_rev_range == 2:
                start, end = rev_range
                start = int(start)
                end = int(end)
                if (start > end):
                    tmp = start
                    start = end
                    end = tmp
                revisions = range(start, end + 1)[-max_items:]
            else:
                raise ValueError()
        except ValueError, msg:
            usage_and_exit("svn2rss.py: Invalid value '%s' for --revision." \
                           % (commit_rev))
    
    for revision in revisions:
        svn2rss = Svn2RSS(svn_path, repos_path, item_url, feed_file, 
                          max_items, feed_url)
        svn2rss.add_revision_item(revision)
        svn2rss.write_output()
    
  
if __name__ == "__main__":
    main()
