#!/usr/bin/perl -w

use Test::More qw(no_plan);
use strict;

require SVN::Core;
require SVN::Repos;
require SVN::Client;

my $repospath = "/tmp/svn-$$";
my $reposurl = "file://$repospath";
my $wcpath = "/tmp/svn-wc-$$";

# This is ugly to create the test repo with SVN::Repos, but
# it seems to be the most reliable way.
ok(SVN::Repos::create("$repospath", undef, undef, undef, undef),
   "create repository at $repospath");

my $ctx = SVN::Client->new;
isa_ok($ctx,'SVN::Client','Client Object');

my $ci_dir1 = $ctx->mkdir(["$reposurl/dir1"]);
isa_ok($ci_dir1,'_p_svn_client_commit_info_t');
is($ci_dir1->revision,1,'commit info revision equals 1');

END {
diag "cleanup";
`rm -rf $repospath`;
#`rm -rf $wcpath`;
}
