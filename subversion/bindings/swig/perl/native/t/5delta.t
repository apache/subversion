#!/usr/bin/perl
use strict;
use Test::More tests => 2;
require SVN::Core;
require SVN::Delta;

SKIP: {
    eval { require IO::String };

    skip "IO::String not installed", 2 if $@;
    my $srctext = 'abcd===eflfjgjkx';
    my $tgttext = 'abcd=--ef==lfjffgjx';

    my $source = IO::String->new ($srctext);
    my $target = IO::String->new ($tgttext);

    my $result = '';
    my $aresult = IO::String->new (\$result);

    my $txstream = SVN::TxDelta::new ($source, $target);

    isa_ok ($txstream, '_p_svn_txdelta_stream_t');
    my $handle = [SVN::TxDelta::apply (IO::String->new ($srctext),
				       $aresult, undef, undef)];

    SVN::TxDelta::send_txstream ($txstream, @$handle);

    is ($result, $tgttext, 'delta self test');

}
