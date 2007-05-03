#!/usr/bin/env python2.5

# TODO:
#
# Move some of the functionality (dict_from_apr_hash, a wrapper around
# svn_ra_get_dir2, etc) into csvn proper.
#
# Remove properties from the given URL.

from csvn.core import *
from csvn.client import ClientSession, ClientURI, User
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

def dict_from_apr_hash(h, val_type, pool):
    d = {}

    hi = apr_hash_first(pool, h)

    # This *can't* be 'hi is not None' even though I thought NULL maps
    # to None...
    while hi:
        key_vp = c_void_p()
        val_vp = c_void_p()
        apr_hash_this(hi, byref(key_vp), None, byref(val_vp))

        key_as_string = cast(key_vp, c_char_p).value
        d[key_as_string] = cast(val_vp, val_type)

        hi = apr_hash_next(hi)

    return d

s = ClientSession(repos_url, user=User(username=options.username))

dirents_hash = POINTER(apr_hash_t)()
fetched_rev = svn_revnum_t()

svn_ra_get_dir2(s,
                byref(dirents_hash),
                byref(fetched_rev),
                None,
                "",
                -1,  # bah, SVN_INVALID_REVNUM is not exported
                0, # don't need any dirent fields
                s.pool)

dirents_dict = dict_from_apr_hash(dirents_hash, POINTER(svn_dirent_t), s.pool)
base_rev = fetched_rev.value

txn = s.txn()

txn.copy(src_path="", dest_path=new_dir_name)

for name in dirents_dict.iterkeys():
    # I'm not sure that base_rev here actually guarantees anything...
    txn.delete(name, base_rev=base_rev)

txn.commit(commit_message)
