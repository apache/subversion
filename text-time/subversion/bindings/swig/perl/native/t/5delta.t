#!/usr/bin/perl
use strict;
use Test::More tests => 2;
require SVN::Core;
require SVN::Delta;

my ($srctext, $tgttext, $result) = ('abcd===eflfjgjkx', 'abcd=--ef==lfjffgjx', '');

open my $source, '<', \$srctext;
open my $target, '<', \$tgttext;
open my $aresult, '>', \$result;

my $txstream = SVN::TxDelta::new ($source, $target);

isa_ok ($txstream, '_p_svn_txdelta_stream_t');
open my $asource, '<', \$srctext;
my $handle = [SVN::TxDelta::apply ($asource, $aresult, undef, undef)];

SVN::TxDelta::send_txstream ($txstream, @$handle);

is ($result, $tgttext, 'delta self test');

