#!/usr/bin/perl

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
