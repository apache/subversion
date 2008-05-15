#!/usr/bin/env perl

# Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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

# This pre-commit hook script will check the files that are are listed
# in "svnlook changed" (except deleted files) for possible problems
# with svn:keywords set on binary files.  By default, this script will
# allow binary files if they have the fixed width format of the
# keyword in the file.  If you want to disallow all keywords in binary
# files, regardless of fixed width format or not, pass --disallowall
# to the script.
#
# The way it determines if the file is binary is it takes the paranoid
# approach that all files are binary, unless proven otherwise.  If a
# file has svn:eol-style set, it considers this file text.  If the
# file has a svn:mime-type of text/*, it is text. Then, the script can
# be passed file extensions to always consider text.
#
# Command line options are:
#
# --revision (-r) = The revision to pass in on svnlook to inspect.  To
#                   be used for testing only, use --transaction in the
#                   hook script.
#
# --transaction (-t) = The transaction to inspect.  $2 in pre-commit
#                      scripts.
#
# --repos = The path to the repository.  $1 in pre-commit scripts.
#
# --svnlook = Path to svnlook. Default: (/usr/bin/svnlook)
#
# --text (-x) = Declared multiple times for each extension.
#               -x .txt -x .html -x .htm
#
# --disallowall = Dissallow all svn:keywords from binary files.
#
# Example usage (inside pre-commit hook script)
#
# REPOS="$1"
# TXN="$2"
# svnkeywordcheck.pl --repos $REPOS --transaction $TXT --text .java --text .txt
#

BEGIN {
    if ( $] >= 5.006_000) {
      require warnings; import warnings;
    } else {
      $^W = 1;
    }
}

use strict;
use Getopt::Long;
use Carp;

# Command line option parsing

my $transaction;
my $revision;
my $repos;
my $svnlook = "/usr/bin/svnlook";
my @text;
my $disallowall = 0;

GetOptions(
    'revision|r=s'    => \$revision,
    'transaction|t=s' => \$transaction,
    'repos=s'         => \$repos,
    'svnlook=s'       => \$svnlook,
    'disallowall'     => \$disallowall,
    'text|x=s'     => \@text,
    );

if (defined($transaction) and !defined($revision)) {
    croak "Can't define both revision and transaction!\n";
}

if (!defined($transaction) and !defined($revision)) {
    croak "Need to pass a revision or a transaction!\n";
}

if (!defined($repos)) {
    croak "Need to pass in repos path!\n";
}

my $flag = (defined($revision)) ? "-r" : "-t";
my $value = (defined($revision)) ? $revision : $transaction;

# Get a list of what has changed.
my @changed = read_from_process("$svnlook changed $flag $value $repos");

# Loop over changed entries, checking each one, except deleted paths.
my @errors;
foreach my $change (@changed) {
    chomp($change);
    if ($change =~ m/^D /) {
        next;
    }
    $change =~ s/^(?:A |U |UU| U)\s+(.*)/$1/;
    if (check($change)) {
        push(@errors, $change);
    }
}

# Report any errors to STDERR to be marsheled back to the client, and
# exit 1.
if (@errors) {
    warn "The following files appear to be binary, and have svn:keywords set,\n";
    warn "yet are not in the fixed width format. Please either fix the keyword,\n";
    warn "or if the file is text, please set the right svn:mime-type or svn:eol-style\n";
    foreach my $error (@errors) {
        warn "\t$error\n";
    }
    exit 1;
}

# Subroutine that checks the paths passed to it.  Checks if it has
# "svn:keywords" and if the file is binary, and greps the output of
# svnlook cat of the file for keywords, while trying to do that inside
# a loop to keep memory usage down.
sub check {
    my $file = shift;
    if (has_svn_property($file, "svn:keywords")) {
        if (file_is_binary($file)) {
            if ($disallowall) {
                return 1;
            } else {
                my @keywords = get_svnkeywords($file);
                my $fh = _pipe("$svnlook cat $flag $value $repos $file");
                while (my $line = <$fh>) {
                    foreach my $keyword (@keywords) {
                        if ($line =~ m/$keyword/) {
                            close($fh);
                            return 1;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

# Heruistics to determine if file is binary.
#
# Take the paranoid approch, everything is binary, unless otherwise
# stated If svn:eol-style is set, it is text If svn:mime-type is
# text/*, it is text a configurable file glob list (extensions, *.txt,
# etc) that are text (defined on the command line)
sub file_is_binary {
    my $file = shift;
    if (has_svn_property($file, "svn:eol-style")) {
        return 0;
    }
    if (has_svn_property($file, "svn:mime-type")) {
        my ($mimetype) = read_from_process("$svnlook propget $flag $value $repos svn:mime-type $file");
        chomp($mimetype);
        $mimetype =~ s/^\s*(.*)/$1/;
        if ($mimetype =~ m/^text\//) {
            return 0;
        }
    }
    foreach my $ext (@text) {
        if ($file =~ m/\Q$ext\E$/) {
            return 0;
        }
    }
    return 1;
}

# Return a list of svn:keywords on a file
sub get_svnkeywords {
    my $file = shift;
    my @lines = read_from_process("$svnlook propget $flag $value $repos svn:keywords $file");
    my @returnlines;
    foreach my $line (@lines) {
        $line =~ s/\s+/ /;
        push(@returnlines, split(/ /, $line));
    }
    return @returnlines;
}

# Checks if a Subversion property is set on a file.
sub has_svn_property {
    my $file = shift;
    my $keyword = shift;
    my @proplist = read_from_process("$svnlook proplist $flag $value $repos $file");
    foreach my $prop (@proplist) {
        chomp($prop);
        if ($prop =~ m/\b$keyword\b/) {
            return 1;
        }
    }
    return 0;
}

# Copied from contrib/hook-scripts/check-mime-type.pl, with some
# modifications. Moved the actual pipe creation to another subroutine
# (_pipe), so I can use _pipe in this code, and the part of the code
# that loops over the output of svnlook cat.
sub safe_read_from_pipe {
    unless (@_) {
        croak "$0: safe_read_from_pipe passed no arguments.\n";
    }
    my $fh = _pipe(@_);
    my @output;
    while (<$fh>) {
        chomp;
        push(@output, $_);
    }
    close($fh);
    my $result = $?;
    my $exit   = $result >> 8;
    my $signal = $result & 127;
    my $cd     = $result & 128 ? "with core dump" : "";
    if ($signal or $cd) {
        warn "$0: pipe from `@_' failed $cd: exit=$exit signal=$signal\n";
    }
    if (wantarray) {
        return ($result, @output);
    } else {
        return $result;
    }
}

# Return the filehandle as a glob so we can loop over it elsewhere.
sub _pipe {
    local *SAFE_READ;
    my $pid = open(SAFE_READ, '-|');
    unless (defined $pid) {
        die "$0: cannot fork: $!\n";
    }
    unless ($pid) {
        open(STDERR, ">&STDOUT") or die "$0: cannot dup STDOUT: $!\n";
        exec(@_) or die "$0: cannot exec `@_': $!\n";
    }
    return *SAFE_READ;
}

# Copied from contrib/hook-scripts/check-mime-type.pl
sub read_from_process {
    unless (@_) {
        croak "$0: read_from_process passed no arguments.\n";
    }
    my ($status, @output) = &safe_read_from_pipe(@_);
    if ($status) {
        if (@output) {
            die "$0: `@_' failed with this output:\n", join("\n", @output), "\n";
        } else {
            die "$0: `@_' failed with no output.\n";
        }
    } else {
        return @output;
    }
}
