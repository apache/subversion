# ----------------------------------------------------------------------
# Configuration
# (edit this file locally as required)

# Path and file name of the 'svnadmin' and 'svnlook' programs
SVNADMIN = 'svnadmin'
SVNLOOK = 'svnlook'

# Verbosity: True for verbose, or False for quiet
VERBOSE = True

# PER-REPOSITORY CONFIGURATION

# The number of revs per shard of the repository being accessed, or 'None'
# for a linear (that is, non-sharded) layout.  This is 1000 for almost all
# repositories in practice.
#
# The correct value can be found in the 'db/format' file in the repository.
# The second line of that file will say something like 'layout sharded 1000'
# or 'layout linear'.
#
# TODO: Read this value automatically from the db/format file.
REVS_PER_SHARD=1000

