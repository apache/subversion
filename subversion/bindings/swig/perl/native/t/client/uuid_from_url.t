#!/usr/bin/perl

use strict;
use warnings;

use Test::More tests => 6;

use lib 't/lib';

BEGIN {
    use_ok('Test::SVN', qw(create_and_load_repo));
    use_ok('SVN::Client');
}

my $repo_path = create_and_load_repo('t/repo.dump');
my $repo_url  = "file://$repo_path";

my $ctx = SVN::Client->new();
isa_ok($ctx, 'SVN::Client');

my $uuid;
my $expected_uuid = 'dd219696-d8bc-db11-9ba3-0011251291e9';

$uuid = $ctx->uuid_from_url($repo_url);
is($uuid, $expected_uuid,
   '$ctx->uuid_from_url($url) returned correct UUID');

$uuid = SVN::Client::uuid_from_url($repo_url, $ctx);
is($uuid, $expected_uuid,
   'SVN::Client::uuid_from_url($url, $ctx) returned correct UUID');

# XXX unsure about this.  The test in 3client.t that this replicates did
# this.  But this is exposing two implementation details of the $ctx
# object -- (a) it's implemented as a hash, (b) it contains a key called
# 'ctx' -- that shouldn't really be exposed.

$uuid = SVN::Client::uuid_from_url($repo_url, $ctx->{'ctx'});
is($uuid, $expected_uuid,
   'SVN::Client::uuid_from_url($url, $ctx->{ctx}) returned correct UUID');
