#!/usr/bin/perl

# Copyright (c) 2009 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================

# require-mergeinfo.pl: check that the committing client supports the
# mergeinfo capability
#
# Usage: require-mergeinfo.pl CAPABILITIES
#
# To enable, add the following line to the repository's start-commit hook:
#
#    require-mergeinfo.pl "$3" || exit 1
#

exit 0 if grep { $_ eq 'mergeinfo' } split ':', $ARGV[0];
print STDERR "Your client is too old to commit to this repository.\n";
print STDERR "A version 1.5 or later client is required.\n";
exit 1;
