#!/usr/bin/env python

# TODO:
#
# Remove properties from the given URL.

from csvn.core import *
from csvn.repos import RemoteRepository, RepositoryURI
from csvn.auth import User
from optparse import OptionParser

usage = """python trunkify.py [OPTION]... URL

Moves all the children of the given URL to a new directory in that
URL.  Convenient for adding a top-level "trunk" directory to a project
without one."""

parser = OptionParser(usage=usage)
parser.add_option("-m", "--message", dest="message",
                  help="use MESSAGE as a log message")
parser.add_option("-F", "--file", dest="file",
                  help="read log message from FILE")
parser.add_option("-u", "--username", dest="username",
                  help="commit the changes as USERNAME")
parser.add_option("-d", "--directory", dest="directory",
                  help="set name of new directory")

(options, args) = parser.parse_args()

if len(args) != 1:
    parser.print_help()
    sys.exit(1)

repos_url = args[0]

# Initialize variables
new_dir_name = "trunk"
if options.directory:
    new_dir_name = options.directory

if options.message:
    commit_message = options.message
elif options.file:
    commit_message = file(options.file).read()
else:
    commit_message = "Move project into new directory '%s'." % new_dir_name

svn_cmdline_init("", stderr)
s = RemoteRepository(repos_url, user=User(username=options.username))

txn = s.txn()

for name in s.list("").keys():
    txn.delete(name)

txn.copy(src_path="", dest_path=new_dir_name)

txn.commit(commit_message)
