#!/usr/bin/perl -w

use Test::More tests => 209;
use strict;

# shut up about variables that are only used once.
# these come from constants and variables used
# by the bindings but not elsewhere in perl space.
no warnings 'once'; 

# Cause the listed functions to die() instead of returning false.  This halts
# the test early, stopping errors in one test propogating down in to later
# tests and causing confusion.
use Fatal qw(open close ok is isa_ok);

use_ok('SVN::Core');
use_ok('SVN::Repos');
use_ok('SVN::Client');
use_ok('SVN::Wc'); # needed for status
use File::Spec::Functions;
use File::Temp qw(tempdir);
use File::Path qw(rmtree);

# do not use cleanup because it will fail, some files we
# will not have write perms to.
my $testpath = tempdir('svn-perl-test-XXXXXX', TMPDIR => 1, CLEANUP => 1);

my $repospath = catdir($testpath,'repo');
my $reposurl = 'file://' . (substr($repospath,0,1) ne '/' ? '/' : '')
               . $repospath;
my $wcpath = catdir($testpath,'wc');
my $importpath = catdir($testpath,'import');

# track current rev ourselves to test against
my $current_rev = 0;

# We want to trap errors ourself
$SVN::Error::handler = undef;

# Get username we are running as
my $username = getpwuid($>);

# This is ugly to create the test repo with SVN::Repos, but
# it seems to be the most reliable way.
ok(SVN::Repos::create("$repospath", undef, undef, undef, undef),
   "create repository at $repospath");

my ($ctx) = SVN::Client->new;
isa_ok($ctx,'SVN::Client','Client Object');

my ($ci_dir1) = $ctx->mkdir(["$reposurl/dir1"]);
isa_ok($ci_dir1,'_p_svn_client_commit_info_t');
$current_rev++;
is($ci_dir1->revision,$current_rev,"commit info revision equals $current_rev");

# Notes on testing methods that support both named and positional parameters,
# with or without optional parameters.
#
# This is going to involve repeated calls to the same functions, with slightly
# different argument lists.  At the moment I'm doing this by creating an
# anonymous function that contains the tests, and takes a 'name' param.
# This 'name' param indicates whether the test was exercised against
# positional arguments with all present ('pos/man'), positional arguments
# with optional arguments omitted ('pos/opt'), named arguments with all
# present ('nam/man'), or named arguments with optional arguments omitted
# ('nam/opt').
#
# Hopefully this will make tests relatively easy to write, and, in
# case of testing errors, make it easy to see which test failed.

my $tester;

# ------------------------------------------------------------------------

SKIP: {
    skip 'Difficult to test on Win32', 3 if $^O eq 'MSWin32';

    ok(rename("$repospath/hooks/pre-revprop-change.tmpl",
              "$repospath/hooks/pre-revprop-change"),
       'Rename pre-revprop-change hook');
    ok(chmod(0700,"$repospath/hooks/pre-revprop-change"),
       'Change permissions on pre-revprop-change hook');

    my ($rps_rev) = $ctx->revprop_set('svn:log','mkdir dir1',
                                      $reposurl, $current_rev, 0);
    is($rps_rev,$current_rev,
       'Returned revnum of current rev from revprop_set');

}

# revprop_list ----------------------------------------------------------

my ($rph, $rplrev);

$tester = sub {
    my $name = shift;

    isa_ok($rph,'HASH','Returned hash reference form revprop_list');
    is($rplrev,$current_rev,'Returned current rev from revprop_list');
    is($rph->{'svn:author'},$username,
       'svn:author is expected user from revprop_list');
    if ($^O eq 'MSWin32') {
	# we skip the log change test on win32 so we have to test
	# for a different var here
	is($rph->{'svn:log'},'Make dir1',
	   'svn:log is expected value from revprop_list');
    } else {
	is($rph->{'svn:log'},'mkdir dir1',
	   'svn:log is expected value from revprop_list');
    }
    ok($rph->{'svn:date'},'svn:date is set from revprop_list');
};

($rph, $rplrev) = $ctx->revprop_list($reposurl, $current_rev);
$tester->('pos/man');

($rph, $rplrev) = $ctx->revprop_list($reposurl);
$tester->('pos/opt');

($rph, $rplrev) = $ctx->revprop_list({
    url      => $reposurl,
    revision => $current_rev
});
$tester->('nam/man');

($rph, $rplrev) = $ctx->revprop_list({
    url      => $reposurl,
});
$tester->('nam/opt');

# checkout ---------------------------------------------------------------

my $rev;
$tester = sub {
    my $name = shift;
    is($rev, $current_rev, "Returned current rev from checkout ($name)");
    ok(-d $wcpath, "$wcpath was created ($name)");
};

$rev = $ctx->checkout($reposurl, $wcpath, 'HEAD', 0);
$tester->('pos/man');

rmtree([ $wcpath ]);
$rev = $ctx->checkout($reposurl, $wcpath);
$tester->('pos/opt');

rmtree([ $wcpath ]);
$rev = $ctx->checkout({
    url      => $reposurl,
    path     => $wcpath,
    revision => 'HEAD',
    recurse  => 0,
});
$tester->('nam/man');

rmtree([ $wcpath ]);
$rev = $ctx->checkout({
    url      => $reposurl,
    path     => $wcpath,
});
$tester->('nam/opt');

rmtree([ $wcpath ]);
$rev = $ctx->checkout({
    url      => $reposurl,
    path     => $wcpath,
    recurse  => 1,
});
$tester->('nam/opt - with recursion');
ok(-d "$wcpath/dir1", "Recursive checkout worked");

# ------------------------------------------------------------------------

is(SVN::Client::url_from_path($wcpath),$reposurl,
   "Returned $reposurl from url_from_path");

# Failure of the next three should cause the test to immediately halt
ok(open(NEW, ">$wcpath/dir1/new"),'Open new file for writing');
ok(print(NEW 'addtest'), 'Print to new file');
ok(close(NEW),'Close new file');

# no return means success
is($ctx->add("$wcpath/dir1/new",0),undef,
   'Returned undef from add schedule operation');

# test the log_msg callback
$ctx->log_msg( 
    sub 
    {
        my ($log_msg,$tmp_file,$commit_items,$pool) = @_;
        isa_ok($log_msg,'SCALAR','log_msg param to callback is a SCALAR');
        isa_ok($tmp_file,'SCALAR','tmp_file param to callback is a SCALAR');
        isa_ok($commit_items,'ARRAY',
               'commit_items param to callback is a SCALAR');
        isa_ok($pool,'_p_apr_pool_t',
               'pool param to callback is a _p_apr_pool_t');
        my $commit_item = shift @$commit_items;
        isa_ok($commit_item,'_p_svn_client_commit_item3_t',
               'commit_item element is a _p_svn_client_commit_item3_t');
        is($commit_item->path(),"$wcpath/dir1/new",
           "commit_item has proper path for committed file");
        is($commit_item->kind(),$SVN::Node::file,
           "kind() shows the node as a file");
        is($commit_item->url(),"$reposurl/dir1/new",
           'URL matches our repos url');
        # revision is 0 because the commit has not happened yet
        # and this is not a copy
        is($commit_item->revision(),0,
           'Revision is 0 since commit has not happened yet');
        is($commit_item->copyfrom_url(),undef,
           'copyfrom_url is undef since file is not a copy');
        is($commit_item->state_flags(),$SVN::Client::COMMIT_ITEM_ADD |
                                       $SVN::Client::COMMIT_ITEM_TEXT_MODS,
           'state_flags are ADD and TEXT_MODS');
        my $prop_changes = $commit_item->incoming_prop_changes();
        isa_ok($prop_changes, 'ARRAY',
               'incoming_prop_changes returns an ARRAY');
        is(scalar(@$prop_changes), 0,
           'No elements in the incoming_prop_changes array because ' .
           ' we did not make any');
        $prop_changes = $commit_item->outgoing_prop_changes();
        is($prop_changes, undef,
           'No outgoing_prop_changes array because we did not create one');
        $$log_msg = 'Add new';
        return 0;
    } );


my ($ci_commit1) = $ctx->commit($wcpath,0);
isa_ok($ci_commit1,'_p_svn_client_commit_info_t',
       'Commit returns a _p_svn_client_commit_info');
$current_rev++;
is($ci_commit1->revision,$current_rev,
   "commit info revision equals $current_rev");

# get rid of log_msg callback
is($ctx->log_msg(undef),undef,
   'Clearing the log_msg callback works');

# info -------------------------------------------------------------------

# test info() on WC
my $receiver = sub {
    my($infopath, $svn_info_t, $pool) = @_;
    is($infopath, 'new', 'path passed to receiver is the same as WC');
    isa_ok($svn_info_t, '_p_svn_info_t');
    isa_ok($pool, '_p_apr_pool_t');
};


is($ctx->info("$wcpath/dir1/new", undef, 'WORKING', $receiver, 0),
   undef, 'info should return undef (pos/man)');
is($ctx->info("$wcpath/dir1/new", undef, 'WORKING', $receiver),
   undef, 'info should return undef (pos/opt)');
is($ctx->info({
    path_or_url => "$wcpath/dir1/new",
    peg_revision => undef,
    revision     => 'WORKING',
    receiver     => $receiver,
    recurse      => 0,
}), undef, 'info should return undef (nam/man)');
is($ctx->info({
    path_or_url => "$wcpath/dir1/new",
    peg_revision => undef,
    revision     => 'WORKING',
    receiver     => $receiver,
}), undef, 'info should return undef (nam/opt)');

my $r = $ctx->info("$wcpath/dir1/newxyz", undef, 'WORKING', sub {}, 0);
isa_ok($r, '_p_svn_error_t',
       'info should return _p_svn_error_t for a nonexistent file');
$r->clear();			# Clear the error, avoid core dump
                                # if built with --enable-maintainer-mode

# log --------------------------------------------------------------------

$receiver = sub {
    my($changed_paths, $revision, $author, $date, $message, $pool) = @_;

    isa_ok($changed_paths, 'HASH', 'changed_paths param is a HASH');
    isa_ok($changed_paths->{'/dir1/new'}, '_p_svn_log_changed_path_t',
	   'Hash value is a _p_svn_log_changed_path_t');
    is($changed_paths->{'/dir1/new'}->action(), 'A',
       'action returns A for add');
    is($changed_paths->{'/dir1/new'}->copyfrom_path(), undef,
       'copyfrom_path returns undef as it is not a copy');
    is($changed_paths->{'/dir1/new'}->copyfrom_rev(),
       $SVN::Core::INVALID_REVNUM,
       'copyfrom_rev is set to INVALID as it is not a copy');
    is($revision, $current_rev, 'revision param matches current rev');
    is($author, $username, 'author param matches expected username');
    ok($date,'date param is defined');
    is($message, 'Add new', 'message param is the expected value');
    isa_ok($pool, '_p_apr_pool_t', 'pool param is _p_apr_pool_t');
};

# Get the log with positional params
is($ctx->log("$reposurl/dir1/new", $current_rev, $current_rev, 1, 0,
	     $receiver), undef, 'log returns undef (pos/man)');

# Get the log with named params
is($ctx->log({
    targets                => "$reposurl/dir1/new",
    start                  => $current_rev,
    end                    => $current_rev,
    discover_changed_paths => 1,
    strict_node_history    => 0,
    receiver               => $receiver,
}), undef, 'log returns undef (nam/man)');

# Get the log with named params, and an arrayref of targets
is($ctx->log({
    targets                => [ "$reposurl/dir1/new" ],
    start                  => $current_rev,
    end                    => $current_rev,
    discover_changed_paths => 1,
    strict_node_history    => 0,
    receiver               => $receiver,
}), undef, 'log returns undef (nam/man)');

# log2 -------------------------------------------------------------------

is($ctx->log2({
    targets                => "$reposurl/dir1/new",
    start                  => $current_rev,
    end                    => $current_rev,
    limit                  => 0,
    discover_changed_paths => 1,
    strict_node_history    => 0,
    receiver               => $receiver,
}), undef, 'log3 returns undef (nam/man)');

# log3 -------------------------------------------------------------------

# Get the log with named params
is($ctx->log3({
    targets                => "$reposurl/dir1/new",
    peg_revision           => undef,
    start                  => $current_rev,
    end                    => $current_rev,
    limit                  => 0,
    discover_changed_paths => 1,
    strict_node_history    => 0,
    receiver               => $receiver,
}), undef, 'log3 returns undef (nam/man)');

# update -----------------------------------------------------------------

is($ctx->update($wcpath,'HEAD', 1), $current_rev,
   'Return from update is the current rev (pos/man)');
is($ctx->update({
    path     => $wcpath,
    revision => 'HEAD',
    recurse  => 1
}), $current_rev, 'Return from update is the current rev (nam/man)');
is($ctx->update({
    path     => $wcpath,
    recurse  => 1
}), $current_rev, 'Return from update is the current rev (nam/opt)');

# propset ----------------------------------------------------------------

# no return so we should get undef as the result
# we will get a _p_svn_error_t if there is an error. 
is($ctx->propset('perl-test', 'test-val', "$wcpath/dir1", 0), undef,
   'propset on a working copy path returns undef (pos/man)');
is($ctx->propset('perl-test', 'test-val', "$wcpath/dir1"), undef,
   'propset on a working copy path returns undef (pos/opt)');
is($ctx->propset({
    propname => 'perl-test',
    propval  => 'test-val',
    target   => "$wcpath/dir1",
    recurse  => 0,
}), undef, 'propset on a working copy path returns undef (nam/man)');
is($ctx->propset({
    propname => 'perl-test',
    propval  => 'test-val',
    target   => "$wcpath/dir1",
}), undef, 'propset on a working copy path returns undef (nam/opt)');


my ($ph) = $ctx->propget('perl-test',"$wcpath/dir1",undef,0);
isa_ok($ph,'HASH','propget returns a hash');
is($ph->{"$wcpath/dir1"},'test-val','perl-test property has the correct value');

# From Eric Miller <eric.miller@amd.com>
#
# When svn_path_basename is called during a svn_client_status call it
# can barf on "." and core dumps the interpreter.
#
# perl: subversion/libsvn_subr/path.c:377: svn_path_basename: Assertion
#     `is_canonical(path, len)' failed.
#
# As outlined in past discussions I found, the code fails because the
# canonical version of "." is "" (empty string) and for some reason that
# path is not getting canonicalized before being passed.
#
# The simple thing to do would be to map "." to "" before svn_client_status
# (or status2) is called, since changing the api directly seems to be a
# source of contention (at least it was 2 years ago J ).
#
SKIP: {
    skip "Test dumps core", 1;
    $ctx->status(".", "HEAD", sub {}, 0, 0, 0, 0);
}

# No revnum for the working copy so we should get INVALID_REVNUM
is($ctx->status($wcpath, undef, sub { 
                                      my ($path,$wc_status) = @_;
                                      is($path,"$wcpath/dir1",
                                         'path param to status callback is' .
                                         'the correct path.');
                                      isa_ok($wc_status,'_p_svn_wc_status_t',
                                             'wc_stats param is a' .
                                             ' _p_svn_wc_status_t');
                                      is($wc_status->prop_status(),
                                         $SVN::Wc::status_modified,
                                         'prop_status is status_modified');
                                      # TODO test the rest of the members
                                    },
                1, 0, 0, 0),
   $SVN::Core::INVALID_REVNUM,
   'status returns INVALID_REVNUM when run against a working copy');

my ($ci_commit2) = $ctx->commit($wcpath,0);
isa_ok($ci_commit2,'_p_svn_client_commit_info_t',
       'commit returns a _p_svn_client_commit_info_t');
$current_rev++;
is($ci_commit2->revision(),$current_rev,
   "commit info revision equals $current_rev");

my $dir1_rev = $current_rev;


my($pl) = $ctx->proplist($reposurl,$current_rev,1);
isa_ok($pl,'ARRAY','proplist returns an ARRAY');
isa_ok($pl->[0], '_p_svn_client_proplist_item_t',
       'array element is a _p_svn_client_proplist_item_t');
is($pl->[0]->node_name(),"$reposurl/dir1",
   'node_name is the expected value');
my $plh = $pl->[0]->prop_hash();
isa_ok($plh,'HASH',
       'prop_hash returns a HASH');
is_deeply($plh, {'perl-test' => 'test-val'}, 'test prop list prop_hash values');

# add a dir to test update
my ($ci_dir2) = $ctx->mkdir(["$reposurl/dir2"]);
isa_ok($ci_dir2,'_p_svn_client_commit_info_t',
       'mkdir returns a _p_svn_client_commit_info_t');
$current_rev++;
is($ci_dir2->revision(),$current_rev,
   "commit info revision equals $current_rev");

# Use explicit revnum to test that instead of just HEAD.
is($ctx->update($wcpath,$current_rev,$current_rev),$current_rev,
   'update returns current rev');

# commit action against a repo returns undef
is($ctx->delete(["$wcpath/dir2"],0),undef,
   'delete returns undef');

# no return means success
is($ctx->revert($wcpath,1),undef,
   'revert returns undef');

my ($ci_copy) = $ctx->copy("$reposurl/dir1",2,"$reposurl/dir3");
isa_ok($ci_copy,'_p_svn_client_commit_info_t',
       'copy returns a _p_svn_client_commitn_info_t when run against repo');
$current_rev++;
is($ci_copy->revision,$current_rev,
   "commit info revision equals $current_rev");

ok(mkdir($importpath),'Make import path dir');
ok(open(FOO, ">$importpath/foo"),'Open file for writing in import path dir');
ok(print(FOO 'foobar'),'Print to the file in import path dir');
ok(close(FOO),'Close file in import path dir');

my ($ci_import) = $ctx->import($importpath,$reposurl,0);
isa_ok($ci_import,'_p_svn_client_commit_info_t',
       'Import returns _p_svn_client_commint_info_t');
$current_rev++;
is($ci_import->revision,$current_rev,
   "commit info revision equals $current_rev");

is($ctx->blame("$reposurl/foo",'HEAD','HEAD', sub {
                                              my ($line_no,$rev,$author,
                                                  $date, $line,$pool) = @_;
                                              is($line_no,0,
                                                 'line_no param is zero');
                                              is($rev,$current_rev,
                                                 'rev param is current rev');
                                              is($author,$username,
                                                 'author param is expected' .
                                                 'value');
                                              ok($date,'date is defined');
                                              is($line,'foobar',
                                                 'line is expected value');
                                              isa_ok($pool,'_p_apr_pool_t',
                                                     'pool param is ' .
                                                     '_p_apr_pool_t');
                                            }),
   undef,
   'blame returns undef');

ok(open(CAT, "+>$testpath/cattest"),'open file for cat output');
is($ctx->cat(\*CAT, "$reposurl/foo", 'HEAD'),undef,
   'cat returns undef');
ok(seek(CAT,0,0),
   'seek the beginning of the cat file');
is(readline(*CAT),'foobar',
   'read the first line of the cat file');
ok(close(CAT),'close cat file');

# the string around the $current_rev exists to expose a past
# bug.  In the past we did not accept values that simply
# had not been converted to a number yet.

# Going to call ls() multiple times, with different sets of arguments
my @ls_args = ([ $reposurl, "$current_rev", 1 ],
	       [ { path_or_url => $reposurl,
		   revision    => "$current_rev",
		   recurse     => 1 } ],
	       );

foreach my $ls_args (@ls_args) {
    my ($dirents) = $ctx->ls(@{ $ls_args });
    isa_ok($dirents, 'HASH','ls returns a HASH');
    isa_ok($dirents->{'dir1'},'_p_svn_dirent_t',
	   'hash value is a _p_svn_dirent_t');
    is($dirents->{'dir1'}->kind(),$SVN::Core::node_dir,
       'kind() returns a dir node');
    is($dirents->{'dir1'}->size(),0,
       'size() returns 0 for a directory');
    is($dirents->{'dir1'}->has_props(),1,
       'has_props() returns true');
    is($dirents->{'dir1'}->created_rev(),$dir1_rev,
       'created_rev() returns expected rev');
    ok($dirents->{'dir1'}->time(),
       'time is defined');
    #diag(scalar(localtime($dirents->{'dir1'}->time() / 1000000)));
    is($dirents->{'dir1'}->last_author(),$username,
       'last_auth() returns expected username');
}

# test removing a property
is($ctx->propset('perl-test', undef, "$wcpath/dir1", 0),undef,
   'propset returns undef');

my ($ph2) = $ctx->propget('perl-test', "$wcpath/dir1", 'WORKING', 0);
isa_ok($ph2,'HASH','propget returns HASH');
is(scalar(keys %$ph2),0,
   'No properties after deleting a property');

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
    isa_ok($dirents,'HASH','ls returns a HASH');

    # return the auth baton to its original setting
    isa_ok($ctx->auth($oldauthbaton),'_p_svn_auth_baton_t',
           'Successfully set auth_baton back to old value');
}

END {
diag('cleanup');
rmtree($testpath);
pass('END block ran through to completion');
}
