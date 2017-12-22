#!/usr/bin/perl -w
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#

use Test::More tests => 302;
use strict;

# shut up about variables that are only used once.
# these come from constants and variables used
# by the bindings but not elsewhere in perl space.
no warnings 'once';

# TEST
use_ok('SVN::Core');
# TEST
use_ok('SVN::Repos');
# TEST
use_ok('SVN::Client');
# TEST
use_ok('SVN::Wc'); # needed for status
use File::Spec::Functions;
use File::Temp qw(tempdir);
use File::Path qw(rmtree);

# do not use cleanup because it will fail, some files we
# will not have write perms to.
my $testpath = tempdir('svn-perl-test-XXXXXX', TMPDIR => 1, CLEANUP => 0);

my $repospath = catdir($testpath,'repo');
my $reposurl = 'file://' . (substr($repospath,0,1) ne '/' ? '/' : '')
               . $repospath;
my $wcpath = catdir($testpath,'wc');
my $importpath = catdir($testpath,'import');

# Use internal style paths on Windows
$reposurl =~ s/\\/\//g;
$wcpath =~ s/\\/\//g;
$importpath =~ s/\\/\//g;

# track current rev ourselves to test against
my $current_rev = 0;

# We want to trap errors ourself
$SVN::Error::handler = undef;

# Get username we are running as
my $username;
if ($^O eq 'MSWin32') {
    $username = getlogin();
} else {
    $username = getpwuid($>) || getlogin();
}

# This is ugly to create the test repo with SVN::Repos, but
# it seems to be the most reliable way.
# TEST
ok(SVN::Repos::create("$repospath", undef, undef, undef, undef),
   "create repository at $repospath");

my ($ctx) = SVN::Client->new;
# TEST
isa_ok($ctx,'SVN::Client','Client Object');

my $uuid_from_url = $ctx->uuid_from_url($reposurl);
# TEST
ok($uuid_from_url,'Valid return from uuid_from_url method form');

# test non method invocation passing a SVN::Client
# TEST
ok(SVN::Client::uuid_from_url($reposurl,$ctx),
   'Valid return from uuid_from_url function form with SVN::Client object');

# test non method invocation passing a _p_svn_client_ctx_t
# TEST
ok(SVN::Client::uuid_from_url($reposurl,$ctx->{'ctx'}),
   'Valid return from uuid_from_url function form with _p_svn_client_ctx object');


my ($ci_dir1) = $ctx->mkdir(["$reposurl/dir1"]);
# TEST
isa_ok($ci_dir1,'_p_svn_client_commit_info_t');
$current_rev++;
# TEST
is($ci_dir1->revision,$current_rev,"commit info revision equals $current_rev");

my ($ci_dir2) = $ctx->mkdir2(["$reposurl/dir2"]);
# TEST
isa_ok($ci_dir2,'_p_svn_commit_info_t');
$current_rev++;
# TEST
is($ci_dir2->revision,$current_rev,"commit info revision equals $current_rev");

my ($ci_dir3) = $ctx->mkdir3(["$reposurl/dir3"],0,undef);
# TEST
isa_ok($ci_dir3,'_p_svn_commit_info_t');
$current_rev++;
# TEST
is($ci_dir3->revision,$current_rev,"commit info revision equals $current_rev");

# TEST
is($ctx->mkdir4(["$reposurl/dir4"],0,undef,sub {
      my ($commit_info) = @_;

      # TEST
      isa_ok($commit_info,'_p_svn_commit_info_t','commit_info type check');

      # TEST
      is($commit_info->revision(),$current_rev + 1, 'commit info revision');

      # TEST
      like($commit_info->date(),
           qr/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{6}Z$/,
           'commit info date');

      # TEST
      is($commit_info->post_commit_err(),undef,'commit info post_commit_error');

      # TEST
      is($commit_info->repos_root(),$reposurl,'commit info repos_root');
    }),
    undef,'Returned undef from mkdir4 operation.');
$current_rev++;


my ($rpgval,$rpgrev) = $ctx->revprop_get('svn:author',$reposurl,$current_rev);
# TEST
is($rpgval,$username,'svn:author set to expected username from revprop_get');
# TEST
is($rpgrev,$current_rev,'Returned revnum of current rev from revprop_get');

if ($^O eq 'MSWin32') {
    # TEST
    ok(open(NEW, ">$repospath/hooks/pre-revprop-change.bat"),
       'Open pre-revprop-change hook for writing');
    # TEST
    ok(print(NEW 'exit 0'), 'Print to hook');
    # TEST
    ok(close(NEW), 'Close hook');
} else {
    # TEST
    ok(rename("$repospath/hooks/pre-revprop-change.tmpl",
              "$repospath/hooks/pre-revprop-change"),
       'Rename pre-revprop-change hook');
    # TEST
    ok(chmod(0700,"$repospath/hooks/pre-revprop-change"),
       'Change permissions on pre-revprop-change hook');
    # TEST
    is(1, 1, '-')
}
my ($rps_rev) = $ctx->revprop_set('svn:log','mkdir dir1',
                                  $reposurl, $current_rev, 0);
# TEST
is($rps_rev,$current_rev,
   'Returned revnum of current rev from revprop_set');

my ($rph, $rplrev) = $ctx->revprop_list($reposurl,$current_rev);
# TEST
isa_ok($rph,'HASH','Returned hash reference form revprop_list');
# TEST
is($rplrev,$current_rev,'Returned current rev from revprop_list');
# TEST
is($rph->{'svn:author'},$username,
   'svn:author is expected user from revprop_list');
# TEST
is($rph->{'svn:log'},'mkdir dir1',
   'svn:log is expected value from revprop_list');

# TEST
ok($rph->{'svn:date'},'svn:date is set from revprop_list');

# TEST
is($ctx->checkout($reposurl,$wcpath,'HEAD',1),$current_rev,
   'Returned current rev from checkout');

# TEST
is($ctx->checkout2($reposurl,$wcpath . '2',undef,'HEAD',1,0),$current_rev,
   'Returned current rev from checkout2');

# TEST
is($ctx->checkout3($reposurl,$wcpath . '3',undef,'HEAD',$SVN::Depth::infinity,
                   0,0),$current_rev, 'Returned current rev from checkout3');

# TEST
is(SVN::Client::url_from_path($wcpath),$reposurl,
   "Returned $reposurl from url_from_path");

# TEST
ok(open(NEW, ">$wcpath/dir1/new"),'Open new file for writing');
# TEST
ok(print(NEW 'addtest'), 'Print to new file');
# TEST
ok(close(NEW),'Close new file');

# no return means success
# TEST
is($ctx->add("$wcpath/dir1/new",0),undef,
   'Returned undef from add schedule operation');

# TEST
ok(open(NEW2, ">$wcpath/dir1/new2"),'Open new2 file for writing');
# TEST
ok(print(NEW2 'addtest2'), 'Print to new2 file');
# TEST
ok(close(NEW2),'Close new2 file');

# no return means success
# TEST
is($ctx->add2("$wcpath/dir1/new2",0,0),undef,
   'Returned undef from add2 schedule operation');

# TEST
ok(open(NEW3, ">$wcpath/dir1/new3"),'Open new3 file for writing');
# TEST
ok(print(NEW3 'addtest3'), 'Print to new3 file');
# TEST
ok(close(NEW3),'Close new3 file');

# no return means success
# TEST
is($ctx->add3("$wcpath/dir1/new3",0,0,0),undef,
   'Returned undef from add3 schedule operation');

# TEST
ok(open(NEW4, ">$wcpath/dir1/new4"),'Open new4 file for writing');
# TEST
ok(print(NEW4 'addtest4'), 'Print to new4 file');
# TEST
ok(close(NEW4),'Close new4 file');

# no return means success
# TEST
is($ctx->add4("$wcpath/dir1/new4",$SVN::Depth::empty,0,0,0),undef,
   'Returned undef from add4 schedule operation');


# test the log_msg callback
$ctx->log_msg(
    sub
    {
        my ($log_msg,$tmp_file,$commit_items,$pool) = @_;
        # TEST
        isa_ok($log_msg,'SCALAR','log_msg param to callback is a SCALAR');
        # TEST
        isa_ok($tmp_file,'SCALAR','tmp_file param to callback is a SCALAR');
        # TEST
        isa_ok($commit_items,'ARRAY',
               'commit_items param to callback is a SCALAR');
        # TEST
        isa_ok($pool,'_p_apr_pool_t',
               'pool param to callback is a _p_apr_pool_t');
        my $commit_item = shift @$commit_items;
        # TEST
        isa_ok($commit_item,'_p_svn_client_commit_item3_t',
               'commit_item element is a _p_svn_client_commit_item3_t');
        # TEST
        is($commit_item->path(),"$wcpath/dir1/new",
           "commit_item has proper path for committed file");
        # TEST
        is($commit_item->kind(),$SVN::Node::file,
           "kind() shows the node as a file");
        # TEST
        is($commit_item->url(),"$reposurl/dir1/new",
           'URL matches our repos url');
        # revision is INVALID because the commit has not happened yet
        # and this is not a copy
        # TEST
        is($commit_item->revision(),$SVN::Core::INVALID_REVNUM,
           'Revision is INVALID since commit has not happened yet');
        # TEST
        is($commit_item->copyfrom_url(),undef,
           'copyfrom_url is undef since file is not a copy');
        # TEST
        is($commit_item->state_flags(),$SVN::Client::COMMIT_ITEM_ADD |
                                       $SVN::Client::COMMIT_ITEM_TEXT_MODS,
           'state_flags are ADD and TEXT_MODS');
        my $prop_changes = $commit_item->incoming_prop_changes();
        # TEST
        isa_ok($prop_changes, 'ARRAY',
               'incoming_prop_changes returns an ARRAY');
        # TEST
        is(scalar(@$prop_changes), 0,
           'No elements in the incoming_prop_changes array because ' .
           ' we did not make any');
        $prop_changes = $commit_item->outgoing_prop_changes();
        # TEST
        is($prop_changes, undef,
           'No outgoing_prop_changes array because we did not create one');
        $$log_msg = 'Add new';
        return 0;
    } );


my ($ci_commit1) = $ctx->commit($wcpath,0);
# TEST
isa_ok($ci_commit1,'_p_svn_client_commit_info_t',
       'Commit returns a _p_svn_client_commit_info');
$current_rev++;
# TEST
is($ci_commit1->revision,$current_rev,
   "commit info revision equals $current_rev");

# get rid of log_msg callback
# TEST
is($ctx->log_msg(undef),undef,
   'Clearing the log_msg callback works');

# test info() on WC
# TEST
is($ctx->info("$wcpath/dir1/new", undef, 'WORKING',
              sub
              {
                 my($infopath,$svn_info_t,$pool) = @_;
                 # TEST
                 is($infopath,"new",'path passed to receiver is same as WC');
                 # TEST
                 isa_ok($svn_info_t,'_p_svn_info_t');
                 # TEST
                 isa_ok($pool,'_p_apr_pool_t',
                        'pool param is _p_apr_pool_t');
              }, 0),
   undef,
   'info should return undef');

my $svn_error = $ctx->info("$wcpath/dir1/newxyz", undef, 'WORKING', sub {}, 0);
# TEST
isa_ok($svn_error, '_p_svn_error_t',
       'info should return _p_svn_error_t for a nonexistent file');
$svn_error->clear(); #don't leak this

# test getting the log

sub test_log_message_receiver {
  my ($changed_paths,$revision,
      $author,$date,$message,$pool) = @_;
  # TEST
  isa_ok($changed_paths,'HASH',
         'changed_paths param is a HASH');
  # TEST
  isa_ok($changed_paths->{'/dir1/new'},
         '_p_svn_log_changed_path_t',
         'Hash value is a _p_svn_log_changed_path_t');
  # TEST
  is($changed_paths->{'/dir1/new'}->action(),'A',
     'action returns A for add');
  # TEST
  is($changed_paths->{'/dir1/new'}->copyfrom_path(),undef,
     'copyfrom_path returns undef as it is not a copy');
  # TEST
  is($changed_paths->{'/dir1/new'}->copyfrom_rev(),
     $SVN::Core::INVALID_REVNUM,
     'copyfrom_rev is set to INVALID as it is not a copy');
  # TEST
  is($revision,$current_rev,
     'revision param matches current rev');
  # TEST
  is($author,$username,
     'author param matches expected username');
  # TEST
  ok($date,'date param is defined');
  # TEST
  is($message,'Add new',
     'message param is the expected value');
  # TEST
  isa_ok($pool,'_p_apr_pool_t',
         'pool param is _p_apr_pool_t');
}
 
# TEST  log range $current_rev:$current_rev
is($ctx->log("$reposurl/dir1/new",$current_rev,$current_rev,1,0,
             \&test_log_message_receiver), 
   undef,
   'log returns undef');
# TEST  log2 range $current_rev:0 limit=1
is($ctx->log2("$reposurl/dir1/new",$current_rev,0,1,1,0,
              \&test_log_message_receiver), 
   undef,
   'log2 returns undef');
# TEST  log3 range $current_rev:0 limit=1
is($ctx->log3("$reposurl/dir1/new",'HEAD',$current_rev,0,1,1,0,
              \&test_log_message_receiver), 
   undef,
   'log3 returns undef');

my @new_paths = qw( dir1/new dir1/new2 dir1/new3 dir1/new4 );
$ctx->log3([ $reposurl, @new_paths ],
           'HEAD',$current_rev,0,1,1,0, sub {
               my ($changed_paths,$revision,$author,$date,$message,$pool) = @_;
               # TEST
               is_deeply([sort keys %$changed_paths],
                         [sort map { "/$_" } @new_paths],
                         "changed_paths for multiple targets");
});

sub get_full_log {
    my ($start, $end) = @_;
    my @log;
    $ctx->log($reposurl, $start, $end, 1, 0, sub { 
        my ($changed_paths, $revision, $author, $date, $msg, undef) = @_; 
        # "unpack" the values of the $changed_paths hash 
        # (_p_svn_log_changed_path_t objects) so that
        # we can use is_deeply() to compare results
        my %hash;
        while (my ($path, $changed) = each %$changed_paths) {
            foreach (qw( action copyfrom_path copyfrom_rev )) {
                $hash{$path}{$_} = $changed->$_()
            }
        }
        push @log, [ \%hash, $revision, $author, $date, $msg ];
    });
    return \@log;
}

# TEST
my $full_log = get_full_log('HEAD',1);
is(scalar @$full_log, $current_rev, "history up to 'HEAD'");

# TEST
my $opt_revision_head = SVN::_Core::new_svn_opt_revision_t();
$opt_revision_head->kind($SVN::Core::opt_revision_head);
is_deeply(get_full_log($opt_revision_head,1),   # got
          $full_log,                            # expected
          "history up to svn_opt_revision_t of kind head");

# TEST
is_deeply(get_full_log($current_rev,1),         # got
          $full_log,                            # expected
          "history up to number $current_rev");

# TEST
my $opt_revision_number = SVN::_Core::new_svn_opt_revision_t();
$opt_revision_number->kind($SVN::Core::opt_revision_number);
$opt_revision_number->value->number($current_rev);
is_deeply(get_full_log($opt_revision_number,1), # got
          $full_log,                            # expected
          "history up to svn_opt_revision_t of kind number and value $current_rev");

sub test_log_entry_receiver {
  my ($log_entry,$pool) = @_;
  # TEST
  isa_ok($log_entry, '_p_svn_log_entry_t',
         'log_entry param');
  # TEST
  isa_ok($pool,'_p_apr_pool_t',
         'pool param');
  # TEST
  is($log_entry->revision,$current_rev,
     'log_entry->revision matches current rev');

  my $revprops = $log_entry->revprops;
  # TEST
  isa_ok($revprops,'HASH',
         'log_entry->revprops');
  # TEST
  is($revprops->{"svn:author"},$username,
     'svn:author revprop matches expected username');
  # TEST
  ok($revprops->{"svn:date"},'svn:date revprop is defined');
  # TEST
  is($revprops->{"svn:log"},'Add new',
     'svn:log revprop is the expected value');

  my $changed_paths = $log_entry->changed_paths2;
  # TEST
  isa_ok($changed_paths,'HASH',
         'log_entry->changed_paths2');
  # TEST
  isa_ok($changed_paths->{'/dir1/new'},
         '_p_svn_log_changed_path2_t',
         'log_entry->changed_paths2 value');
  # TEST
  is($changed_paths->{'/dir1/new'}->action(),'A',
     'action returns A for add');
  # TEST
  is($changed_paths->{'/dir1/new'}->node_kind(),$SVN::Node::file,
     'node_kind returns $SVN::Node::file');
  # TEST
  is($changed_paths->{'/dir1/new'}->text_modified(),$SVN::Tristate::true,
     'text_modified returns true');
  # TEST
  is($changed_paths->{'/dir1/new'}->props_modified(),$SVN::Tristate::false,
     'props_modified returns false');
  # TEST
  is($changed_paths->{'/dir1/new'}->copyfrom_path(),undef,
     'copyfrom_path returns undef as it is not a copy');
  # TEST
  is($changed_paths->{'/dir1/new'}->copyfrom_rev(),
     $SVN::Core::INVALID_REVNUM,
     'copyfrom_rev is set to INVALID as it is not a copy');
}

# TEST
is($ctx->log4("$reposurl/dir1/new",
              'HEAD',$current_rev,0,1, # peg rev, start rev, end rev, limit
              1,1,0, # discover_changed_paths, strict_node_history, include_merged_revisions
              undef, # revprops
              \&test_log_entry_receiver), 
   undef,
   'log4 returns undef');

# TEST
is($ctx->log5("$reposurl/dir1/new",
              'HEAD',[$current_rev,0],1, # peg rev, rev ranges, limit
              1,1,0, # discover_changed_paths, strict_node_history, include_merged_revisions
              undef, # revprops
              \&test_log_entry_receiver), 
   undef,
   'log5 returns undef');

# test the different forms to specify revision ranges
sub get_revs {
    my ($rev_ranges) = @_;
    my @revs;
    $ctx->log5($reposurl, 'HEAD', $rev_ranges, 0, 0, 0, 0, undef, sub { 
        my ($log_entry,$pool) = @_;
        push @revs, $log_entry->revision;
    });
    return \@revs;
}

my $top = SVN::_Core::new_svn_opt_revision_range_t();
$top->start('HEAD');
$top->end('HEAD');
my $bottom = SVN::_Core::new_svn_opt_revision_range_t();
$bottom->start(1);
$bottom->end($current_rev-1);

# TEST
is_deeply(get_revs($top),   
          [ $current_rev ], 'single svn_opt_revision_range_t');
# TEST
is_deeply(get_revs([$top]), 
          [ $current_rev ], 'list of svn_opt_revision_range_t');
# TEST
is_deeply(get_revs(['HEAD', 'HEAD']),
          [ $current_rev ], 'single [start, end]');
# TEST
is_deeply(get_revs([['HEAD', 'HEAD']]),
          [ $current_rev ], 'list of [start, end]');
# TEST
is_deeply(get_revs([$current_rev, $current_rev]),
          [ $current_rev ], 'single [start, end]');
# TEST
is_deeply(get_revs([[$current_rev, $current_rev]]),
          [ $current_rev ], 'list of [start, end]');
# TEST
is_deeply(get_revs([1, 'HEAD']),
          [ 1..$current_rev ], 'single [start, end]');
# TEST
is_deeply(get_revs([[1, 'HEAD']]),
          [ 1..$current_rev ], 'list of [start, end]');
# TEST
is_deeply(get_revs([1, $opt_revision_head]),
          [ 1..$current_rev ], 'single [start, end]');
# TEST
is_deeply(get_revs([[1, $opt_revision_head]]),
          [ 1..$current_rev ], 'list of [start, end]');
# TEST
is_deeply(get_revs($bottom), 
          [ 1..$current_rev-1 ], 'single svn_opt_revision_range_t');
# TEST
is_deeply(get_revs([$bottom]), 
          [ 1..$current_rev-1 ], 'list of svn_opt_revision_range_t');
# TEST
is_deeply(get_revs([1, $current_rev-1]),
          [ 1..$current_rev-1 ], 'single [start, end]');
# TEST
is_deeply(get_revs([[1, $current_rev-1]]),
          [ 1..$current_rev-1 ], 'list of [start, end]');
# TEST
is_deeply(get_revs([[1, $current_rev-1], $top]),
          [ 1..$current_rev ], 'mixed list of ranges');
# TEST
is_deeply(get_revs([$bottom, ['HEAD', 'HEAD']]),
          [ 1..$current_rev ], 'mixed list of ranges');
# TEST
is_deeply(get_revs([$bottom, $top]),
          [ 1..$current_rev ], 'mixed list of ranges');
          

# TEST
is($ctx->update($wcpath,'HEAD',1),$current_rev,
   'Return from update is the current rev');

my $update2_result = $ctx->update2([$wcpath],'HEAD',1,0);
# TEST
isa_ok($update2_result,'ARRAY','update2 returns a list');
# TEST
is(scalar(@$update2_result),1,'update2 member count');
# TEST
is($update2_result->[0],$current_rev,'return from update2 is the current rev');

my $update3_result = $ctx->update3([$wcpath],'HEAD',$SVN::Depth::infinity,
	                                 0,0,0);
# TEST
isa_ok($update3_result,'ARRAY','update3 returns a list');
# TEST
is(scalar(@$update3_result),1,'update3 member count');
# TEST
is($update3_result->[0],$current_rev,'return from update3 is the current rev');

my $update4_result = $ctx->update4([$wcpath],'HEAD',$SVN::Depth::infinity,
                                   0,0,0,1,0);
# TEST
isa_ok($update4_result,'ARRAY','update4 returns a list');
# TEST
is(scalar(@$update4_result),1,'update4 member count');
# TEST
is($update4_result->[0],$current_rev,'return from update4 is the current rev');

# no return so we should get undef as the result
# we will get a _p_svn_error_t if there is an error.
# TEST
is($ctx->propset('perl-test','test-val',"$wcpath/dir1",0),undef,
   'propset on a working copy path returns undef');

my ($ph) = $ctx->propget('perl-test',"$wcpath/dir1",undef,0);
# TEST
isa_ok($ph,'HASH','propget returns a hash');
# TEST
is($ph->{"$wcpath/dir1"},'test-val','perl-test property has the correct value');

# No revnum for the working copy so we should get INVALID_REVNUM
# TEST
is($ctx->status($wcpath, undef, sub {
                                      my ($path,$wc_status) = @_;
                                      # TEST
                                      is($path,"$wcpath/dir1",
                                         'path param to status callback is' .
                                         ' the correct path.');
                                      # TEST
                                      isa_ok($wc_status,'_p_svn_wc_status_t',
                                             'wc_stats param');
                                      # TEST
                                      is($wc_status->text_status(),
                                         $SVN::Wc::Status::normal,
                                         'text_status param to status' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->prop_status(),
                                         $SVN::Wc::Status::modified,
                                         'prop_status param to status' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->locked(), 0,
                                         'locked param to status callback');
                                      # TEST
                                      is($wc_status->copied(), 0,
                                         'copied param to status callback');
                                      # TEST
                                      is($wc_status->switched(), 0,
                                         'switched param to status callback');
                                      # TEST
                                      is($wc_status->repos_text_status(),
                                         $SVN::Wc::Status::none,
                                         'repos_text_status param to status' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->repos_prop_status(),
                                         $SVN::Wc::Status::none,
                                         'repos_prop_status param to status' .
                                         ' callback');
                                    },
                1, 0, 0, 0),
   $SVN::Core::INVALID_REVNUM,
   'status returns INVALID_REVNUM when run against a working copy');

# No revnum for the working copy so we should get INVALID_REVNUM
# TEST
is($ctx->status2($wcpath, undef, sub {
                                      my ($path,$wc_status) = @_;
                                      # TEST
                                      is($path,"$wcpath/dir1",
                                         'path param to status2 callback');
                                      # TEST
                                      isa_ok($wc_status,'_p_svn_wc_status2_t',
                                             'wc_stats param to the status2' .
                                             ' callback');
                                      # TEST
                                      is($wc_status->text_status(),
                                         $SVN::Wc::Status::normal,
                                         'text_status param to status2' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->prop_status(),
                                         $SVN::Wc::Status::modified,
                                         'prop_status param to status2' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->locked(), 0,
                                         'locked param to status2' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->copied(), 0,
                                         'copied param to status2' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->switched(), 0,
                                         'switched param to status2' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->repos_text_status(),
                                         $SVN::Wc::Status::none,
                                         'repos_text_status param to status2' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->repos_prop_status(),
                                         $SVN::Wc::Status::none,
                                         'repos_prop_status param to status2' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->repos_lock(), undef,
                                        'repos_lock param to status2 callback');
                                      # TEST
                                      is($wc_status->url(),"$reposurl/dir1",
                                        'url param to status2 callback');
                                      # TEST
                                      is($wc_status->ood_last_cmt_rev(),
                                         $SVN::Core::INVALID_REVNUM,
                                         'ood_last_cmt_rev to status2' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->ood_last_cmt_date(), 0,
                                         'ood_last_cmt_date to status2' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->ood_kind(),
                                         $SVN::Node::none,
                                         'ood_kind param to status2 callback');
                                      # TEST
                                      is($wc_status->ood_last_cmt_author(),
                                         undef,
                                         'ood_last_cmt_author to status2' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->tree_conflict(), undef,
                                         'tree_conflict to status2 callback');
                                      # TEST
                                      is($wc_status->file_external(), 0,
                                         'file_external to status2 callback');
                                      # TEST
                                      is($wc_status->pristine_text_status(),
                                         $SVN::Wc::Status::normal,
                                         'pristine_text_status param to' .
                                         ' status2 callback');
                                      # TEST
                                      is($wc_status->pristine_prop_status(),
                                         $SVN::Wc::Status::modified,
                                         'pristine_prop_status param to' .
                                         ' status2 callback');
                                    },
                1, 0, 0, 0, 0),
   $SVN::Core::INVALID_REVNUM,
   'status2 returns INVALID_REVNUM when run against a working copy');

# No revnum for the working copy so we should get INVALID_REVNUM
# TEST
is($ctx->status3($wcpath, undef, sub {
                                      my ($path,$wc_status) = @_;
                                      # TEST
                                      is($path,"$wcpath/dir1",
                                         'path param to status3 callback');
                                      # TEST
                                      isa_ok($wc_status,'_p_svn_wc_status2_t',
                                             'wc_stats param to the status3' .
                                             ' callback');
                                      # TEST
                                      is($wc_status->text_status(),
                                         $SVN::Wc::Status::normal,
                                         'text_status param to status3' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->prop_status(),
                                         $SVN::Wc::Status::modified,
                                         'prop_status param to status3' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->locked(), 0,
                                         'locked param to status3' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->copied(), 0,
                                         'copied param to status3' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->switched(), 0,
                                         'switched param to status3' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->repos_text_status(),
                                         $SVN::Wc::Status::none,
                                         'repos_text_status param to status3' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->repos_prop_status(),
                                         $SVN::Wc::Status::none,
                                         'repos_prop_status param to status3' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->repos_lock(), undef,
                                        'repos_lock param to status3 callback');
                                      # TEST
                                      is($wc_status->url(),"$reposurl/dir1",
                                        'url param to status3 callback');
                                      # TEST
                                      is($wc_status->ood_last_cmt_rev(),
                                         $SVN::Core::INVALID_REVNUM,
                                         'ood_last_cmt_rev to status3' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->ood_last_cmt_date(), 0,
                                         'ood_last_cmt_date to status3' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->ood_kind(),
                                         $SVN::Node::none,
                                         'ood_kind param to status3 callback');
                                      # TEST
                                      is($wc_status->ood_last_cmt_author(),
                                         undef,
                                         'ood_last_cmt_author to status3' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->tree_conflict(), undef,
                                         'tree_conflict to status3 callback');
                                      # TEST
                                      is($wc_status->file_external(), 0,
                                         'file_external to status3 callback');
                                      # TEST
                                      is($wc_status->pristine_text_status(),
                                         $SVN::Wc::Status::normal,
                                         'pristine_text_status param to' .
                                         ' status3 callback');
                                      # TEST
                                      is($wc_status->pristine_prop_status(),
                                         $SVN::Wc::Status::modified,
                                         'pristine_prop_status param to' .
                                         ' status3 callback');
                                    },
                $SVN::Depth::infinity, 0, 0, 0, 0, undef),
   $SVN::Core::INVALID_REVNUM,
   'status3 returns INVALID_REVNUM when run against a working copy');

# No revnum for the working copy so we should get INVALID_REVNUM
# TEST
is($ctx->status4($wcpath, undef, sub {
                                      my ($path,$wc_status, $pool) = @_;
                                      # TEST
                                      is($path,"$wcpath/dir1",
                                         'path param to status4 callback');
                                      # TEST
                                      isa_ok($wc_status,'_p_svn_wc_status2_t',
                                             'wc_stats param to the status4' .
                                             ' callback');
                                      # TEST
                                      is($wc_status->text_status(),
                                         $SVN::Wc::Status::normal,
                                         'text_status param to status4' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->prop_status(),
                                         $SVN::Wc::Status::modified,
                                         'prop_status param to status4' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->locked(), 0,
                                         'locked param to status4' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->copied(), 0,
                                         'copied param to status4' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->switched(), 0,
                                         'switched param to status4' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->repos_text_status(),
                                         $SVN::Wc::Status::none,
                                         'repos_text_status param to status4' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->repos_prop_status(),
                                         $SVN::Wc::Status::none,
                                         'repos_prop_status param to status4' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->repos_lock(), undef,
                                        'repos_lock param to status4 callback');
                                      # TEST
                                      is($wc_status->url(),"$reposurl/dir1",
                                        'url param to status4 callback');
                                      # TEST
                                      is($wc_status->ood_last_cmt_rev(),
                                         $SVN::Core::INVALID_REVNUM,
                                         'ood_last_cmt_rev to status4' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->ood_last_cmt_date(), 0,
                                         'ood_last_cmt_date to status4' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->ood_kind(),
                                         $SVN::Node::none,
                                         'ood_kind param to status4 callback');
                                      # TEST
                                      is($wc_status->ood_last_cmt_author(),
                                         undef,
                                         'ood_last_cmt_author to status4' .
                                         ' callback');
                                      # TEST
                                      is($wc_status->tree_conflict(), undef,
                                         'tree_conflict to status4 callback');
                                      # TEST
                                      is($wc_status->file_external(), 0,
                                         'file_external to status4 callback');
                                      # TEST
                                      is($wc_status->pristine_text_status(),
                                         $SVN::Wc::Status::normal,
                                         'pristine_text_status param to' .
                                         ' status4 callback');
                                      # TEST
                                      is($wc_status->pristine_prop_status(),
                                         $SVN::Wc::Status::modified,
                                         'pristine_prop_status param to' .
                                         ' status4 callback');
                                      # TEST
                                      isa_ok($pool, '_p_apr_pool_t',
                                             'pool param to status4' .
                                             ' callback'); 
                                    },
                $SVN::Depth::infinity, 0, 0, 0, 0, undef),
   $SVN::Core::INVALID_REVNUM,
   'status4 returns INVALID_REVNUM when run against a working copy');


my ($ci_commit2) = $ctx->commit($wcpath,0);
# TEST
isa_ok($ci_commit2,'_p_svn_client_commit_info_t',
       'commit returns a _p_svn_client_commit_info_t');
$current_rev++;
# TEST
is($ci_commit2->revision(),$current_rev,
   "commit info revision equals $current_rev");

my $dir1_rev = $current_rev;


my($pl) = $ctx->proplist($reposurl,$current_rev,1);
# TEST
isa_ok($pl,'ARRAY','proplist returns an ARRAY');
# TEST
isa_ok($pl->[0], '_p_svn_client_proplist_item_t',
       'proplist array element');
# TEST
is($pl->[0]->node_name(),"$reposurl/dir1",
   'node_name is the expected value');
my $plh = $pl->[0]->prop_hash();
# TEST
isa_ok($plh,'HASH',
       'prop_hash returns a HASH');
# TEST
is_deeply($plh, {'perl-test' => 'test-val'}, 'test prop list prop_hash values');

# add a dir to test update
my ($ci_dir5) = $ctx->mkdir(["$reposurl/dir5"]);
# TEST
isa_ok($ci_dir5,'_p_svn_client_commit_info_t',
       'mkdir returns a _p_svn_client_commit_info_t');
$current_rev++;
# TEST
is($ci_dir5->revision(),$current_rev,
   "commit info revision equals $current_rev");

# Use explicit revnum to test that instead of just HEAD.
# TEST
is($ctx->update($wcpath,$current_rev,$current_rev),$current_rev,
   'update returns current rev');

# commit action against a repo returns undef
# TEST
is($ctx->delete(["$wcpath/dir2"],0),undef,
   'delete returns undef');

# no return means success
# TEST
is($ctx->revert($wcpath,1),undef,
   'revert returns undef');

my ($ci_copy) = $ctx->copy("$reposurl/dir1",2,"$reposurl/dir3");
# TEST
isa_ok($ci_copy,'_p_svn_client_commit_info_t',
       'copy returns a _p_svn_client_commitn_info_t when run against repo');
$current_rev++;
# TEST
is($ci_copy->revision,$current_rev,
   "commit info revision equals $current_rev");

# TEST
ok(mkdir($importpath),'Make import path dir');
# TEST
ok(open(FOO, ">$importpath/foo"),'Open file for writing in import path dir');
# TEST
ok(print(FOO 'foobar'),'Print to the file in import path dir');
# TEST
ok(close(FOO),'Close file in import path dir');

my ($ci_import) = $ctx->import($importpath,$reposurl,0);
# TEST
isa_ok($ci_import,'_p_svn_client_commit_info_t',
       'Import returns _p_svn_client_commint_info_t');
$current_rev++;
# TEST
is($ci_import->revision,$current_rev,
   "commit info revision equals $current_rev");

# TEST
is($ctx->blame("$reposurl/foo",'HEAD','HEAD', sub {
                                              my ($line_no,$rev,$author,
                                                  $date, $line,$pool) = @_;
                                              # TEST
                                              is($line_no,0,
                                                 'line_no param is zero');
                                              # TEST
                                              is($rev,$current_rev,
                                                 'rev param is current rev');
                                              # TEST
                                              is($author,$username,
                                                 'author param is expected' .
                                                 'value');
                                              # TEST
                                              ok($date,'date is defined');
                                              if ($^O eq 'MSWin32') {
                                                #### Why two \r-s?
                                                # TEST
                                                is($line,"foobar\r\r",
                                                   'line is expected value');
                                              } else {
                                                # TEST
                                                is($line,'foobar',
                                                   'line is expected value');
                                              }
                                              # TEST
                                              isa_ok($pool,'_p_apr_pool_t',
                                                     'pool param');
                                            }),
   undef,
   'blame returns undef');

# TEST
ok(open(CAT, "+>$testpath/cattest"),'open file for cat output');
# TEST
is($ctx->cat(\*CAT, "$reposurl/foo", 'HEAD'),undef,
   'cat returns undef');
# TEST
ok(seek(CAT,0,0),
   'seek the beginning of the cat file');
# TEST
is(readline(*CAT),'foobar',
   'read the first line of the cat file');
# TEST
ok(close(CAT),'close cat file');

# the string around the $current_rev exists to expose a past
# bug.  In the past we did not accept values that simply
# had not been converted to a number yet.
my ($dirents) = $ctx->ls($reposurl,"$current_rev", 1);
# TEST
isa_ok($dirents, 'HASH','ls returns a HASH');
# TEST
isa_ok($dirents->{'dir1'},'_p_svn_dirent_t',
       'dirents hash value');
# TEST
is($dirents->{'dir1'}->kind(),$SVN::Core::node_dir,
   'kind() returns a dir node');
# TEST
is($dirents->{'dir1'}->size(), -1,
   'size() returns -1 for a directory');
# TEST
is($dirents->{'dir1'}->has_props(),1,
   'has_props() returns true');
# TEST
is($dirents->{'dir1'}->created_rev(),$dir1_rev,
   'created_rev() returns expected rev');
# TEST
ok($dirents->{'dir1'}->time(),
   'time is defined');
#diag(scalar(localtime($dirents->{'dir1'}->time() / 1000000)));
# TEST
is($dirents->{'dir1'}->last_author(),$username,
   'last_auth() returns expected username');

# test removing a property
# TEST
is($ctx->propset('perl-test', undef, "$wcpath/dir1", 0),undef,
   'propset returns undef');

my ($ph2) = $ctx->propget('perl-test', "$wcpath/dir1", 'WORKING', 0);
# TEST
isa_ok($ph2,'HASH','propget returns HASH');
# TEST
is(scalar(keys %$ph2),0,
   'No properties after deleting a property');

# test cancel callback
my $cancel_cb_called = 0;
$ctx->cancel(sub { $cancel_cb_called++; 0 });
my $log_entries_received = 0;
$ctx->log5($reposurl,
              'HEAD',['HEAD',1],0, # peg rev, rev ranges, limit
              1,1,0, # discover_changed_paths, strict_node_history, include_merged_revisions
              undef, # revprops
              sub { $log_entries_received++ });
# TEST
ok($cancel_cb_called, 'cancel callback was called');
# TEST
is($log_entries_received, $current_rev, 'log entries received');

my $cancel_msg = "stop the presses";
$ctx->cancel(sub { $cancel_msg });
$svn_error = $ctx->log5($reposurl,
              'HEAD',['HEAD',1],0, # peg rev, rev ranges, limit
              1,1,0, # discover_changed_paths, strict_node_history, include_merged_revisions
              undef, # revprops
              sub { });
# TEST
isa_ok($svn_error, '_p_svn_error_t', 'return of a cancelled operation');
# TEST
is($svn_error->apr_err, $SVN::Error::CANCELLED, "SVN_ERR_CANCELLED");
{
    # If we're running a debug build, $svn_error may be the top of a
    # chain of svn_error_t's (all with message "traced call"), we need 
    # to get to the bottom svn_error_t to check for the original message.
    my $chained = $svn_error;
    $chained = $chained->child while $chained->child;
    # TEST
    is($chained->message, $cancel_msg, 'cancellation message');
}

$svn_error->clear(); # don't leak this
$ctx->cancel(undef); # reset cancel callback


SKIP: {
    # This is ugly.  It is included here as an aide to understand how
    # to test this and because it makes my life easier as I only have
    # one command to run to test it.  If you want to use this you need
    # to change the usernames, passwords, and paths to the client cert.
    # It assumes that there is a repo running on localhost port 443 at
    # via SSL.  The repo cert should trip a client trust issue.  The
    # client cert should be encrypted and require a pass to use it.
    # Finally uncomment the skip line below.

    # Before shipping make sure the following line is uncommented.
    skip 'Impossible to test without external effort to setup https', 7;

    sub simple_prompt {
        my $cred = shift;
        my $realm = shift;
        my $username_passed = shift;
        my $may_save = shift;
        my $pool = shift;

        ok(1,'simple_prompt called');
        $cred->username('breser');
        $cred->password('foo');
    }

    sub ssl_server_trust_prompt {
        my $cred = shift;
        my $realm = shift;
        my $failures = shift;
        my $cert_info = shift;
        my $may_save = shift;
        my $pool = shift;

        ok(1,'ssl_server_trust_prompt called');
        $cred->may_save(0);
        $cred->accepted_failures($failures);
    }

    sub ssl_client_cert_prompt {
        my $cred = shift;
        my $realm = shift;
        my $may_save = shift;
        my $pool = shift;

        ok(1,'ssl_client_cert_prompt called');
        $cred->cert_file('/home/breser/client-pass.p12');
    }

    sub ssl_client_cert_pw_prompt {
        my $cred = shift;
        my $may_save = shift;
        my $pool = shift;

        ok(1,'ssl_client_cert_pw_prompt called');
        $cred->password('test');
    }

    my $oldauthbaton = $ctx->auth();

    # TEST
    isa_ok($ctx->auth(SVN::Client::get_simple_prompt_provider(
                                sub { simple_prompt(@_,'x') },2),
               SVN::Client::get_ssl_server_trust_prompt_provider(
                                \&ssl_server_trust_prompt),
               SVN::Client::get_ssl_client_cert_prompt_provider(
                                \&ssl_client_cert_prompt,2),
               SVN::Client::get_ssl_client_cert_pw_prompt_provider(
                                \&ssl_client_cert_pw_prompt,2)
              ),'_p_svn_auth_baton_t',
              'auth() accessor returns _p_svn_auth_baton');

    # if this doesn't work we will get an svn_error_t so by
    # getting a hash we know it worked.
    my ($dirents) = $ctx->ls('https://localhost/svn/test','HEAD',1);
    # TEST
    isa_ok($dirents,'HASH','ls returns a HASH');

    # return the auth baton to its original setting
    # TEST
    isa_ok($ctx->auth($oldauthbaton),'_p_svn_auth_baton_t',
           'Successfully set auth_baton back to old value');
}

# Keep track of the ok-ness ourselves, since we need to know the exact
# number of tests at the start of this file. The 'subtest' feature of
# Test::More would be perfect for this, but it's only available in very
# recent perl versions, it seems.
my $ok = 1;
# Get a list of platform specific providers, using the default
# configuration and pool.
my @providers = @{SVN::Core::auth_get_platform_specific_client_providers(undef, undef)};
foreach my $p (@providers) {
    $ok &= defined($p) && $p->isa('_p_svn_auth_provider_object_t');
}
# TEST
ok($ok, 'svn_auth_get_platform_specific_client_providers returns _p_svn_auth_provider_object_t\'s');

SKIP: {
  skip 'Gnome-Keyring support not compiled in', 1
      unless defined &SVN::Core::auth_set_gnome_keyring_unlock_prompt_func;

  # Test setting gnome_keyring prompt function. This just sets the proper
  # attributes in the auth baton and checks the return value (which should
  # be a reference to the passed function reference). This does not
  # actually try the prompt, since that would require setting up a
  # gnome-keyring-daemon...
  sub gnome_keyring_unlock_prompt {
      my $keyring_name = shift;
      my $pool = shift;

      'test';
  }

  my $callback = \&gnome_keyring_unlock_prompt;
  my $result = SVN::Core::auth_set_gnome_keyring_unlock_prompt_func(
                   $ctx->auth(), $callback);
  # TEST
  is(${$result}, $callback, 'auth_set_gnome_keyring_unlock_prompt_func result equals parameter');
}

END {
diag('cleanup');
rmtree($testpath);
}
