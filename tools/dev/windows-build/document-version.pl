#!/usr/local/bin/perl -w
use strict;

use Tie::File;
#use Cwd 'getcwd';

die "Bad args '@ARGV'" unless (@ARGV >= 3  && @ARGV <= 4);

my ($filename, $TARGETDIR, $SVNDIR, $BUILDDESCR) = (@ARGV, "");

my (@file, $version, $lines);

tie (@file, 'Tie::File', $filename)
	or die $!;

$version  =  `svnversion -n` or die;
$version  =~ tr/M//d;
$version .=  '-' . $BUILDDESCR if $BUILDDESCR;

/^#define SVN_VER_TAG/ and s/(?<=dev build).*(?=\)"$)/-r$version/
	for @file;
/^#define SVN_VER_NUMTAG/ and s/(?<=-dev).*(?="$)/-r$version/
	for @file;

mkdir $TARGETDIR unless -d $TARGETDIR;

chdir $SVNDIR;
system "svn diff -x-p > $TARGETDIR\\$version.diff"
	and die $!;

