#!/usr/bin/env python

import sys, getopt, os, popen2
import pickle
from StringIO import StringIO

# Python 2.3 is required by PyRSS2Gen
py_version  = sys.version_info
if sys.version_info[0:2] < (2,3):
    sys.stderr.write("Error: Python 2.3 or higher required")
    sys.exit(1)
    
import datetime

try:
    import PyRSS2Gen
except ImportError:
    sys.stderr.write("Please install PyRSS2Gen before running this script\n")
    sys.stderr.write("PyRSS2Gen can be downloaded from: \n")
    sys.stderr.write("http://www.dalkescientific.com/Python/PyRSS2Gen.html\n")

def usage():
    print "Usage: svn2rss.py [-h|--help] [--svn-path] --revision <rev> "
    print "                  --repos-path <path> "
    print "                  --url <url> --rss-file <file>"
    print "       svn-path : path where svn binaries are installed"
    print "       url      : link in the rss item that points to the"
    print "                  viewcvs page for the revision"
    print ""
    print "Generates a RSS 2.0 file containing commit information."
    print "Once the maximum number of items is reached, the oldest element"
    print "is removed.  The item title is the Revision number and the item"
    print "description contains the author, date, log messages and changed"
    print "paths."
    

if len(sys.argv) == 1:
    usage()
    sys.exit(2)
   
try:
    opts, args = getopt.getopt(sys.argv[1:],"h", ["help", "svn-path=",
                                                    "revision=",
                                                    "repos-path=", "url=",
                                                    "rss-file="])
except getopt.GetoptError, msg:
    print msg
    sys.stderr.write(usage())
    sys.exit(2)

for opt, arg in opts:
    if opt in ("-h", "--help"):
        usage()
        sys.exit(0)
    elif opt == "--svn-path":
        svn_path = arg
    elif opt == "--revision":
        commit_rev = arg
    elif opt == "--repos-path":
        repos_path = arg
    elif opt == "--url":
        url = arg
    elif opt == "--rss-file":
        rss_file = arg

class SVN2RSS:
    def __init__(self, svn_path, revision, repos_path, url, rss_file):
        self.max_items = 20
        self.svn_path = svn_path
        self.revision = revision
        self.repos_path = repos_path
        self.url = url
        self.rss_file = rss_file
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
        f.write (s.getvalue())
        f.close()

    def make_rss_item(self):
        """ Generate PyRSS2Gen Item from the commit info """
        item_title = "Revision " + self.revision
        item_link = url + "?rev=" + self.revision
        rss_item = PyRSS2Gen.RSSItem(title = item_title,
                                     link = item_link,
                                     description = self.make_rss_item_desc(),
                                     guid = PyRSS2Gen.Guid(item_link),
                                     pubDate = datetime.datetime.now())
        return rss_item

    def make_rss(self):
        """ Generate a PyRSS2Gen RSS2 object """
        if os.path.exists(self.pickle_file):
            f = open(self.pickle_file, "r")
            rss = pickle.load(f)
            f.close()
            if len(rss.items) == self.max_items :
                rss.items.pop()
            rss.items.insert(0, self.rss_item)
        else:
            rss_item = self.rss_item
            rss = PyRSS2Gen.RSS2(
                              title = "Foo's SVN Commits Feed",
                              link = "http://www.foo.com/project",
                              description = "The latest SVN commits",
                              lastBuildDate = datetime.datetime.now(),
                              items = [rss_item])

        return rss

svn2rss = SVN2RSS(svn_path, commit_rev, repos_path, url, rss_file)
rss = svn2rss.rss
svn2rss.pickle()
rss.write_xml(open(rss_file, "w"))
