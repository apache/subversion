#!/usr/bin/perl
use strict;
use Test::More tests => 3;
require SVN::Core;
require SVN::Delta;

my ($srctext, $tgttext, $result) = ('abcd===eflfjgjkx', 'abcd=--ef==lfjffgjx', '');

open my $source, '<', \$srctext;
open my $target, '<', \$tgttext;
open my $aresult, '>', \$result;

my $txstream = SVN::TxDelta::new ($source, $target);

isa_ok ($txstream, '_p_svn_txdelta_stream_t');
open my $asource, '<', \$srctext;
my ($md5, @handle) = SVN::TxDelta::apply ($asource, $aresult, undef);

SVN::TxDelta::send_txstream ($txstream, @handle);

is ($result, $tgttext, 'delta self test');

is("$md5", 'a22b3dadcbddac48d2f1eae3ec5fb86a', 'md5 matched');
