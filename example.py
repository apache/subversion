from csvn.repos import *
from csvn.client import *
import os

if os.path.exists("/tmp/test-repos"):
    svn_repos_delete("/tmp/test-repos", Pool())
repos = Repos("/tmp/test-repos", username="joecommitter",
              create=True)
print "Repos UUID: ", repos.uuid()

# Create a new transaction
txn = repos.txn()

# You can create a file from a Python string
txn.put("file1.txt", contents="Hello world one!", create=True)

# ... or from a Python file
file("/tmp/contents.txt", "w").write("Hello world two!")
txn.put("file2.txt", contents=file("/tmp/contents.txt"), create=True)

# Create some directories
txn.mkdir("a")
txn.mkdir("a/b")

# Commit the transaction
new_rev = txn.commit("Create file1.txt and file2.txt. "
                     "Also create some directories")
print "Committed revision %d" % new_rev


# Transaction number 2
txn = repos.txn()

# Copy a to c, but remove the subdirectories
txn.copy(src_path="a", dest_path="c")
txn.delete("c/b")

# Copy files around in the repository
txn.copy(src_path="file1.txt", src_rev=1,
         dest_path="file3.txt")
txn.copy(src_path="file2.txt", src_rev=1,
         dest_path="file4.txt")

# Modify some files while we're at it
txn.put("file1.txt", contents="Hello world one and a half!")

# Commit our changes
new_rev = txn.commit("Create copies of file1.txt, file2.txt, and some "
                     "random directories. Also modify file1.txt.")
print "Committed revision %d" % new_rev

# Transaction number 3
txn = repos.txn()

# Replace file3.txt with the new version of file1.txt
txn.delete("file3.txt")
txn.copy(src_path="file1.txt", src_rev=2,
         dest_path="file3.txt")

# Commit our changes
new_rev = txn.commit("Replace file3.txt with a new copy of file1.txt")
print "Committed revision %d" % new_rev

session = ClientSession("file:///tmp/test-repos")

# Create a new transaction
txn = session.txn()

if session.check_path("blahdir") != svn_node_none:
    txn.delete("blahdir")
txn.mkdir("blahdir")
txn.mkdir("blahdir/dj")
txn.mkdir("blahdir/dj/a")
txn.mkdir("blahdir/dj/a/b")
txn.mkdir("blahdir/dj/a/b/c")
txn.mkdir("blahdir/dj/a/b/c/d")
txn.mkdir("blahdir/dj/a/b/c/d/e")
txn.mkdir("blahdir/dj/a/b/c/d/e/f")
txn.mkdir("blahdir/dj/a/b/c/d/e/f/g")
txn.put("blahdir/dj/a/b/c/d/e/f/g/h.txt", file("/tmp/contents.txt"),
        create=True)

rev = txn.commit("create blahdir and descendents")
print "Committed revision %d" % rev
