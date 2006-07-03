#!/usr/bin/env python

"""Usage: svn2rss.py [OPTION...]

 -h | --help         Show this help message
 -P | --svn-path=    path where svn binaries are installed
 -r | --revision=    svn revision
 -p | --repos-path=  svn repository to generate RSS 2.0 feed
 -u | --item-url=    link to appear in the rss item
 -f | --rss-file=    filename to store the rss feed
 -m | --max-items=   maximum items to store in the rss feed
 -U | --feed-url=    global RSS feed url 

Generates a RSS 2.0 file containing commit information.  Once the
maximum number of items is reached, older elements are removed.  The
item title is the revision number, and the item description contains
the author, date, log messages and changed paths."""

import sys

# Python 2.3 is required by PyRSS2Gen
py_version  = sys.version_info
if sys.version_info[0:2] < (2,3):
    sys.stderr.write("Error: Python 2.3 or higher required.")
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

# All clear on the custom module checks.  Import some standard stuff.    
import getopt, os, popen2, pickle, datetime
from StringIO import StringIO

def usage_and_exit(errmsg=None):
    """Print a usage message, plus an ERRMSG (if provided), then exit.
    If ERRMSG is provided, the usage message is printed to stderr and
    the script exits with a non-zero error code.  Otherwise, the usage
    message goes to stdout, and the script exits with a zero
    errorcode."""
    stream = errmsg is not None and sys.stderr or sys.stdout
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

max_items = 20
commit_rev = None
item_url = feed_url = None

if len(sys.argv) == 1:
    usage_and_exit("Not enough arguments provided.")
try:
    opts, args = getopt.gnu_getopt(sys.argv[1:], "hP:r:p:u:f:m:U:", [
                                                      "help", "svn-path=",
                                                      "revision=",
                                                      "repos-path=",
                                                      "item-url=",
                                                      "rss-file=",
                                                      "max-items=",
                                                      "feed-url="])
except getopt.GetoptError, msg:
    usage_and_exit(msg)
for opt, arg in opts:
    if opt in ("-h", "--help"):
        usage_and_exit()
    elif opt in ("-P", "--svn-path"):
        svn_path = arg
    elif opt in ("-r", "--revision"):
        commit_rev = arg
    elif opt in ("-p", "--repos-path"):
        repos_path = arg
    elif opt in ("-u", "--item-url"):
        item_url = arg
        check_url(item_url, opt)
    elif opt in ("-f", "--rss-file"):
        rss_file = arg
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

class SVN2RSS:
    def __init__(self, svn_path, revision, repos_path, item_url, rss_file, 
                 max_items, feed_url):
        self.svn_path = svn_path
        self.revision = revision
        self.repos_path = repos_path
        self.item_url = item_url
        self.rss_file = rss_file
        self.max_items = max_items
        self.feed_url = feed_url

        self.rss_item_desc = self.make_rss_item_desc()
        self.svnlook = os.path.join(self.svn_path, "svnlook")
        (file, ext) = os.path.splitext(self.rss_file)
        self.pickle_file = file + ".pickle"
        self.rss_item = self.make_rss_item()
        self.rss = self.make_rss()
        
    def make_rss_item_desc(self):
        cmd = "svnlook info -r " + self.revision + " " + self.repos_path
        out, x, y = popen2.popen3(cmd)
        cmd_out = out.readlines()
        Author = "\nAuthor: " + cmd_out[0]
        Date = "Date: " + cmd_out[1]
        New_Revision = "Revision: " + self.revision
        Log = "Log: " + cmd_out[3]
        out.close()
        x.close()
        y.close()
        
        cmd = "svnlook changed -r " + self.revision + " " + self.repos_path
        out, x, y = popen2.popen3(cmd)
        cmd_out = out.readlines()
        changed_files = "Modified: \n"
        for item in cmd_out:
            changed_files = changed_files + item
        item_desc = Author + Date + New_Revision + "\n" + \
                    Log + changed_files
        out.close()
        x.close()
        y.close()
        
        return item_desc
        
    def pickle(self):
        s = StringIO()    
        pickle.dump(self.rss, s)
        f = open(self.pickle_file,"w")
        f.write(s.getvalue())
        f.close()

    def make_rss_item(self):
        """ Generate PyRSS2Gen Item from the commit info """
        item_title = "Revision " + self.revision
        item_link = self.item_url + "?rev=" + self.revision
        rss_item = PyRSS2Gen.RSSItem(title = item_title,
                                     link = item_link,
                                     description = self.make_rss_item_desc(),
                                     guid = PyRSS2Gen.Guid(item_link),
                                     pubDate = datetime.datetime.now())
        return rss_item

    def make_rss(self):
        """ Generate a PyRSS2Gen RSS2 object """
        if os.path.exists(self.pickle_file):
            rss = pickle.load(open(self.pickle_file, "r"))
            rss.items.insert(0, self.rss_item)
            if(len(rss.items) > self.max_items):
                del(rss.items[self.max_items:])
        else:
            rss_item = self.rss_item
            rss_title = "%s's SVN Commits Feed" \
                        % (os.path.basename(self.repos_path))
            rss = PyRSS2Gen.RSS2(
                              title = rss_title,
                              link = self.feed_url,
                              description = "The latest SVN commits",
                              lastBuildDate = datetime.datetime.now(),
                              items = [rss_item])

        return rss

try:
    if (commit_rev == None):
        cmd = "svnlook youngest " + repos_path
        out, x, y = popen2.popen3(cmd)
        cmd_out = out.readlines()
        revisions = [int(cmd_out[0])]
        out.close()
        x.close()
        y.close()
    else:
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
            usage_and_exit("svn2rss.py: Invalid value '%s' for --revision." % (commit_rev))

    for revision in revisions:
        revision = str(revision)
        svn2rss = SVN2RSS(svn_path, revision, repos_path, item_url, rss_file, 
                          max_items, feed_url)
        rss = svn2rss.rss
        svn2rss.pickle()

        rss.write_xml(open(svn2rss.rss_file, "w"))

except ValueError, msg:
    usage_and_exit("svn2rss.py: Invalid value '%s' for --revision." % (commit_rev))
